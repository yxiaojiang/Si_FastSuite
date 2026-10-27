#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "olp/ui/OcctViewport.h"
#include "OcctViewportPrivate.h"
#include "olp/ui/MainWindow.h"
#include "olp/cad/OcctCadKernel.h"

#ifdef OLP_HAS_QT6
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QTimer>
#include <QWheelEvent>
#endif


#include <algorithm>
#include <cmath>
#include <unordered_map>

#ifdef OLP_HAS_OCCT
#include <AIS_InteractiveContext.hxx>
#include <AIS_Manipulator.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <WNT_Window.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#endif

namespace olp::ui {

core::Transform4d OcctViewport::computeActiveUFrameInWorld() const noexcept {
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Frame) {
            return node.localTransform;
        }
    }
    return core::Transform4d::identity();
}


core::Transform4d OcctViewport::findTargetSeamFrameWorld(const std::string& targetId) const {
    const auto* target = doc_.findTarget(targetId);
    if (target == nullptr) {
        return core::Transform4d::identity();
    }
    const core::Transform4d uframeInWorld = computeActiveUFrameInWorld();
    for (const auto& cu : doc_.controllers) {
        for (const auto& prog : cu.programs) {
            for (const auto& op : prog.operations) {
                for (const auto& t : op.targets) {
                    if (t.id == targetId && !op.seamId.empty()) {
                        if (const auto* seam = doc_.findSeam(op.seamId)) {
                            if (!seam->sampledFrames.empty()) {
                                const core::Vec3 targetPosWorld = (uframeInWorld * target->poseInUFrame).pos;
                                double bestDistSq = 1e30;
                                size_t bestIdx = 0;
                                for (size_t i = 0; i < seam->sampledFrames.size(); ++i) {
                                    const double d2 = (seam->sampledFrames[i].position - targetPosWorld).normSq();
                                    if (d2 < bestDistSq) {
                                        bestDistSq = d2;
                                        bestIdx = i;
                                    }
                                }
                                return seam->sampledFrames[bestIdx].frame;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    return uframeInWorld;
}


bool OcctViewport::setTargetGizmo(const std::string& targetId) {
    if (targetId.empty()) {
        clearTargetGizmo();
        return true;
    }

    const auto* target = doc_.findTarget(targetId);
    if (target == nullptr) {
        clearTargetGizmo();
        return false;
    }

    activeTargetId_ = targetId;
    const core::Transform4d uframeInWorld = computeActiveUFrameInWorld();
    tcpGizmo_.setUFrameInWorld(uframeInWorld);

    const core::Transform4d seamFrameWorld = findTargetSeamFrameWorld(targetId);
    const core::Transform4d worldToUFrame = uframeInWorld.inverse();
    tcpGizmo_.setSeamFrameInUFrame(worldToUFrame * seamFrameWorld);

#ifdef OLP_HAS_OCCT
    if (view_ != nullptr && !view_->context.IsNull()) {
        setupTargetGizmoPresentation();
        syncGizmoPosition();
        if (!view_->view.IsNull()) {
            view_->view->Redraw();
        }
    }
#endif

#ifdef OLP_HAS_QT6
    Q_EMIT targetGizmoChanged(QString::fromStdString(targetId));
#endif
    return true;
}


void OcctViewport::clearTargetGizmo() {
    if (tcpGizmo_.isDragging()) {
        tcpGizmo_.cancelDrag();
    }
    activeTargetId_.clear();
#ifdef OLP_HAS_OCCT
    if (view_ != nullptr && !view_->context.IsNull()) {
        if (!view_->manipulator.IsNull()) {
            view_->manipulator->DeactivateCurrentMode();
            if (view_->manipulator->IsAttached()) {
                view_->manipulator->Detach();
            }
            view_->manipulator.Nullify();
        }
        if (!view_->targetGizmoProxyAis.IsNull()) {
            view_->context->Remove(view_->targetGizmoProxyAis, false);
            view_->targetGizmoProxyAis.Nullify();
        }
        if (!view_->view.IsNull()) {
            view_->view->Redraw();
        }
    }
#endif
#ifdef OLP_HAS_QT6
    Q_EMIT targetGizmoChanged(QString());
#endif
}


void OcctViewport::setupTargetGizmoPresentation() {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->context.IsNull() || activeTargetId_.empty()) {
        return;
    }
    const auto* target = doc_.findTarget(activeTargetId_);
    if (target == nullptr) {
        return;
    }

    const core::Transform4d uframeInWorld = computeActiveUFrameInWorld();
    const core::Transform4d targetWorld = uframeInWorld * target->poseInUFrame;

    if (view_->targetGizmoProxyAis.IsNull()) {
        TopoDS_Shape sphereShape = BRepPrimAPI_MakeSphere(gp_Pnt(0, 0, 0), 4.0).Shape();
        view_->targetGizmoProxyAis = new AIS_Shape(sphereShape);
        view_->targetGizmoProxyAis->SetColor(Quantity_NOC_YELLOW);
        view_->context->Display(view_->targetGizmoProxyAis, false);
        view_->context->Deactivate(view_->targetGizmoProxyAis);
    }

    gp_Trsf proxyTrsf;
    proxyTrsf.SetTranslation(gp_Vec(targetWorld.pos.x, targetWorld.pos.y, targetWorld.pos.z));
    view_->targetGizmoProxyAis->SetLocalTransformation(proxyTrsf);

    if (view_->manipulator.IsNull()) {
        view_->manipulator = new AIS_Manipulator();
        view_->manipulator->SetSize(100.0f);
        view_->manipulator->SetZoomPersistence(true);
        view_->manipulator->SetModeActivationOnDetection(true);

        AIS_Manipulator::OptionsForAttach opts;
        opts.SetAdjustPosition(false);
        opts.SetAdjustSize(false);
        opts.SetEnableModes(true);
        view_->manipulator->Attach(view_->targetGizmoProxyAis, opts);

        view_->manipulator->SetPart(AIS_MM_Scaling, false);
        view_->context->Deactivate(view_->manipulator, AIS_MM_Scaling);
    }
#endif
}


void OcctViewport::syncGizmoPosition() {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->context.IsNull() || view_->manipulator.IsNull() || activeTargetId_.empty()) {
        return;
    }
    const auto* target = doc_.findTarget(activeTargetId_);
    if (target == nullptr) {
        return;
    }

    const core::Transform4d uframeInWorld = computeActiveUFrameInWorld();
    const core::Transform4d targetWorld = uframeInWorld * target->poseInUFrame;

    if (!view_->targetGizmoProxyAis.IsNull()) {
        gp_Trsf proxyTrsf;
        proxyTrsf.SetTranslation(gp_Vec(targetWorld.pos.x, targetWorld.pos.y, targetWorld.pos.z));
        view_->targetGizmoProxyAis->SetLocalTransformation(proxyTrsf);
    }

    core::Mat3 rotManip = core::Mat3::identity();
    switch (tcpGizmo_.referenceFrame()) {
    case GizmoRefFrame::Tool:
        rotManip = targetWorld.rot;
        break;
    case GizmoRefFrame::World:
        rotManip = core::Mat3::identity();
        break;
    case GizmoRefFrame::UFrame:
        rotManip = uframeInWorld.rot;
        break;
    case GizmoRefFrame::Seam:
        rotManip = findTargetSeamFrameWorld(activeTargetId_).rot;
        break;
    }

    const auto dirZ = rotManip * core::Vec3(0, 0, 1);
    const auto dirX = rotManip * core::Vec3(1, 0, 0);
    const auto& p = targetWorld.pos;

    gp_Ax2 ax2(
        gp_Pnt(p.x, p.y, p.z),
        gp_Dir(dirZ.x, dirZ.y, dirZ.z),
        gp_Dir(dirX.x, dirX.y, dirX.x));

    view_->manipulator->SetPosition(ax2);
    view_->context->Update(view_->manipulator, false);
    view_->context->RecomputeSelectionOnly(view_->manipulator);
#endif
}


void OcctViewport::setGizmoReferenceFrame(GizmoRefFrame refFrame) {
    tcpGizmo_.setReferenceFrame(refFrame);
#ifdef OLP_HAS_OCCT
    syncGizmoPosition();
    if (view_ != nullptr && !view_->view.IsNull()) {
        view_->view->Redraw();
    }
#endif
}


void OcctViewport::setGizmoSnapSteps(bool enabled, double linearStepMm, double angularStepDeg) {
    tcpGizmo_.setSnapSteps(enabled, linearStepMm, angularStepDeg);
}



#ifdef OLP_HAS_QT6
void OcctViewport::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->view.IsNull()) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    view_->lastPos = QPoint(
        static_cast<int>(std::lround(event->position().x() * dpr)),
        static_cast<int>(std::lround(event->position().y() * dpr)));
    const int x = view_->lastPos.x();
    const int y = view_->lastPos.y();
    if (event->button() == Qt::LeftButton) {
        if (!view_->manipulator.IsNull() && view_->manipulator->HasActiveMode()) {
            view_->manipulator->StartTransform(x, y, view_->view);
            std::vector<std::string> dragTargets;
            if (multiTargetProvider_) {
                dragTargets = multiTargetProvider_();
            }
            if (dragTargets.empty() && !activeTargetId_.empty()) {
                dragTargets.push_back(activeTargetId_);
            }
            if (!dragTargets.empty()) {
                tcpGizmo_.beginDrag(dragTargets);
            }
            view_->isClick = false;
            return;
        }

        view_->pressPos = view_->lastPos;
        view_->isClick = true;
        view_->view->StartRotation(x, y);
    } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        view_->panning = true;
    }
#else
    (void)event;
#endif
}

void OcctViewport::mouseMoveEvent(QMouseEvent* event) {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->view.IsNull()) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const QPoint pos(
        static_cast<int>(std::lround(event->position().x() * dpr)),
        static_cast<int>(std::lround(event->position().y() * dpr)));

    if (tcpGizmo_.isDragging()) {
        if (!view_->manipulator.IsNull() && view_->manipulator->HasActiveTransformation()) {
            gp_Trsf aTrsf = view_->manipulator->Transform(pos.x(), pos.y(), view_->view);

            const core::Transform4d uframeInWorld = computeActiveUFrameInWorld();
            core::Mat3 rotManip = core::Mat3::identity();
            const auto* target = doc_.findTarget(activeTargetId_);
            if (target != nullptr) {
                const core::Transform4d targetWorld = uframeInWorld * target->poseInUFrame;
                switch (tcpGizmo_.referenceFrame()) {
                case GizmoRefFrame::Tool:
                    rotManip = targetWorld.rot;
                    break;
                case GizmoRefFrame::World:
                    rotManip = core::Mat3::identity();
                    break;
                case GizmoRefFrame::UFrame:
                    rotManip = uframeInWorld.rot;
                    break;
                case GizmoRefFrame::Seam:
                    rotManip = findTargetSeamFrameWorld(activeTargetId_).rot;
                    break;
                }
            }

            const gp_XYZ transPart = aTrsf.TranslationPart();
            const core::Vec3 transWorld(transPart.X(), transPart.Y(), transPart.Z());
            const core::Vec3 rawTrans = rotManip.transposed() * transWorld;

            core::Mat3 rotWorld;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    rotWorld.m[r][c] = aTrsf.Value(r + 1, c + 1);
                }
            }
            const core::Mat3 rotRef = rotManip.transposed() * rotWorld * rotManip;
            core::Transform4d tempT;
            tempT.rot = rotRef;
            const auto wpr = tempT.toWPR();
            const core::Vec3 rawRotEulerDeg(wpr.wDeg, wpr.pDeg, wpr.rDeg);

            tcpGizmo_.updateDragDelta(rawTrans, rawRotEulerDeg);
            view_->view->Redraw();
        }
        view_->lastPos = pos;
        return;
    }

    if (event->buttons() & Qt::LeftButton) {
        if (view_->isClick && (pos - view_->pressPos).manhattanLength() > 4) {
            view_->isClick = false;
            view_->rotating = true;
        }
        if (view_->rotating) {
            view_->view->Rotation(pos.x(), pos.y());
        }
    } else if (view_->panning) {
        view_->view->Pan(pos.x() - view_->lastPos.x(), view_->lastPos.y() - pos.y());
    } else {
        // Pre-selection hover dynamic highlighting
        if (!view_->context.IsNull()) {
            view_->context->MoveTo(pos.x(), pos.y(), view_->view, true);
        }
    }
    view_->lastPos = pos;
