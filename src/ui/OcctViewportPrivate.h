#pragma once

#include "olp/ui/OcctViewport.h"

#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef OLP_HAS_QT6
#include <QPoint>
#endif

#ifdef OLP_HAS_OCCT
#include <AIS_InteractiveContext.hxx>
#include <AIS_Manipulator.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#endif

namespace olp::ui {

struct OcctViewport::ViewState {
#ifdef OLP_HAS_OCCT
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
#ifdef _WIN32
    HWND renderHwnd{nullptr};
#endif
    std::unordered_map<const AIS_InteractiveObject*, std::string> aisToNodeId{};
    std::unordered_map<std::string, Handle(AIS_Shape)> nodeIdToAis{};
    std::vector<Handle(AIS_Shape)> seamAisObjects{};
    Handle(AIS_Shape) highlightEdgeAis{};
    Handle(AIS_Manipulator) manipulator{};
    Handle(AIS_Shape) targetGizmoProxyAis{};
    Handle(AIS_Shape) playbackTrailAis{};
    Handle(AIS_Shape) playbackTcpAis{};
#endif
#ifdef OLP_HAS_QT6
    bool rotating{false};
    bool panning{false};
    bool isClick{false};
    QPoint pressPos{};
    QPoint lastPos{};
#endif
};


} // namespace olp::ui
