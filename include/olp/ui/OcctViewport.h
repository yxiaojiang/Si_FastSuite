#pragma once

#include "olp/ui/TcpGizmo.h"
#include <string>

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

// M03: 3D Viewport Wrapper (Qt6 QWidget + OpenCASCADE V3d_View / AIS_InteractiveContext)
class OcctViewport {
public:
    OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr);
    ~OcctViewport();

    void setSelectionMode(SelectionSubShapeMode mode) noexcept { selectionMode_ = mode; }
    SelectionSubShapeMode selectionMode() const noexcept { return selectionMode_; }

    void setDisplayMode(ViewportDisplayMode mode) noexcept { displayMode_ = mode; }
    ViewportDisplayMode displayMode() const noexcept { return displayMode_; }

    void fitAll() noexcept;
    TcpGizmo& tcpGizmo() noexcept { return tcpGizmo_; }

private:
    app::ProjectDocument& doc_;
    SelectionSubShapeMode selectionMode_{SelectionSubShapeMode::Edge};
    ViewportDisplayMode displayMode_{ViewportDisplayMode::ShadedWithEdges};
    TcpGizmo tcpGizmo_;
};

} // namespace olp::ui
