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


namespace olp::cad {

std::vector<CadEdgeInfo> OcctCadKernel::getEdges(int cadHandleId) const {
    auto it = solids_.find(cadHandleId);
    if (it == solids_.end()) return {};
    return it->second.edges;
}

std::vector<CadFaceInfo> OcctCadKernel::getFaces(int cadHandleId) const {
    auto it = solids_.find(cadHandleId);
    if (it == solids_.end()) return {};
    return it->second.faces;
}

std::vector<int> OcctCadKernel::getAdjacentFaces(int cadHandleId, int edgeIndex) const {
    auto it = solids_.find(cadHandleId);
    if (it == solids_.end()) return {};
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(it->second.edges.size())) return {};
    return it->second.edges[edgeIndex].adjacentFaceIndices;
}

double OcctCadKernel::pointToFaceBoxDistance(const core::Vec3& pt, const CadFaceInfo& face) noexcept {
    // Distance to bounded planar face
    const double planeDist = std::abs((pt - face.origin).dot(face.outwardNormal));
    double dx = 0.0, dy = 0.0, dz = 0.0;
    if (pt.x < face.bboxMin.x) dx = face.bboxMin.x - pt.x;
    else if (pt.x > face.bboxMax.x) dx = pt.x - face.bboxMax.x;
    if (pt.y < face.bboxMin.y) dy = face.bboxMin.y - pt.y;
    else if (pt.y > face.bboxMax.y) dy = pt.y - face.bboxMax.y;
    if (pt.z < face.bboxMin.z) dz = face.bboxMin.z - pt.z;
    else if (pt.z > face.bboxMax.z) dz = pt.z - face.bboxMax.z;
    const double boxDist = std::sqrt(dx * dx + dy * dy + dz * dz);
    return std::max(planeDist, boxDist);
}

std::optional<CrossPartFaceMatch> OcctCadKernel::findClosestMatingFaceAcrossParts(
    int sourceCadHandleId,
    int edgeIndex,
    const std::vector<int>& candidateCadHandles,
    double maxDistanceTolMm) const {
    auto itSrc = solids_.find(sourceCadHandleId);
    if (itSrc == solids_.end()) return std::nullopt;
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(itSrc->second.edges.size())) return std::nullopt;

    const CadEdgeInfo& edge = itSrc->second.edges[edgeIndex];
    const core::Vec3 samplePt = edge.midPt;

    std::optional<CrossPartFaceMatch> bestMatch;
    double bestDist = maxDistanceTolMm;

    for (int candHandle : candidateCadHandles) {
        if (candHandle == sourceCadHandleId) continue;
        auto itCand = solids_.find(candHandle);
        if (itCand == solids_.end()) continue;

        for (const auto& face : itCand->second.faces) {
            // Ensure edge tangent lies approximately in the candidate face plane
            if (std::abs(edge.tangent.dot(face.outwardNormal)) > 0.25) {
                continue;
            }
            const double dist = pointToFaceBoxDistance(samplePt, face);
            if (dist <= bestDist) {
                bestDist = dist;
                bestMatch = CrossPartFaceMatch{
                    candHandle,
                    itCand->second.name,
                    face.faceIndex,
                    dist,
                    face.outwardNormal
                };
            }
        }
    }
    return bestMatch;
}

