#include "olp/ui/ShellWindow.h"

#include "olp/ui/OcctViewport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QMouseEvent>

#include <cmath>
#include <set>
#include <sstream>

namespace olp::ui {
namespace {

enum class IconKind {
    New, Open, Save, Import, Sample,
    Undo, Redo, Copy, Paste, Delete,
    Layout, Program, Fit,
    Pick, Path, Sim, Export
};

QIcon commandIcon(IconKind kind, const QColor& ink) {
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    switch (kind) {
    case IconKind::New:
        painter.drawRoundedRect(8, 6, 14, 18, 2, 2);
        painter.drawLine(12, 14, 18, 14);
        painter.drawLine(15, 11, 15, 17);
        break;
    case IconKind::Open:
        painter.drawRect(7, 12, 18, 12);
        painter.drawLine(7, 12, 12, 8);
        painter.drawLine(12, 8, 20, 8);
        painter.drawLine(20, 8, 20, 12);
        break;
    case IconKind::Save:
        painter.drawRoundedRect(8, 6, 16, 18, 2, 2);
        painter.drawRect(12, 6, 8, 6);
        painter.drawLine(11, 18, 21, 18);
        break;
    case IconKind::Import:
        painter.drawLine(16, 6, 16, 18);
        painter.drawLine(11, 14, 16, 19);
        painter.drawLine(21, 14, 16, 19);
        painter.drawLine(8, 24, 24, 24);
        break;
    case IconKind::Sample:
        painter.drawRect(8, 10, 16, 12);
        painter.drawLine(8, 10, 16, 6);
        painter.drawLine(24, 10, 16, 6);
        break;
    case IconKind::Undo:
        painter.drawArc(10, 8, 12, 12, 30 * 16, 180 * 16);
        painter.drawLine(10, 14, 6, 11);
        painter.drawLine(10, 14, 8, 18);
        break;
    case IconKind::Redo:
        painter.drawArc(10, 8, 12, 12, 0, 180 * 16);
        painter.drawLine(22, 14, 26, 11);
        painter.drawLine(22, 14, 24, 18);
        break;
    case IconKind::Copy:
        painter.drawRect(12, 8, 12, 14);
        painter.drawRect(8, 12, 12, 14);
        break;
    case IconKind::Paste:
        painter.drawRoundedRect(9, 8, 14, 16, 2, 2);
        painter.drawLine(12, 8, 12, 6);
        painter.drawLine(12, 6, 20, 6);
        painter.drawLine(20, 6, 20, 8);
        break;
    case IconKind::Delete:
        painter.drawLine(10, 11, 22, 11);
        painter.drawRect(11, 11, 10, 12);
        painter.drawLine(14, 8, 18, 8);
        break;
    case IconKind::Layout:
        painter.drawRect(7, 7, 8, 8);
        painter.drawRect(17, 7, 8, 8);
        painter.drawRect(7, 17, 8, 8);
        painter.drawRect(17, 17, 8, 8);
        break;
    case IconKind::Program:
        painter.drawEllipse(QPointF(9, 10), 2.5, 2.5);
        painter.drawEllipse(QPointF(18, 16), 2.5, 2.5);
        painter.drawEllipse(QPointF(24, 10), 2.5, 2.5);
        painter.drawLine(11, 11, 16, 15);
        painter.drawLine(20, 15, 22, 11);
        break;
    case IconKind::Fit:
        painter.drawLine(7, 12, 7, 7);
        painter.drawLine(7, 7, 12, 7);
        painter.drawLine(20, 7, 25, 7);
        painter.drawLine(25, 7, 25, 12);
        painter.drawLine(25, 20, 25, 25);
        painter.drawLine(25, 25, 20, 25);
        painter.drawLine(12, 25, 7, 25);
        painter.drawLine(7, 25, 7, 20);
        break;
    case IconKind::Pick:
        painter.drawLine(10, 22, 16, 10);
        painter.drawLine(16, 10, 22, 16);
        painter.drawLine(14, 16, 18, 20);
        break;
    case IconKind::Path:
        painter.drawLine(7, 22, 14, 12);
        painter.drawLine(14, 12, 20, 18);
        painter.drawLine(20, 18, 26, 8);
        break;
    case IconKind::Sim:
        painter.drawPolygon(QPolygonF({QPointF(12, 8), QPointF(24, 16), QPointF(12, 24)}));
        break;
    case IconKind::Export:
        painter.drawLine(16, 20, 16, 8);
        painter.drawLine(11, 12, 16, 7);
        painter.drawLine(21, 12, 16, 7);
        painter.drawLine(8, 24, 24, 24);
        break;
    }
    return QIcon(pix);
}

QAction* disabledAction(QWidget* owner, const QString& text, const QString& tip, IconKind kind, const QColor& ink) {
    auto* action = new QAction(commandIcon(kind, ink), text, owner);
    action->setToolTip(tip);
    action->setStatusTip(tip);
    action->setEnabled(false);
    return action;
}

QWidget* commandGroup(QWidget* parent, const QString& title, const QList<QAction*>& actions) {
    auto* group = new QWidget(parent);
    auto* column = new QVBoxLayout(group);
    column->setContentsMargins(8, 4, 8, 2);
    column->setSpacing(1);
    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    for (auto* action : actions) {
        auto* button = new QToolButton(group);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(24, 24));
        button->setAutoRaise(true);
        button->setMinimumWidth(52);
        row->addWidget(button);
    }
    auto* caption = new QLabel(title, group);
    caption->setAlignment(Qt::AlignCenter);
    caption->setStyleSheet(QString::fromUtf8("color: #5d6c78; font-size: 11px;"));
    column->addLayout(row);
    column->addWidget(caption);
    return group;
}

QWidget* groupDivider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QString::fromUtf8("color: #c5d0d8;"));
    line->setFixedWidth(8);
    return line;
}

QString roleText(core::CollisionRole role) {
    switch (role) {
    case core::CollisionRole::Workpiece: return QString::fromUtf8("工件");
    case core::CollisionRole::Fixture: return QString::fromUtf8("夹具");
    case core::CollisionRole::Environment: return QString::fromUtf8("环境");
    case core::CollisionRole::RobotLink: return QString::fromUtf8("机器人连杆");
    case core::CollisionRole::WeldingTorch: return QString::fromUtf8("焊枪");
    case core::CollisionRole::Ignored: return QString::fromUtf8("忽略");
    }
    return QString::fromUtf8("其他");
}

QString typeText(core::SceneNodeType type) {
    switch (type) {
    case core::SceneNodeType::Group: return QString::fromUtf8("组");
    case core::SceneNodeType::Part: return QString::fromUtf8("零件");
    case core::SceneNodeType::Robot: return QString::fromUtf8("机器人");
    case core::SceneNodeType::Tool: return QString::fromUtf8("工具");
    case core::SceneNodeType::Frame: return QString::fromUtf8("坐标系");
    }
    return QString::fromUtf8("对象");
}

QString motionText(core::MotionType motion) {
    switch (motion) {
    case core::MotionType::J: return "J";
    case core::MotionType::L: return "L";
    case core::MotionType::C: return "C";
    }
    return "?";
}

QString statusText(core::TargetValidationState state) {
    switch (state) {
    case core::TargetValidationState::Unchecked: return QString::fromUtf8("未检查");
    case core::TargetValidationState::Valid: return QString::fromUtf8("通过");
    case core::TargetValidationState::Unreachable: return QString::fromUtf8("不可达");
    case core::TargetValidationState::JointLimitExceeded: return QString::fromUtf8("超限位");
    case core::TargetValidationState::NearJointLimit: return QString::fromUtf8("靠近限位");
    case core::TargetValidationState::Singularity: return QString::fromUtf8("奇异");
    case core::TargetValidationState::ConfigJump: return QString::fromUtf8("构型跳变");
    case core::TargetValidationState::Collision: return QString::fromUtf8("碰撞");
    case core::TargetValidationState::NearCollision: return QString::fromUtf8("间隙不足");
    }
    return QString::fromUtf8("未知");
}

QDockWidget* makeDock(QMainWindow* owner, const QString& title, QWidget* body) {
    auto* dock = new QDockWidget(title, owner);
    dock->setObjectName(title);
    dock->setWidget(body);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    return dock;
}

const core::Target* findTargetById(const app::ProjectDocument& doc, const std::string& id) {
    for (const auto& cu : doc.controllers) {
        for (const auto& program : cu.programs) {
            for (const auto& op : program.operations) {
                for (const auto& target : op.targets) {
                    if (target.id == id) {
                        return &target;
                    }
                }
            }
        }
    }
    return nullptr;
}

} // namespace

QString formatSpeedCellText(const core::Target& target) {
    if (target.useWeldSpeed) {
        return QString("WELD_SPEED (%1 mm/s)").arg(target.speedValue, 0, 'f', 0);
    }
    if (target.motion == core::MotionType::J) {
        return QString("%1%").arg(target.speedValue, 0, 'f', 0);
    }
    return QString("%1 mm/s").arg(target.speedValue, 0, 'f', 0);
}

