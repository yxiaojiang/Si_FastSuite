#pragma once

#include "olp/ui/TcpGizmo.h"

#ifdef OLP_HAS_QT6
#include <QWidget>
#endif

#include <memory>

namespace olp::ui {

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

// M03: 3D view. The shell owns this widget. Camera orbit, pan, zoom, and fit
// are handled here. Picking and the gizmo overlay come later.
#ifdef OLP_HAS_QT6
class OcctViewport : public QWidget {
#else
class OcctViewport {
#endif
public:
#ifdef OLP_HAS_QT6
    OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr, QWidget* parent = nullptr);
    ~OcctViewport() override;
#else
    OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr);
    ~OcctViewport();
#endif

    void setSelectionMode(SelectionSubShapeMode mode) noexcept { selectionMode_ = mode; }
    SelectionSubShapeMode selectionMode() const noexcept { return selectionMode_; }

    void setDisplayMode(ViewportDisplayMode mode) noexcept { displayMode_ = mode; }
    ViewportDisplayMode displayMode() const noexcept { return displayMode_; }

    void fitAll();
    void syncScene();
    TcpGizmo& tcpGizmo() noexcept { return tcpGizmo_; }

#ifdef OLP_HAS_QT6
protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    QPaintEngine* paintEngine() const override;
#endif

private:
    struct ViewState;
    void ensureView();
    void redrawView();

    app::ProjectDocument& doc_;
    SelectionSubShapeMode selectionMode_{SelectionSubShapeMode::Edge};
    ViewportDisplayMode displayMode_{ViewportDisplayMode::ShadedWithEdges};
    TcpGizmo tcpGizmo_;
    std::unique_ptr<ViewState> view_;
    bool fitted_{false};
};

} // namespace olp::ui
