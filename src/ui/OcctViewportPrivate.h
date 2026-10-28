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
#include <gp_Trsf.hxx>
#endif

namespace olp::ui {

#ifdef OLP_HAS_OCCT
inline gp_Trsf toGpTrsf(const core::Transform4d& t) noexcept {
    gp_Trsf trsf;
    const auto& r = t.rot.m;
    const auto& p = t.pos;
    trsf.SetValues(
        r[0][0], r[0][1], r[0][2], p.x,
        r[1][0], r[1][1], r[1][2], p.y,
        r[2][0], r[2][1], r[2][2], p.z);
    return trsf;
}

inline core::Transform4d fromGpTrsf(const gp_Trsf& trsf) noexcept {
    core::Transform4d out;
    out.pos.x = trsf.TranslationPart().X();
    out.pos.y = trsf.TranslationPart().Y();
    out.pos.z = trsf.TranslationPart().Z();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.rot.m[r][c] = trsf.Value(r + 1, c + 1);
        }
    }
    return out;
}
#endif

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
    std::array<Handle(AIS_Shape), 7> robotLinkAis{};
    std::string activeRobotNodeId{};
    std::array<core::Transform4d, 7> sceneRobotLinkTransforms{};
    std::array<core::Transform4d, 7> currentRobotLinkTransforms{};
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
