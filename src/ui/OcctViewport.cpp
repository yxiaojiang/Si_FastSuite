#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "olp/ui/OcctViewport.h"
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
#endif
#ifdef OLP_HAS_QT6
    bool rotating{false};
    bool panning{false};
    bool isClick{false};
    QPoint pressPos{};
    QPoint lastPos{};
#endif
};

#if defined(_WIN32) && defined(OLP_HAS_QT6) && defined(OLP_HAS_OCCT)
namespace {

LRESULT CALLBACK occtRenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        auto* viewPtr = reinterpret_cast<V3d_View*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (viewPtr != nullptr) {
            viewPtr->MustBeResized();
            viewPtr->Redraw();
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND createOcctRenderWindow(HWND parentHwnd) {
    static ATOM classAtom = 0;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (classAtom == 0) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = occtRenderWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"SiFastSuiteOcctRenderHost";
        classAtom = RegisterClassExW(&wc);
    }
    const LONG_PTR parentStyle = GetWindowLongPtrW(parentHwnd, GWL_STYLE);
    if ((parentStyle & WS_CLIPCHILDREN) == 0) {
        SetWindowLongPtrW(parentHwnd, GWL_STYLE, parentStyle | WS_CLIPCHILDREN);
    }
    RECT rc{};
    GetClientRect(parentHwnd, &rc);
    const int w = std::max<int>(1, rc.right - rc.left);
    const int h = std::max<int>(1, rc.bottom - rc.top);
    return CreateWindowExW(
        0,
        L"SiFastSuiteOcctRenderHost",
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, w, h,
        parentHwnd,
        nullptr,
        hInst,
        nullptr);
}

void syncOcctRenderWindowSize(HWND parentHwnd, HWND renderHwnd) {
    if (parentHwnd == nullptr || renderHwnd == nullptr) {
        return;
    }
    RECT rc{};
    GetClientRect(parentHwnd, &rc);
    SetWindowPos(
        renderHwnd,
        nullptr,
        0,
        0,
        std::max<int>(1, rc.right - rc.left),
        std::max<int>(1, rc.bottom - rc.top),
        SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace
#endif

#ifdef OLP_HAS_QT6
OcctViewport::OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr, QWidget* parent)
    : OcctViewport(doc, cmdMgr, nullptr, parent) {}

OcctViewport::OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr, cad::ICadKernel* cadKernel, QWidget* parent)
    : QWidget(parent), doc_(doc), cmdMgr_(cmdMgr), cadKernel_(cadKernel), tcpGizmo_(doc, cmdMgr), view_(std::make_unique<ViewState>()) {
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoMousePropagation);
    const QColor background(30, 34, 38);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, background);
    pal.setColor(QPalette::Base, background);
    setPalette(pal);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(640, 420);
}

OcctViewport::~OcctViewport() {
    if (attachedWin_ != nullptr && winSelectionListenerId_ >= 0) {
        attachedWin_->removeSelectionListener(winSelectionListenerId_);
        winSelectionListenerId_ = -1;
    }
    clearTargetGizmo();
#if defined(OLP_HAS_OCCT) && defined(_WIN32)
    if (view_ != nullptr) {
        if (view_->renderHwnd != nullptr) {
            SetWindowLongPtrW(view_->renderHwnd, GWLP_USERDATA, 0);
        }
        view_->highlightEdgeAis.Nullify();
        view_->seamAisObjects.clear();
        view_->nodeIdToAis.clear();
        view_->aisToNodeId.clear();
        view_->view.Nullify();
        view_->context.Nullify();
        view_->viewer.Nullify();
        if (view_->renderHwnd != nullptr && IsWindow(view_->renderHwnd)) {
            DestroyWindow(view_->renderHwnd);
            view_->renderHwnd = nullptr;
        }
    }
#endif
}
#else
OcctViewport::OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr, cad::ICadKernel* cadKernel)
    : doc_(doc), cmdMgr_(cmdMgr), cadKernel_(cadKernel), tcpGizmo_(doc, cmdMgr), view_(std::make_unique<ViewState>()) {}

