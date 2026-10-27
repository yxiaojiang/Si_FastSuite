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

namespace olp::cad {

OcctCadKernel::OcctCadKernel() = default;
OcctCadKernel::~OcctCadKernel() = default;

namespace {


std::string hashStepFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::uint64_t hash = 14695981039346656037ull;
    char buffer[4096];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ull;
        }
        if (n <= 0) break;
    }
    std::ostringstream oss;
    oss << "fnv1a64:" << std::hex << hash;
    return oss.str();
}

core::CollisionRole collisionRoleForPartName(const std::string& name) {
    if (name.find("Clamp") != std::string::npos ||
        name.find("Fixture") != std::string::npos ||
        name.find("Table") != std::string::npos ||
        name.find("Pedestal") != std::string::npos) {
        return core::CollisionRole::Fixture;
    }
    if (name.find("Static_") != std::string::npos ||
        name.find("Robot_") != std::string::npos) {
        return core::CollisionRole::Environment;
    }
    return core::CollisionRole::Workpiece;
}

} // namespace

bool OcctCadKernel::importStep(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes) {
#ifdef OLP_HAS_OCCT
    if (importStepWithOcct(filePath, outPartNodes)) {
        return true;
    }
    outPartNodes.clear();
#endif
    return importStepBuiltin(filePath, outPartNodes);
}

