#include "olp/ui/TcpGizmo.h"
#include <cmath>

namespace olp::ui {

TcpGizmo::TcpGizmo(app::ProjectDocument& doc, app::CommandManager& cmdMgr)
    : doc_(doc), cmdMgr_(cmdMgr) {}

void TcpGizmo::setSnapSteps(bool enabled, double linearStepMm, double angularStepDeg) noexcept {
    snapEnabled_ = enabled;
    linearSnapMm_ = (linearStepMm > 1e-6) ? linearStepMm : 1.0;
    angularSnapDeg_ = (angularStepDeg > 1e-6) ? angularStepDeg : 1.0;
}

double TcpGizmo::snapValue(double val, double step) const noexcept {
    if (!snapEnabled_ || step <= 1e-6) return val;
    return std::round(val / step) * step;
}

bool TcpGizmo::beginDrag(const std::vector<std::string>& selectedTargetIds) {
    snapshots_.clear();
    for (const auto& id : selectedTargetIds) {
        const core::Target* t = doc_.findTarget(id);
        if (t != nullptr) {
            snapshots_.push_back({id, t->poseInUFrame, t->poseInUFrame});
        }
    }
    isDragging_ = !snapshots_.empty();
    return isDragging_;
}

void TcpGizmo::updateDragDelta(
    const core::Vec3& rawTranslationMm,
    const core::Vec3& rawRotationEulerDeg) {
    if (!isDragging_ || snapshots_.empty()) return;

    const core::Vec3 snappedTrans{
        snapValue(rawTranslationMm.x, linearSnapMm_),
        snapValue(rawTranslationMm.y, linearSnapMm_),
        snapValue(rawTranslationMm.z, linearSnapMm_)
    };
    const core::Vec3 snappedRotDeg{
        snapValue(rawRotationEulerDeg.x, angularSnapDeg_),
        snapValue(rawRotationEulerDeg.y, angularSnapDeg_),
        snapValue(rawRotationEulerDeg.z, angularSnapDeg_)
    };

    core::Transform4d deltaT;
    deltaT.pos = snappedTrans;
    deltaT.rot =
        core::Mat3::rotZ(core::deg2rad(snappedRotDeg.z)) *
        core::Mat3::rotY(core::deg2rad(snappedRotDeg.y)) *
        core::Mat3::rotX(core::deg2rad(snappedRotDeg.x));

    for (auto& snap : snapshots_) {
        if (refFrame_ == GizmoRefFrame::Tool) {
            snap.currentPreviewPoseInUFrame = snap.initialPoseInUFrame * deltaT;
        } else if (refFrame_ == GizmoRefFrame::Seam && hasSeamFrame_) {
            const core::Transform4d seam = seamFrameInUFrame_;
            snap.currentPreviewPoseInUFrame = seam * deltaT * seam.inverse() * snap.initialPoseInUFrame;
        } else if (refFrame_ == GizmoRefFrame::World) {
            const core::Transform4d worldToUFrame = uframeInWorld_.inverse();
            snap.currentPreviewPoseInUFrame.pos =
                snap.initialPoseInUFrame.pos + worldToUFrame.rot * deltaT.pos;
            snap.currentPreviewPoseInUFrame.rot =
                (worldToUFrame.rot * deltaT.rot * uframeInWorld_.rot) * snap.initialPoseInUFrame.rot;
        } else {
            // UFrame, and Seam before a seam frame has been supplied.
            snap.currentPreviewPoseInUFrame.pos = snap.initialPoseInUFrame.pos + deltaT.pos;
            snap.currentPreviewPoseInUFrame.rot = deltaT.rot * snap.initialPoseInUFrame.rot;
        }
    }

    // Rule 3: Emit preview only — do NOT mutate ProjectDocument or push to CommandManager
    if (previewCallback_) {
        GizmoPreviewState st;
        st.primaryTargetId = snapshots_.front().targetId;
        st.previewPoseInUFrame = snapshots_.front().currentPreviewPoseInUFrame;
        previewCallback_(st);
    }
}

bool TcpGizmo::commitDrag() {
    if (!isDragging_ || snapshots_.empty()) {
        return false;
    }

    bool moved = false;
    for (const auto& s : snapshots_) {
        const double pos = (s.currentPreviewPoseInUFrame.pos - s.initialPoseInUFrame.pos).norm();
        const double ang = s.currentPreviewPoseInUFrame.rot.angularDistanceRad(s.initialPoseInUFrame.rot);
        if (pos > 1e-9 || ang > 1e-9) {
            moved = true;
            break;
        }
    }
    if (!moved) {
        cancelDrag();
        return false;
    }

    if (snapshots_.size() == 1) {
        // Rule 4: Single target drag produces exactly 1 ModifyTargetPoseCommand
        const auto& s = snapshots_.front();
        cmdMgr_.executeCommand(std::make_unique<app::ModifyTargetPoseCommand>(
            doc_, s.targetId, s.currentPreviewPoseInUFrame, false));
    } else {
        // Rule 5: Multi-selection drag merges into 1 MacroCommand
        auto macro = std::make_unique<app::MacroCommand>(
            "Multi-Target Gizmo Drag (" + std::to_string(snapshots_.size()) + " targets)");
        for (const auto& s : snapshots_) {
            macro->addSubCommand(std::make_unique<app::ModifyTargetPoseCommand>(
                doc_, s.targetId, s.currentPreviewPoseInUFrame, false));
        }
        cmdMgr_.executeCommand(std::move(macro));
    }

    snapshots_.clear();
    isDragging_ = false;
    return true;
}

void TcpGizmo::cancelDrag() {
    if (!isDragging_) return;
    if (previewCallback_ && !snapshots_.empty()) {
        GizmoPreviewState st;
        st.primaryTargetId = snapshots_.front().targetId;
        st.previewPoseInUFrame = snapshots_.front().initialPoseInUFrame;
        previewCallback_(st);
    }
    snapshots_.clear();
    isDragging_ = false;
}

} // namespace olp::ui