ParsedSpeedCell parseSpeedCellText(const QString& rawText, double fallbackSpeed) {
    ParsedSpeedCell res;
    const QString text = rawText.trimmed();
    if (text.isEmpty()) {
        return res;
    }

    const bool hasWeld = text.contains("weld", Qt::CaseInsensitive);
    const bool isCmMin = text.contains("cm/min", Qt::CaseInsensitive);

    QString clean = text;
    clean = clean.remove("WELD_SPEED", Qt::CaseInsensitive)
                 .remove("cm/min", Qt::CaseInsensitive)
                 .remove("mm/s", Qt::CaseInsensitive)
                 .remove("%")
                 .remove("(")
                 .remove(")")
                 .remove("[")
                 .remove("]")
                 .trimmed();

    bool ok = false;
    double v = clean.toDouble(&ok);
    if (!ok && clean.isEmpty() && hasWeld) {
        res.useWeldSpeed = true;
        res.speedValue = (fallbackSpeed > 0.0) ? fallbackSpeed : 10.0;
        res.valid = true;
        return res;
    }

    if (ok && v > 0.0) {
        if (isCmMin) {
            v = (v * 10.0) / 60.0; // cm/min -> mm/s
        }
        res.speedValue = v;
        res.useWeldSpeed = hasWeld;
        res.valid = true;
        return res;
    }

    return res;
}

TwistScanWidget::TwistScanWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 110);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void TwistScanWidget::setSamples(const std::vector<algo::TwistScanSample>& samples, double currentAngleDeg) {
    samples_ = samples;
    currentAngleDeg_ = currentAngleDeg;
    hoveredIndex_ = -1;
    update();
}

std::optional<double> TwistScanWidget::findOptimalFeasibleAngleDeg() const {
    if (samples_.empty()) return std::nullopt;

    const size_t n = samples_.size();
    auto isFeasible = [](const algo::TwistScanSample& s) {
        return s.reachable && s.collisionFree && !s.nearSingularityOrLimit && s.minClearanceMm >= 5.0;
    };

    size_t bestStart = 0;
    size_t bestLen = 0;
    size_t curStart = 0;
    size_t curLen = 0;

    for (size_t i = 0; i < 2 * n; ++i) {
        if (isFeasible(samples_[i % n])) {
            if (curLen == 0) curStart = i;
            ++curLen;
            if (curLen > bestLen) {
                bestLen = curLen;
                bestStart = curStart;
            }
            if (curLen >= n) break;
        } else {
            curLen = 0;
        }
    }

    if (bestLen == 0) {
        for (size_t i = 0; i < 2 * n; ++i) {
            if (samples_[i % n].reachable && samples_[i % n].collisionFree) {
                if (curLen == 0) curStart = i;
                ++curLen;
                if (curLen > bestLen) {
                    bestLen = curLen;
                    bestStart = curStart;
                }
                if (curLen >= n) break;
            } else {
                curLen = 0;
            }
        }
    }

    if (bestLen == 0) return std::nullopt;

    const size_t midIdx = (bestStart + bestLen / 2) % n;
    return samples_[midIdx].twistAngleDeg;
}

void TwistScanWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();
    const int cx = w / 2;
    const int cy = h / 2;
    const int rOut = std::min(cx, cy) - 6;
    const int rIn = std::max(10, rOut - 16);

    if (samples_.empty()) {
        painter.setPen(QPen(QColor(100, 100, 100, 140), 1.0, Qt::DashLine));
        painter.setBrush(QColor(40, 44, 52, 120));
        painter.drawEllipse(QPointF(cx, cy), rOut, rOut);
        painter.setBrush(QColor(30, 33, 39));
        painter.drawEllipse(QPointF(cx, cy), rIn, rIn);
        painter.setPen(QColor(180, 180, 180));
        painter.setFont(QFont("Segoe UI", 9));
        painter.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("选择路径点以扫描自转角"));
        return;
    }

    const size_t n = samples_.size();
    const double stepDeg = 360.0 / static_cast<double>(n);

    for (size_t i = 0; i < n; ++i) {
        const auto& s = samples_[i];
        QColor color;
        if (!s.reachable || !s.collisionFree) {
            color = QColor(225, 55, 55); // Red
        } else if (s.nearSingularityOrLimit || s.minClearanceMm < 5.0) {
            color = QColor(235, 185, 25); // Yellow
        } else {
            color = QColor(45, 195, 80); // Green
        }

        if (static_cast<int>(i) == hoveredIndex_) {
            color = color.lighter(130);
        }

        const double a1 = core::deg2rad(s.twistAngleDeg);
        const double a2 = core::deg2rad(s.twistAngleDeg + stepDeg);

        QPolygonF sectorPoly;
        sectorPoly << QPointF(cx + rIn * std::cos(a1), cy - rIn * std::sin(a1))
                   << QPointF(cx + rOut * std::cos(a1), cy - rOut * std::sin(a1))
                   << QPointF(cx + rOut * std::cos(a2), cy - rOut * std::sin(a2))
                   << QPointF(cx + rIn * std::cos(a2), cy - rIn * std::sin(a2));

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(sectorPoly);
    }

    // Inner background circle
    painter.setPen(QPen(QColor(30, 33, 39), 1.5));
    painter.setBrush(QColor(36, 40, 48));
    painter.drawEllipse(QPointF(cx, cy), rIn, rIn);

    // Current angle needle / pointer
    const double curRad = core::deg2rad(currentAngleDeg_);
    const QPointF needleTip(cx + (rOut + 2) * std::cos(curRad), cy - (rOut + 2) * std::sin(curRad));
    const QPointF needleBase(cx + (rIn - 4) * std::cos(curRad), cy - (rIn - 4) * std::sin(curRad));
    painter.setPen(QPen(QColor(255, 255, 255), 2.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(needleBase, needleTip);

    // Center text
    painter.setPen(QColor(240, 240, 240));
    painter.setFont(QFont("Segoe UI", 9, QFont::Bold));
    painter.drawText(QRect(cx - rIn, cy - rIn, 2 * rIn, 2 * rIn), Qt::AlignCenter,
                     QString("%1°").arg(currentAngleDeg_, 0, 'f', 0));
}

void TwistScanWidget::mousePressEvent(QMouseEvent* event) {
    if (samples_.empty()) return;
    const double dx = event->pos().x() - (width() / 2);
    const double dy = (height() / 2) - event->pos().y();
    if (std::hypot(dx, dy) < 8.0) return;

    double deg = core::rad2deg(std::atan2(dy, dx));
    deg = std::round(deg / 5.0) * 5.0;
    if (deg > 180.0) deg -= 360.0;
    if (deg <= -180.0) deg += 360.0;
    emit angleSelected(deg);
}

void TwistScanWidget::mouseMoveEvent(QMouseEvent* event) {
    if (samples_.empty()) return;
    const double dx = event->pos().x() - (width() / 2);
    const double dy = (height() / 2) - event->pos().y();
    const double r = std::hypot(dx, dy);
    const int rOut = std::min(width(), height()) / 2 - 6;
    const int rIn = std::max(10, rOut - 16);

    if (r >= rIn - 4 && r <= rOut + 4) {
        double deg = core::rad2deg(std::atan2(dy, dx));
        if (deg > 180.0) deg -= 360.0;
        if (deg < -180.0) deg += 360.0;

        int closest = 0;
        double minDiff = 1e9;
        for (size_t i = 0; i < samples_.size(); ++i) {
            const double diff = std::abs(core::normalizeAngleRad(core::deg2rad(samples_[i].twistAngleDeg - deg)));
            if (diff < minDiff) {
                minDiff = diff;
                closest = static_cast<int>(i);
            }
        }
        hoveredIndex_ = closest;
        const auto& s = samples_[static_cast<size_t>(closest)];
        QString statusDesc = QString::fromUtf8("可行安全");
        if (!s.reachable) statusDesc = QString::fromUtf8("不可达 (超出工作空间)");
        else if (!s.collisionFree) statusDesc = QString::fromUtf8("碰撞干涉");
        else if (s.nearSingularityOrLimit) statusDesc = QString::fromUtf8("警示 (接近奇异或关节限位)");
        else if (s.minClearanceMm < 5.0) statusDesc = QString::fromUtf8("间隙偏小 (< 5mm)");

        setToolTip(QString::fromUtf8("自转角: %1°\n评估: %2\n最小间隙: %3 mm")
            .arg(s.twistAngleDeg, 0, 'f', 1)
            .arg(statusDesc)
            .arg(s.minClearanceMm, 0, 'f', 1));
        update();
    } else {
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            update();
        }
    }
}

