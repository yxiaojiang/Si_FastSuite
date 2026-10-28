#pragma once

#include "olp/core/ProjectModel.h"
#include "olp/device/RobotModel.h"
#include "olp/ui/TcpGizmo.h"

#include <QObject>
#include <QPointer>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class QStatusBar;

namespace olp::ui {

class MainWindow;

struct GizmoPreviewSnapshot {
    std::string primaryTargetId;
    core::Transform4d previewPoseInUFrame;
    core::Transform4d tcpWorld;
    core::Transform4d robotBaseInWorld;
    core::Transform4d utoolOffset;
    std::array<double, 6> refJointsRad{};
    std::optional<core::FanucConfig> lockedConfig;
    device::RobotModel robotModel;
    core::UnitsSettings settings;
    std::vector<core::SceneNode> sceneNodes;
    std::string activeWorkpieceId;
    std::vector<std::string> exemptionPartIds;
    bool isWeldingRole{false};
    bool valid{false};
};

class GizmoDragPreviewHandler : public QObject {
    Q_OBJECT
public:
    GizmoDragPreviewHandler(
        MainWindow& controller,
        TcpGizmo& gizmo,
        QStatusBar* statusBar,
        QObject* parent = nullptr);
    ~GizmoDragPreviewHandler() override = default;

private:
    void handlePreview(const GizmoPreviewState& preview);
    void updateClearanceStatusBar(const GizmoPreviewSnapshot& snap, double clearance);

    MainWindow& controller_;
    QPointer<QStatusBar> statusBar_;

    std::shared_ptr<std::atomic<uint64_t>> previewSeq_;
    std::shared_ptr<std::atomic<bool>> previewBusy_;
    std::shared_ptr<std::mutex> previewMutex_;
    std::shared_ptr<GizmoPreviewSnapshot> latestSnapshot_;
};

} // namespace olp::ui