bool OcctCadKernel::importStepBuiltin(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }

    double unitScaleToMm = 1.0;
    bool inData = false;
    for (const auto& raw : lines) {
        if (!inData) {
            if (raw.find("DATA;") != std::string::npos) inData = true;
            continue;
        }
        if (raw.find("ENDSEC;") != std::string::npos) break;
        if (raw.empty() || raw[0] == '/' || raw[0] == '*') continue;
        if (raw.back() != ';') {
            // Wrapped ISO-10303-21 entity. A line reader would truncate it.
            return false;
        }
        if (raw.find("CIRCLE") != std::string::npos ||
            raw.find("CYLINDRICAL_SURFACE") != std::string::npos ||
            raw.find("CONICAL_SURFACE") != std::string::npos ||
            raw.find("SPHERICAL_SURFACE") != std::string::npos ||
            raw.find("TOROIDAL_SURFACE") != std::string::npos ||
            raw.find("B_SPLINE") != std::string::npos ||
            raw.find("SURFACE_OF_REVOLUTION") != std::string::npos ||
            raw.find("OFFSET_SURFACE") != std::string::npos) {
            // Headless adapter only understands planes and straight edges.
            return false;
        }
    }
    for (const auto& raw : lines) {
        if (raw.find("SI_UNIT") != std::string::npos && raw.find(".METRE.") != std::string::npos) {
            unitScaleToMm = (raw.find(".MILLI.") == std::string::npos) ? 1000.0 : 1.0;
        }
    }

    // Parse ISO-10303-21 STEP entities (CARTESIAN_POINT, VERTEX_POINT, DIRECTION,
    // EDGE_CURVE, ORIENTED_EDGE, EDGE_LOOP, FACE_OUTER_BOUND, AXIS2_PLACEMENT_3D,
    // PLANE, ADVANCED_FACE, CLOSED_SHELL, MANIFOLD_SOLID_BREP, PRODUCT)
    std::unordered_map<int, core::Vec3> cartesianPts;
    std::unordered_map<int, int> vertexToPt;
    std::unordered_map<int, core::Vec3> directions;
    struct StepEdge { int v1{0}; int v2{0}; };
    std::unordered_map<int, StepEdge> edgeCurves;
    std::unordered_map<int, int> orientedToEdge;
    std::unordered_map<int, std::vector<int>> edgeLoops;
    std::unordered_map<int, int> boundToLoop;
    struct Axis2Placement { int ptId{0}; int dirZId{0}; int dirXId{0}; };
    std::unordered_map<int, Axis2Placement> placements;
    struct StepPlane { std::string name{}; int placementId{0}; };
    std::unordered_map<int, StepPlane> planes;
    struct StepFace { std::string name{}; int boundId{0}; int surfId{0}; bool sameSense{true}; };
    std::unordered_map<int, StepFace> advFaces;
    struct StepShell { std::string name{}; std::vector<int> faceIds{}; };
    std::unordered_map<int, StepShell> closedShells;
    struct StepSolid { int id{0}; std::string name{}; int shellId{0}; };
    std::vector<StepSolid> manifoldSolids;
    std::string fallbackProductName = "STEP_Part";

    std::regex reId(R"(^\s*#(\d+)\s*=\s*(.*)$)");
    std::regex reCoords(R"(\(\s*([-\d.Ee+]+)\s*,\s*([-\d.Ee+]+)\s*,\s*([-\d.Ee+]+)\s*\))");
    std::regex reRefs(R"(#(\d+))");
    std::regex reName(R"('([^']*)')");

    try {
    for (const std::string& line : lines) {
        std::smatch mId;
        if (!std::regex_search(line, mId, reId)) {
            continue;
        }
        const int eid = std::stoi(mId[1].str());
        const std::string rhs = mId[2].str();

        if (rhs.find("CARTESIAN_POINT") == 0) {
            std::smatch mc;
            if (std::regex_search(rhs, mc, reCoords)) {
                cartesianPts[eid] = {
                    std::stod(mc[1].str()) * unitScaleToMm,
                    std::stod(mc[2].str()) * unitScaleToMm,
                    std::stod(mc[3].str()) * unitScaleToMm
                };
            }
        } else if (rhs.find("DIRECTION") == 0) {
            std::smatch mc;
            if (std::regex_search(rhs, mc, reCoords)) {
                directions[eid] = core::Vec3{
                    std::stod(mc[1].str()),
                    std::stod(mc[2].str()),
                    std::stod(mc[3].str())
                }.normalized();
            }
        } else if (rhs.find("VERTEX_POINT") == 0) {
            std::smatch mr;
            if (std::regex_search(rhs, mr, reRefs)) {
                vertexToPt[eid] = std::stoi(mr[1].str());
            }
        } else if (rhs.find("EDGE_CURVE") == 0) {
            std::vector<int> refs;
            for (std::sregex_iterator it(rhs.begin(), rhs.end(), reRefs), end; it != end; ++it) {
                refs.push_back(std::stoi((*it)[1].str()));
            }
            if (refs.size() >= 2) {
                edgeCurves[eid] = {refs[0], refs[1]};
            }
        } else if (rhs.find("ORIENTED_EDGE") == 0) {
            std::vector<int> refs;
            for (std::sregex_iterator it(rhs.begin(), rhs.end(), reRefs), end; it != end; ++it) {
                refs.push_back(std::stoi((*it)[1].str()));
            }
            if (!refs.empty()) {
                orientedToEdge[eid] = refs.back();
            }
        } else if (rhs.find("EDGE_LOOP") == 0) {
            std::vector<int> refs;
            for (std::sregex_iterator it(rhs.begin(), rhs.end(), reRefs), end; it != end; ++it) {
                refs.push_back(std::stoi((*it)[1].str()));
            }
            edgeLoops[eid] = std::move(refs);
        } else if (rhs.find("FACE_OUTER_BOUND") == 0 || rhs.find("FACE_BOUND") == 0) {
            std::smatch mr;
            if (std::regex_search(rhs, mr, reRefs)) {
                boundToLoop[eid] = std::stoi(mr[1].str());
            }
        } else if (rhs.find("AXIS2_PLACEMENT_3D") == 0) {
            std::vector<int> refs;
            for (std::sregex_iterator it(rhs.begin(), rhs.end(), reRefs), end; it != end; ++it) {
                refs.push_back(std::stoi((*it)[1].str()));
            }
            if (refs.size() >= 3) {
                placements[eid] = {refs[0], refs[1], refs[2]};
            } else if (!refs.empty()) {
                placements[eid] = {refs[0], 0, 0};
            }
        } else if (rhs.find("PLANE") == 0) {
            std::string pname;
            std::smatch mn;
            if (std::regex_search(rhs, mn, reName)) pname = mn[1].str();
            std::smatch mr;
            int pid = 0;
            if (std::regex_search(rhs, mr, reRefs)) pid = std::stoi(mr[1].str());
            planes[eid] = {pname, pid};
        } else if (rhs.find("ADVANCED_FACE") == 0) {
            std::string fname;
            std::smatch mn;
            if (std::regex_search(rhs, mn, reName)) fname = mn[1].str();
            std::vector<int> refs;
            for (std::sregex_iterator it(rhs.begin(), rhs.end(), reRefs), end; it != end; ++it) {
                refs.push_back(std::stoi((*it)[1].str()));
            }
            const bool sense = (rhs.rfind(".F.") == std::string::npos);
            if (refs.size() >= 2) {
                advFaces[eid] = {fname, refs[0], refs.back(), sense};
            }
        } else if (rhs.find("CLOSED_SHELL") == 0) {
            std::string sname;
            std::smatch mn;
            if (std::regex_search(rhs, mn, reName)) sname = mn[1].str();
            std::vector<int> refs;
            for (std::sregex_iterator it(rhs.begin(), rhs.end(), reRefs), end; it != end; ++it) {
                refs.push_back(std::stoi((*it)[1].str()));
            }
            closedShells[eid] = {sname, std::move(refs)};
        } else if (rhs.find("MANIFOLD_SOLID_BREP") == 0) {
            std::string bname;
            std::smatch mn;
            if (std::regex_search(rhs, mn, reName)) bname = mn[1].str();
            std::smatch mr;
            int sid = 0;
            if (std::regex_search(rhs, mr, reRefs)) sid = std::stoi(mr[1].str());
            manifoldSolids.push_back({eid, bname, sid});
        } else if (rhs.find("PRODUCT(") == 0) {
            std::smatch mn;
            if (std::regex_search(rhs, mn, reName) && !mn[1].str().empty()) {
                fallbackProductName = mn[1].str();
            }
        }
    }
    } catch (const std::exception&) {
        return false;
    }

    if (manifoldSolids.empty()) {
        return false;
    }

    auto resolveVertexPoint = [&](int vid) -> std::optional<core::Vec3> {
        auto itV = vertexToPt.find(vid);
        if (itV == vertexToPt.end()) return std::nullopt;
        auto itP = cartesianPts.find(itV->second);
        if (itP == cartesianPts.end()) return std::nullopt;
        return itP->second;
    };

    int solidCounter = 0;
    for (const auto& ms : manifoldSolids) {
        ++solidCounter;
        SolidRecord rec;
        rec.handleId = nextHandleId_++;
        rec.sourceFile = filePath;
        rec.contentHash = hashStepFile(filePath);
        rec.name = ms.name.empty()
            ? (fallbackProductName + "_Solid_" + std::to_string(solidCounter))
            : ms.name;

        core::Vec3 globalMin{1e18, 1e18, 1e18};
        core::Vec3 globalMax{-1e18, -1e18, -1e18};
        bool hasAnyPt = false;

        std::unordered_map<int, int> stepEdgeIdToLocalIdx;

        auto itShell = closedShells.find(ms.shellId);
        if (itShell != closedShells.end()) {
            int localFaceIdx = 0;
            for (int advFaceId : itShell->second.faceIds) {
                auto itAF = advFaces.find(advFaceId);
                if (itAF == advFaces.end()) continue;

                CadFaceInfo finfo;
                finfo.faceIndex = localFaceIdx++;
                finfo.name = itAF->second.name;
                finfo.bboxMin = {1e18, 1e18, 1e18};
                finfo.bboxMax = {-1e18, -1e18, -1e18};

                auto itPl = planes.find(itAF->second.surfId);
                if (itPl != planes.end()) {
                    finfo.surfaceType = CadSurfaceType::Plane;
                    auto itAx = placements.find(itPl->second.placementId);
                    if (itAx != placements.end()) {
                        if (cartesianPts.count(itAx->second.ptId)) {
                            finfo.origin = cartesianPts[itAx->second.ptId];
                        }
                        if (directions.count(itAx->second.dirZId)) {
                            finfo.outwardNormal = directions[itAx->second.dirZId];
                            if (!itAF->second.sameSense) {
                                finfo.outwardNormal = -finfo.outwardNormal;
                            }
                        }
                    }
                } else {
                    finfo.surfaceType = CadSurfaceType::Cylinder;
                }

                // Traverse face loop edges
                auto itLoopId = boundToLoop.find(itAF->second.boundId);
                if (itLoopId != boundToLoop.end() && edgeLoops.count(itLoopId->second)) {
                    for (int oeId : edgeLoops[itLoopId->second]) {
                        auto itECId = orientedToEdge.find(oeId);
                        if (itECId == orientedToEdge.end()) continue;
                        const int ecId = itECId->second;
                        auto itEC = edgeCurves.find(ecId);
                        if (itEC == edgeCurves.end()) continue;

                        auto p1 = resolveVertexPoint(itEC->second.v1);
                        auto p2 = resolveVertexPoint(itEC->second.v2);
                        if (!p1 || !p2) continue;

                        for (const auto& p : {*p1, *p2}) {
                            finfo.bboxMin.x = std::min(finfo.bboxMin.x, p.x);
                            finfo.bboxMin.y = std::min(finfo.bboxMin.y, p.y);
                            finfo.bboxMin.z = std::min(finfo.bboxMin.z, p.z);
                            finfo.bboxMax.x = std::max(finfo.bboxMax.x, p.x);
                            finfo.bboxMax.y = std::max(finfo.bboxMax.y, p.y);
                            finfo.bboxMax.z = std::max(finfo.bboxMax.z, p.z);

                            globalMin.x = std::min(globalMin.x, p.x);
                            globalMin.y = std::min(globalMin.y, p.y);
                            globalMin.z = std::min(globalMin.z, p.z);
                            globalMax.x = std::max(globalMax.x, p.x);
                            globalMax.y = std::max(globalMax.y, p.y);
                            globalMax.z = std::max(globalMax.z, p.z);
                            hasAnyPt = true;
                        }

                        int localEdgeIdx = -1;
                        auto itExisting = stepEdgeIdToLocalIdx.find(ecId);
                        if (itExisting == stepEdgeIdToLocalIdx.end()) {
                            localEdgeIdx = static_cast<int>(rec.edges.size());
                            stepEdgeIdToLocalIdx[ecId] = localEdgeIdx;
                            CadEdgeInfo einfo;
                            einfo.edgeIndex = localEdgeIdx;
                            einfo.startPt = *p1;
                            einfo.endPt = *p2;
                            einfo.midPt = (*p1 + *p2) * 0.5;
                            const core::Vec3 diff = *p2 - *p1;
                            einfo.lengthMm = diff.norm();
                            einfo.tangent = diff.normalized();
                            einfo.isLinear = true;
                            einfo.adjacentFaceIndices.push_back(finfo.faceIndex);
                            rec.edges.push_back(std::move(einfo));
                        } else {
                            localEdgeIdx = itExisting->second;
                            auto& adj = rec.edges[localEdgeIdx].adjacentFaceIndices;
                            if (std::find(adj.begin(), adj.end(), finfo.faceIndex) == adj.end()) {
                                adj.push_back(finfo.faceIndex);
                            }
                        }
                    }
                }
                rec.faces.push_back(std::move(finfo));
            }
        }

        // Fallback if shell faces were complex B-Splines/Cylinders not expanded inline
        if (!hasAnyPt && !cartesianPts.empty()) {
            for (const auto& kv : cartesianPts) {
                globalMin.x = std::min(globalMin.x, kv.second.x);
                globalMin.y = std::min(globalMin.y, kv.second.y);
                globalMin.z = std::min(globalMin.z, kv.second.z);
                globalMax.x = std::max(globalMax.x, kv.second.x);
                globalMax.y = std::max(globalMax.y, kv.second.y);
                globalMax.z = std::max(globalMax.z, kv.second.z);
            }
        }

        rec.bboxMin = globalMin;
        rec.bboxMax = globalMax;

        core::SceneNode node;
        node.id = "part_" + std::to_string(rec.handleId);
        node.name = rec.name;
        node.type = core::SceneNodeType::Part;
        node.sourceCadFile = filePath;
        node.cadHandleId = rec.handleId;

        node.collisionRole = collisionRoleForPartName(rec.name);

        node.renderMesh = detail::makeBoxMesh(rec.bboxMin, rec.bboxMax);
        node.collisionMesh = node.renderMesh;

        solids_[rec.handleId] = std::move(rec);
        outPartNodes.push_back(std::move(node));
    }

    return !outPartNodes.empty();
}