ShellWindow::ShellWindow(MainWindow& controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    setWindowTitle(QString::fromUtf8("SiFastSuite OLP — 焊接离线编程"));
    resize(1440, 900);
    buildChrome();
    documentListenerId_ = controller_.commandManager().addDocumentChangedListener([this] {
        if (inItemChanged_) {
            QTimer::singleShot(0, this, [this] { refreshFromDocument(); });
            return;
        }
        refreshFromDocument();
    });
    selectionListenerId_ = controller_.addSelectionListener([this] { applySelection(); });
    applyWorkbench(ActiveWorkbench::LayoutWorkbench);
    refreshFromDocument();
    statusBar()->showMessage(QString::fromUtf8("布局工作台。场景、属性和点动保持打开。用工具栏加载示例工位。"));
}

ShellWindow::~ShellWindow() {
    if (documentListenerId_ >= 0) {
        controller_.commandManager().removeDocumentChangedListener(documentListenerId_);
    }
    if (selectionListenerId_ >= 0) {
        controller_.removeSelectionListener(selectionListenerId_);
    }
}

void ShellWindow::setSampleLoader(std::function<void()> loader) {
    loader_ = std::move(loader);
}

QStringList ShellWindow::dockTitles() const {
    QStringList titles;
    const auto docks = findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* dock : docks) {
        titles.push_back(dock->windowTitle());
    }
    return titles;
}

bool ShellWindow::zoneChecked(const QString& title) const {
    const auto docks = findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* dock : docks) {
        if (dock->windowTitle() == title) {
            return dock->toggleViewAction()->isChecked();
        }
    }
    return false;
}

