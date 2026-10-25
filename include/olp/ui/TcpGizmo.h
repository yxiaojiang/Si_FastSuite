#pragma once

#include "olp/app/CommandManager.h"
#include <functional>
#include <string>
#include <vector>

namespace olp::ui {

enum class GizmoRefFrame {
    World,
    Tool,
    UFrame,
    Seam
};

struct GizmoPreviewState {
    std::string primaryTargetId{};
    core::Transform4d previewPoseInUFrame{};
    bool ikReachable{true};
    bool hasCollision{false};
};

// M03: Custom TcpGizmo Controller Layer above OCCT AIS_Manipulator
// Enforces the 5 strict design rules from OLP_System_Design_Claude.md (v1.1):
// 1. Never calls AIS_Manipulator::Attach() on document targets
// 2. Reference frame switching (World / Tool / UFrame / Seam) & step snapping (mm / deg)
// 3. Dragging only emits preview (latest-frame worker evaluation, zero document mutation)
// 4. Mouse release commits a single ModifyTargetPoseCommand (or MacroCommand); Esc cancels cleanly
// 5. Multi-selection drag applies delta to all selected targets and commits one MacroCommand
class TcpGizmo {
public:
    TcpGizmo(app::ProjectDocument& doc, app::CommandManager& cmdMgr);

    void setReferenceFrame(GizmoRefFrame refFrame) noexcept { refFrame_ = refFrame; }
    GizmoRefFrame referenceFrame() const noexcept { return refFrame_; }

    void setSnapSteps(bool enabled, double linearStepMm = 1.0, double angularStepDeg = 1.0) noexcept;

    // UFRAME pose in the world frame. World-mode drags are converted with this.
    // Identity leaves World and UFrame translations equal.
    void setUFrameInWorld(const core::Transform4d& uframeInWorld) noexcept { uframeInWorld_ = uframeInWorld; }

    // Seam frame expressed in the same coordinates as poseInUFrame.
    void setSeamFrameInUFrame(const core::Transform4d& seamInUFrame) noexcept {
        seamFrameInUFrame_ = seamInUFrame;
        hasSeamFrame_ = true;
    }

    // Begin dragging one or more selected targets
    bool beginDrag(const std::vector<std::string>& selectedTargetIds);

    // Update drag with raw translation (mm) and rotation (deg) delta in active reference frame
    void updateDragDelta(
        const core::Vec3& rawTranslationMm,
        const core::Vec3& rawRotationEulerDeg);

    // Release mouse -> commits exactly ONE command (ModifyTargetPoseCommand or MacroCommand) to Undo stack
    bool commitDrag();

    // Press Esc -> cancels active drag and restores original poses without touching Undo stack
    void cancelDrag();

    bool isDragging() const noexcept { return isDragging_; }

    void setPreviewCallback(std::function<void(const GizmoPreviewState&)> cb) {
        previewCallback_ = std::move(cb);
    }

private:
    struct DragSnapshot {
        std::string targetId{};
        core::Transform4d initialPoseInUFrame{};
        core::Transform4d currentPreviewPoseInUFrame{};
    };

    app::ProjectDocument& doc_;
    app::CommandManager& cmdMgr_;
    GizmoRefFrame refFrame_{GizmoRefFrame::Tool};
    core::Transform4d uframeInWorld_{};
    core::Transform4d seamFrameInUFrame_{};
    bool hasSeamFrame_{false};
    bool snapEnabled_{true};
    double linearSnapMm_{1.0};
    double angularSnapDeg_{1.0};
    bool isDragging_{false};
    std::vector<DragSnapshot> snapshots_{};
    std::function<void(const GizmoPreviewState&)> previewCallback_{};

    double snapValue(double val, double step) const noexcept;
};

} // namespace olp::ui