bool OcctCadKernel::importStl(const std::string& filePath, core::TriMesh& outMesh) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }

    outMesh = core::TriMesh{};
    outMesh.bboxMin = {1e18, 1e18, 1e18};
    outMesh.bboxMax = {-1e18, -1e18, -1e18};

    // Check binary STL header (80 bytes + uint32 triangle count)
    ifs.seekg(0, std::ios::end);
    const std::streamsize fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (fileSize >= 84) {
        char header[80]{};
        ifs.read(header, 80);
        uint32_t triCount = 0;
        ifs.read(reinterpret_cast<char*>(&triCount), sizeof(uint32_t));
        const std::streamsize expectedSize = 84 + static_cast<std::streamsize>(triCount) * 50;
        if (expectedSize == fileSize && triCount > 0) {
            outMesh.vertices.reserve(triCount * 3);
            outMesh.triangles.reserve(triCount);
            for (uint32_t i = 0; i < triCount; ++i) {
                float normal[3]{};
                float v[9]{};
                uint16_t attr = 0;
                ifs.read(reinterpret_cast<char*>(normal), 12);
                ifs.read(reinterpret_cast<char*>(v), 36);
                ifs.read(reinterpret_cast<char*>(&attr), 2);
                const uint32_t baseIdx = static_cast<uint32_t>(outMesh.vertices.size());
                for (int k = 0; k < 3; ++k) {
                    core::Vec3 pt{v[k * 3 + 0], v[k * 3 + 1], v[k * 3 + 2]};
                    outMesh.vertices.push_back(pt);
                    outMesh.bboxMin.x = std::min(outMesh.bboxMin.x, pt.x);
                    outMesh.bboxMin.y = std::min(outMesh.bboxMin.y, pt.y);
                    outMesh.bboxMin.z = std::min(outMesh.bboxMin.z, pt.z);
                    outMesh.bboxMax.x = std::max(outMesh.bboxMax.x, pt.x);
                    outMesh.bboxMax.y = std::max(outMesh.bboxMax.y, pt.y);
                    outMesh.bboxMax.z = std::max(outMesh.bboxMax.z, pt.z);
                }
                outMesh.triangles.push_back({baseIdx, baseIdx + 1, baseIdx + 2});
            }
            return true;
        }
    }
    return false;
}


