#pragma once

#include "olp/algo/CollisionAndSim.h"
#include "olp/app/CommandManager.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/device/RobotModel.h"
#include <functional>
#include <string>
#include <vector>

namespace olp::ui {

// Section 1.2 & M01: Dual Workbench separation (Layout Workbench vs OLP Programming Workbench)
enum class ActiveWorkbench {
    LayoutWorkbench, // 工位布局工作台 (STEP import, robot replacement, UTOOL/UFRAME setup)
    OlpWorkbench     // 离线编程工作台 (Seam picking, toolpath generation/editing, simulation, .ls export)
};

// M01: Main Window Controller managing Workbenches, Document, Command Stack & 3D Viewport
class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    void switchWorkbench(ActiveWorkbench wb) noexcept { activeWorkbench_ = wb; }
    ActiveWorkbench activeWorkbench() const noexcept { return activeWorkbench_; }

    app::ProjectDocument& document() noexcept { return doc_; }
    app::CommandManager& commandManager() noexcept { return cmdMgr_; }
    cad::OcctCadKernel& cadKernel() noexcept { return cadKernel_; }
    const device::RobotModel& activeRobotModel() const noexcept { return robotModel_; }

    void selectSceneNode(const std::string& nodeId);
    void selectOperation(const std::string& operationId);
    void selectTarget(const std::string& targetId, const std::string& operationId = "");
    const std::string& selectedSceneNodeId() const noexcept { return selectedSceneNodeId_; }
    const std::string& selectedOperationId() const noexcept { return selectedOperationId_; }
    const std::string& selectedTargetId() const noexcept { return selectedTargetId_; }
    int addSelectionListener(std::function<void()> listener);
    void removeSelectionListener(int listenerId);

    // M08: Target navigation & toolpath editing
    bool stepTarget(bool forward, bool wrap = false);
    bool generateOperationFromSelectedSeam(const core::ToolpathGenParams* params = nullptr);
    bool smoothTargetOrientations(
        const std::string& startTargetId = "",
        const std::string& endTargetId = "",
        const std::string& operationId = "");

    // M09: Process parameters & TCP Z spin tuning
    bool rotateTargetTcpZ(const std::string& targetId, double angleRad);
    bool rotateTargetsTcpZ(const std::vector<std::string>& targetIds, double angleRad);
    bool modifyTargetProcessParams(
        const std::string& targetId,
        const app::TargetProcessParams& params);
    bool modifyTargetsProcessParams(
        const std::vector<std::string>& targetIds,
        const app::TargetProcessParams& params);

    // M10: Operation validation, kinematics/singularity/collision checks & twist scan
    algo::SimulationResult validateOperation(const std::string& operationId);
    algo::SimulationResult validateCurrentOperation();
    std::vector<algo::TwistScanSample> scanSelectedTargetTwistAngles(double stepDeg = 5.0) const;
    const algo::SimulationResult& lastSimulationResult() const noexcept { return lastSimulationResult_; }

    void noteSampleRun(int exitCode, int frameCount, double cycleTimeSec, double weldTimeSec) noexcept;
    int lastSampleExitCode() const noexcept { return lastSampleExitCode_; }
    int lastFrameCount() const noexcept { return lastFrameCount_; }
    double lastCycleTimeSec() const noexcept { return lastCycleTimeSec_; }
    double lastWeldTimeSec() const noexcept { return lastWeldTimeSec_; }

private:
    void notifySelectionChanged();

    app::ProjectDocument doc_{};
    app::CommandManager cmdMgr_{200};
    cad::OcctCadKernel cadKernel_{};
    device::RobotModel robotModel_{device::RobotModel::createFanucM10iA()};
    ActiveWorkbench activeWorkbench_{ActiveWorkbench::LayoutWorkbench};
    std::string selectedSceneNodeId_{};
    std::string selectedOperationId_{};
    std::string selectedTargetId_{};
    struct SelectionListener {
        int id{0};
        std::function<void()> fn{};
    };
    std::vector<SelectionListener> selectionListeners_{};
    int nextSelectionListenerId_{1};
    algo::SimulationResult lastSimulationResult_{};
    int lastSampleExitCode_{0};
    int lastFrameCount_{0};
    double lastCycleTimeSec_{0.0};
    double lastWeldTimeSec_{0.0};
};

} // namespace olp::ui
