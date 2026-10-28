#include "olp/ui/GizmoDragPreviewHandler.h"
#include "olp/ui/MainWindow.h"
#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"

#include <QMetaObject>
#include <QStatusBar>
#include <QThreadPool>

namespace olp::ui {

GizmoDragPreviewHandler::GizmoDragPreviewHandler(
    MainWindow& controller,
    TcpGizmo& gizmo,
    QStatusBar* statusBar,
    QObject* parent)
    : QObject(parent)
    , controller_(controller)
    , statusBar_(statusBar)
    , previewSeq_(std::make_shared<std::atomic<uint64_t>>(0))
    , previewBusy_(std::make_shared<std::atomic<bool>>(false))
    , previewMutex_(std::make_shared<std::mutex>())
    , latestSnapshot_(std::make_shared<GizmoPreviewSnapshot>()) {
    gizmo.setPreviewCallback([this](const GizmoPreviewState& preview) {
        handlePreview(preview);
    });
}

void GizmoDragPreviewHandler::handlePreview(const GizmoPreviewState& preview) {
    // 1. Immediately update status bar Euler angles on UI thread
    const auto wpr = preview.previewPoseInUFrame.toWPR();
    if (statusBar_ != nullptr) {
        statusBar_->showMessage(QString::fromUtf8("Gizmo 微调中: %1 [X=%2, Y=%3, Z=%4, W=%5°, P=%6°, R=%7°]")
            .arg(QString::fromStdString(preview.primaryTargetId))
            .arg(wpr.x, 0, 'f', 1)
            .arg(wpr.y, 0, 'f', 1)
            .arg(wpr.z, 0, 'f', 1)
            .arg(wpr.wDeg, 0, 'f', 1)
            .arg(wpr.pDeg, 0, 'f', 1)
            .arg(wpr.rDeg, 0, 'f', 1));
    }

    // 2. Capture immutable value snapshot from controller / document on UI thread (M10-FCL-002)
    GizmoPreviewSnapshot snap;
    snap.primaryTargetId = preview.primaryTargetId;
    snap.previewPoseInUFrame = preview.previewPoseInUFrame;
    snap.robotModel = controller_.activeRobotModel();
    snap.settings = controller_.document().settings;
    snap.sceneNodes = controller_.document().sceneNodes;

    snap.robotBaseInWorld = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    snap.utoolOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    for (const auto& node : snap.sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            snap.robotBaseInWorld = node.localTransform;
        } else if (node.type == core::SceneNodeType::Tool) {
            snap.utoolOffset = node.localTransform;
        }
    }