bool OcctCadKernel::evaluateEdgeAtArcLength(
    int cadHandleId,
    int edgeIndex,
    double arcLengthMm,
    core::Vec3& outPoint,
    core::Vec3& outTangent) const {
    auto it = solids_.find(cadHandleId);
#ifdef OLP_HAS_OCCT
    auto itEdges = edgeShapes_.find(cadHandleId);
    if (itEdges != edgeShapes_.end() && edgeIndex >= 0 && edgeIndex < static_cast<int>(itEdges->second.size())) {
        const TopoDS_Edge& edge = itEdges->second[edgeIndex];
        BRepAdaptor_Curve curve(edge);
        const double fParam = curve.FirstParameter();
        const double lParam = curve.LastParameter();
        double totalLen = 0.0;
        if (it != solids_.end() && edgeIndex < static_cast<int>(it->second.edges.size()) &&
            it->second.edges[edgeIndex].lengthMm > 1e-7) {
            totalLen = it->second.edges[edgeIndex].lengthMm;
        } else {
            totalLen = GCPnts_AbscissaPoint::Length(curve);
        }
        const double clampedS = std::clamp(arcLengthMm, 0.0, totalLen);
        double param = fParam;
        if (clampedS <= 1e-7) {
            param = fParam;
        } else if (clampedS >= totalLen - 1e-7) {
            param = lParam;
        } else {
            GCPnts_AbscissaPoint absPt(curve, clampedS, fParam);
            if (absPt.IsDone()) {
                param = absPt.Parameter();
            } else {
                const double ratio = (totalLen > 1e-9) ? (clampedS / totalLen) : 0.0;
                param = fParam + ratio * (lParam - fParam);
            }
        }
        gp_Pnt p;
        gp_Vec v;
        curve.D1(param, p, v);
        outPoint = {p.X(), p.Y(), p.Z()};
        outTangent = (v.Magnitude() > 1e-7) ? core::Vec3{v.X(), v.Y(), v.Z()}.normalized() : core::Vec3{1, 0, 0};
        return true;
    }
#endif
    if (it == solids_.end()) return false;
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(it->second.edges.size())) return false;

    const auto& edge = it->second.edges[edgeIndex];
    const double clampedS = std::clamp(arcLengthMm, 0.0, edge.lengthMm);
    const double ratio = (edge.lengthMm > 1e-9) ? (clampedS / edge.lengthMm) : 0.0;
    outPoint = edge.startPt + (edge.endPt - edge.startPt) * ratio;
    outTangent = edge.tangent;
    return true;
}

bool OcctCadKernel::evaluateFaceOutwardNormal(
    int cadHandleId,
    int faceIndex,
    const core::Vec3& samplePoint,
    core::Vec3& outNormal) const {
    auto it = solids_.find(cadHandleId);
    if (it == solids_.end()) return false;
    if (faceIndex < 0 || faceIndex >= static_cast<int>(it->second.faces.size())) return false;
    const auto& finfo = it->second.faces[faceIndex];

#ifdef OLP_HAS_OCCT
    if (finfo.surfaceType != CadSurfaceType::Plane) {
        auto itFaces = faceShapes_.find(cadHandleId);
        if (itFaces != faceShapes_.end() && faceIndex < static_cast<int>(itFaces->second.size())) {
            const TopoDS_Face& face = itFaces->second[faceIndex];
            TopLoc_Location loc;
            const Handle(Geom_Surface) geomSurf = BRep_Tool::Surface(face, loc);
            if (!geomSurf.IsNull()) {
                gp_Pnt queryPt(samplePoint.x, samplePoint.y, samplePoint.z);
                if (!loc.IsIdentity()) {
                    queryPt.Transform(loc.Transformation().Inverted());
                }
                ShapeAnalysis_Surface sas(geomSurf);
                const gp_Pnt2d uv = sas.ValueOfUV(queryPt, 1e-4);
                BRepAdaptor_Surface surf(face, false);
                BRepLProp_SLProps props(surf, uv.X(), uv.Y(), 1, 1e-7);
                if (props.IsNormalDefined()) {
                    gp_Dir n = props.Normal();
                    if (face.Orientation() == TopAbs_REVERSED) {
                        n.Reverse();
                    }
                    outNormal = {n.X(), n.Y(), n.Z()};
                    return true;
                }
            }
        }
    }
#endif
    outNormal = finfo.outwardNormal;
    return true;
}

#ifdef OLP_HAS_OCCT
struct ChainStepCandidate {
    int nextEdgeIdx{-1};
    bool isReversed{false};
    gp_Pnt nextPt{};
};

static gp_Dir getCurveTangentAtParam(const TopoDS_Edge& e, double param) {
    BRepAdaptor_Curve curve(e);
    gp_Pnt p;
    gp_Vec v;
    curve.D1(param, p, v);
    if (v.Magnitude() > 1e-7) {
        return gp_Dir(v);
    }
    return gp_Dir(1, 0, 0);
}