bool OcctCadKernel::importRobotStlMillimeters(const std::string& filePath, core::TriMesh& outMesh) {
    if (!importStl(filePath, outMesh)) {
        return false;
    }
    constexpr double kMetersToMm = 1000.0;
    for (auto& vertex : outMesh.vertices) {
        vertex = vertex * kMetersToMm;
    }
    outMesh.bboxMin = outMesh.bboxMin * kMetersToMm;
    outMesh.bboxMax = outMesh.bboxMax * kMetersToMm;
    return true;
}

bool OcctCadKernel::importStepViaOcct(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes) {
#ifdef OLP_HAS_OCCT
    outPartNodes.clear();
    return importStepWithOcct(filePath, outPartNodes);
#else
    (void)filePath;
    outPartNodes.clear();
    return false;
#endif
}

bool OcctCadKernel::importStepViaBuiltin(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes) {
    outPartNodes.clear();
    return importStepBuiltin(filePath, outPartNodes);
}

#ifdef OLP_HAS_OCCT
namespace {

bool isGenericShapeName(const std::string& name) {
    if (name.empty()) return true;
    static const char* kGeneric[] = {
        "SOLID", "SHELL", "FACE", "EDGE", "COMPOUND", "ASSEMBLY",
        "COMPSOLID", "WIRE", "VERTEX", "SHAPE", "ADVANCED_FACE",
        "CLOSED_SHELL", "OPEN_SHELL"
    };
    for (const char* generic : kGeneric) {
        if (name == generic) return true;
    }
    if (name.rfind("=>", 0) == 0) return true;
    bool allDigits = true;
    for (unsigned char ch : name) {
        if (!std::isdigit(ch)) {
            allDigits = false;
            break;
        }
    }
    return allDigits;
}

std::string xcafLabelName(const TDF_Label& label) {
    Handle(TDataStd_Name) nameAttr;
    if (!label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) return {};
    return TCollection_AsciiString(nameAttr->Get()).ToCString();
}

struct NamedSolid {
    TopoDS_Shape shape;
    std::string name;
    bool claimed{false};
};

void addNamedSolid(std::vector<NamedSolid>& out, const TopoDS_Shape& shape, const std::string& name) {
    if (shape.IsNull() || isGenericShapeName(name)) return;
    if (shape.ShapeType() == TopAbs_SOLID) {
        out.push_back({shape, name, false});
        return;
    }
    int solidCount = 0;
    TopoDS_Shape onlySolid;
    for (TopExp_Explorer solids(shape, TopAbs_SOLID); solids.More(); solids.Next()) {
        ++solidCount;
        onlySolid = solids.Current();
    }
    if (solidCount == 1) out.push_back({onlySolid, name, false});
}

void collectStepSolidNames(STEPCAFControl_Reader& reader, std::vector<NamedSolid>& out) {
    const Handle(XSControl_WorkSession) session = reader.ChangeReader().WS();
    if (session.IsNull() || session->TransferReader().IsNull()) return;
    const Handle(Interface_InterfaceModel) model = session->Model();
    if (model.IsNull()) return;
    const Handle(XSControl_TransferReader)& transfer = session->TransferReader();
    const Handle(Transfer_TransientProcess) process = session->MapReader();
    for (int i = 1; i <= model->NbEntities(); ++i) {
        const Handle(StepShape_SolidModel) solidEnt =
            Handle(StepShape_SolidModel)::DownCast(model->Value(i));
        if (solidEnt.IsNull() || solidEnt->Name().IsNull() || solidEnt->Name()->IsEmpty()) continue;
        // HasResult() stays false for these solids after a CAF transfer, but ShapeResult()
        // still returns the solid. Do not gate on HasResult().
        TopoDS_Shape shape = transfer->ShapeResult(solidEnt);
        if (shape.IsNull() && !process.IsNull()) {
            shape = TransferBRep::ShapeResult(process, solidEnt);
        }
        addNamedSolid(out, shape, solidEnt->Name()->ToCString());
    }
}

bool labelSeen(const std::vector<TDF_Label>& seen, const TDF_Label& label) {
    for (const TDF_Label& item : seen) {
        if (item.IsEqual(label)) return true;
    }
    return false;
}

void collectXcafSolidNames(const TDF_Label& label, std::vector<NamedSolid>& out, std::vector<TDF_Label>& seen) {
    if (label.IsNull() || labelSeen(seen, label)) return;
    seen.push_back(label);

    TopoDS_Shape shape;
    const std::string name = xcafLabelName(label);
    if (XCAFDoc_ShapeTool::GetShape(label, shape) && !isGenericShapeName(name) &&
        !XCAFDoc_ShapeTool::IsAssembly(label)) {
        int solidCount = 0;
        TopoDS_Shape onlySolid;
        for (TopExp_Explorer solids(shape, TopAbs_SOLID); solids.More(); solids.Next()) {
            ++solidCount;
            onlySolid = solids.Current();
        }
        if (shape.ShapeType() == TopAbs_SOLID || solidCount == 1) {
            addNamedSolid(out, shape.ShapeType() == TopAbs_SOLID ? shape : onlySolid, name);
        }
    }

    if (XCAFDoc_ShapeTool::IsAssembly(label)) {
        NCollection_Sequence<TDF_Label> components;
        if (XCAFDoc_ShapeTool::GetComponents(label, components)) {
            for (int i = 1; i <= components.Length(); ++i) {
                collectXcafSolidNames(components.Value(i), out, seen);
                TDF_Label referred;
                if (XCAFDoc_ShapeTool::GetReferredShape(components.Value(i), referred)) {
                    collectXcafSolidNames(referred, out, seen);
                }
            }
        }
    }

    NCollection_Sequence<TDF_Label> subShapes;
    if (XCAFDoc_ShapeTool::GetSubShapes(label, subShapes)) {
        for (int i = 1; i <= subShapes.Length(); ++i) {
            collectXcafSolidNames(subShapes.Value(i), out, seen);
        }
    }
}

std::string matchSolidName(const TopoDS_Shape& solid, std::vector<NamedSolid>& named) {
    for (NamedSolid& item : named) {
        if (item.claimed) continue;
        if (solid.IsEqual(item.shape) || solid.IsPartner(item.shape)) {
            item.claimed = true;
            return item.name;
        }
    }

    GProp_GProps solidProps;
    BRepGProp::VolumeProperties(solid, solidProps);
    const gp_Pnt center = solidProps.CentreOfMass();
    const double volume = std::abs(solidProps.Mass());
    double bestDist = 0.05;
    NamedSolid* best = nullptr;
    for (NamedSolid& item : named) {
        if (item.claimed) continue;
        GProp_GProps itemProps;
        BRepGProp::VolumeProperties(item.shape, itemProps);
        const double dist = center.Distance(itemProps.CentreOfMass());
        const double itemVolume = std::abs(itemProps.Mass());
        const double scale = std::max(1.0, volume);
        if (dist < bestDist && std::abs(itemVolume - volume) / scale < 1e-3) {
            bestDist = dist;
            best = &item;
            if (dist < 1e-6) break;
        }
    }
    if (best == nullptr) return {};
    best->claimed = true;
    return best->name;
}

int faceIndexOf(const std::vector<TopoDS_Face>& faces, const TopoDS_Shape& shape) {
    for (int i = 0; i < static_cast<int>(faces.size()); ++i) {
        if (faces[static_cast<std::size_t>(i)].IsSame(shape) ||
            faces[static_cast<std::size_t>(i)].IsEqual(shape)) {
            return i;
        }
    }
    return -1;
}

} // namespace

