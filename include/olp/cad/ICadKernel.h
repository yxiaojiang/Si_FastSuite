#pragma once

#include "olp/core/ProjectModel.h"
#include <memory>
#include <string>
#include <vector>

namespace olp::cad {

struct CadEdgeInfo {
    int edgeIndex{-1};
    core::Vec3 startPt{};
    core::Vec3 endPt{};
    core::Vec3 midPt{};
    core::Vec3 tangent{};
    double lengthMm{0.0};
    bool isLinear{true};
    std::vector<int> adjacentFaceIndices{};
};

enum class CadSurfaceType {
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
    BSpline,
    Other
};

struct CadFaceInfo {
    int faceIndex{-1};
    std::string name{};
    CadSurfaceType surfaceType{CadSurfaceType::Plane};
    core::Vec3 origin{};
    core::Vec3 outwardNormal{0.0, 0.0, 1.0};
    core::Vec3 bboxMin{};
    core::Vec3 bboxMax{};
};

struct CrossPartFaceMatch {
    int partHandleId{-1};
    std::string partName{};
    int faceIndex{-1};
    double distanceMm{0.0};
    core::Vec3 outwardNormal{};
};

// M02: Abstract CAD Kernel Interface (isolates OpenCASCADE from upper layers)
class ICadKernel {
public:
    virtual ~ICadKernel() = default;

    // Import STEP (.step/.stp) preserving assembly solids, names, colors, and converting units to mm
    virtual bool importStep(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes) = 0;

    // Import STL (.stl) as visual/collision triangle mesh
    virtual bool importStl(const std::string& filePath, core::TriMesh& outMesh) = 0;

    // Topology queries
    virtual std::vector<CadEdgeInfo> getEdges(int cadHandleId) const = 0;
    virtual std::vector<CadFaceInfo> getFaces(int cadHandleId) const = 0;
    virtual std::vector<int> getAdjacentFaces(int cadHandleId, int edgeIndex) const = 0;

    // M07 Critical Fix: Find closest mating face across adjacent solids for T-joint fillet welds
    virtual std::optional<CrossPartFaceMatch> findClosestMatingFaceAcrossParts(
        int sourceCadHandleId,
        int edgeIndex,
        const std::vector<int>& candidateCadHandles,
        double maxDistanceTolMm = 1.0) const = 0;

    // Curve evaluation at arc length s in [0, length]
    virtual bool evaluateEdgeAtArcLength(
        int cadHandleId,
        int edgeIndex,
        double arcLengthMm,
        core::Vec3& outPoint,
        core::Vec3& outTangent) const = 0;

    // Surface outward normal evaluation at 3D point projected onto face
    virtual bool evaluateFaceOutwardNormal(
        int cadHandleId,
        int faceIndex,
        const core::Vec3& samplePoint,
        core::Vec3& outNormal) const = 0;

    // Generate fine render mesh & coarse collision mesh
    virtual bool tessellate(
        int cadHandleId,
        double fineDeflectionMm,
        double coarseDeflectionMm,
        core::TriMesh& outRenderMesh,
        core::TriMesh& outCollisionMesh) const = 0;

    // Persistent topological signature for edge binding across saves/re-imports
    virtual core::TopologicalSignature computeEdgeSignature(
        int cadHandleId,
        int edgeIndex) const = 0;
};

} // namespace olp::cad