static ChainStepCandidate findNextContinuousEdge(
    const gp_Pnt& currPt,
    const gp_Dir& outDir,
    const std::set<int>& visitedEdges,
    const std::vector<TopoDS_Edge>& edgeShapes,
    double maxAngleRad) {
    ChainStepCandidate best;
    double minAngle = maxAngleRad + 1e-4;

    for (size_t i = 0; i < edgeShapes.size(); ++i) {
        const int candIdx = static_cast<int>(i);
        if (visitedEdges.count(candIdx) > 0) {
            continue;
        }

        const TopoDS_Edge& candEdge = edgeShapes[i];
        BRepAdaptor_Curve curve(candEdge);
        const double fParam = curve.FirstParameter();
        const double lParam = curve.LastParameter();
        const gp_Pnt pStart = curve.Value(fParam);
        const gp_Pnt pEnd = curve.Value(lParam);

        const double dStart = currPt.Distance(pStart);
        const double dEnd = currPt.Distance(pEnd);

        const bool matchStart = (dStart < 0.05);
        const bool matchEnd = (dEnd < 0.05);

        if (!matchStart && !matchEnd) {
            continue;
        }

        gp_Dir candDir;
        bool candReversed = false;
        gp_Pnt farPt;

        if (matchStart && (!matchEnd || dStart <= dEnd)) {
            candDir = getCurveTangentAtParam(candEdge, fParam);
            candReversed = false;
            farPt = pEnd;
        } else {
            candDir = getCurveTangentAtParam(candEdge, lParam).Reversed();
            candReversed = true;
            farPt = pStart;
        }

        const double angle = outDir.Angle(candDir);
        if (angle <= maxAngleRad && angle < minAngle) {
            minAngle = angle;
            best.nextEdgeIdx = candIdx;
            best.isReversed = candReversed;
            best.nextPt = farPt;
        }
    }
    return best;
}
#endif

