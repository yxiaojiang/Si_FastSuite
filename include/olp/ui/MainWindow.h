#pragma once

#include "olp/app/CommandManager.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/device/RobotModel.h"
#include "olp/ui/OcctViewport.h"
#include <string>

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
    OcctViewport& viewport() noexcept { return viewport_; }
    const device::RobotModel& activeRobotModel() const noexcept { return robotModel_; }

private:
    app::ProjectDocument doc_{};
    app::CommandManager cmdMgr_{200};
    cad::OcctCadKernel cadKernel_{};
    device::RobotModel robotModel_{device::RobotModel::createFanucM10iA()};
    OcctViewport viewport_;
    ActiveWorkbench activeWorkbench_{ActiveWorkbench::LayoutWorkbench};
};

} // namespace olp::ui
