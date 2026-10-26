#include "olp/ui/OcctViewport.h"

#ifdef OLP_HAS_QT6
#include <QFocusEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QTimer>
#include <QWheelEvent>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>

#ifdef OLP_HAS_OCCT
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <WNT_Window.hxx>
#include <gp_Pnt.hxx>
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
#endif
#ifdef OLP_HAS_QT6
    bool rotating{false};
    bool panning{false};
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
    : QWidget(parent), doc_(doc), tcpGizmo_(doc, cmdMgr), view_(std::make_unique<ViewState>()) {
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
#if defined(OLP_HAS_OCCT) && defined(_WIN32)
    if (view_ != nullptr) {
        if (view_->renderHwnd != nullptr) {
            SetWindowLongPtrW(view_->renderHwnd, GWLP_USERDATA, 0);
        }
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
OcctViewport::OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr)
    : doc_(doc), tcpGizmo_(doc, cmdMgr), view_(std::make_unique<ViewState>()) {}

OcctViewport::~OcctViewport() = default;
#endif

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

void OcctViewport::syncScene() {
#ifdef OLP_HAS_OCCT
    if (view_ == nullptr || view_->context.IsNull()) {
        return;
    }
    view_->context->RemoveAll(false);
    int shown = 0;
    for (const auto& node : doc_.sceneNodes) {
        if (!node.visible || node.collisionRole == core::CollisionRole::Ignored) {
            continue;
        }
        const core::Vec3& lo = node.renderMesh.bboxMin;
        const core::Vec3& hi = node.renderMesh.bboxMax;
        if ((hi - lo).norm() < 1.0) {
            continue;
        }
        const gp_Pnt cornerMin(std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z));
        const gp_Pnt cornerMax(std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z));
        const TopoDS_Shape box = BRepPrimAPI_MakeBox(cornerMin, cornerMax).Shape();
        Handle(AIS_Shape) displayed = new AIS_Shape(box);
        Quantity_NameOfColor color = Quantity_NOC_GRAY;
        if (node.collisionRole == core::CollisionRole::Workpiece) {
            color = Quantity_NOC_STEELBLUE;
        } else if (node.collisionRole == core::CollisionRole::Fixture) {
            color = Quantity_NOC_ORANGE;
        } else if (node.type == core::SceneNodeType::Robot) {
            color = Quantity_NOC_GOLDENROD;
        }
        displayed->SetColor(color);
        displayed->SetTransparency(0.15);
        view_->context->Display(displayed, false);
        view_->context->SetDisplayMode(displayed, 1, false);
        ++shown;
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
        view_->view->StartRotation(x, y);
        view_->rotating = true;
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
    if (view_->rotating) {
        view_->view->Rotation(pos.x(), pos.y());
    } else if (view_->panning) {
        view_->view->Pan(pos.x() - view_->lastPos.x(), view_->lastPos.y() - pos.y());
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
            view_->rotating = false;
        } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
            view_->panning = false;
        }
    }
#else
    (void)event;
#endif
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