EdgeChainResult OcctCadKernel::findTangentContinuousEdgeChain(
    int cadHandleId,
    int startEdgeIndex,
    double maxDeflectionAngleDeg) const {
    EdgeChainResult result;
#ifdef OLP_HAS_OCCT
    auto itEdges = edgeShapes_.find(cadHandleId);
    if (itEdges == edgeShapes_.end()) {
        return result;
    }
    const auto& edgeShapes = itEdges->second;
    if (startEdgeIndex < 0 || startEdgeIndex >= static_cast<int>(edgeShapes.size())) {
        return result;
    }

    auto itSolid = solids_.find(cadHandleId);
    if (itSolid == solids_.end() || itSolid->second.edges.empty()) {
        return result;
    }
    const auto& edgeInfos = itSolid->second.edges;

    auto itShape = shapes_.find(cadHandleId);
    if (itShape == shapes_.end() || itShape->second.IsNull()) {
        ChainedEdge single;
        single.edgeIndex = startEdgeIndex;
        single.isReversed = false;
        single.lengthMm = edgeInfos[startEdgeIndex].lengthMm;
        result.edges.push_back(single);
        result.totalLengthMm = single.lengthMm;
        result.isClosed = false;
        return result;
    }

    const double maxAngleRad = core::deg2rad(std::clamp(maxDeflectionAngleDeg, 0.1, 90.0));

    std::set<int> visited;
    visited.insert(startEdgeIndex);

    const TopoDS_Edge seedEdge = edgeShapes[startEdgeIndex];
    BRepAdaptor_Curve seedCurve(seedEdge);
    const double seedFirstParam = seedCurve.FirstParameter();
    const double seedLastParam = seedCurve.LastParameter();
    const gp_Pnt seedPStart = seedCurve.Value(seedFirstParam);
    const gp_Pnt seedPEnd = seedCurve.Value(seedLastParam);
    const gp_Dir seedStartDir = getCurveTangentAtParam(seedEdge, seedFirstParam);
    const gp_Dir seedEndDir = getCurveTangentAtParam(seedEdge, seedLastParam);

    std::vector<ChainedEdge> forwardChain;
    gp_Pnt currPt = seedPEnd;
    gp_Dir currOutDir = seedEndDir;
    bool loopClosed = false;

    if (seedPStart.Distance(seedPEnd) < 0.05 && seedEndDir.Angle(seedStartDir) <= maxAngleRad) {
        loopClosed = true;
    }

    while (!loopClosed && seedPStart.Distance(seedPEnd) >= 0.05) {
        auto step = findNextContinuousEdge(currPt, currOutDir, visited, edgeShapes, maxAngleRad);
        if (step.nextEdgeIdx < 0) {
            break;
        }

        visited.insert(step.nextEdgeIdx);
        ChainedEdge ce;
        ce.edgeIndex = step.nextEdgeIdx;
        ce.isReversed = step.isReversed;
        ce.lengthMm = edgeInfos[step.nextEdgeIdx].lengthMm;
        forwardChain.push_back(ce);

        const TopoDS_Edge nextEdge = edgeShapes[step.nextEdgeIdx];
        BRepAdaptor_Curve nextCurve(nextEdge);
        if (step.isReversed) {
            currOutDir = getCurveTangentAtParam(nextEdge, nextCurve.FirstParameter()).Reversed();
        } else {
            currOutDir = getCurveTangentAtParam(nextEdge, nextCurve.LastParameter());
        }

        if (step.nextPt.Distance(seedPStart) < 0.05) {
            if (currOutDir.Angle(seedStartDir) <= maxAngleRad) {
                loopClosed = true;
            }
            break;
        }

        currPt = step.nextPt;
    }

    std::vector<ChainedEdge> backwardChain;
    if (!loopClosed && seedPStart.Distance(seedPEnd) >= 0.05) {
        currPt = seedPStart;
        currOutDir = seedStartDir.Reversed();

        while (true) {
            auto step = findNextContinuousEdge(currPt, currOutDir, visited, edgeShapes, maxAngleRad);
            if (step.nextEdgeIdx < 0) {
                break;
            }

            visited.insert(step.nextEdgeIdx);
            ChainedEdge ce;
            ce.edgeIndex = step.nextEdgeIdx;
            ce.isReversed = !step.isReversed;
            ce.lengthMm = edgeInfos[step.nextEdgeIdx].lengthMm;
            backwardChain.push_back(ce);

            currPt = step.nextPt;
            const TopoDS_Edge nextEdge = edgeShapes[step.nextEdgeIdx];
            BRepAdaptor_Curve nextCurve(nextEdge);
            if (step.isReversed) {
                currOutDir = getCurveTangentAtParam(nextEdge, nextCurve.FirstParameter()).Reversed();
            } else {
                currOutDir = getCurveTangentAtParam(nextEdge, nextCurve.LastParameter());
            }
        }
    }

    std::vector<ChainedEdge> finalChain;
    for (auto it = backwardChain.rbegin(); it != backwardChain.rend(); ++it) {
        finalChain.push_back(*it);
    }
    ChainedEdge seed;
    seed.edgeIndex = startEdgeIndex;
    seed.isReversed = false;
    seed.lengthMm = edgeInfos[startEdgeIndex].lengthMm;
    finalChain.push_back(seed);
    for (const auto& ce : forwardChain) {
        finalChain.push_back(ce);
    }

    double totalLen = 0.0;
    for (const auto& ce : finalChain) {
        totalLen += ce.lengthMm;
    }

    if (finalChain.size() > 1 && !loopClosed) {
        const auto& firstE = edgeShapes[finalChain.front().edgeIndex];
        const auto& lastE = edgeShapes[finalChain.back().edgeIndex];
        BRepAdaptor_Curve firstCurve(firstE);
        BRepAdaptor_Curve lastCurve(lastE);
        gp_Pnt chainStartPt = finalChain.front().isReversed
            ? firstCurve.Value(firstCurve.LastParameter())
            : firstCurve.Value(firstCurve.FirstParameter());
        gp_Pnt chainEndPt = finalChain.back().isReversed
            ? lastCurve.Value(lastCurve.FirstParameter())
            : lastCurve.Value(lastCurve.LastParameter());
        gp_Dir chainStartDir = finalChain.front().isReversed
            ? getCurveTangentAtParam(firstE, firstCurve.LastParameter()).Reversed()
            : getCurveTangentAtParam(firstE, firstCurve.FirstParameter());
        gp_Dir chainEndDir = finalChain.back().isReversed
            ? getCurveTangentAtParam(lastE, lastCurve.FirstParameter()).Reversed()
            : getCurveTangentAtParam(lastE, lastCurve.LastParameter());
        if (chainStartPt.Distance(chainEndPt) < 0.05 &&
            chainEndDir.Angle(chainStartDir) <= maxAngleRad) {
            loopClosed = true;
        }
    }

    result.edges = std::move(finalChain);
    result.totalLengthMm = totalLen;
    result.isClosed = loopClosed;
    return result;
#else
    auto itSolid = solids_.find(cadHandleId);
    if (itSolid != solids_.end() && startEdgeIndex >= 0 &&
        startEdgeIndex < static_cast<int>(itSolid->second.edges.size())) {
        ChainedEdge single;
        single.edgeIndex = startEdgeIndex;
        single.isReversed = false;
        single.lengthMm = itSolid->second.edges[startEdgeIndex].lengthMm;
        result.edges.push_back(single);
        result.totalLengthMm = single.lengthMm;
        result.isClosed = false;
    }
    return result;
#endif
}


