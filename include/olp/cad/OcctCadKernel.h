#pragma once

#include "olp/cad/ICadKernel.h"
#include <unordered_map>

namespace olp::cad {

// M02: OpenCASCADE (OCCT) implementation of ICadKernel
class OcctCadKernel final : public ICadKernel {
public:
    OcctCadKernel();
    ~OcctCadKernel() override;

    bool importStep(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes) override;
    bool importStl(const std::string& filePath, core::TriMesh& outMesh) override;

    // ROS-Industrial visual/collision STL files are meters. This loads one and scales to mm.
    bool importRobotStlMillimeters(const std::string& filePath, core::TriMesh& outMesh);

    // OpenCASCADE import, including files the built-in planar parser would accept.
    // importStep prefers this path when OpenCASCADE is available.
    bool importStepViaOcct(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes);

    // Built-in planar parser. Used to compare edge order before trusting the OpenCASCADE path.
    bool importStepViaBuiltin(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes);

    std::vector<CadEdgeInfo> getEdges(int cadHandleId) const override;
    std::vector<CadFaceInfo> getFaces(int cadHandleId) const override;
    std::vector<int> getAdjacentFaces(int cadHandleId, int edgeIndex) const override;

    std::optional<CrossPartFaceMatch> findClosestMatingFaceAcrossParts(
        int sourceCadHandleId,
        int edgeIndex,
        const std::vector<int>& candidateCadHandles,
        double maxDistanceTolMm = 1.0) const override;

    bool evaluateEdgeAtArcLength(
        int cadHandleId,
        int edgeIndex,
        double arcLengthMm,
        core::Vec3& outPoint,
        core::Vec3& outTangent) const override;

    bool evaluateFaceOutwardNormal(
        int cadHandleId,
        int faceIndex,
        const core::Vec3& samplePoint,
        core::Vec3& outNormal) const override;

    bool tessellate(
        int cadHandleId,
        double fineDeflectionMm,
        double coarseDeflectionMm,
        core::TriMesh& outRenderMesh,
        core::TriMesh& outCollisionMesh) const override;

    core::TopologicalSignature computeEdgeSignature(
        int cadHandleId,
        int edgeIndex) const override;

private:
    struct SolidRecord {
        int handleId{-1};
        std::string sourceFile{};
        std::string name{};
        core::Vec3 bboxMin{0.0, 0.0, 0.0};
        core::Vec3 bboxMax{0.0, 0.0, 0.0};
        std::vector<CadEdgeInfo> edges{};
        std::vector<CadFaceInfo> faces{};
        std::string contentHash{};
    };

    int nextHandleId_{1};
    std::unordered_map<int, SolidRecord> solids_{};

    static double pointToFaceBoxDistance(const core::Vec3& pt, const CadFaceInfo& face) noexcept;
    bool importStepBuiltin(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes);
#ifdef OLP_HAS_OCCT
    bool importStepWithOcct(const std::string& filePath, std::vector<core::SceneNode>& outPartNodes);
#endif
};

} // namespace olp::cad
