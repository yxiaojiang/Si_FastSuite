#pragma once

#include "olp/algo/CollisionAndSim.h"
#include "olp/cad/ICadKernel.h"
#include "olp/core/ProjectModel.h"
#include <optional>
#include <vector>

namespace olp::algo {

// Section 6.1 & M07 / M08: Seam Coordinate Frame & Toolpath Generation Algorithms
class SeamAndToolpathAlgo {
public:
    // Section 6.1 Step 1-4: Compute local seam coordinate frame from tangent t and two outward normals n1, n2
    // X = normalize(t), b = normalize(n1 + n2), Z0 = -b (pointing into seam root), Y = normalize(Z0 x X), Z = X x Y
    static std::optional<core::Transform4d> computeSeamFrame(
        const core::Vec3& point,
        const core::Vec3& tangent,
        const core::Vec3& outwardNormal1,
        const core::Vec3& outwardNormal2) noexcept;

    // Section 6.1 Step 5: Torch Pose = SeamFrame * Rx(workAngle) * Ry(travelAngle) * Rz(twistAngle)
    static core::Transform4d applyWeldingAngles(
        const core::Transform4d& seamFrame,
        double workAngleDeg,
        double travelAngleDeg,
        double twistAngleDeg) noexcept;

    // M07: Build SeamDefinition from picked edge (handles both single-solid adjacent faces & T-joint cross-part faces)
    static std::optional<core::SeamDefinition> buildSeamFromPickedEdge(
        const cad::ICadKernel& cadKernel,
        int sourceCadHandleId,
        int edgeIndex,
        const std::vector<int>& allWorkpieceCadHandles,
        double sampleStepMm = 20.0,
        double startTrimMm = 5.0);

    // M07: Build SeamDefinition across a tangent-continuous edge chain
    static std::optional<core::SeamDefinition> buildSeamFromEdgeChain(
        const cad::ICadKernel& cadKernel,
        int sourceCadHandleId,
        const cad::EdgeChainResult& chain,
        const std::vector<int>& allWorkpieceCadHandles,
        double sampleStepMm = 20.0,
        double startTrimMm = 0.0,
        double endTrimMm = 0.0);

    // Walk the seam start forward until the torch spheres meet the safe clearance.
    // The fillet angles stay as set on the operation. Returns false if the 25% trim cap is still blocked.
    static bool extendStartTrimUntilTorchClears(
        const cad::ICadKernel& cadKernel,
        int sourceCadHandleId,
        int edgeIndex,
        const std::vector<int>& allWorkpieceCadHandles,
        double sampleStepMm,
        const CollisionAndSimEngine& collision,
        const core::Transform4d& uframeInWorld,
        const std::vector<core::SceneNode>& sceneNodes,
        core::SeamDefinition& inOutSeam,
        core::Operation& inOutOperation);

    // M08 & M09: Generate Targets (APPROACH, WELD_START, WELD_VIA*, WELD_END, RETRACT)
    // Rule 2: Preserves existing targets where userModified == true by matching arcLengthRatio
    static void generateOperationTargets(
        const core::SeamDefinition& seam,
        const core::Transform4d& uframeInWorld,
        core::Operation& inOutOperation);

    // M08: Smooth intermediate target orientations between startTarget and endTarget using SLERP
    static bool interpolateOrientationsSlerp(
        core::Operation& inOutOperation,
        const std::string& startTargetId = "",
        const std::string& endTargetId = "",
        const core::SeamDefinition* seam = nullptr,
        const core::Transform4d& uframeInWorld = core::Transform4d::identity());

    // M09: Rotate Target TCP orientation around its local Z axis by angleRad (torch spin tuning)
    static bool rotateTargetAroundTcpZ(
        core::Target& target,
        double angleRad);
};

} // namespace olp::algo
