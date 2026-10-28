#pragma once

#include "olp/cad/ICadKernel.h"
#include "olp/ui/TcpGizmo.h"

#ifdef OLP_HAS_QT6
#include <QWidget>
#endif

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace olp::ui {

class MainWindow;

enum class SelectionSubShapeMode {
    Object = 0, // TopAbs_SHAPE / solid level
    Vertex = 1, // TopAbs_VERTEX
    Edge   = 2, // TopAbs_EDGE (for M07 seam edge picking)
    Face   = 4  // TopAbs_FACE (for M04 pedestal/UFRAME picking)
};

enum class ViewportDisplayMode {
    Shaded,
    ShadedWithEdges,
    Wireframe
};

struct PickedEdge {
    std::string nodeId{};
    int cadHandleId{-1};
    int edgeIndex{-1};
    core::Vec3 startPt{};
    core::Vec3 endPt{};
    core::Vec3 midPt{};
    core::Vec3 tangent{};
    double lengthMm{0.0};
    bool isLinear{true};
};

// M03: 3D view. The shell owns this widget. Camera orbit, pan, zoom, fit,
// B-Rep mesh rendering, and edge picking are handled here.
#ifdef OLP_HAS_QT6
class OcctViewport : public QWidget {
    Q_OBJECT
#else
class OcctViewport {
#endif
public:
#ifdef OLP_HAS_QT6
    OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr, QWidget* parent = nullptr);
    OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr, cad::ICadKernel* cadKernel, QWidget* parent = nullptr);
    ~OcctViewport() override;
#else
    OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr, cad::ICadKernel* cadKernel = nullptr);
    ~OcctViewport();
#endif

    void setCadKernel(cad::ICadKernel* cadKernel) noexcept { cadKernel_ = cadKernel; }
    cad::ICadKernel* cadKernel() const noexcept { return cadKernel_; }

    void setSelectionMode(SelectionSubShapeMode mode);
    SelectionSubShapeMode selectionMode() const noexcept { return selectionMode_; }

    void setDisplayMode(ViewportDisplayMode mode);
    ViewportDisplayMode displayMode() const noexcept { return displayMode_; }

    void fitAll();
    void syncScene();
    void setPlaybackTrail(const std::vector<core::Vec3>& tcpPoints);
    void setPlaybackTcp(const core::Vec3& tcpWorld, bool arcActive);
    void setPlaybackRobotTransforms(const std::array<core::Transform4d, 7>& linkWorldTransforms);
    std::array<core::Transform4d, 7> playbackRobotTransforms() const;
    void syncRobotPosture(const std::array<double, 6>& jointsRad);
    void clearPlaybackPreview();
    TcpGizmo& tcpGizmo() noexcept { return tcpGizmo_; }

    bool setTargetGizmo(const std::string& targetId);
    const std::string& activeTargetId() const noexcept { return activeTargetId_; }
    void clearTargetGizmo();
    void syncGizmoPosition();
    void setGizmoReferenceFrame(GizmoRefFrame refFrame);
    GizmoRefFrame gizmoReferenceFrame() const noexcept { return tcpGizmo_.referenceFrame(); }
    void setGizmoSnapSteps(bool enabled, double linearStepMm = 1.0, double angularStepDeg = 1.0);
    void setMultiTargetProvider(std::function<std::vector<std::string>()> provider) {
        multiTargetProvider_ = std::move(provider);
    }

    void attachMainWindow(MainWindow* win);
    MainWindow* attachedMainWindow() const noexcept { return attachedWin_; }

    void setEdgePickedCallback(std::function<void(const PickedEdge&)> cb) { edgePickedCb_ = std::move(cb); }
    const std::optional<PickedEdge>& lastPickedEdge() const noexcept { return lastPickedEdge_; }
    void clearPickedEdge() noexcept { lastPickedEdge_.reset(); }

    bool pickAt(int pixelX, int pixelY);
    bool selectEdge(int cadHandleId, int edgeIndex);

#ifdef OLP_HAS_QT6
Q_SIGNALS:
    void edgePicked(const QString& nodeId, int cadHandleId, int edgeIndex);
    void sceneNodePicked(const QString& nodeId);
    void targetGizmoChanged(const QString& targetId);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
#endif

private:
    struct ViewState;
    void ensureView();
    void redrawView();
    void updateSelectionModes();
    bool performPicking(int pixelX, int pixelY);
    void setupTargetGizmoPresentation();
    core::Transform4d computeActiveUFrameInWorld() const noexcept;
    core::Transform4d findTargetSeamFrameWorld(const std::string& targetId) const;

    app::ProjectDocument& doc_;
    app::CommandManager& cmdMgr_;
    cad::ICadKernel* cadKernel_{nullptr};
    SelectionSubShapeMode selectionMode_{SelectionSubShapeMode::Edge};
    ViewportDisplayMode displayMode_{ViewportDisplayMode::ShadedWithEdges};
    TcpGizmo tcpGizmo_;
    std::unique_ptr<ViewState> view_;
    bool fitted_{false};
    std::string activeTargetId_{};
    std::function<std::vector<std::string>()> multiTargetProvider_{};
    std::optional<PickedEdge> lastPickedEdge_{};
    std::function<void(const PickedEdge&)> edgePickedCb_{};
    MainWindow* attachedWin_{nullptr};
    int winSelectionListenerId_{-1};
};

} // namespace olp::ui
