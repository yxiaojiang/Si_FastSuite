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

void OcctViewport::setSelectionMode(SelectionSubShapeMode mode) {
    selectionMode_ = mode;
    updateSelectionModes();
    redrawView();
}


void OcctViewport::updateSelectionModes() {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->context.IsNull()) {
        return;
    }
    view_->context->SetPixelTolerance(5);
    for (const auto& [objPtr, nodeId] : view_->aisToNodeId) {
        auto it = view_->nodeIdToAis.find(nodeId);
        if (it == view_->nodeIdToAis.end() || it->second.IsNull()) {
            continue;
        }
        const Handle(AIS_Shape)& aisShape = it->second;
        const core::SceneNode* node = doc_.findSceneNode(nodeId);
        if (node == nullptr) {
            continue;
        }

        view_->context->Deactivate(aisShape);
        if (selectionMode_ == SelectionSubShapeMode::Edge) {
            // Activate edge picking primarily on workpiece parts
            if (node->collisionRole == core::CollisionRole::Workpiece) {
                view_->context->Activate(aisShape, AIS_Shape::SelectionMode(TopAbs_EDGE), false);
            }
        } else if (selectionMode_ == SelectionSubShapeMode::Face) {
            if (node->collisionRole == core::CollisionRole::Workpiece || node->collisionRole == core::CollisionRole::Fixture) {
                view_->context->Activate(aisShape, AIS_Shape::SelectionMode(TopAbs_FACE), false);
            }
        } else if (selectionMode_ == SelectionSubShapeMode::Vertex) {
            if (node->collisionRole == core::CollisionRole::Workpiece) {
                view_->context->Activate(aisShape, AIS_Shape::SelectionMode(TopAbs_VERTEX), false);
            }
        } else if (selectionMode_ == SelectionSubShapeMode::Object) {
            view_->context->Activate(aisShape, AIS_Shape::SelectionMode(TopAbs_SHAPE), false);
        }
    }
#endif
}


bool OcctViewport::performPicking(int pixelX, int pixelY) {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->context.IsNull() || view_->view.IsNull()) {
        return false;
    }
    view_->context->MoveTo(pixelX, pixelY, view_->view, false);
    view_->context->SelectDetected();

    view_->context->InitSelected();
    if (!view_->context->MoreSelected()) {
        clearPickedEdge();
        return false;
    }

    Handle(AIS_InteractiveObject) pickedObj = view_->context->SelectedInteractive();
    std::string nodeId{};
    auto it = view_->aisToNodeId.find(pickedObj.get());
    if (it != view_->aisToNodeId.end()) {
        nodeId = it->second;
    }
    if (nodeId.empty()) {
        return false;
    }

    const core::SceneNode* node = doc_.findSceneNode(nodeId);
    if (node == nullptr) {
        return false;
    }

    if (selectionMode_ == SelectionSubShapeMode::Edge && view_->context->HasSelectedShape()) {
        TopoDS_Shape pickedSub = view_->context->SelectedShape();
        if (pickedSub.ShapeType() == TopAbs_EDGE) {
            const TopoDS_Edge edge = TopoDS::Edge(pickedSub);
            int edgeIdx = -1;
            if (auto* occtKernel = dynamic_cast<cad::OcctCadKernel*>(cadKernel_)) {
                edgeIdx = occtKernel->findEdgeIndex(node->cadHandleId, edge);
            }
            if (edgeIdx >= 0) {
                PickedEdge info{};
                info.nodeId = nodeId;
                info.cadHandleId = node->cadHandleId;
                info.edgeIndex = edgeIdx;
                if (cadKernel_ != nullptr) {
                    const auto edges = cadKernel_->getEdges(node->cadHandleId);
                    if (edgeIdx < static_cast<int>(edges.size())) {
                        const auto& e = edges[edgeIdx];
                        info.startPt = e.startPt;
                        info.endPt = e.endPt;
                        info.midPt = e.midPt;
                        info.tangent = e.tangent;
                        info.lengthMm = e.lengthMm;
                        info.isLinear = e.isLinear;
                    }
                }
                lastPickedEdge_ = info;

                // Visual highlight on the picked edge
                if (!view_->highlightEdgeAis.IsNull()) {
                    view_->context->Remove(view_->highlightEdgeAis, false);
                    view_->highlightEdgeAis.Nullify();
                }
                view_->highlightEdgeAis = new AIS_Shape(edge);
                view_->highlightEdgeAis->SetColor(Quantity_NOC_MAGENTA1);
                view_->highlightEdgeAis->SetWidth(4.0);
                view_->context->Display(view_->highlightEdgeAis, false);

                if (edgePickedCb_) {
                    edgePickedCb_(info);
                }
#ifdef OLP_HAS_QT6
                Q_EMIT edgePicked(QString::fromStdString(nodeId), node->cadHandleId, edgeIdx);
#endif
                view_->view->Redraw();
                return true;
            }
        }
    }