void ShellWindow::buildChrome() {
    setStyleSheet(QString::fromUtf8(
        "QMainWindow { background: #d5dde4; }"
        "QMenuBar { background: #243746; color: #f2f5f7; }"
        "QMenuBar::item:selected { background: #3d5d78; }"
        "QMenu { background: #f7f9fb; color: #1c2830; }"
        "QToolBar { background: #f4f7fa; border: 0; border-bottom: 1px solid #d5dee6; spacing: 0; padding: 0; }"
        "QToolButton { color: #1c2830; padding: 2px 4px; border: 1px solid transparent; border-radius: 4px; background: transparent; }"
        "QToolButton:hover { background: #e5f1f7; }"
        "QToolButton:checked { background: #d5edf6; color: #143644; border-color: #1f6f8b; }"
        "QToolButton:disabled { color: #93a1ab; }"
        "QDockWidget::title { background: #2c4558; color: white; padding: 6px; }"
        "QStatusBar { background: #243746; color: #f2f5f7; }"
        "QTreeWidget, QTableWidget, QListWidget, QPlainTextEdit { background: #fbfcfd; color: #1c2830; }"
        "QHeaderView::section { background: #e4ebf1; padding: 4px; }"));

    const QColor fileInk(31, 95, 148);
    const QColor editInk(47, 79, 99);
    const QColor benchInk(20, 122, 138);
    const QColor viewInk(36, 122, 74);
    const QColor pathInk(176, 96, 32);
    const QString later = QString::fromUtf8("后续接入，现在还不能用。");

    auto* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    auto* newAction = disabledAction(this, QString::fromUtf8("新建"), later, IconKind::New, fileInk);
    auto* openAction = disabledAction(this, QString::fromUtf8("打开"), later, IconKind::Open, fileInk);
    auto* saveAction = disabledAction(this, QString::fromUtf8("保存"), later, IconKind::Save, fileInk);
    auto* importAction = disabledAction(this, QString::fromUtf8("导入"), later, IconKind::Import, fileInk);
    auto* loadAction = fileMenu->addAction(commandIcon(IconKind::Sample, fileInk), QString::fromUtf8("加载示例工位"));
    fileMenu->insertAction(loadAction, newAction);
    fileMenu->insertAction(loadAction, openAction);
    fileMenu->insertAction(loadAction, saveAction);
    fileMenu->insertAction(loadAction, importAction);
    loadAction->setToolTip(QString::fromUtf8("导入示例焊接工位并生成焊缝。"));
    connect(loadAction, &QAction::triggered, this, &ShellWindow::loadSample);
    fileMenu->addSeparator();
    fileMenu->addAction(QString::fromUtf8("退出"), this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(QString::fromUtf8("编辑"));
    undoAction_ = editMenu->addAction(commandIcon(IconKind::Undo, editInk), QString::fromUtf8("撤销"));
    redoAction_ = editMenu->addAction(commandIcon(IconKind::Redo, editInk), QString::fromUtf8("重做"));
    auto* copyAction = disabledAction(this, QString::fromUtf8("复制"), later, IconKind::Copy, editInk);
    auto* pasteAction = disabledAction(this, QString::fromUtf8("粘贴"), later, IconKind::Paste, editInk);
    auto* deleteAction = disabledAction(this, QString::fromUtf8("删除"), later, IconKind::Delete, editInk);
    editMenu->addSeparator();
    editMenu->addAction(copyAction);
    editMenu->addAction(pasteAction);
    editMenu->addAction(deleteAction);
    connect(undoAction_, &QAction::triggered, this, [this] {
        controller_.commandManager().undo();
        refreshFromDocument();
    });
    connect(redoAction_, &QAction::triggered, this, [this] {
        controller_.commandManager().redo();
        refreshFromDocument();
    });

    auto* workbenchMenu = menuBar()->addMenu(QString::fromUtf8("工作台"));
    auto* workbenchGroup = new QActionGroup(this);
    layoutAction_ = workbenchMenu->addAction(commandIcon(IconKind::Layout, benchInk), QString::fromUtf8("布局工作台"));
    olpAction_ = workbenchMenu->addAction(commandIcon(IconKind::Program, benchInk), QString::fromUtf8("编程工作台"));
    layoutAction_->setIconText(QString::fromUtf8("布局"));
    olpAction_->setIconText(QString::fromUtf8("编程"));
    layoutAction_->setToolTip(QString::fromUtf8("布局工作台：看工位、装夹和坐标系。"));
    olpAction_->setToolTip(QString::fromUtf8("编程工作台：看程序、路径点和问题。"));
    layoutAction_->setCheckable(true);
    olpAction_->setCheckable(true);
    workbenchGroup->addAction(layoutAction_);
    workbenchGroup->addAction(olpAction_);
    connect(layoutAction_, &QAction::triggered, this, &ShellWindow::showLayoutWorkbench);
    connect(olpAction_, &QAction::triggered, this, &ShellWindow::showOlpWorkbench);

    auto* viewMenu = menuBar()->addMenu(QString::fromUtf8("视图"));
    menuBar()->addMenu(QString::fromUtf8("帮助"))->addAction(
        QString::fromUtf8("关于"), this, [this] {
            statusBar()->showMessage(QString::fromUtf8(
                "SiFastSuite OLP。左键旋转，中键或右键平移，滚轮向前拉近、向后拉远。"));
        });

    auto* fitAction = new QAction(commandIcon(IconKind::Fit, viewInk), QString::fromUtf8("整个工作台"), this);
    fitAction->setIconText(QString::fromUtf8("全景"));
    fitAction->setToolTip(QString::fromUtf8("把整个工作台收进视口。"));
    connect(fitAction, &QAction::triggered, this, [this] {
        if (viewport_ != nullptr) {
            viewport_->fitAll();
        }
    });
    viewMenu->addAction(fitAction);
    auto* pickAction = new QAction(commandIcon(IconKind::Pick, pathInk), QString::fromUtf8("拾取"), this);
    pickAction->setIconText(QString::fromUtf8("拾取"));
    pickAction->setCheckable(true);
    pickAction->setChecked(true);
    auto* pathAction = new QAction(commandIcon(IconKind::Path, pathInk), QString::fromUtf8("生成路径"), this);
    pathAction->setIconText(QString::fromUtf8("路径"));
    pathAction->setToolTip(QString::fromUtf8("由选中焊缝自动生成操作路径。"));
    connect(pathAction, &QAction::triggered, this, [this] {
        if (controller_.generateOperationFromSelectedSeam()) {
            statusBar()->showMessage(QString::fromUtf8("已由焊缝生成焊接操作路径。"), 3000);
            refreshFromDocument();
        } else {
            statusBar()->showMessage(QString::fromUtf8("未能生成路径：未找到焊缝定义。"), 3000);
        }
    });

    auto* slerpAction = new QAction(commandIcon(IconKind::Path, pathInk), QString::fromUtf8("姿态平滑"), this);
    slerpAction->setIconText(QString::fromUtf8("平滑"));
    slerpAction->setToolTip(QString::fromUtf8("对选中操作路径多点姿态执行 SLERP 球形线性插值平滑。"));
    connect(slerpAction, &QAction::triggered, this, [this] {
        const auto selectedIds = selectedTargetIdsFromTable();
        bool ok = false;
        if (selectedIds.size() >= 2) {
            ok = controller_.smoothTargetOrientations(selectedIds.front(), selectedIds.back());
        } else {
            ok = controller_.smoothTargetOrientations();
        }

        if (ok) {
            if (selectedIds.size() >= 2) {
                statusBar()->showMessage(
                    QString::fromUtf8("已完成局部区间 [%1 -> %2] 路径姿态 SLERP 平滑插值。")
                        .arg(QString::fromStdString(selectedIds.front()))
                        .arg(QString::fromStdString(selectedIds.back())), 3000);
            } else {
                statusBar()->showMessage(QString::fromUtf8("已完成路径姿态 SLERP 平滑插值。"), 3000);
            }
            refreshFromDocument();
        } else {
            statusBar()->showMessage(QString::fromUtf8("未能执行姿态平滑：没有有效操作或中间点（无姿态变化）。"), 3000);
        }
    });

    auto* simAction = disabledAction(this, QString::fromUtf8("仿真"), QString::fromUtf8("播放机器人轨迹。") + later, IconKind::Sim, pathInk);
    auto* exportAction = disabledAction(this, QString::fromUtf8("导出"), QString::fromUtf8("导出 FANUC 程序。") + later, IconKind::Export, pathInk);

    auto* prevTargetAction = new QAction(QString::fromUtf8("上一目标点"), this);
    prevTargetAction->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    prevTargetAction->setToolTip(QString::fromUtf8("单步切换到上一个目标点 [快捷键: '[']"));
    connect(prevTargetAction, &QAction::triggered, this, [this] {
        controller_.stepTarget(false);
    });
    addAction(prevTargetAction);

    auto* nextTargetAction = new QAction(QString::fromUtf8("下一目标点"), this);
    nextTargetAction->setShortcut(QKeySequence(Qt::Key_BracketRight));
    nextTargetAction->setToolTip(QString::fromUtf8("单步切换到下一个目标点 [快捷键: ']']"));
    connect(nextTargetAction, &QAction::triggered, this, [this] {
        controller_.stepTarget(true);
    });
    addAction(nextTargetAction);
    loadAction->setIconText(QString::fromUtf8("示例"));
    newAction->setIconText(QString::fromUtf8("新建"));
    openAction->setIconText(QString::fromUtf8("打开"));
    saveAction->setIconText(QString::fromUtf8("保存"));
    importAction->setIconText(QString::fromUtf8("导入"));
    undoAction_->setIconText(QString::fromUtf8("撤销"));
    redoAction_->setIconText(QString::fromUtf8("重做"));
    copyAction->setIconText(QString::fromUtf8("复制"));
    pasteAction->setIconText(QString::fromUtf8("粘贴"));
    deleteAction->setIconText(QString::fromUtf8("删除"));

    auto* ribbon = new QWidget();
    auto* ribbonLayout = new QHBoxLayout(ribbon);
    ribbonLayout->setContentsMargins(4, 0, 4, 0);
    ribbonLayout->setSpacing(0);
    ribbonLayout->addWidget(commandGroup(ribbon, QString::fromUtf8("文件"),
        {newAction, openAction, saveAction, importAction, loadAction}));
    ribbonLayout->addWidget(groupDivider(ribbon));
    ribbonLayout->addWidget(commandGroup(ribbon, QString::fromUtf8("编辑"),
        {undoAction_, redoAction_, copyAction, pasteAction, deleteAction}));
    ribbonLayout->addWidget(groupDivider(ribbon));
    ribbonLayout->addWidget(commandGroup(ribbon, QString::fromUtf8("工作台"),
        {layoutAction_, olpAction_}));
    ribbonLayout->addWidget(groupDivider(ribbon));
    ribbonLayout->addWidget(commandGroup(ribbon, QString::fromUtf8("编程"),
        {pickAction, pathAction, slerpAction, simAction, exportAction}));
    ribbonLayout->addWidget(groupDivider(ribbon));
    ribbonLayout->addWidget(commandGroup(ribbon, QString::fromUtf8("视图"), {fitAction}));
    ribbonLayout->addStretch(1);

    auto* toolbar = addToolBar(QString::fromUtf8("主工具栏"));
    toolbar->setMovable(false);
    toolbar->addWidget(ribbon);

    viewport_ = new OcctViewport(controller_.document(), controller_.commandManager(), &controller_.cadKernel(), this);
    viewport_->attachMainWindow(&controller_);
    setCentralWidget(viewport_);

    connect(pickAction, &QAction::toggled, this, [this](bool checked) {
        if (viewport_ != nullptr) {
            viewport_->setSelectionMode(checked ? SelectionSubShapeMode::Edge : SelectionSubShapeMode::Object);
        }
    });

    connect(viewport_, &OcctViewport::edgePicked, this, [this](const QString& nodeId, int cadHandleId, int edgeIndex) {
        controller_.selectSceneNode(nodeId.toStdString());
        statusBar()->showMessage(QString::fromUtf8("Picked edge %1: handle %2, edge %3").arg(nodeId).arg(cadHandleId).arg(edgeIndex), 5000);
    });

    connect(viewport_, &OcctViewport::sceneNodePicked, this, [this](const QString& nodeId) {
        controller_.selectSceneNode(nodeId.toStdString());
    });

    viewport_->tcpGizmo().setPreviewCallback([this](const GizmoPreviewState& preview) {
        const auto wpr = preview.previewPoseInUFrame.toWPR();
        statusBar()->showMessage(QString::fromUtf8("Gizmo 微调中: %1 [X=%2, Y=%3, Z=%4, W=%5°, P=%6°, R=%7°]")
            .arg(QString::fromStdString(preview.primaryTargetId))
            .arg(wpr.x, 0, 'f', 1)
            .arg(wpr.y, 0, 'f', 1)
            .arg(wpr.z, 0, 'f', 1)
            .arg(wpr.wDeg, 0, 'f', 1)
            .arg(wpr.pDeg, 0, 'f', 1)
            .arg(wpr.rDeg, 0, 'f', 1));
    });

    viewport_->setMultiTargetProvider([this]() -> std::vector<std::string> {
        return selectedTargetIdsFromTable();
    });

    sceneTree_ = new QTreeWidget();
    sceneTree_->setHeaderLabels({QString::fromUtf8("名称"), QString::fromUtf8("类型"), QString::fromUtf8("角色")});
    sceneTree_->header()->setStretchLastSection(true);
    sceneDock_ = makeDock(this, QString::fromUtf8("场景"), sceneTree_);
    connect(sceneTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (current == nullptr || isRefreshing()) {
            return;
        }
        const QString nodeId = current->data(0, Qt::UserRole).toString();
        if (!nodeId.isEmpty()) {
            controller_.selectSceneNode(nodeId.toStdString());
        }
    });

    programTree_ = new QTreeWidget();
    programTree_->setHeaderLabels({QString::fromUtf8("程序"), QString::fromUtf8("说明")});
    programDock_ = makeDock(this, QString::fromUtf8("程序"), programTree_);
    connect(programTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (current == nullptr || isRefreshing()) {
            return;
        }
        const QString kind = current->data(0, Qt::UserRole).toString();
        const QString id = current->data(0, Qt::UserRole + 1).toString();
        if (kind == "operation" && !id.isEmpty()) {
            controller_.selectOperation(id.toStdString());
        }
    });

    targetTable_ = new QTableWidget(0, 7);
    targetTable_->setHorizontalHeaderLabels({
        QString::fromUtf8("名称"),
        QString::fromUtf8("运动"),
        QString::fromUtf8("速度"),
        QString::fromUtf8("平滑度"),
        QString::fromUtf8("工艺事件"),
        QString::fromUtf8("状态"),
        QString::fromUtf8("间隙 mm")
    });
    targetTable_->horizontalHeader()->setStretchLastSection(true);
    targetTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    targetTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    targetDock_ = makeDock(this, QString::fromUtf8("路径点"), targetTable_);
    connect(targetTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row < 0 || isRefreshing()) {
            return;
        }
        auto* item = targetTable_->item(row, 0);
        if (item != nullptr) {
            controller_.selectTarget(
                item->data(Qt::UserRole).toString().toStdString(),
                item->data(Qt::UserRole + 1).toString().toStdString());
        }
    });
    connect(targetTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item == nullptr || isRefreshing()) {
            return;
        }
        const int row = item->row();
        const int col = item->column();
        auto* nameItem = targetTable_->item(row, 0);
        if (nameItem == nullptr) return;
        const std::string targetId = nameItem->data(Qt::UserRole).toString().toStdString();
        if (targetId.empty()) return;

        const auto* target = controller_.document().findTarget(targetId);
        if (target == nullptr) return;

        app::TargetProcessParams params;
        const QString text = item->text().trimmed();

        if (col == 1) { // 运动: J or L
            if (text.compare("J", Qt::CaseInsensitive) == 0) {
                params.motion = core::MotionType::J;
            } else if (text.compare("L", Qt::CaseInsensitive) == 0) {
                params.motion = core::MotionType::L;
            } else {
                QTimer::singleShot(0, this, [this] { refreshFromDocument(); });
                return;
            }
        } else if (col == 2) { // 速度: e.g. "200", "200 mm/s", "50%", "50 cm/min", "WELD_SPEED (80 mm/s)"
            const auto parsed = parseSpeedCellText(text, target->speedValue);
            if (parsed.valid) {
                params.speedValue = parsed.speedValue;
                params.useWeldSpeed = parsed.useWeldSpeed;
            } else {
                QTimer::singleShot(0, this, [this] { refreshFromDocument(); });
                return;
            }
        } else if (col == 3) { // 平滑度 / 终止类型: FINE or CNT [n]
            if (text.compare("FINE", Qt::CaseInsensitive) == 0) {
                params.termination = core::TerminationType::FINE;
                params.cntValue = 0;
            } else if (text.startsWith("CNT", Qt::CaseInsensitive)) {
                params.termination = core::TerminationType::CNT;
                QString numStr = text.mid(3).trimmed();
                bool ok = false;
                int val = numStr.toInt(&ok);
                params.cntValue = ok ? std::clamp(val, 0, 100) : 50;
            } else {
                bool ok = false;
                int val = text.toInt(&ok);
                if (ok) {
                    params.termination = core::TerminationType::CNT;
                    params.cntValue = std::clamp(val, 0, 100);
                } else {
                    QTimer::singleShot(0, this, [this] { refreshFromDocument(); });
                    return;
                }
            }
        } else if (col == 4) { // 工艺事件
            if (text.startsWith("Arc Start", Qt::CaseInsensitive) || text.startsWith("ArcStart", Qt::CaseInsensitive)) {
                int sched = 1;
                int lbracket = text.indexOf('[');
                int rbracket = text.indexOf(']');
                if (lbracket >= 0 && rbracket > lbracket) {
                    bool ok = false;
                    int s = text.mid(lbracket + 1, rbracket - lbracket - 1).toInt(&ok);
                    if (ok && s >= 1 && s <= 32) sched = s;
                }
                params.events = std::vector<core::ProcessEvent>{{core::ProcessEventType::ArcStart, sched, 0.0, ""}};
            } else if (text.startsWith("Arc End", Qt::CaseInsensitive) || text.startsWith("ArcEnd", Qt::CaseInsensitive)) {
                int sched = 1;
                int lbracket = text.indexOf('[');
                int rbracket = text.indexOf(']');
                if (lbracket >= 0 && rbracket > lbracket) {
                    bool ok = false;
                    int s = text.mid(lbracket + 1, rbracket - lbracket - 1).toInt(&ok);
                    if (ok && s >= 1 && s <= 32) sched = s;
                }
                params.events = std::vector<core::ProcessEvent>{{core::ProcessEventType::ArcEnd, sched, 0.0, ""}};
            } else if (text == "-" || text.isEmpty() || text.compare("None", Qt::CaseInsensitive) == 0) {
                params.events = std::vector<core::ProcessEvent>{};
            } else {
                QTimer::singleShot(0, this, [this] { refreshFromDocument(); });
                return;
            }
        } else {
            return;
        }

        struct ItemChangeScope {
            bool& flag;
            explicit ItemChangeScope(bool& f) : flag(f) { flag = true; }
            ~ItemChangeScope() { flag = false; }
        } itemScope(inItemChanged_);

        if (controller_.modifyTargetProcessParams(targetId, params)) {
            statusBar()->showMessage(QString::fromUtf8("已更新目标点工艺参数。"), 3000);
        } else {
            QTimer::singleShot(0, this, [this] { refreshFromDocument(); });
        }
    });

    propertyText_ = new QPlainTextEdit();
    propertyText_->setReadOnly(true);

    auto* propertyContainer = new QWidget();
    auto* propLayout = new QVBoxLayout(propertyContainer);
    propLayout->setContentsMargins(4, 4, 4, 4);
    propLayout->setSpacing(6);
    propLayout->addWidget(propertyText_, 1);

    spinTuningWidget_ = new QGroupBox(QString::fromUtf8("焊枪自转角微调 (TCP Z 自转)"));
    auto* spinLayout = new QVBoxLayout(spinTuningWidget_);
    spinLayout->setContentsMargins(6, 6, 6, 6);
    spinLayout->setSpacing(4);

    twistScanWidget_ = new TwistScanWidget(this);
    spinLayout->addWidget(twistScanWidget_);

    connect(twistScanWidget_, &TwistScanWidget::angleSelected, this, [this](double clickedAngleDeg) {
        const auto selectedIds = selectedTargetIdsFromTable();
        std::vector<std::string> ids = selectedIds;
        if (ids.empty() && !controller_.selectedTargetId().empty()) {
            ids.push_back(controller_.selectedTargetId());
        }
        if (ids.empty()) return;
        double currentZAngleDeg = 0.0;
        const auto* target = controller_.document().findTarget(ids.front());
        if (target != nullptr) {
            currentZAngleDeg = core::rad2deg(std::atan2(target->userDeltaOffset.rot.m[1][0], target->userDeltaOffset.rot.m[0][0]));
        }
        const double deltaDeg = clickedAngleDeg - currentZAngleDeg;
        if (std::abs(deltaDeg) > 1e-4) {
            if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deltaDeg))) {
                statusBar()->showMessage(QString::fromUtf8("已将目标点绕 TCP Z 轴自转至 %1°").arg(clickedAngleDeg, 0, 'f', 1), 3000);
                refreshFromDocument();
            }
        }
    });

    auto* optBtn = new QPushButton(QString::fromUtf8("★ 一键微调至最优自转角"));
    optBtn->setToolTip(QString::fromUtf8("自动在 72 组采样中定位远离碰撞与奇异的最大连续可行域中心角"));
    connect(optBtn, &QPushButton::clicked, this, [this] {
        if (!twistScanWidget_) return;
        const auto optAngle = twistScanWidget_->findOptimalFeasibleAngleDeg();
        if (!optAngle.has_value()) {
            statusBar()->showMessage(QString::fromUtf8("未找到完全无碰撞且远离奇异的自转角。"), 3000);
            return;
        }
        const auto selectedIds = selectedTargetIdsFromTable();
        std::vector<std::string> ids = selectedIds;
        if (ids.empty() && !controller_.selectedTargetId().empty()) {
            ids.push_back(controller_.selectedTargetId());
        }
        if (ids.empty()) return;

        double currentZAngleDeg = 0.0;
        const auto* target = controller_.document().findTarget(ids.front());
        if (target != nullptr) {
            currentZAngleDeg = core::rad2deg(std::atan2(target->userDeltaOffset.rot.m[1][0], target->userDeltaOffset.rot.m[0][0]));
        }
        const double deltaDeg = *optAngle - currentZAngleDeg;
        if (std::abs(deltaDeg) > 1e-4) {
            if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deltaDeg))) {
                statusBar()->showMessage(QString::fromUtf8("已自动微调至最优自转角 %1° (增量 %2°)").arg(*optAngle, 0, 'f', 1).arg(deltaDeg, 0, 'f', 1), 3000);
                refreshFromDocument();
            }
        } else {
            statusBar()->showMessage(QString::fromUtf8("当前已处于最优自转角 %1°。").arg(*optAngle, 0, 'f', 1), 3000);
        }
    });
    spinLayout->addWidget(optBtn);

    // Row 1: Fast step buttons
    auto* stepBtnLayout = new QHBoxLayout();
    stepBtnLayout->setSpacing(4);
    const std::vector<std::pair<QString, double>> stepAngles = {
        {"-15°", -15.0},
        {"-5°", -5.0},
        {"-1°", -1.0},
        {"+1°", +1.0},
        {"+5°", +5.0},
        {"+15°", +15.0}
    };
    for (const auto& [lbl, deg] : stepAngles) {
        auto* btn = new QPushButton(lbl);
        btn->setToolTip(QString::fromUtf8("绕焊枪 TCP 本地 Z 轴自转 %1").arg(lbl));
        connect(btn, &QPushButton::clicked, this, [this, deg] {
            const auto selectedIds = selectedTargetIdsFromTable();
            std::vector<std::string> ids = selectedIds;
            if (ids.empty() && !controller_.selectedTargetId().empty()) {
                ids.push_back(controller_.selectedTargetId());
            }
            if (ids.empty()) return;
            if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deg))) {
                statusBar()->showMessage(QString::fromUtf8("已将目标点绕 TCP Z 轴自转 %1°").arg(deg, 0, 'f', 1), 3000);
                refreshFromDocument();
            }
        });
        stepBtnLayout->addWidget(btn);
    }
    spinLayout->addLayout(stepBtnLayout);

    // Row 2: Custom spinbox and step rotation
    auto* customLayout = new QHBoxLayout();
    auto* angleSpin = new QDoubleSpinBox();
    angleSpin->setRange(0.1, 180.0);
    angleSpin->setValue(10.0);
    angleSpin->setSuffix("°");
    angleSpin->setSingleStep(1.0);

    auto* ccwBtn = new QPushButton(QString::fromUtf8("逆时针 ⟲"));
    connect(ccwBtn, &QPushButton::clicked, this, [this, angleSpin] {
        const auto selectedIds = selectedTargetIdsFromTable();
        std::vector<std::string> ids = selectedIds;
        if (ids.empty() && !controller_.selectedTargetId().empty()) {
            ids.push_back(controller_.selectedTargetId());
        }
        if (ids.empty()) return;
        const double deg = -angleSpin->value();
        if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deg))) {
            statusBar()->showMessage(QString::fromUtf8("已将目标点绕 TCP Z 轴自转 %1°").arg(deg, 0, 'f', 1), 3000);
            refreshFromDocument();
        }
    });

    auto* cwBtn = new QPushButton(QString::fromUtf8("顺时针 ⟳"));
    connect(cwBtn, &QPushButton::clicked, this, [this, angleSpin] {
        const auto selectedIds = selectedTargetIdsFromTable();
        std::vector<std::string> ids = selectedIds;
        if (ids.empty() && !controller_.selectedTargetId().empty()) {
            ids.push_back(controller_.selectedTargetId());
        }
        if (ids.empty()) return;
        const double deg = angleSpin->value();
        if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deg))) {
            statusBar()->showMessage(QString::fromUtf8("已将目标点绕 TCP Z 轴自转 +%1°").arg(deg, 0, 'f', 1), 3000);
            refreshFromDocument();
        }
    });

    customLayout->addWidget(angleSpin);
    customLayout->addWidget(ccwBtn);
    customLayout->addWidget(cwBtn);
    spinLayout->addLayout(customLayout);

    propLayout->addWidget(spinTuningWidget_);
    spinTuningWidget_->setVisible(false);

    propertyDock_ = makeDock(this, QString::fromUtf8("属性"), propertyContainer);

    auto* jogBody = new QWidget();
    auto* jogLayout = new QVBoxLayout(jogBody);
    jogReadout_ = new QLabel(QString::fromUtf8("点动只显示角度，不写入程序。"));
    jogReadout_->setWordWrap(true);
    jogLayout->addWidget(jogReadout_);
    const auto& robot = controller_.activeRobotModel();
    for (int joint = 0; joint < 6; ++joint) {
        auto* row = new QHBoxLayout();
        auto* name = new QLabel(QString::fromStdString(robot.jointLimits[static_cast<size_t>(joint)].name));
        name->setMinimumWidth(28);
        auto* slider = new QSlider(Qt::Horizontal);
        const double lower = core::rad2deg(robot.jointLimits[static_cast<size_t>(joint)].lowerRad);
        const double upper = core::rad2deg(robot.jointLimits[static_cast<size_t>(joint)].upperRad);
        slider->setRange(static_cast<int>(lower * 10.0), static_cast<int>(upper * 10.0));
        const double home = core::rad2deg(robot.homeJointsRad[static_cast<size_t>(joint)]);
        slider->setValue(static_cast<int>(home * 10.0));
        jogSliders_[joint] = slider;
        row->addWidget(name);
        row->addWidget(slider, 1);
        jogLayout->addLayout(row);
        connect(slider, &QSlider::valueChanged, this, [this](int) {
            if (isRefreshing()) {
                return;
            }
            QStringList parts;
            for (int index = 0; index < 6; ++index) {
                parts.push_back(QString("J%1 %2°").arg(index + 1).arg(jogSliders_[index]->value() / 10.0, 0, 'f', 1));
            }
            jogReadout_->setText(QString::fromUtf8("预览（未写入程序）：") + parts.join("  "));
        });
    }
    jogLayout->addStretch(1);
    jogDock_ = makeDock(this, QString::fromUtf8("关节点动"), jogBody);

    issueList_ = new QListWidget();
    connect(issueList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString targetId = item->data(Qt::UserRole).toString();
        const QString opId = item->data(Qt::UserRole + 1).toString();
        if (!targetId.isEmpty()) {
            if (!opId.isEmpty()) {
                controller_.selectOperation(opId.toStdString());
            }
            controller_.selectTarget(targetId.toStdString(), opId.toStdString());
        }
    });
    connect(issueList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString targetId = item->data(Qt::UserRole).toString();
        const QString opId = item->data(Qt::UserRole + 1).toString();
        if (!targetId.isEmpty()) {
            if (!opId.isEmpty()) {
                controller_.selectOperation(opId.toStdString());
            }
            controller_.selectTarget(targetId.toStdString(), opId.toStdString());
        }
    });
    issueDock_ = makeDock(this, QString::fromUtf8("问题"), issueList_);

    addDockWidget(Qt::LeftDockWidgetArea, sceneDock_);
    addDockWidget(Qt::LeftDockWidgetArea, programDock_);
    addDockWidget(Qt::RightDockWidgetArea, propertyDock_);
    splitDockWidget(propertyDock_, jogDock_, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, targetDock_);
    tabifyDockWidget(targetDock_, issueDock_);

    viewMenu->addAction(sceneDock_->toggleViewAction());
    viewMenu->addAction(programDock_->toggleViewAction());
    viewMenu->addAction(targetDock_->toggleViewAction());
    viewMenu->addAction(propertyDock_->toggleViewAction());
    viewMenu->addAction(jogDock_->toggleViewAction());
    viewMenu->addAction(issueDock_->toggleViewAction());

    auto* timeline = new QWidget();
    auto* timelineLayout = new QHBoxLayout(timeline);
    timelineLayout->setContentsMargins(8, 2, 8, 2);
    timelineLabel_ = new QLabel(QString::fromUtf8("时间轴"));
    timelineSlider_ = new QSlider(Qt::Horizontal);
    timelineSlider_->setEnabled(false);
    timelineSlider_->setRange(0, 0);
    auto* timelineNote = new QLabel(QString::fromUtf8("播放条在仿真画面接入后可用。"));
    timelineLayout->addWidget(timelineLabel_);
    timelineLayout->addWidget(timelineSlider_, 1);
    timelineLayout->addWidget(timelineNote);
    auto* timelineBar = new QToolBar(QString::fromUtf8("时间轴"));
    timelineBar->addWidget(timeline);
    addToolBar(Qt::BottomToolBarArea, timelineBar);

    resizeDocks({sceneDock_, programDock_}, {280, 280}, Qt::Horizontal);
    resizeDocks({propertyDock_, jogDock_}, {280, 220}, Qt::Vertical);
}