    const auto* t = controller_.document().findTarget(preview.primaryTargetId);
    if (t != nullptr) {
        int uframeNum = 1;
        for (const auto& cu : controller_.document().controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    for (const auto& tgt : op.targets) {
                        if (tgt.id == t->id) {
                            uframeNum = (prog.uframeNumber > 0) ? prog.uframeNumber : 1;
                            snap.activeWorkpieceId = op.targetWorkpieceId;
                            snap.exemptionPartIds = op.exemptionPartIds;
                            break;
                        }
                    }
                }
            }
        }
        const core::Transform4d uframeInWorld = controller_.document().computeUFrameInWorld(uframeNum);
        snap.tcpWorld = uframeInWorld * preview.previewPoseInUFrame;

        if (t->cache.valid || t->cache.status != core::TargetValidationState::Unchecked) {
            snap.refJointsRad = t->cache.jointsRad;
            snap.lockedConfig = t->cache.solvedConfig;
        } else {
            snap.refJointsRad = snap.robotModel.homeJointsRad;
            snap.lockedConfig = std::nullopt;
        }

        snap.isWeldingRole = (t->role == core::TargetRole::WELD_START ||
                              t->role == core::TargetRole::WELD_VIA ||
                              t->role == core::TargetRole::WELD_END);
        snap.valid = true;
    }

    // 3. Atomically update latest snapshot and request sequence number
    {
        std::lock_guard<std::mutex> lock(*previewMutex_);
        *latestSnapshot_ = snap;
    }
    const uint64_t seq = ++(*previewSeq_);

    // 4. Dispatch worker task to thread pool if not already running
    bool expected = false;
    if (previewBusy_->compare_exchange_strong(expected, true)) {
        QPointer<GizmoDragPreviewHandler> self(this);
        auto seqPtr = previewSeq_;
        auto busyPtr = previewBusy_;
        auto mutexPtr = previewMutex_;
        auto snapPtr = latestSnapshot_;

        QThreadPool::globalInstance()->start([self, seqPtr, busyPtr, mutexPtr, snapPtr]() {
            while (true) {
                const uint64_t currentSeq = seqPtr->load();
                GizmoPreviewSnapshot currentSnap;
                {
                    std::lock_guard<std::mutex> lock(*mutexPtr);
                    currentSnap = *snapPtr;
                }

                if (currentSnap.valid) {
                    algo::OpwKinematics kin(currentSnap.robotModel);
                    algo::CollisionAndSimEngine engine(kin, currentSnap.settings);

                    // Solve IK for preview TCP pose to check links 1..6 + torch spheres (M10-FCL-002)
                    const core::Transform4d tcpInBase = currentSnap.robotBaseInWorld.inverse() * currentSnap.tcpWorld;
                    const auto sols = kin.inverseKinematics(tcpInBase, currentSnap.utoolOffset, false);
                    const auto best = kin.selectBestSolution(sols, currentSnap.refJointsRad, currentSnap.lockedConfig, true);

                    std::optional<std::array<double, 6>> joints = std::nullopt;
                    if (best) {
                        joints = best->jointsRad;
                    }

                    double clearance = 0.0;
                    std::string hitPart;
                    engine.checkRobotAndTorchCollision(
                        currentSnap.tcpWorld, joints, currentSnap.robotBaseInWorld, currentSnap.utoolOffset,
                        currentSnap.sceneNodes, currentSnap.activeWorkpieceId, currentSnap.isWeldingRole,
                        clearance, hitPart, currentSnap.exemptionPartIds);

                    if (self && seqPtr->load() == currentSeq) {
                        QMetaObject::invokeMethod(self, [self, currentSeq, seqPtr, clearance, currentSnap]() {
                            if (!self || seqPtr->load() != currentSeq) return;
                            self->updateClearanceStatusBar(currentSnap, clearance);
                        }, Qt::QueuedConnection);
                    }
                }

                // Concurrency race fix (M10-FCL-002):
                // If a newer preview arrived during computation, continue immediately without clearing busy flag
                if (seqPtr->load() != currentSeq) {
                    continue;
                }

                // Release busy flag
                busyPtr->store(false);

                // Crucial: check if another request arrived right before or while clearing busy flag
                if (seqPtr->load() != currentSeq) {
                    bool exp = false;
                    if (busyPtr->compare_exchange_strong(exp, true)) {
                        continue; // Re-acquired busy flag; continue loop to process latest frame
                    }
                }

                break; // Fully processed, worker exits cleanly
            }
        });
    }
}

void GizmoDragPreviewHandler::updateClearanceStatusBar(const GizmoPreviewSnapshot& snap, double clearance) {
    if (statusBar_ == nullptr) {
        return;
    }
    const auto wpr = snap.previewPoseInUFrame.toWPR();
    statusBar_->showMessage(QString::fromUtf8("Gizmo 微调中: %1 [X=%2, Y=%3, Z=%4, W=%5°, P=%6°, R=%7°] 间隙: %8 mm%9")
        .arg(QString::fromStdString(snap.primaryTargetId))
        .arg(wpr.x, 0, 'f', 1)
        .arg(wpr.y, 0, 'f', 1)
        .arg(wpr.z, 0, 'f', 1)
        .arg(wpr.wDeg, 0, 'f', 1)
        .arg(wpr.pDeg, 0, 'f', 1)
        .arg(wpr.rDeg, 0, 'f', 1)
        .arg(clearance, 0, 'f', 1)
        .arg(clearance <= 0.0 ? QString::fromUtf8(" (碰撞!)") : (clearance < 5.0 ? QString::fromUtf8(" (近碰撞)") : QString())));
}

} // namespace olp::ui
