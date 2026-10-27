#include "olp/ui/OcctViewport.h"
#include "OcctViewportPrivate.h"

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
    if (view_ == nullptr || view_->context.IsNull()) {
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

} // namespace olp::ui