#else
    (void)event;
#endif
}

void OcctViewport::mouseReleaseEvent(QMouseEvent* event) {
#ifdef OLP_HAS_OCCT
    if (view_ != nullptr) {
        if (event->button() == Qt::LeftButton) {
            if (tcpGizmo_.isDragging()) {
                if (!view_->manipulator.IsNull()) {
                    view_->manipulator->StopTransform(false);
                }
                tcpGizmo_.commitDrag();
                syncGizmoPosition();
                if (!view_->view.IsNull()) {
                    view_->view->Redraw();
                }
                return;
            }
            if (view_->rotating) {
                view_->rotating = false;
            } else if (view_->isClick) {
                view_->isClick = false;
                performPicking(view_->lastPos.x(), view_->lastPos.y());
            }
        } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
            view_->panning = false;
        }
    }
#else
    (void)event;
#endif
}

void OcctViewport::keyPressEvent(QKeyEvent* event) {
#ifdef OLP_HAS_OCCT
    if (event->key() == Qt::Key_Escape && tcpGizmo_.isDragging()) {
        if (view_ != nullptr && !view_->manipulator.IsNull()) {
            view_->manipulator->StopTransform(false);
        }
        tcpGizmo_.cancelDrag();
        syncGizmoPosition();
        if (view_ != nullptr && !view_->view.IsNull()) {
            view_->view->Redraw();
        }
        event->accept();
        return;
    }
#endif
    QWidget::keyPressEvent(event);
}

#endif

} // namespace olp::ui
