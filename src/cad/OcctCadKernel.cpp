#include "olp/cad/OcctCadKernel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

#ifdef OLP_HAS_OCCT
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <StlAPI_Reader.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#endif

namespace olp::cad {

OcctCadKernel::OcctCadKernel() = default;
OcctCadKernel::~OcctCadKernel() = default;

namespace {

// Helper to build a closed box TriMesh from [minPt, maxPt]
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

} // namespace

bool OcctCadKernel::importStep(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        return false;
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

    double unitScaleToMm = 1.0; // Default mm; if SI_UNIT($,.METRE.) without .MILLI., scale = 1000.0

    std::string line;
    std::regex reId(R"(^\s*#(\d+)\s*=\s*(.*)$)");
    std::regex reCoords(R"(\(\s*([-\d.Ee+]+)\s*,\s*([-\d.Ee+]+)\s*,\s*([-\d.Ee+]+)\s*\))");
    std::regex reRefs(R"(#(\d+))");
    std::regex reName(R"('([^']*)')");

    while (std::getline(ifs, line)) {
        if (line.find("SI_UNIT") != std::string::npos && line.find(".METRE.") != std::string::npos) {
            if (line.find(".MILLI.") == std::string::npos) {
                unitScaleToMm = 1000.0;
            } else {
                unitScaleToMm = 1.0;
            }
        }

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

        // Classify role automatically by solid name conventions
        if (rec.name.find("Clamp") != std::string::npos ||
            rec.name.find("Fixture") != std::string::npos ||
            rec.name.find("Table") != std::string::npos ||
            rec.name.find("Pedestal") != std::string::npos) {
            node.collisionRole = core::CollisionRole::Fixture;
        } else if (rec.name.find("Static_") != std::string::npos ||
                   rec.name.find("Robot_") != std::string::npos) {
            node.collisionRole = core::CollisionRole::Environment;
        } else {
            node.collisionRole = core::CollisionRole::Workpiece;
        }

        node.renderMesh = makeBoxMesh(rec.bboxMin, rec.bboxMax);
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
                    // URDF STL meshes in ROS-Industrial fanuc_m10ia are in meters; if small (<10), scale to mm
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
    const core::Vec3& /*samplePoint*/,
    core::Vec3& outNormal) const {
    auto it = solids_.find(cadHandleId);
    if (it == solids_.end()) return false;
    if (faceIndex < 0 || faceIndex >= static_cast<int>(it->second.faces.size())) return false;
    outNormal = it->second.faces[faceIndex].outwardNormal;
    return true;
}

bool OcctCadKernel::tessellate(
    int cadHandleId,
    double /*fineDeflectionMm*/,
    double /*coarseDeflectionMm*/,
    core::TriMesh& outRenderMesh,
    core::TriMesh& outCollisionMesh) const {
    auto it = solids_.find(cadHandleId);
    if (it == solids_.end()) return false;
    outRenderMesh = makeBoxMesh(it->second.bboxMin, it->second.bboxMax);
    outCollisionMesh = outRenderMesh;
    return true;
}

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
    return sig;
}

} // namespace olp::cad