#ifdef OLP_HAS_OCCT
TopoDS_Shape OcctCadKernel::getShape(int cadHandleId) const {
    auto it = shapes_.find(cadHandleId);
    if (it != shapes_.end()) {
        return it->second;
    }
    return TopoDS_Shape();
}

TopoDS_Edge OcctCadKernel::getEdgeShape(int cadHandleId, int edgeIndex) const {
    auto it = edgeShapes_.find(cadHandleId);
    if (it != edgeShapes_.end() && edgeIndex >= 0 && edgeIndex < static_cast<int>(it->second.size())) {
        return it->second[edgeIndex];
    }
    return TopoDS_Edge();
}

int OcctCadKernel::findEdgeIndex(int cadHandleId, const TopoDS_Edge& edge) const {
    if (edge.IsNull()) return -1;
    auto it = edgeShapes_.find(cadHandleId);
    if (it == edgeShapes_.end()) return -1;
    const auto& list = it->second;
    const TopoDS_Shape strippedEdge = edge.Located(TopLoc_Location());
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].IsSame(edge) || list[i].IsPartner(edge) || list[i].IsSame(strippedEdge)) {
            return static_cast<int>(i);
        }
    }
    try {
        const TopoDS_Edge localEdge = TopoDS::Edge(strippedEdge);
        BRepAdaptor_Curve curve(localEdge);
        const double len = GCPnts_AbscissaPoint::Length(curve);
        GCPnts_AbscissaPoint mid(curve, std::max(0.0, len * 0.5), curve.FirstParameter());
        gp_Pnt midPtVal = curve.Value(curve.FirstParameter());
        if (mid.IsDone()) {
            midPtVal = curve.Value(mid.Parameter());
        }
        core::Vec3 midPt{midPtVal.X(), midPtVal.Y(), midPtVal.Z()};
        auto recIt = solids_.find(cadHandleId);
        if (recIt != solids_.end()) {
            for (size_t i = 0; i < recIt->second.edges.size(); ++i) {
                const auto& einfo = recIt->second.edges[i];
                if (std::abs(einfo.lengthMm - len) < 0.2 && (einfo.midPt - midPt).norm() < 1.0) {
                    return static_cast<int>(i);
                }
            }
        }
    } catch (...) {}
    return -1;
}
#endif

core::TopologicalSignature OcctCadKernel::computeEdgeSignature(
    int cadHandleId,
    int edgeIndex) const {
    core::TopologicalSignature sig;
    auto it = solids_.find(cadHandleId);
    if (it == solids_.end()) return sig;
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(it->second.edges.size())) return sig;

    const auto& edge = it->second.edges[edgeIndex];
    sig.cadFilePath = it->second.sourceFile;
    sig.partName = it->second.name;
    sig.topologyIndex = edgeIndex;
    sig.startPoint = edge.startPt;
    sig.endPoint = edge.endPt;
    sig.midPoint = edge.midPt;
    sig.midTangent = edge.tangent;
    sig.arcLengthMm = edge.lengthMm;
    sig.cadFileHash = it->second.contentHash;
    return sig;
}


} // namespace olp::cad
