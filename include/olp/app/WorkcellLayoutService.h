#pragma once

#include "olp/app/CommandManager.h"
#include "olp/cad/ICadKernel.h"
#include "olp/device/RobotModel.h"
#include <string>
#include <vector>

namespace olp::app {

struct SceneRobotReplaceResult {
    bool success{false};
    int totalSolidsImported{0};
    int staticRobotSolidsHidden{0};
    std::string activeRobotNodeId{};
    core::Transform4d detectedRobotBaseTransform{};
};

// M04: Workcell Layout & Section 3.4 Scheme A (Full Scene STEP Import + Robot Replacement)
class WorkcellLayoutService {
public:
    // Section 3.4 Scheme A: Import full workcell STEP (e.g. Full_Welding_Cell_With_Robot.step),
    // automatically identify static robot solids (FANUC_M10iA_Static_*), determine pedestal mounting base,
    // replace with movable library robot (FANUC M-10iA), and hide static robot solids.
    static SceneRobotReplaceResult importFullSceneAndReplaceRobot(
        ProjectDocument& doc,
        cad::ICadKernel& cadKernel,
        const std::string& stepFilePath,
        const device::RobotModel& libraryRobotModel);

    // M04: Define or update UFRAME by 3 picked points (Origin, +X direction point, XY plane point)
    static std::string defineUFrameByThreePoints(
        ProjectDocument& doc,
        int uframeNumber,
        const std::string& frameName,
        const core::Vec3& originWorld,
        const core::Vec3& ptXWorld,
        const core::Vec3& ptXYWorld);
};

class ImportSceneAndReplaceRobotCommand final : public ICommand {
public:
    ImportSceneAndReplaceRobotCommand(
        ProjectDocument& doc,
        cad::ICadKernel& cadKernel,
        std::string stepFilePath,
        device::RobotModel libraryRobotModel,
        SceneRobotReplaceResult* resultOut = nullptr);

    void execute() override;
    void undo() override;
    std::string description() const override { return "Import scene and replace static robot"; }
    bool didMutate() const noexcept override { return mutated_; }

private:
    ProjectDocument& doc_;
    cad::ICadKernel& cadKernel_;
    std::string stepFilePath_;
    device::RobotModel libraryRobotModel_;
    SceneRobotReplaceResult* resultOut_{nullptr};
    SceneRobotReplaceResult result_{};
    std::vector<core::SceneNode> beforeNodes_{};
    std::vector<core::SceneNode> afterNodes_{};
    bool beforeDirty_{false};
    bool applied_{false};
    bool mutated_{false};
};

class DefineUFrameCommand final : public ICommand {
public:
    DefineUFrameCommand(
        ProjectDocument& doc,
        int uframeNumber,
        std::string frameName,
        core::Vec3 originWorld,
        core::Vec3 ptXWorld,
        core::Vec3 ptXYWorld,
        std::string* nodeIdOut = nullptr);

    void execute() override;
    void undo() override;
    std::string description() const override { return "Define UFRAME"; }
    bool didMutate() const noexcept override { return mutated_; }

private:
    ProjectDocument& doc_;
    int uframeNumber_{1};
    std::string frameName_;
    core::Vec3 originWorld_{};
    core::Vec3 ptXWorld_{};
    core::Vec3 ptXYWorld_{};
    std::string* nodeIdOut_{nullptr};
    std::string nodeId_{};
    std::vector<core::SceneNode> beforeNodes_{};
    std::vector<core::SceneNode> afterNodes_{};
    bool beforeDirty_{false};
    bool applied_{false};
    bool mutated_{false};
};

class CreateSeamFromPickedEdgeCommand final : public ICommand {
public:
    CreateSeamFromPickedEdgeCommand(
        ProjectDocument& doc,
        cad::ICadKernel& cadKernel,
        int sourceCadHandleId,
        int edgeIndex,
        double sampleStepMm = 50.0);

    void execute() override;
    void undo() override;
    std::string description() const override;
    bool didMutate() const noexcept override { return mutated_; }
    const std::string& createdSeamId() const noexcept { return createdSeamId_; }

private:
    ProjectDocument& doc_;
    cad::ICadKernel& cadKernel_;
    int sourceCadHandleId_{-1};
    int edgeIndex_{-1};
    double sampleStepMm_{50.0};
    core::SeamDefinition createdSeam_{};
    std::string createdSeamId_{};
    bool beforeDirty_{false};
    bool mutated_{false};
};

} // namespace olp::app
