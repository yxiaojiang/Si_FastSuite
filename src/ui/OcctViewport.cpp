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
#include "olp/algo/OpwKinematics.h"
#include "olp/algo/FclCollisionQueries.h"
#include "olp/device/RobotModel.h"

#ifdef OLP_HAS_QT6
#include <QEnterEvent>
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
#include <gp_Ax3.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#endif

namespace olp::ui {


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
    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
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
    setCursor(Qt::ArrowCursor);
    setMinimumSize(160, 120);
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
        if (node.type == core::SceneNodeType::Robot) {
            bounds.Add(gp_Pnt(node.localTransform.pos.x - 700.0, node.localTransform.pos.y - 700.0, node.localTransform.pos.z));
            bounds.Add(gp_Pnt(node.localTransform.pos.x + 700.0, node.localTransform.pos.y + 700.0, node.localTransform.pos.z + 1200.0));
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


void OcctViewport::syncScene() {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr) {
        return;
    }

    view_->activeRobotNodeId.clear();
    for (auto& linkAis : view_->robotLinkAis) {
        linkAis.Nullify();
    }

    auto* occtKernel = dynamic_cast<cad::OcctCadKernel*>(cadKernel_);
    cad::OcctCadKernel localCad;
    cad::OcctCadKernel* loader = (occtKernel != nullptr) ? occtKernel : &localCad;

    for (const auto& node : doc_.sceneNodes) {
        if (!node.visible || node.collisionRole == core::CollisionRole::Ignored) {
            continue;
        }

        if (node.type == core::SceneNodeType::Robot) {
            view_->activeRobotNodeId = node.id;
            view_->sceneRobotLinkTransforms = algo::forwardRobotLinkWorldFrames(node.currentJointsRad, node.localTransform);
            view_->currentRobotLinkTransforms = view_->sceneRobotLinkTransforms;

            const auto robotModel = device::RobotModel::createFanucM10iA();

            static std::array<TopoDS_Shape, 7> s_robotLinkFaces;
            static bool s_robotLinksLoaded = false;
            if (!s_robotLinksLoaded) {
                bool allOk = true;
                for (size_t k = 0; k < 7; ++k) {
                    core::TriMesh mesh;
                    if (loader->importRobotStlMillimeters(robotModel.visualMeshFiles[k], mesh) && !mesh.triangles.empty()) {
                        const int nbNodes = static_cast<int>(mesh.vertices.size());
                        const int nbTri = static_cast<int>(mesh.triangles.size());
                        Handle(Poly_Triangulation) tri = new Poly_Triangulation(nbNodes, nbTri, false);
                        for (int i = 0; i < nbNodes; ++i) {
                            tri->SetNode(i + 1, gp_Pnt(mesh.vertices[i].x, mesh.vertices[i].y, mesh.vertices[i].z));
                        }
                        for (int i = 0; i < nbTri; ++i) {
                            tri->SetTriangle(i + 1, Poly_Triangle(
                                static_cast<int>(mesh.triangles[i][0] + 1),
                                static_cast<int>(mesh.triangles[i][1] + 1),
                                static_cast<int>(mesh.triangles[i][2] + 1)));
                        }
                        TopoDS_Face face;
                        BRep_Builder bb;
                        bb.MakeFace(face);
                        bb.UpdateFace(face, tri);
                        s_robotLinkFaces[k] = face;
                    } else {
                        allOk = false;
                    }
                }
                s_robotLinksLoaded = allOk;
            }

            if (s_robotLinksLoaded) {
                for (size_t k = 0; k < 7; ++k) {
                    Handle(AIS_Shape) linkAis = new AIS_Shape(s_robotLinkFaces[k]);
                    if (displayMode_ == ViewportDisplayMode::ShadedWithEdges) {
                        linkAis->SetDisplayMode(AIS_Shaded);
                        linkAis->Attributes()->SetFaceBoundaryDraw(true);
                        linkAis->Attributes()->SetupOwnFaceBoundaryAspect();
                        linkAis->Attributes()->FaceBoundaryAspect()->SetColor(Quantity_NOC_BLACK);
                        linkAis->Attributes()->FaceBoundaryAspect()->SetWidth(1.0);
                    } else if (displayMode_ == ViewportDisplayMode::Shaded) {
                        linkAis->SetDisplayMode(AIS_Shaded);
                        linkAis->Attributes()->SetFaceBoundaryDraw(false);
                    } else {
                        linkAis->SetDisplayMode(AIS_WireFrame);
                    }
                    linkAis->SetColor(Quantity_NOC_GOLDENROD);
                    linkAis->SetLocalTransformation(toGpTrsf(view_->sceneRobotLinkTransforms[k]));
                    view_->robotLinkAis[k] = linkAis;
                }
            }
            break;
        }
    }

    if (view_->context.IsNull()) {
        return;
    }
    view_->context->RemoveAll(false);
    view_->aisToNodeId.clear();
    view_->nodeIdToAis.clear();
    view_->seamAisObjects.clear();
    view_->highlightEdgeAis.Nullify();
    view_->manipulator.Nullify();
    view_->targetGizmoProxyAis.Nullify();

    int shown = 0;
    for (size_t k = 0; k < 7; ++k) {
        if (!view_->robotLinkAis[k].IsNull()) {
            view_->context->Display(view_->robotLinkAis[k], false);
            view_->aisToNodeId[view_->robotLinkAis[k].get()] = view_->activeRobotNodeId;
            view_->nodeIdToAis[view_->activeRobotNodeId + "_link_" + std::to_string(k)] = view_->robotLinkAis[k];
            ++shown;
        }
    }

    for (const auto& node : doc_.sceneNodes) {
        if (!node.visible || node.collisionRole == core::CollisionRole::Ignored) {
            continue;
        }

        if (node.type == core::SceneNodeType::Robot) {
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

void OcctViewport::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    setCursor(Qt::ArrowCursor);
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
    const qreal dpr = devicePixelRatioF();
    const int pixelX = static_cast<int>(std::lround(event->position().x() * dpr));
    const int pixelY = static_cast<int>(std::lround(event->position().y() * dpr));
    const Handle(Graphic3d_Camera) camera = view_->view->Camera();
    const double notches = std::clamp(std::abs(delta) / 120.0, 1.0, 6.0);

    const double factor = delta > 0 ? std::pow(1.12, notches) : std::pow(1.0 / 1.12, notches);

    double zoomAtPointXv = 0.0;
    double zoomAtPointYv = 0.0;
    view_->view->Convert(pixelX, pixelY, zoomAtPointXv, zoomAtPointYv);

    const double dxv = zoomAtPointXv / factor;
    const double dyv = zoomAtPointYv / factor;

    camera->SetScale(std::clamp(camera->Scale() / factor, 1.0, 1.0e7));

    const gp_Dir& dir = camera->Direction();
    const gp_Ax3 cameraCS(camera->Center(), dir.Reversed(), dir ^ camera->Up());
    const double dx = zoomAtPointXv - dxv;
    const double dy = zoomAtPointYv - dyv;
    const gp_Vec cameraPan =
        gp_Vec(cameraCS.XDirection()) * dx + gp_Vec(cameraCS.YDirection()) * dy;
    gp_Trsf panTrsf;
    panTrsf.SetTranslation(cameraPan);
    camera->Transform(panTrsf);



    view_->view->ZFitAll();
    view_->view->Redraw();
    event->accept();
#else
    (void)event;
#endif
}
#endif

} // namespace olp::ui