OcctViewport::~OcctViewport() {
    if (attachedWin_ != nullptr && winSelectionListenerId_ >= 0) {
        attachedWin_->removeSelectionListener(winSelectionListenerId_);
        winSelectionListenerId_ = -1;
    }
    clearTargetGizmo();
}
#endif

void OcctViewport::attachMainWindow(MainWindow* win) {
    if (attachedWin_ != nullptr && winSelectionListenerId_ >= 0) {
        attachedWin_->removeSelectionListener(winSelectionListenerId_);
        winSelectionListenerId_ = -1;
    }
    attachedWin_ = win;
    if (attachedWin_ != nullptr) {
        winSelectionListenerId_ = attachedWin_->addSelectionListener([this]() {
            if (attachedWin_ != nullptr) {
                const std::string& targetId = attachedWin_->selectedTargetId();
                if (targetId != activeTargetId_) {
                    setTargetGizmo(targetId);
                }
            }
        });
        if (!attachedWin_->selectedTargetId().empty()) {
            setTargetGizmo(attachedWin_->selectedTargetId());
        }
    }
}

void OcctViewport::setSelectionMode(SelectionSubShapeMode mode) {
    selectionMode_ = mode;
    updateSelectionModes();
    redrawView();
}

void OcctViewport::setDisplayMode(ViewportDisplayMode mode) {
    displayMode_ = mode;
    syncScene();
}

void OcctViewport::redrawView() {
#ifdef OLP_HAS_OCCT
    if (view_ != nullptr && !view_->view.IsNull()) {
        view_->view->MustBeResized();
        view_->view->Redraw();
    }
#endif
}

void OcctViewport::fitAll() {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->view.IsNull()) {
        return;
    }
    Bnd_Box bounds;
    for (const auto& node : doc_.sceneNodes) {
        if (!node.visible) {
            continue;
        }
        const core::Vec3& lo = node.renderMesh.bboxMin;
        const core::Vec3& hi = node.renderMesh.bboxMax;
        if ((hi - lo).norm() < 1.0) {
            continue;
        }
        bounds.Add(gp_Pnt(std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z)));
        bounds.Add(gp_Pnt(std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z)));
    }
    if (bounds.IsVoid()) {
        view_->view->FitAll(0.02, false);
    } else {
        view_->view->FitAll(bounds, 0.08, false);
    }
    view_->view->ZFitAll();
    view_->view->Redraw();
    fitted_ = true;
#else
    fitted_ = fitted_;
#endif
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