void ShellWindow::applyWorkbench(ActiveWorkbench workbench) {
    controller_.switchWorkbench(workbench);
    const bool layout = workbench == ActiveWorkbench::LayoutWorkbench;
    sceneDock_->setVisible(true);
    propertyDock_->setVisible(true);
    jogDock_->setVisible(true);
    programDock_->setVisible(!layout);
    targetDock_->setVisible(!layout);
    issueDock_->setVisible(!layout);
    if (layoutAction_ != nullptr) {
        layoutAction_->setChecked(layout);
        olpAction_->setChecked(!layout);
    }
    setWindowTitle(layout
        ? QString::fromUtf8("SiFastSuite OLP — 布局工作台")
        : QString::fromUtf8("SiFastSuite OLP — 编程工作台"));
}

void ShellWindow::showLayoutWorkbench() {
    applyWorkbench(ActiveWorkbench::LayoutWorkbench);
    statusBar()->showMessage(QString::fromUtf8("布局工作台：场景、属性、点动和三维视口。"));
}

void ShellWindow::showOlpWorkbench() {
    applyWorkbench(ActiveWorkbench::OlpWorkbench);
    statusBar()->showMessage(QString::fromUtf8("编程工作台：场景、程序、路径点、点动、问题和属性。"));
}

void ShellWindow::loadSample() {
    if (!loader_) {
        statusBar()->showMessage(QString::fromUtf8("没有接上示例工位。"));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    statusBar()->showMessage(QString::fromUtf8("正在导入示例工位并生成焊缝…"));
    QApplication::processEvents();
    loader_();
    if (controller_.lastFrameCount() > 0 || !controller_.document().controllers.empty()) {
        showOlpWorkbench();
    }
    refreshFromDocument();
    if (viewport_ != nullptr) {
        viewport_->syncScene();
    }
    const int frames = controller_.lastFrameCount();
    if (timelineSlider_ != nullptr && frames > 0) {
        timelineSlider_->setRange(0, frames - 1);
        timelineLabel_->setText(QString::fromUtf8("时间轴  %1 帧").arg(frames));
    }
    QApplication::restoreOverrideCursor();
    std::ostringstream summary;
    summary.setf(std::ios::fixed);
    summary.precision(1);
    summary << "示例工位已加载。节拍 " << controller_.lastCycleTimeSec()
            << " 秒，焊接 " << controller_.lastWeldTimeSec()
            << " 秒，" << frames << " 帧。";
    if (controller_.lastSampleExitCode() != 0) {
        summary << " 有阻塞问题，文件仍已写出。";
    }
    statusBar()->showMessage(QString::fromStdString(summary.str()));
}

void ShellWindow::refreshFromDocument() {
    if (isRefreshing()) {
        return;
    }
    RefreshGuard guard(refreshDepth_);
    rebuildSceneTree();
    rebuildProgramTree();
    rebuildTargetTable();
    fillIssues();
    syncJogFromRobot();
    updateUndoActions();
    if (viewport_ != nullptr) {
        viewport_->syncScene();
    }
    applySelection();
}

void ShellWindow::rebuildSceneTree() {
    sceneTree_->clear();
    const auto& nodes = controller_.document().sceneNodes;
    if (nodes.empty()) {
        auto* empty = new QTreeWidgetItem(sceneTree_, {QString::fromUtf8("尚未加载工位"), "", ""});
        empty->setFlags(empty->flags() & ~Qt::ItemIsSelectable);
        return;
    }
    auto ensureGroup = [&](const QString& title) {
        for (int i = 0; i < sceneTree_->topLevelItemCount(); ++i) {
            if (sceneTree_->topLevelItem(i)->text(0) == title) {
                return sceneTree_->topLevelItem(i);
            }
        }
        return new QTreeWidgetItem(sceneTree_, {title, "", ""});
    };
    for (const auto& node : nodes) {
        QString group = QString::fromUtf8("其他");
        if (!node.visible || node.collisionRole == core::CollisionRole::Ignored) {
            group = QString::fromUtf8("已隐藏的静态外形");
        } else if (node.type == core::SceneNodeType::Frame) {
            group = QString::fromUtf8("坐标系");
        } else if (node.type == core::SceneNodeType::Robot) {
            group = QString::fromUtf8("机器人");
        } else if (node.collisionRole == core::CollisionRole::Workpiece) {
            group = QString::fromUtf8("工件");
        } else if (node.collisionRole == core::CollisionRole::Fixture) {
            group = QString::fromUtf8("夹具");
        }
        auto* item = new QTreeWidgetItem(ensureGroup(group), {
            QString::fromStdString(node.name),
            typeText(node.type),
            roleText(node.collisionRole)
        });
        item->setData(0, Qt::UserRole, QString::fromStdString(node.id));
    }
    sceneTree_->expandAll();
}

void ShellWindow::rebuildProgramTree() {
    programTree_->clear();
    const auto& doc = controller_.document();
    if (doc.seams.empty() && doc.controllers.empty()) {
        new QTreeWidgetItem(programTree_, {QString::fromUtf8("尚未生成程序"), ""});
        return;
    }
    if (!doc.seams.empty()) {
        auto* seams = new QTreeWidgetItem(programTree_, {QString::fromUtf8("焊缝"), ""});
        for (const auto& seam : doc.seams) {
            new QTreeWidgetItem(seams, {
                QString::fromStdString(seam.name),
                QString::fromUtf8("起点后移 %1 mm").arg(seam.startTrimMm, 0, 'f', 0)
            });
        }
    }
    for (const auto& cu : doc.controllers) {
        auto* controllerItem = new QTreeWidgetItem(programTree_, {
            QString::fromStdString(cu.name.empty() ? cu.id : cu.name),
            QString::fromUtf8("控制器")
        });
        for (const auto& program : cu.programs) {
            auto* programItem = new QTreeWidgetItem(controllerItem, {
                QString::fromStdString(program.name),
                QString::fromUtf8("程序")
            });
            for (const auto& op : program.operations) {
                auto* opItem = new QTreeWidgetItem(programItem, {
                    QString::fromStdString(op.name),
                    QString::fromUtf8("操作")
                });
                opItem->setData(0, Qt::UserRole, "operation");
                opItem->setData(0, Qt::UserRole + 1, QString::fromStdString(op.id));
            }
        }
    }
    programTree_->expandAll();
}

void ShellWindow::rebuildTargetTable() {
    RefreshGuard guard(refreshDepth_);
    targetTable_->setRowCount(0);
    const core::Operation* operation = nullptr;
    const std::string& selected = controller_.selectedOperationId();
    for (const auto& cu : controller_.document().controllers) {
        for (const auto& program : cu.programs) {
            for (const auto& op : program.operations) {
                if (operation == nullptr) {
                    operation = &op;
                }
                if (!selected.empty() && op.id == selected) {
                    operation = &op;
                }
            }
        }
    }
    if (operation == nullptr) {
        return;
    }
    int row = 0;
    for (const auto& target : operation->targets) {
        targetTable_->insertRow(row);
        auto* name = new QTableWidgetItem(QString::fromStdString(target.name));
        name->setData(Qt::UserRole, QString::fromStdString(target.id));
        name->setData(Qt::UserRole + 1, QString::fromStdString(operation->id));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        targetTable_->setItem(row, 0, name);

        targetTable_->setItem(row, 1, new QTableWidgetItem(motionText(target.motion)));

        targetTable_->setItem(row, 2, new QTableWidgetItem(formatSpeedCellText(target)));

        const QString term = (target.termination == core::TerminationType::FINE)
            ? QString("FINE")
            : QString("CNT %1").arg(target.cntValue);
        targetTable_->setItem(row, 3, new QTableWidgetItem(term));

        QString evStr = "-";
        for (const auto& ev : target.events) {
            if (ev.type == core::ProcessEventType::ArcStart) {
                evStr = QString("Arc Start[%1]").arg(ev.scheduleNumber);
                break;
            } else if (ev.type == core::ProcessEventType::ArcEnd) {
                evStr = QString("Arc End[%1]").arg(ev.scheduleNumber);
                break;
            }
        }
        targetTable_->setItem(row, 4, new QTableWidgetItem(evStr));

        auto* statusItem = new QTableWidgetItem(statusText(target.cache.status));
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
        targetTable_->setItem(row, 5, statusItem);

        auto* clearanceItem = new QTableWidgetItem(
            QString::number(target.cache.minClearanceMm, 'f', 1));
        clearanceItem->setFlags(clearanceItem->flags() & ~Qt::ItemIsEditable);
        targetTable_->setItem(row, 6, clearanceItem);

        ++row;
    }
}

void ShellWindow::showPropertiesForScene(const QString& nodeId) {
    if (spinTuningWidget_) {
        spinTuningWidget_->setVisible(false);
    }
    if (nodeId.isEmpty()) {
        return;
    }
    const auto* node = controller_.document().findSceneNode(nodeId.toStdString());
    if (node == nullptr) {
        return;
    }
    const auto wpr = node->localTransform.toWPR();
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(1);
    text << node->name << "\n"
         << "类型  " << typeText(node->type).toStdString() << "\n"
         << "碰撞角色  " << roleText(node->collisionRole).toStdString() << "\n"
         << "可见  " << (node->visible ? "是" : "否") << "\n"
         << "位置 mm  " << wpr.x << "  " << wpr.y << "  " << wpr.z << "\n"
         << "姿态 WPR °  " << wpr.wDeg << "  " << wpr.pDeg << "  " << wpr.rDeg << "\n";
    propertyText_->setPlainText(QString::fromStdString(text.str()));
}

void ShellWindow::showPropertiesForTarget(const QString& targetId) {
    if (spinTuningWidget_) {
        spinTuningWidget_->setVisible(true);
    }
    const auto* target = findTargetById(controller_.document(), targetId.toStdString());
    if (target == nullptr) {
        return;
    }
    const auto wpr = target->poseInUFrame.toWPR();
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(1);
    text << target->name << "\n"
         << "运动  " << motionText(target->motion).toStdString() << "\n"
         << "速度  " << (target->useWeldSpeed ? "WELD_SPEED (" + std::to_string(static_cast<int>(target->speedValue)) + " mm/s)" : std::to_string(static_cast<int>(target->speedValue))) << "\n"
         << "平滑度  " << (target->termination == core::TerminationType::FINE ? "FINE" : "CNT " + std::to_string(target->cntValue)) << "\n";
    if (!target->events.empty()) {
        for (const auto& ev : target->events) {
            if (ev.type == core::ProcessEventType::ArcStart) {
                text << "工艺事件  Arc Start[" << ev.scheduleNumber << "]\n";
            } else if (ev.type == core::ProcessEventType::ArcEnd) {
                text << "工艺事件  Arc End[" << ev.scheduleNumber << "]\n";
            }
        }
    }
    text << "状态  " << statusText(target->cache.status).toStdString() << "\n"
         << "间隙 mm  " << target->cache.minClearanceMm << "\n";
    if (target->cache.valid) {
        text << "构型  " << target->cache.solvedConfig.toString() << "\n";
        text << "可操作度  " << target->cache.manipulabilityIndex << "\n";
        text << "关节角 °  J1:" << core::rad2deg(target->cache.jointsRad[0])
             << " J2:" << core::rad2deg(target->cache.jointsRad[1])
             << " J3:" << core::rad2deg(target->cache.jointsRad[2])
             << " J4:" << core::rad2deg(target->cache.jointsRad[3])
             << " J5:" << core::rad2deg(target->cache.jointsRad[4])
             << " J6:" << core::rad2deg(target->cache.jointsRad[5]) << "\n";
    }
    text << "UFRAME 位置 mm  " << wpr.x << "  " << wpr.y << "  " << wpr.z << "\n"
         << "UFRAME 姿态 WPR °  " << wpr.wDeg << "  " << wpr.pDeg << "  " << wpr.rDeg << "\n";
    if (!target->cache.issueMessage.empty()) {
        text << "说明  " << target->cache.issueMessage << "\n";
    }
    propertyText_->setPlainText(QString::fromStdString(text.str()));

    if (twistScanWidget_ != nullptr) {
        const double curZDeg = core::rad2deg(std::atan2(target->userDeltaOffset.rot.m[1][0], target->userDeltaOffset.rot.m[0][0]));
        const auto samples = controller_.scanSelectedTargetTwistAngles(5.0);
        twistScanWidget_->setSamples(samples, curZDeg);
    }
}

void ShellWindow::updateUndoActions() {
    if (undoAction_ == nullptr) {
        return;
    }
    undoAction_->setEnabled(controller_.commandManager().canUndo());
    redoAction_->setEnabled(controller_.commandManager().canRedo());
}

void ShellWindow::fillIssues() {
    issueList_->clear();
    std::set<std::pair<std::string, std::string>> seen;

    auto addIssueItem = [this, &seen](const std::string& opId, const std::string& tid,
                                      const std::string& opName, const std::string& tName,
                                      core::TargetValidationState state, const std::string& msg) {
        if (msg.empty() || state == core::TargetValidationState::Valid ||
            state == core::TargetValidationState::Unchecked) {
            return;
        }
        if (!seen.insert({tid, msg}).second) {
            return;
        }
        auto* item = new QListWidgetItem();
        item->setText(QString("[%1] %2: %3")
            .arg(QString::fromStdString(opName.empty() ? opId : opName))
            .arg(QString::fromStdString(tName.empty() ? tid : tName))
            .arg(QString::fromStdString(msg)));
        item->setData(Qt::UserRole, QString::fromStdString(tid));
        item->setData(Qt::UserRole + 1, QString::fromStdString(opId));

        if (state == core::TargetValidationState::Collision ||
            state == core::TargetValidationState::Unreachable ||
            state == core::TargetValidationState::JointLimitExceeded ||
            state == core::TargetValidationState::ConfigJump) {
            item->setForeground(QColor(220, 50, 50));
        } else {
            item->setForeground(QColor(230, 140, 20));
        }
        issueList_->addItem(item);
    };

    // 1. SimulationResult issues (includes segment no-inverse, sweep collision, sweep clearance, ConfigJump)
    for (const auto& issue : controller_.lastSimulationResult().issues) {
        std::string opName = issue.operationId;
        const auto* op = controller_.document().findOperation(issue.operationId);
        if (op != nullptr && !op->name.empty()) {
            opName = op->name;
        }
        std::string tName = issue.targetName;
        if (tName.empty()) {
            const auto* t = controller_.document().findTarget(issue.targetId);
            if (t != nullptr) tName = t->name;
        }
        addIssueItem(issue.operationId, issue.targetId, opName, tName, issue.state, issue.description);
    }

    // 2. Direct TargetCache issues from Document
    for (const auto& cu : controller_.document().controllers) {
        for (const auto& program : cu.programs) {
            for (const auto& op : program.operations) {
                for (const auto& target : op.targets) {
                    addIssueItem(op.id, target.id, op.name, target.name, target.cache.status, target.cache.issueMessage);
                }
            }
        }
    }

    if (issueList_->count() == 0) {
        auto* cleanItem = new QListWidgetItem(QString::fromUtf8("未发现干涉或运动学异常 (全部正常)"));
        cleanItem->setForeground(QColor(50, 160, 80));
        cleanItem->setFlags(cleanItem->flags() & ~Qt::ItemIsSelectable);
        issueList_->addItem(cleanItem);
    }
}

void ShellWindow::applySelection() {
    if (!controller_.selectedTargetId().empty()) {
        showPropertiesForTarget(QString::fromStdString(controller_.selectedTargetId()));
        if (viewport_ != nullptr) {
            viewport_->setTargetGizmo(controller_.selectedTargetId());
        }
        syncJogFromRobot();

        if (targetTable_ != nullptr) {
            RefreshGuard guard(refreshDepth_);
            bool needsRebuild = (targetTable_->rowCount() == 0);
            if (!needsRebuild) {
                auto* firstItem = targetTable_->item(0, 0);
                if (firstItem != nullptr) {
                    const std::string tableOpId = firstItem->data(Qt::UserRole + 1).toString().toStdString();
                    if (!controller_.selectedOperationId().empty() && tableOpId != controller_.selectedOperationId()) {
                        needsRebuild = true;
                    }
                }
            }
            if (needsRebuild) {
                rebuildTargetTable();
            }

            for (int r = 0; r < targetTable_->rowCount(); ++r) {
                auto* item = targetTable_->item(r, 0);
                if (item != nullptr && item->data(Qt::UserRole).toString().toStdString() == controller_.selectedTargetId()) {
                    targetTable_->selectRow(r);
                    targetTable_->scrollToItem(item);
                    break;
                }
            }
        }

        if (programTree_ != nullptr && !controller_.selectedOperationId().empty()) {
            RefreshGuard guard(refreshDepth_);
            for (int i = 0; i < programTree_->topLevelItemCount(); ++i) {
                auto* top = programTree_->topLevelItem(i);
                for (int j = 0; j < top->childCount(); ++j) {
                    auto* progItem = top->child(j);
                    for (int k = 0; k < progItem->childCount(); ++k) {
                        auto* opItem = progItem->child(k);
                        if (opItem->data(0, Qt::UserRole + 1).toString().toStdString() == controller_.selectedOperationId()) {
                            programTree_->setCurrentItem(opItem);
                            break;
                        }
                    }
                }
            }
        }
        return;
    }
    if (viewport_ != nullptr) {
        viewport_->clearTargetGizmo();
    }
    if (!controller_.selectedSceneNodeId().empty()) {
        showPropertiesForScene(QString::fromStdString(controller_.selectedSceneNodeId()));
        return;
    }
    if (controller_.selectedOperationId().empty()) {
        return;
    }
    {
        RefreshGuard guard(refreshDepth_);
        rebuildTargetTable();
    }
    for (const auto& cu : controller_.document().controllers) {
        for (const auto& program : cu.programs) {
            for (const auto& op : program.operations) {
                if (op.id != controller_.selectedOperationId()) {
                    continue;
                }
                propertyText_->setPlainText(QString::fromUtf8("操作  ")
                    + QString::fromStdString(op.name)
                    + QString::fromUtf8("\n路径点  ")
                    + QString::number(static_cast<int>(op.targets.size())));
                return;
            }
        }
    }
}

void ShellWindow::syncJogFromRobot() {
    const core::SceneNode* robot = nullptr;
    for (const auto& node : controller_.document().sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            robot = &node;
            break;
        }
    }
    const auto& model = controller_.activeRobotModel();
    QStringList parts;
    for (int joint = 0; joint < 6; ++joint) {
        if (jogSliders_[joint] == nullptr) {
            continue;
        }
        const double rad = robot != nullptr
            ? robot->currentJointsRad[static_cast<size_t>(joint)]
            : model.homeJointsRad[static_cast<size_t>(joint)];
        jogSliders_[joint]->setValue(static_cast<int>(std::lround(core::rad2deg(rad) * 10.0)));
        parts.push_back(QString("J%1 %2°").arg(joint + 1).arg(core::rad2deg(rad), 0, 'f', 1));
    }
    if (jogReadout_ != nullptr) {
        jogReadout_->setText(QString::fromUtf8("场景机器人：") + parts.join("  "));
    }
}

std::vector<std::string> ShellWindow::selectedTargetIdsFromTable() const {
    std::vector<std::string> ids;
    if (targetTable_ == nullptr) return ids;

    std::set<int> rowSet;
    const auto selectedIndexes = targetTable_->selectionModel() ? targetTable_->selectionModel()->selectedIndexes() : QModelIndexList();
    for (const auto& idx : selectedIndexes) {
        rowSet.insert(idx.row());
    }

    for (int r : rowSet) {
        auto* item = targetTable_->item(r, 0);
        if (item != nullptr) {
            const std::string id = item->data(Qt::UserRole).toString().toStdString();
            if (!id.empty()) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

} // namespace olp::ui

