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
#ifdef OLP_HAS_OCCT
    auto itEdges = edgeShapes_.find(cadHandleId);
    if (itEdges != edgeShapes_.end() && edgeIndex >= 0 && edgeIndex < static_cast<int>(itEdges->second.size())) {
        const TopoDS_Edge& edge = itEdges->second[edgeIndex];
        BRepAdaptor_Curve curve(edge);
        const double totalLen = GCPnts_AbscissaPoint::Length(curve);
        const double clampedS = std::clamp(arcLengthMm, 0.0, totalLen);
        if (clampedS <= 1e-7) {
            const double param = curve.FirstParameter();
            gp_Pnt p;
            gp_Vec v;
            curve.D1(param, p, v);
            outPoint = {p.X(), p.Y(), p.Z()};
            outTangent = (v.Magnitude() > 1e-7) ? core::Vec3{v.X(), v.Y(), v.Z()}.normalized() : core::Vec3{1, 0, 0};
            return true;
        } else if (clampedS >= totalLen - 1e-7) {
            const double param = curve.LastParameter();
            gp_Pnt p;
            gp_Vec v;
            curve.D1(param, p, v);
            outPoint = {p.X(), p.Y(), p.Z()};
            outTangent = (v.Magnitude() > 1e-7) ? core::Vec3{v.X(), v.Y(), v.Z()}.normalized() : core::Vec3{1, 0, 0};
            return true;
        } else {
            GCPnts_AbscissaPoint absPt(curve, clampedS, curve.FirstParameter());
            if (absPt.IsDone()) {
                const double param = absPt.Parameter();
                gp_Pnt p;
                gp_Vec v;
                curve.D1(param, p, v);
                outPoint = {p.X(), p.Y(), p.Z()};
                outTangent = (v.Magnitude() > 1e-7) ? core::Vec3{v.X(), v.Y(), v.Z()}.normalized() : core::Vec3{1, 0, 0};
                return true;
            }
        }
    }
#endif
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

    std::vector<ChainedEdge> forwardChain;
    gp_Pnt currPt = seedPEnd;
    gp_Dir currOutDir = getCurveTangentAtParam(seedEdge, seedLastParam);
    bool loopClosed = false;

    if (seedPStart.Distance(seedPEnd) < 0.05) {
        loopClosed = true;
    }

    while (!loopClosed) {
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

        if (step.nextPt.Distance(seedPStart) < 0.05) {
            loopClosed = true;
            break;
        }

        currPt = step.nextPt;
        const TopoDS_Edge nextEdge = edgeShapes[step.nextEdgeIdx];
        BRepAdaptor_Curve nextCurve(nextEdge);
        if (step.isReversed) {
            currOutDir = getCurveTangentAtParam(nextEdge, nextCurve.FirstParameter()).Reversed();
        } else {
            currOutDir = getCurveTangentAtParam(nextEdge, nextCurve.LastParameter());
        }
    }

    std::vector<ChainedEdge> backwardChain;
    if (!loopClosed) {
        currPt = seedPStart;
        currOutDir = getCurveTangentAtParam(seedEdge, seedFirstParam).Reversed();

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
        if (chainStartPt.Distance(chainEndPt) < 0.05) {
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

    outRenderMesh = makeBoxMesh(it->second.bboxMin, it->second.bboxMax);
    outCollisionMesh = outRenderMesh;
    return true;
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
            solids_[rec.handleId] = std::move(rec);

            tessellate(node.cadHandleId, 0.2, 2.0, node.renderMesh, node.collisionMesh);
            outPartNodes.push_back(std::move(node));
        }
    }
    return !outPartNodes.empty();
}
#endif

} // namespace olp::cad