void OcctViewport::syncScene() {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->context.IsNull()) {
        return;
    }
    view_->context->RemoveAll(false);
    view_->aisToNodeId.clear();
    view_->nodeIdToAis.clear();
    view_->seamAisObjects.clear();
    view_->highlightEdgeAis.Nullify();
    view_->manipulator.Nullify();
    view_->targetGizmoProxyAis.Nullify();

    auto* occtKernel = dynamic_cast<cad::OcctCadKernel*>(cadKernel_);
    int shown = 0;

    for (const auto& node : doc_.sceneNodes) {
        if (!node.visible || node.collisionRole == core::CollisionRole::Ignored) {
            continue;
        }

        TopoDS_Shape shape;
        if (occtKernel != nullptr && node.cadHandleId > 0) {
            shape = occtKernel->getShape(node.cadHandleId);
        }

        Handle(AIS_Shape) displayed;
        if (!shape.IsNull()) {
            // Real CAD B-Rep shape presentation
            displayed = new AIS_Shape(shape);
        } else if (node.renderMesh.vertices.size() >= 3 && !node.renderMesh.triangles.empty()) {
            // Tessellated triangle mesh fallback
            const int nbNodes = static_cast<int>(node.renderMesh.vertices.size());
            const int nbTri = static_cast<int>(node.renderMesh.triangles.size());
            Handle(Poly_Triangulation) tri = new Poly_Triangulation(nbNodes, nbTri, false);
            for (int i = 0; i < nbNodes; ++i) {
                const auto& v = node.renderMesh.vertices[i];
                tri->SetNode(i + 1, gp_Pnt(v.x, v.y, v.z));
            }
            for (int i = 0; i < nbTri; ++i) {
                const auto& t = node.renderMesh.triangles[i];
                tri->SetTriangle(i + 1, Poly_Triangle(
                    static_cast<int>(t[0] + 1),
                    static_cast<int>(t[1] + 1),
                    static_cast<int>(t[2] + 1)));
            }
            TopoDS_Face face;
            BRep_Builder bb;
            bb.MakeFace(face);
            bb.UpdateFace(face, tri);
            displayed = new AIS_Shape(face);
        } else {
            // Bounding box representation fallback
            const core::Vec3& lo = node.renderMesh.bboxMin;
            const core::Vec3& hi = node.renderMesh.bboxMax;
            if ((hi - lo).norm() < 1.0) {
                continue;
            }
            const gp_Pnt cornerMin(std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z));
            const gp_Pnt cornerMax(std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z));
            displayed = new AIS_Shape(BRepPrimAPI_MakeBox(cornerMin, cornerMax).Shape());
        }

        // Apply local node transform
        gp_Trsf trsf;
        const auto& r = node.localTransform.rot.m;
        const auto& p = node.localTransform.pos;
        trsf.SetValues(
            r[0][0], r[0][1], r[0][2], p.x,
            r[1][0], r[1][1], r[1][2], p.y,
            r[2][0], r[2][1], r[2][2], p.z);
        displayed->SetLocalTransformation(trsf);

        // Color coding by role
        Quantity_NameOfColor color = Quantity_NOC_GRAY50;
        if (node.collisionRole == core::CollisionRole::Workpiece) {
            color = Quantity_NOC_STEELBLUE;
        } else if (node.collisionRole == core::CollisionRole::Fixture) {
            color = Quantity_NOC_ORANGE;
        } else if (node.type == core::SceneNodeType::Robot) {
            color = Quantity_NOC_GOLDENROD;
        }
        displayed->SetColor(color);
        displayed->SetTransparency(0.0);

        // Display mode styling
        if (displayMode_ == ViewportDisplayMode::ShadedWithEdges) {
            displayed->SetDisplayMode(AIS_Shaded);
            displayed->Attributes()->SetFaceBoundaryDraw(true);
            displayed->Attributes()->SetupOwnFaceBoundaryAspect();
            displayed->Attributes()->FaceBoundaryAspect()->SetColor(Quantity_NOC_BLACK);
            displayed->Attributes()->FaceBoundaryAspect()->SetWidth(1.0);
        } else if (displayMode_ == ViewportDisplayMode::Shaded) {
            displayed->SetDisplayMode(AIS_Shaded);
            displayed->Attributes()->SetFaceBoundaryDraw(false);
        } else {
            displayed->SetDisplayMode(AIS_WireFrame);
        }

        view_->context->Display(displayed, false);
        view_->aisToNodeId[displayed.get()] = node.id;
        view_->nodeIdToAis[node.id] = displayed;
        ++shown;
    }

    // Render defined weld seams in 3D
    for (const auto& seam : doc_.seams) {
        if (seam.sampledFrames.size() >= 2) {
            BRepBuilderAPI_MakePolygon poly;
            for (const auto& sf : seam.sampledFrames) {
                poly.Add(gp_Pnt(sf.position.x, sf.position.y, sf.position.z));
            }
            if (poly.IsDone()) {
                Handle(AIS_Shape) seamAis = new AIS_Shape(poly.Shape());
                seamAis->SetColor(Quantity_NOC_CYAN);
                seamAis->SetWidth(3.0);
                view_->context->Display(seamAis, false);
                view_->seamAisObjects.push_back(seamAis);
            }
        }
    }

    updateSelectionModes();
    if (!activeTargetId_.empty()) {
        setupTargetGizmoPresentation();
        syncGizmoPosition();
    }
    if (lastPickedEdge_.has_value()) {
        selectEdge(lastPickedEdge_->cadHandleId, lastPickedEdge_->edgeIndex);
    }
    view_->context->UpdateCurrentViewer();

    if (!fitted_ && shown > 0) {
        fitAll();
        if (!view_->view.IsNull()) {
            view_->view->SetProj(V3d_TypeOfOrientation_Zup_AxoRight);
            fitAll();
        }
    } else if (!view_->view.IsNull()) {
        view_->view->Redraw();
    }
