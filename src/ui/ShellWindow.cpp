#include "olp/ui/ShellWindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSlider>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <sstream>

namespace olp::ui {
namespace {

class ViewportPlaceholder : public QWidget {
public:
    explicit ViewportPlaceholder(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(640, 420);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(28, 40, 49));
        painter.setPen(QColor(48, 68, 82));
        const int step = 40;
        for (int x = 0; x < width(); x += step) {
            painter.drawLine(x, 0, x, height());
        }
        for (int y = 0; y < height(); y += step) {
            painter.drawLine(0, y, width(), y);
        }

        const QPoint origin(56, height() - 56);
        painter.setPen(QPen(QColor(214, 84, 78), 3));
        painter.drawLine(origin, origin + QPoint(72, 0));
        painter.setPen(QPen(QColor(92, 184, 108), 3));
        painter.drawLine(origin, origin + QPoint(0, -72));
        painter.setPen(QPen(QColor(86, 156, 214), 3));
        painter.drawLine(origin, origin + QPoint(42, -42));
        painter.setPen(QColor(220, 226, 230));
        painter.drawText(origin + QPoint(76, 4), "X");
        painter.drawText(origin + QPoint(-14, -76), "Z");
        painter.drawText(origin + QPoint(46, -46), "Y");

        painter.setPen(QColor(236, 240, 243));
        QFont title = painter.font();
        title.setPixelSize(22);
        title.setBold(true);
        painter.setFont(title);
        const QString heading = QString::fromUtf8("三维视口");
        painter.drawText(rect().adjusted(0, -40, 0, 0), Qt::AlignCenter, heading);

        painter.setPen(QColor(176, 190, 200));
        QFont body = painter.font();
        body.setPixelSize(13);
        body.setBold(false);
        painter.setFont(body);
        painter.drawText(
            rect().adjusted(40, 24, -40, 0),
            Qt::AlignHCenter | Qt::AlignVCenter,
            QString::fromUtf8("这里显示工件、机器人和焊缝。\n旋转、拾取和焊枪拖拽在视口接入后可用。"));
    }
};

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

ShellWindow::ShellWindow(MainWindow& controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
    setWindowTitle(QString::fromUtf8("SiFastSuite OLP — 焊接离线编程"));
    resize(1440, 900);
    buildChrome();
    controller_.commandManager().setOnDocumentChangedCallback([this] { refreshFromDocument(); });
    applyWorkbench(controller_.activeWorkbench());
    refreshFromDocument();
    statusBar()->showMessage(QString::fromUtf8("布局工作台。用工具栏加载示例工位。"));
}

ShellWindow::~ShellWindow() {
    controller_.commandManager().setOnDocumentChangedCallback(nullptr);
}

void ShellWindow::setSampleLoader(std::function<SampleSessionResult()> loader) {
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
        "QToolBar { background: #eef3f7; border: 0; spacing: 6px; padding: 4px; }"
        "QToolButton { padding: 6px 12px; color: #1c2830; }"
        "QToolButton:checked { background: #1f6f8b; color: white; border-radius: 3px; }"
        "QDockWidget::title { background: #2c4558; color: white; padding: 6px; }"
        "QStatusBar { background: #243746; color: #f2f5f7; }"
        "QTreeWidget, QTableWidget, QListWidget, QPlainTextEdit { background: #fbfcfd; color: #1c2830; }"
        "QHeaderView::section { background: #e4ebf1; padding: 4px; }"));

    auto* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    auto* loadAction = fileMenu->addAction(QString::fromUtf8("加载示例工位"));
    connect(loadAction, &QAction::triggered, this, &ShellWindow::loadSample);
    fileMenu->addSeparator();
    fileMenu->addAction(QString::fromUtf8("退出"), this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(QString::fromUtf8("编辑"));
    undoAction_ = editMenu->addAction(QString::fromUtf8("撤销"));
    redoAction_ = editMenu->addAction(QString::fromUtf8("重做"));
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
    layoutAction_ = workbenchMenu->addAction(QString::fromUtf8("布局工作台"));
    olpAction_ = workbenchMenu->addAction(QString::fromUtf8("编程工作台"));
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
                "SiFastSuite OLP。当前是主窗口分区，三维视口尚未接入。"));
        });

    auto* toolbar = addToolBar(QString::fromUtf8("主工具栏"));
    toolbar->setMovable(false);
    toolbar->addAction(loadAction);
    toolbar->addSeparator();
    toolbar->addAction(layoutAction_);
    toolbar->addAction(olpAction_);
    toolbar->addSeparator();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);

    setCentralWidget(new ViewportPlaceholder(this));

    sceneTree_ = new QTreeWidget();
    sceneTree_->setHeaderLabels({QString::fromUtf8("名称"), QString::fromUtf8("类型"), QString::fromUtf8("角色")});
    sceneTree_->header()->setStretchLastSection(true);
    sceneDock_ = makeDock(this, QString::fromUtf8("场景"), sceneTree_);
    connect(sceneTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (current == nullptr || refreshing_) {
            return;
        }
        showPropertiesForScene(current->data(0, Qt::UserRole).toString());
    });

    programTree_ = new QTreeWidget();
    programTree_->setHeaderLabels({QString::fromUtf8("程序"), QString::fromUtf8("说明")});
    programDock_ = makeDock(this, QString::fromUtf8("程序"), programTree_);
    connect(programTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (current == nullptr || refreshing_) {
            return;
        }
        const QString kind = current->data(0, Qt::UserRole).toString();
        const QString id = current->data(0, Qt::UserRole + 1).toString();
        if (kind == "target") {
            showPropertiesForTarget(id);
        }
    });

    targetTable_ = new QTableWidget(0, 5);
    targetTable_->setHorizontalHeaderLabels({
        QString::fromUtf8("名称"),
        QString::fromUtf8("运动"),
        QString::fromUtf8("速度"),
        QString::fromUtf8("状态"),
        QString::fromUtf8("间隙 mm")
    });
    targetTable_->horizontalHeader()->setStretchLastSection(true);
    targetTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    targetTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    targetDock_ = makeDock(this, QString::fromUtf8("路径点"), targetTable_);
    connect(targetTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row < 0 || refreshing_) {
            return;
        }
        auto* item = targetTable_->item(row, 0);
        if (item != nullptr) {
            showPropertiesForTarget(item->data(Qt::UserRole).toString());
        }
    });

    propertyText_ = new QPlainTextEdit();
    propertyText_->setReadOnly(true);
    propertyDock_ = makeDock(this, QString::fromUtf8("属性"), propertyText_);

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
            if (refreshing_) {
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
    sceneDock_->setVisible(layout);
    programDock_->setVisible(!layout);
    targetDock_->setVisible(!layout);
    issueDock_->setVisible(!layout);
    jogDock_->setVisible(!layout);
    propertyDock_->setVisible(true);
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
    statusBar()->showMessage(QString::fromUtf8("布局工作台：场景、属性和三维视口。"));
}