#ifdef OLP_HAS_QT6
    Q_EMIT sceneNodePicked(QString::fromStdString(nodeId));
#endif
    view_->view->Redraw();
    return true;
#else
    (void)pixelX;
    (void)pixelY;
    return false;
#endif
}


bool OcctViewport::pickAt(int pixelX, int pixelY) {
    return performPicking(pixelX, pixelY);
}


bool OcctViewport::selectEdge(int cadHandleId, int edgeIndex) {
#ifdef OLP_HAS_OCCT
    if (cadKernel_ == nullptr) {
        return false;
    }
    const core::SceneNode* targetNode = nullptr;
    for (const auto& node : doc_.sceneNodes) {
        if (node.cadHandleId == cadHandleId) {
            targetNode = &node;
            break;
        }
    }
    if (targetNode == nullptr) {
        return false;
    }

    const auto edges = cadKernel_->getEdges(cadHandleId);
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size())) {
        return false;
    }
    const auto& e = edges[edgeIndex];

    PickedEdge info{};
    info.nodeId = targetNode->id;
    info.cadHandleId = cadHandleId;
    info.edgeIndex = edgeIndex;
    info.startPt = e.startPt;
    info.endPt = e.endPt;
    info.midPt = e.midPt;
    info.tangent = e.tangent;
    info.lengthMm = e.lengthMm;
    info.isLinear = e.isLinear;
    lastPickedEdge_ = info;

    if (view_ != nullptr && !view_->context.IsNull()) {
        auto* occtKernel = dynamic_cast<cad::OcctCadKernel*>(cadKernel_);
        if (occtKernel != nullptr) {
            const TopoDS_Edge edgeShape = occtKernel->getEdgeShape(cadHandleId, edgeIndex);
            if (!edgeShape.IsNull()) {
                if (!view_->highlightEdgeAis.IsNull()) {
                    view_->context->Remove(view_->highlightEdgeAis, false);
                    view_->highlightEdgeAis.Nullify();
                }
                view_->highlightEdgeAis = new AIS_Shape(edgeShape);
                view_->highlightEdgeAis->SetColor(Quantity_NOC_MAGENTA1);
                view_->highlightEdgeAis->SetWidth(4.0);

                gp_Trsf trsf;
                const auto& r = targetNode->localTransform.rot.m;
                const auto& p = targetNode->localTransform.pos;
                trsf.SetValues(
                    r[0][0], r[0][1], r[0][2], p.x,
                    r[1][0], r[1][1], r[1][2], p.y,
                    r[2][0], r[2][1], r[2][2], p.z);
                view_->highlightEdgeAis->SetLocalTransformation(trsf);

                view_->context->Display(view_->highlightEdgeAis, false);
                if (!view_->view.IsNull()) {
                    view_->view->Redraw();
                }
            }
        }
    }

    if (edgePickedCb_) {
        edgePickedCb_(info);
    }
#ifdef OLP_HAS_QT6
    Q_EMIT edgePicked(QString::fromStdString(targetNode->id), cadHandleId, edgeIndex);
#endif
    return true;
#else
    (void)cadHandleId;
    (void)edgeIndex;
    return false;
#endif
}


} // namespace olp::ui