#else
    (void)doc_;
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

void OcctViewport::ensureView() {
#ifdef OLP_HAS_QT6
#ifdef OLP_HAS_OCCT
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
        return;
    }
    if (view_ == nullptr || !view_->view.IsNull()) {
        return;
    }
    if (winId() == 0) {
        return;
    }
    try {
        Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
        Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display);
        view_->viewer = new V3d_Viewer(driver);
        view_->viewer->SetDefaultLights();
        view_->viewer->SetLightOn();
        view_->context = new AIS_InteractiveContext(view_->viewer);
        view_->view = view_->viewer->CreateView();
#ifdef _WIN32
        view_->renderHwnd = createOcctRenderWindow(reinterpret_cast<HWND>(winId()));
        const Aspect_Handle targetHandle = reinterpret_cast<Aspect_Handle>(
            view_->renderHwnd != nullptr ? view_->renderHwnd : reinterpret_cast<HWND>(winId()));
#else
        const Aspect_Handle targetHandle = reinterpret_cast<Aspect_Handle>(winId());
#endif
        Handle(WNT_Window) window = new WNT_Window(targetHandle);
        view_->view->SetWindow(window);
        if (!window->IsMapped()) {
            window->Map();
        }
#ifdef _WIN32
        if (view_->renderHwnd != nullptr) {
            SetWindowLongPtrW(
                view_->renderHwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(view_->view.get()));
        }
#endif
        view_->view->SetBackgroundColor(Quantity_Color(30.0 / 255.0, 34.0 / 255.0, 38.0 / 255.0, Quantity_TOC_RGB));
        view_->view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_WHITE, 0.08, V3d_WIREFRAME);
        view_->view->MustBeResized();
        view_->view->SetProj(V3d_TypeOfOrientation_Zup_AxoRight);
        syncScene();
        view_->view->Redraw();
    } catch (const Standard_Failure&) {
#ifdef _WIN32
        if (view_->renderHwnd != nullptr) {
            SetWindowLongPtrW(view_->renderHwnd, GWLP_USERDATA, 0);
            DestroyWindow(view_->renderHwnd);
            view_->renderHwnd = nullptr;
        }
#endif
        view_->view.Nullify();
        view_->context.Nullify();
        view_->viewer.Nullify();
    }
#endif
#endif
}

#ifdef OLP_HAS_QT6
void OcctViewport::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureView();
}

void OcctViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
#ifdef OLP_HAS_OCCT
    if (view_ && !view_->view.IsNull()) {
#ifdef _WIN32
        syncOcctRenderWindowSize(reinterpret_cast<HWND>(winId()), view_->renderHwnd);
#endif
        view_->view->MustBeResized();
        view_->view->Redraw();
    }
#endif
}

void OcctViewport::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 34, 38));
#ifdef OLP_HAS_OCCT
    if (view_ && !view_->view.IsNull()) {
        view_->view->Redraw();
    }
#endif
    (void)event;
}

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

void OcctViewport::wheelEvent(QWheelEvent* event) {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->view.IsNull()) {
        return;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }
    if (delta == 0) {
        return;
    }
    const Handle(Graphic3d_Camera) camera = view_->view->Camera();
    const double scale = camera->Scale();
    const double notches = std::clamp(std::abs(delta) / 120.0, 1.0, 6.0);
    const double factor = delta > 0 ? std::pow(1.0 / 1.12, notches) : std::pow(1.12, notches);
    camera->SetScale(std::clamp(scale * factor, 1.0, 1.0e7));
    view_->view->ZFitAll();
    view_->view->Redraw();
    event->accept();
#else
    (void)event;
#endif
}
#endif

} // namespace olp::ui