void ShellWindow::showOlpWorkbench() {
    applyWorkbench(ActiveWorkbench::OlpWorkbench);
    statusBar()->showMessage(QString::fromUtf8("编程工作台：程序、路径点、点动、问题和属性。"));
}

void ShellWindow::loadSample() {
    if (!loader_) {
        statusBar()->showMessage(QString::fromUtf8("没有接上示例工位。"));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    statusBar()->showMessage(QString::fromUtf8("正在导入示例工位并生成焊缝…"));
    QApplication::processEvents();
    lastResult_ = loader_();
    refreshFromDocument();
    applyWorkbench(controller_.activeWorkbench());
    fillIssues();
    if (timelineSlider_ != nullptr && lastResult_.frameCount > 0) {
        timelineSlider_->setRange(0, lastResult_.frameCount - 1);
        timelineLabel_->setText(QString::fromUtf8("时间轴  %1 帧").arg(lastResult_.frameCount));
    }
    QApplication::restoreOverrideCursor();
    statusBar()->showMessage(QString::fromStdString(lastResult_.summary));
}

void ShellWindow::refreshFromDocument() {
    if (refreshing_) {
        return;
    }
    refreshing_ = true;
    rebuildSceneTree();
    rebuildProgramTree();
    rebuildTargetTable();
    updateUndoActions();
    refreshing_ = false;
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
                for (const auto& target : op.targets) {
                    auto* targetItem = new QTreeWidgetItem(opItem, {
                        QString::fromStdString(target.name),
                        motionText(target.motion)
                    });
                    targetItem->setData(0, Qt::UserRole, "target");
                    targetItem->setData(0, Qt::UserRole + 1, QString::fromStdString(target.id));
                }
            }
        }
    }
    programTree_->expandAll();
}

void ShellWindow::rebuildTargetTable() {
    targetTable_->setRowCount(0);
    int row = 0;
    for (const auto& cu : controller_.document().controllers) {
        for (const auto& program : cu.programs) {
            for (const auto& op : program.operations) {
                for (const auto& target : op.targets) {
                    targetTable_->insertRow(row);
                    auto* name = new QTableWidgetItem(QString::fromStdString(target.name));
                    name->setData(Qt::UserRole, QString::fromStdString(target.id));
                    targetTable_->setItem(row, 0, name);
                    targetTable_->setItem(row, 1, new QTableWidgetItem(motionText(target.motion)));
                    const QString speed = target.motion == core::MotionType::J
                        ? QString("%1%").arg(target.speedValue, 0, 'f', 0)
                        : QString("%1 mm/s").arg(target.speedValue, 0, 'f', 0);
                    targetTable_->setItem(row, 2, new QTableWidgetItem(speed));
                    targetTable_->setItem(row, 3, new QTableWidgetItem(statusText(target.cache.status)));
                    targetTable_->setItem(row, 4, new QTableWidgetItem(
                        QString::number(target.cache.minClearanceMm, 'f', 1)));
                    ++row;
                }
            }
        }
    }
}

void ShellWindow::showPropertiesForScene(const QString& nodeId) {
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
         << "速度  " << target->speedValue << "\n"
         << "状态  " << statusText(target->cache.status).toStdString() << "\n"
         << "间隙 mm  " << target->cache.minClearanceMm << "\n"
         << "UFRAME 位置 mm  " << wpr.x << "  " << wpr.y << "  " << wpr.z << "\n"
         << "UFRAME 姿态 WPR °  " << wpr.wDeg << "  " << wpr.pDeg << "  " << wpr.rDeg << "\n";
    if (!target->cache.issueMessage.empty()) {
        text << "说明  " << target->cache.issueMessage << "\n";
    }
    propertyText_->setPlainText(QString::fromStdString(text.str()));
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
    if (lastResult_.issues.empty()) {
        issueList_->addItem(QString::fromUtf8("没有校验问题。"));
        return;
    }
    for (const auto& issue : lastResult_.issues) {
        issueList_->addItem(QString::fromStdString(issue));
    }
    issueDock_->setVisible(controller_.activeWorkbench() == ActiveWorkbench::OlpWorkbench);
    issueDock_->raise();
}

} // namespace olp::ui

