#include "olp/ui/OcctViewport.h"
#include "OcctViewportPrivate.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/device/RobotModel.h"
#include "olp/algo/FclCollisionQueries.h"

#ifdef OLP_HAS_OCCT
#include <AIS_Shape.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <Quantity_Color.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#endif

namespace olp::ui {

void OcctViewport::clearPlaybackPreview() {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr) {
        return;
    }
    view_->currentRobotLinkTransforms = view_->sceneRobotLinkTransforms;
    for (size_t k = 0; k < 7; ++k) {
        if (!view_->robotLinkAis[k].IsNull()) {
            view_->robotLinkAis[k]->SetLocalTransformation(toGpTrsf(view_->sceneRobotLinkTransforms[k]));
            if (!view_->context.IsNull()) {
                view_->context->Redisplay(view_->robotLinkAis[k], false);
            }
        }
    }
    if (view_->context.IsNull()) {
        return;
    }
    if (!view_->playbackTrailAis.IsNull()) {
        view_->context->Remove(view_->playbackTrailAis, false);
        view_->playbackTrailAis.Nullify();
    }
    if (!view_->playbackTcpAis.IsNull()) {
        view_->context->Remove(view_->playbackTcpAis, false);
        view_->playbackTcpAis.Nullify();
    }
    if (!view_->view.IsNull()) {
        view_->view->Redraw();
    }
#else
    return;
#endif
}

void OcctViewport::setPlaybackTrail(const std::vector<core::Vec3>& tcpPoints) {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->context.IsNull()) {
        return;
    }
    if (!view_->playbackTrailAis.IsNull()) {
        view_->context->Remove(view_->playbackTrailAis, false);
        view_->playbackTrailAis.Nullify();
    }
    if (tcpPoints.size() >= 2) {
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& point : tcpPoints) {
            polygon.Add(gp_Pnt(point.x, point.y, point.z));
        }
        if (polygon.IsDone()) {
            view_->playbackTrailAis = new AIS_Shape(polygon.Shape());
            view_->playbackTrailAis->SetColor(Quantity_NOC_ORANGE);
            view_->playbackTrailAis->SetWidth(2.5);
            view_->context->Display(view_->playbackTrailAis, false);
        }
    }
    if (!view_->view.IsNull()) {
        view_->view->Redraw();
    }
#else
    (void)tcpPoints;
#endif
}

void OcctViewport::setPlaybackTcp(const core::Vec3& tcpWorld, bool arcActive) {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->context.IsNull()) {
        return;
    }
    const bool displayed = !view_->playbackTcpAis.IsNull() &&
        view_->context->IsDisplayed(view_->playbackTcpAis);
    if (!displayed) {
        if (!view_->playbackTcpAis.IsNull()) {
            view_->context->Remove(view_->playbackTcpAis, false);
        }
        view_->playbackTcpAis = new AIS_Shape(BRepPrimAPI_MakeSphere(8.0).Shape());
        view_->context->Display(view_->playbackTcpAis, false);
    }
    gp_Trsf transform;
    transform.SetTranslation(gp_Vec(tcpWorld.x, tcpWorld.y, tcpWorld.z));
    view_->playbackTcpAis->SetLocalTransformation(transform);
    view_->playbackTcpAis->SetColor(arcActive ? Quantity_NOC_RED : Quantity_NOC_GOLDENROD);
    view_->context->Redisplay(view_->playbackTcpAis, false);
    if (!view_->view.IsNull()) {
        view_->view->Redraw();
    }
#else
    (void)tcpWorld;
    (void)arcActive;
#endif
}

void OcctViewport::setPlaybackRobotTransforms(const std::array<core::Transform4d, 7>& linkWorldTransforms) {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr) {
        return;
    }
    view_->currentRobotLinkTransforms = linkWorldTransforms;
    bool updated = false;
    for (size_t k = 0; k < 7; ++k) {
        if (!view_->robotLinkAis[k].IsNull()) {
            view_->robotLinkAis[k]->SetLocalTransformation(toGpTrsf(linkWorldTransforms[k]));
            if (!view_->context.IsNull()) {
                view_->context->Redisplay(view_->robotLinkAis[k], false);
            }
            updated = true;
        }
    }
    if (updated && !view_->view.IsNull()) {
        view_->view->Redraw();
    }
#else
    (void)linkWorldTransforms;
#endif
}

std::array<core::Transform4d, 7> OcctViewport::playbackRobotTransforms() const {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr) return {};
    bool hasAis = false;
    for (size_t k = 0; k < 7; ++k) {
        if (!view_->robotLinkAis[k].IsNull()) {
            hasAis = true;
            break;
        }
    }
    if (hasAis) {
        std::array<core::Transform4d, 7> out{};
        for (size_t k = 0; k < 7; ++k) {
            if (!view_->robotLinkAis[k].IsNull()) {
                out[k] = fromGpTrsf(view_->robotLinkAis[k]->Transformation());
            } else {
                out[k] = view_->currentRobotLinkTransforms[k];
            }
        }
        return out;
    }
    return view_->currentRobotLinkTransforms;
#else
    return {};
#endif
}

void OcctViewport::syncRobotPosture(const std::array<double, 6>& jointsRad) {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->activeRobotNodeId.empty()) {
        return;
    }
    const auto* node = doc_.findSceneNode(view_->activeRobotNodeId);
    if (node == nullptr) {
        return;
    }
    view_->sceneRobotLinkTransforms = algo::forwardRobotLinkWorldFrames(jointsRad, node->localTransform);
    view_->currentRobotLinkTransforms = view_->sceneRobotLinkTransforms;
    for (size_t k = 0; k < 7; ++k) {
        if (!view_->robotLinkAis[k].IsNull()) {
            view_->robotLinkAis[k]->SetLocalTransformation(toGpTrsf(view_->sceneRobotLinkTransforms[k]));
            if (!view_->context.IsNull()) {
                view_->context->Redisplay(view_->robotLinkAis[k], false);
            }
        }
    }
    if (!view_->view.IsNull()) {
        view_->view->Redraw();
    }
#else
    (void)jointsRad;
#endif
}

} // namespace olp::ui
