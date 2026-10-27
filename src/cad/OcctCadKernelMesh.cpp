#include "olp/cad/OcctCadKernel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#ifdef OLP_HAS_OCCT
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <StepData_StepModel.hxx>
#include <StepShape_SolidModel.hxx>
#include <StlAPI_Reader.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TransferBRep.hxx>
#include <Transfer_TransientProcess.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_InterfaceModel.hxx>
#include <Interface_Static.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <NCollection_Sequence.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Label.hxx>
#include <TDocStd_Document.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>
#include <Geom_Line.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GProp_GProps.hxx>
#endif

#include "OcctCadKernelDetail.h"

namespace olp::cad::detail {

core::TriMesh makeBoxMesh(const core::Vec3& bmin, const core::Vec3& bmax) {
    core::TriMesh mesh;
    mesh.bboxMin = bmin;
    mesh.bboxMax = bmax;
    mesh.vertices = {
        {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z},
        {bmax.x, bmax.y, bmin.z}, {bmin.x, bmax.y, bmin.z},
        {bmin.x, bmin.y, bmax.z}, {bmax.x, bmin.y, bmax.z},
        {bmax.x, bmax.y, bmax.z}, {bmin.x, bmax.y, bmax.z}
    };
    mesh.triangles = {
        {0, 2, 1}, {0, 3, 2}, // bottom
        {4, 5, 6}, {4, 6, 7}, // top
        {0, 1, 5}, {0, 5, 4}, // front
        {2, 3, 7}, {2, 7, 6}, // back
        {1, 2, 6}, {1, 6, 5}, // right
        {3, 0, 4}, {3, 4, 7}  // left
    };
    return mesh;
}


} // namespace olp::cad::detail

namespace olp::cad {

#ifdef OLP_HAS_OCCT
static void extractTrianglesFromShape(const TopoDS_Shape& shape, core::TriMesh& outMesh) {
    outMesh.vertices.clear();
    outMesh.triangles.clear();
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
        const TopoDS_Face face = TopoDS::Face(faces.Current());
        TopLoc_Location loc;
        const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;
        const gp_Trsf trsf = loc.Transformation();
        const uint32_t baseIdx = static_cast<uint32_t>(outMesh.vertices.size());
        const int nbNodes = tri->NbNodes();
        for (int i = 1; i <= nbNodes; ++i) {
            gp_Pnt p = tri->Node(i).Transformed(trsf);
            outMesh.vertices.push_back({p.X(), p.Y(), p.Z()});
        }
        const bool reversed = (face.Orientation() == TopAbs_REVERSED) ^ trsf.IsNegative();
        const int nbTriangles = tri->NbTriangles();
        for (int i = 1; i <= nbTriangles; ++i) {
            int n1 = 0, n2 = 0, n3 = 0;
            tri->Triangle(i).Get(n1, n2, n3);
            if (reversed) std::swap(n1, n2);
            outMesh.triangles.push_back({
                baseIdx + static_cast<uint32_t>(n1 - 1),
                baseIdx + static_cast<uint32_t>(n2 - 1),
                baseIdx + static_cast<uint32_t>(n3 - 1)
            });
        }
    }
}
#endif

bool OcctCadKernel::tessellate(
    int cadHandleId,
    double fineDeflectionMm,
    double coarseDeflectionMm,
    core::TriMesh& outRenderMesh,
    core::TriMesh& outCollisionMesh) const {
    auto it = solids_.find(cadHandleId);
    if (it == solids_.end()) return false;

#ifdef OLP_HAS_OCCT
    auto shapeIt = shapes_.find(cadHandleId);
    if (shapeIt != shapes_.end() && !shapeIt->second.IsNull()) {
        const TopoDS_Shape& shape = shapeIt->second;
        const double fineDef = fineDeflectionMm > 0.0 ? fineDeflectionMm : 0.2;
        const double coarseDef = coarseDeflectionMm > 0.0 ? coarseDeflectionMm : 2.0;

        BRepTools::Clean(shape);
        BRepMesh_IncrementalMesh coarseMesh(shape, coarseDef);
        coarseMesh.Perform();
        extractTrianglesFromShape(shape, outCollisionMesh);

        BRepTools::Clean(shape);
        BRepMesh_IncrementalMesh fineMesh(shape, fineDef);
        fineMesh.Perform();
        extractTrianglesFromShape(shape, outRenderMesh);

        outRenderMesh.bboxMin = it->second.bboxMin;
        outRenderMesh.bboxMax = it->second.bboxMax;
        outCollisionMesh.bboxMin = it->second.bboxMin;
        outCollisionMesh.bboxMax = it->second.bboxMax;
        return true;
    }
#else
    (void)fineDeflectionMm;
    (void)coarseDeflectionMm;
#endif

    outRenderMesh = detail::makeBoxMesh(it->second.bboxMin, it->second.bboxMax);
    outCollisionMesh = outRenderMesh;
    return true;
}


} // namespace olp::cad