bool OcctCadKernel::importStepWithOcct(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes) {
    Interface_Static::SetCVal("xstep.cascade.unit", "MM");
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument("MDTV-XCAF", doc);

    STEPCAFControl_Reader reader;
    reader.SetColorMode(true);
    reader.SetNameMode(true);
    if (reader.ReadFile(filePath.c_str()) != IFSelect_RetDone) {
        return false;
    }
    if (!reader.Transfer(doc)) {
        return false;
    }

    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    NCollection_Sequence<TDF_Label> roots;
    shapeTool->GetFreeShapes(roots);
    const std::string fingerprint = hashStepFile(filePath);
    std::vector<NamedSolid> namedSolids;
    collectStepSolidNames(reader, namedSolids);

    for (int i = 1; i <= roots.Length(); ++i) {
        const TDF_Label label = roots.Value(i);
        const TopoDS_Shape shape = shapeTool->GetShape(label);
        if (shape.IsNull()) continue;

        std::vector<TDF_Label> seenLabels;
        collectXcafSolidNames(label, namedSolids, seenLabels);
        const std::string rootName = xcafLabelName(label);
        int solidCount = 0;
        for (TopExp_Explorer countSolids(shape, TopAbs_SOLID); countSolids.More(); countSolids.Next()) {
            ++solidCount;
        }

        TopExp_Explorer solids(shape, TopAbs_SOLID);
        for (; solids.More(); solids.Next()) {
            const TopoDS_Solid solid = TopoDS::Solid(solids.Current());
            SolidRecord rec;
            rec.handleId = nextHandleId_++;
            rec.sourceFile = filePath;
            rec.contentHash = fingerprint;
            rec.name = matchSolidName(solid, namedSolids);
            if (rec.name.empty() && solidCount == 1 && !isGenericShapeName(rootName)) {
                rec.name = rootName;
            }
            if (rec.name.empty()) {
                rec.name = "OCCT_Solid_" + std::to_string(rec.handleId);
            }

            std::vector<TopoDS_Face> faceShapes;
            for (TopExp_Explorer faces(solid, TopAbs_FACE); faces.More(); faces.Next()) {
                const TopoDS_Face face = TopoDS::Face(faces.Current());
                faceShapes.push_back(face);
                BRepAdaptor_Surface surf(face);
                const double u0 = surf.FirstUParameter();
                const double u1 = surf.LastUParameter();
                const double v0 = surf.FirstVParameter();
                const double v1 = surf.LastVParameter();
                const double uMid = (std::isfinite(u0) && std::isfinite(u1)) ? 0.5 * (u0 + u1) : 0.0;
                const double vMid = (std::isfinite(v0) && std::isfinite(v1)) ? 0.5 * (v0 + v1) : 0.0;
                gp_Pnt originPnt;
                surf.D0(uMid, vMid, originPnt);
                BRepLProp_SLProps props(surf, uMid, vMid, 1, 1e-7);
                core::Vec3 normal{0.0, 0.0, 1.0};
                if (props.IsNormalDefined()) {
                    gp_Dir n = props.Normal();
                    if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
                    normal = {n.X(), n.Y(), n.Z()};
                }
                CadFaceInfo finfo;
                finfo.faceIndex = static_cast<int>(rec.faces.size());
                finfo.origin = {originPnt.X(), originPnt.Y(), originPnt.Z()};
                finfo.outwardNormal = normal;
                finfo.surfaceType = (surf.GetType() == GeomAbs_Plane) ? CadSurfaceType::Plane : CadSurfaceType::Other;
                Bnd_Box faceBox;
                BRepBndLib::Add(face, faceBox);
                if (!faceBox.IsVoid()) {
                    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
                    faceBox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                    finfo.bboxMin = {xmin, ymin, zmin};
                    finfo.bboxMax = {xmax, ymax, zmax};
                }
                rec.faces.push_back(std::move(finfo));
            }

            NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>
                edgeAncestors;
            TopExp::MapShapesAndAncestors(solid, TopAbs_EDGE, TopAbs_FACE, edgeAncestors);

            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edgeMap;
            TopExp::MapShapes(solid, TopAbs_EDGE, edgeMap);
            std::vector<TopoDS_Edge> edgeList;
            for (int e = 1; e <= edgeMap.Extent(); ++e) {
                const TopoDS_Edge edge = TopoDS::Edge(edgeMap(e));
                if (BRep_Tool::Degenerated(edge)) continue;
                BRepAdaptor_Curve curve(edge);
                const double length = GCPnts_AbscissaPoint::Length(curve);
                gp_Pnt start = curve.Value(curve.FirstParameter());
                gp_Pnt end = curve.Value(curve.LastParameter());
                GCPnts_AbscissaPoint mid(curve, std::max(0.0, length * 0.5), curve.FirstParameter());
                gp_Pnt midPt = start;
                gp_Vec tangent(1.0, 0.0, 0.0);
                if (mid.IsDone()) {
                    curve.D1(mid.Parameter(), midPt, tangent);
                }
                // Curve parameter direction, not the face-loop orientation. Flipping a
                // reversed edge starts the fillet at the other end.

                CadEdgeInfo einfo;
                einfo.edgeIndex = static_cast<int>(rec.edges.size());
                einfo.startPt = {start.X(), start.Y(), start.Z()};
                einfo.endPt = {end.X(), end.Y(), end.Z()};
                einfo.midPt = {midPt.X(), midPt.Y(), midPt.Z()};
                einfo.tangent = core::Vec3{tangent.X(), tangent.Y(), tangent.Z()}.normalized();
                einfo.lengthMm = length;
                einfo.isLinear = curve.GetType() == GeomAbs_Line;
                if (edgeAncestors.Contains(edge)) {
                    for (const TopoDS_Shape& parent : edgeAncestors.FindFromKey(edge)) {
                        const int faceIndex = faceIndexOf(faceShapes, parent);
                        if (faceIndex >= 0 &&
                            std::find(einfo.adjacentFaceIndices.begin(), einfo.adjacentFaceIndices.end(),
                                      faceIndex) == einfo.adjacentFaceIndices.end()) {
                            einfo.adjacentFaceIndices.push_back(faceIndex);
                        }
                    }
                }
                rec.edges.push_back(std::move(einfo));
                edgeList.push_back(edge);
            }

            Bnd_Box box;
            BRepBndLib::Add(solid, box);
            if (!box.IsVoid()) {
                double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
                box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                rec.bboxMin = {xmin, ymin, zmin};
                rec.bboxMax = {xmax, ymax, zmax};
            }

            core::SceneNode node;
            node.id = "part_" + std::to_string(rec.handleId);
            node.name = rec.name;
            node.type = core::SceneNodeType::Part;
            node.sourceCadFile = filePath;
            node.cadHandleId = rec.handleId;
            node.collisionRole = collisionRoleForPartName(rec.name);

            shapes_[rec.handleId] = solid;
            edgeShapes_[rec.handleId] = std::move(edgeList);
            faceShapes_[rec.handleId] = std::move(faceShapes);
            solids_[rec.handleId] = std::move(rec);

            tessellate(node.cadHandleId, 0.2, 2.0, node.renderMesh, node.collisionMesh);
            outPartNodes.push_back(std::move(node));
        }
    }
    return !outPartNodes.empty();
}
#endif


} // namespace olp::cad
