#include "olp/ui/ShellWindow.h"
#include "olp/ui/GizmoDragPreviewHandler.h"
#include "olp/ui/IssueListPanel.h"
#include "olp/ui/TargetPropertyPanel.h"
#include "olp/ui/TargetWaypointTable.h"
#include "olp/ui/TwistScanWidget.h"
#include "olp/ui/TimelinePanel.h"
#include "olp/ui/LsExportDialog.h"
#include "ShellChromeHelpers.h"
#include "ShellLabels.h"

#include "olp/ui/OcctViewport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QScrollArea>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTableWidget>
#include <QThreadPool>
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
    setCursor(Qt::ArrowCursor);
    setStyleSheet(QString::fromUtf8(
        "QMainWindow { background: #d5dde4; }"
        "QMenuBar { background: #243746; color: #f2f5f7; padding: 2px 6px; }"
        "QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 4px; }"
        "QMenuBar::item:hover, QMenuBar::item:selected { background: #3d5d78; color: #ffffff; }"
        "QMenuBar::item:pressed { background: #1a2a36; color: #ffffff; }"
        "QMenu { background: #ffffff; color: #1c2830; border: 1px solid #c5d0d9; padding: 4px 0px; border-radius: 4px; }"
        "QMenu::item { padding: 6px 28px 6px 24px; background: transparent; }"
        "QMenu::item:selected, QMenu::item:hover { background: #3d5d78; color: #ffffff; }"
        "QMenu::item:disabled { color: #9aa7b1; background: transparent; }"
        "QMenu::item:disabled:selected, QMenu::item:disabled:hover { color: #9aa7b1; background: transparent; }"
        "QMenu::separator { height: 1px; background: #e1e7ec; margin: 4px 8px; }"
        "QToolBar { background: #f4f7fa; border: 0; border-bottom: 1px solid #d5dee6; spacing: 0; padding: 0; }"
        "QToolButton { color: #1c2830; padding: 2px 4px; border: 1px solid transparent; border-radius: 4px; background: transparent; }"
        "QToolButton:hover { background: #e5f1f7; }"
        "QToolButton:checked { background: #d5edf6; color: #143644; border-color: #1f6f8b; }"
        "QToolButton:disabled { color: #93a1ab; }"
        "QDockWidget { color: #ffffff; font-weight: bold; }"
        "QDockWidget > QWidget { color: #1c2830; font-weight: normal; }"
        "QDockWidget::title { background: #243746; color: #ffffff; padding: 6px 10px; font-weight: bold; }"
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
    auto* openAction = fileMenu->addAction(commandIcon(IconKind::Open, fileInk), QString::fromUtf8("打开..."));
    openAction->setToolTip(QString::fromUtf8("打开 JSON 工程文件。"));
    connect(openAction, &QAction::triggered, this, &ShellWindow::openProjectFile);

    auto* saveAction = fileMenu->addAction(commandIcon(IconKind::Save, fileInk), QString::fromUtf8("保存..."));
    saveAction->setToolTip(QString::fromUtf8("保存当前工程为 JSON 文件。"));
    connect(saveAction, &QAction::triggered, this, &ShellWindow::saveProjectFile);

    auto* importAction = disabledAction(this, QString::fromUtf8("导入"), later, IconKind::Import, fileInk);
    auto* loadAction = fileMenu->addAction(commandIcon(IconKind::Sample, fileInk), QString::fromUtf8("加载示例工位"));
    fileMenu->insertAction(loadAction, newAction);
    fileMenu->insertAction(loadAction, openAction);
    fileMenu->insertAction(loadAction, saveAction);
    fileMenu->insertAction(loadAction, importAction);
    loadAction->setToolTip(QString::fromUtf8("导入示例焊接工位并生成焊缝。"));
    connect(loadAction, &QAction::triggered, this, &ShellWindow::loadSample);
    exportMenuAction_ = fileMenu->addAction(commandIcon(IconKind::Export, fileInk), QString::fromUtf8("导出 FANUC .ls 程序..."));
    exportMenuAction_->setToolTip(QString::fromUtf8("导出当前工位程序为 FANUC ASCII .ls 格式。"));
    connect(exportMenuAction_, &QAction::triggered, this, [this] {
        LsExportDialog dlg(controller_, this);
        dlg.exec();
    });
    exportPdfAction_ = fileMenu->addAction(commandIcon(IconKind::Export, fileInk), QString::fromUtf8("导出 2D PDF 工艺报告..."));
    exportPdfAction_->setToolTip(QString::fromUtf8("导出当前工位 SOP 工艺指导书为 PDF 文件。"));
    connect(exportPdfAction_, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, QString::fromUtf8("导出 2D PDF 工艺报告"),
            QStringLiteral("Workcell_SOP_Report.pdf"),
            QStringLiteral("PDF 文件 (*.pdf)"));
        if (path.isEmpty()) return;
        PdfReportOptions opts;
        if (viewport_ != nullptr && viewport_->isVisible()) {
            opts.snapshotImage = viewport_->grab().toImage();
            opts.includeViewportSnapshot = !opts.snapshotImage.isNull();
        }
        opts.cycleTimeSec = controller_.lastCycleTimeSec();
        opts.weldTimeSec = controller_.lastWeldTimeSec();
        if (opts.cycleTimeSec > opts.weldTimeSec) {
            opts.airTimeSec = opts.cycleTimeSec - opts.weldTimeSec;
        }
        for (const auto& seam : controller_.document().seams) {
            if (seam.sampledFrames.size() >= 2) {
                opts.totalWeldLengthMm += std::max(0.0, seam.sampledFrames.back().arcLengthMm - seam.sampledFrames.front().arcLengthMm);
            }
        }
        std::string err;
        if (controller_.exportWorkcellPdfReport(path.toStdString(), opts, &err)) {
            statusBar()->showMessage(QString::fromUtf8("已成功导出 PDF 工艺报告: %1").arg(path), 5000);
        } else {
            QMessageBox::warning(this, QString::fromUtf8("导出失败"), QString::fromStdString(err));
        }
    });
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
                "SiFastSuite OLP。右键自由旋转，中键移动平移，左键拾取选择，滚轮依光标缩放。"));
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
    pathAction_ = new QAction(commandIcon(IconKind::Path, pathInk), QString::fromUtf8("生成路径"), this);
    pathAction_->setIconText(QString::fromUtf8("路径"));
    pathAction_->setToolTip(QString::fromUtf8("由选中焊缝自动生成操作路径。"));
    connect(pathAction_, &QAction::triggered, this, [this] {
        if (controller_.generateOperationFromSelectedSeam()) {
            statusBar()->showMessage(QString::fromUtf8("已由焊缝生成焊接操作路径。"), 3000);
            refreshFromDocument();
        } else {
            statusBar()->showMessage(QString::fromUtf8("未能生成路径：未找到焊缝定义。"), 3000);
        }
    });

    slerpAction_ = new QAction(commandIcon(IconKind::Path, pathInk), QString::fromUtf8("姿态平滑"), this);
    slerpAction_->setIconText(QString::fromUtf8("平滑"));
    slerpAction_->setToolTip(QString::fromUtf8("对选中操作路径多点姿态执行 SLERP 球形线性插值平滑。"));
    connect(slerpAction_, &QAction::triggered, this, [this] {
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
    exportAction_ = new QAction(commandIcon(IconKind::Export, pathInk), QString::fromUtf8("导出"), this);
    exportAction_->setIconText(QString::fromUtf8("导出"));
    exportAction_->setToolTip(QString::fromUtf8("导出当前工位程序为 FANUC ASCII .ls 格式。"));
    connect(exportAction_, &QAction::triggered, this, [this] {
        LsExportDialog dlg(controller_, this);
        dlg.exec();
    });

    prevTargetAction_ = new QAction(QString::fromUtf8("上一目标点"), this);
    prevTargetAction_->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    prevTargetAction_->setToolTip(QString::fromUtf8("单步切换到上一个目标点 [快捷键: '[']"));
    connect(prevTargetAction_, &QAction::triggered, this, [this] {
        controller_.stepTarget(false);
    });
    addAction(prevTargetAction_);

    nextTargetAction_ = new QAction(QString::fromUtf8("下一目标点"), this);
    nextTargetAction_->setShortcut(QKeySequence(Qt::Key_BracketRight));
    nextTargetAction_->setToolTip(QString::fromUtf8("单步切换到下一个目标点 [快捷键: ']']"));
    connect(nextTargetAction_, &QAction::triggered, this, [this] {
        controller_.stepTarget(true);
    });
    addAction(nextTargetAction_);
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
        {pickAction, pathAction_, slerpAction_, simAction, exportAction_}));
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

    gizmoPreviewHandler_ = std::make_unique<GizmoDragPreviewHandler>(
        controller_, viewport_->tcpGizmo(), statusBar(), this);

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

    targetTable_ = new TargetWaypointTable(
        controller_,
        inItemChanged_,
        [this] { return isRefreshing(); },
        [this] { QTimer::singleShot(0, this, [this] { refreshFromDocument(); }); });
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
    propertyPanel_ = new TargetPropertyPanel(
        controller_,
        [this] { return selectedTargetIdsFromTable(); },
        [this] { refreshFromDocument(); });
    auto* propScroll = new QScrollArea();
    propScroll->setWidget(propertyPanel_);
    propScroll->setWidgetResizable(true);
    propScroll->setFrameShape(QFrame::NoFrame);
    propertyDock_ = makeDock(this, QString::fromUtf8("属性"), propScroll);

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
    auto* jogScroll = new QScrollArea();
    jogScroll->setWidget(jogBody);
    jogScroll->setWidgetResizable(true);
    jogScroll->setFrameShape(QFrame::NoFrame);
    jogDock_ = makeDock(this, QString::fromUtf8("关节点动"), jogScroll);

    issueList_ = new IssueListPanel();
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

    timeline_ = new TimelinePanel(controller_, this);
    timeline_->setViewportHooks(
        [this](const algo::BakedFrame& frame) {
            if (viewport_ == nullptr) {
                return;
            }
            viewport_->setPlaybackTcp(frame.tcpWorldPos, frame.arcActive);
            viewport_->setPlaybackRobotTransforms(frame.linkWorldTransforms);
            if (jogReadout_ == nullptr) {
                return;
            }
            QStringList parts;
            for (int joint = 0; joint < 6; ++joint) {
                parts.push_back(QString("J%1 %2°").arg(joint + 1).arg(
                    core::rad2deg(frame.jointsRad[static_cast<size_t>(joint)]), 0, 'f', 1));
            }
            jogReadout_->setText(QString::fromUtf8("仿真（未写入程序）：") + parts.join("  "));
        },
        [this](const std::vector<core::Vec3>& trail) {
            if (viewport_ != nullptr) {
                viewport_->setPlaybackTrail(trail);
            }
        },
        [this] {
            if (viewport_ != nullptr) {
                viewport_->clearPlaybackPreview();
            }
        });
    auto* timelineBar = new QToolBar(QString::fromUtf8("时间轴"));
    timelineBar->addWidget(timeline_);
    addToolBar(Qt::BottomToolBarArea, timelineBar);

    resizeDocks({sceneDock_, programDock_}, {280, 280}, Qt::Horizontal);
    resizeDocks({propertyDock_, jogDock_}, {280, 220}, Qt::Vertical);
    updateActionStates();
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

void ShellWindow::openProjectFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("打开工程文件"),
        QString(),
        QStringLiteral("JSON 工程文件 (*.json);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::string err;
    const bool ok = controller_.openProject(path.toStdString(), &err);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        statusBar()->showMessage(QString::fromUtf8("打开工程失败: ") + QString::fromStdString(err));
        return;
    }
    if (!controller_.document().controllers.empty()) {
        showOlpWorkbench();
    }
    refreshFromDocument();
    if (viewport_ != nullptr) {
        viewport_->syncScene();
    }
    statusBar()->showMessage(QString::fromUtf8("工程已打开: ") + path);
}

void ShellWindow::saveProjectFile() {
    const QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("保存工程文件"),
        QStringLiteral("project.json"),
        QStringLiteral("JSON 工程文件 (*.json);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::string err;
    const bool ok = controller_.saveProject(path.toStdString(), &err);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        statusBar()->showMessage(QString::fromUtf8("保存工程失败: ") + QString::fromStdString(err));
        return;
    }
    statusBar()->showMessage(QString::fromUtf8("工程已保存: ") + path);
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
    updateActionStates();
    if (timeline_ != nullptr) {
        timeline_->invalidate();
    }
    if (viewport_ != nullptr) {
        const auto& doc = controller_.document();
        const bool sceneChanged = (doc.sceneNodes.size() != lastSceneNodeCount_ || doc.seams.size() != lastSeamCount_);
        lastSceneNodeCount_ = doc.sceneNodes.size();
        lastSeamCount_ = doc.seams.size();

        if (sceneChanged) {
            viewport_->syncScene();
        } else {
            for (const auto& node : doc.sceneNodes) {
                if (node.type == core::SceneNodeType::Robot) {
                    viewport_->syncRobotPosture(node.currentJointsRad);
                    break;
                }
            }
        }
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
    targetTable_->rebuild();
}

void ShellWindow::showPropertiesForScene(const QString& nodeId) {
    propertyPanel_->showSceneNode(nodeId);
}

void ShellWindow::showPropertiesForTarget(const QString& targetId) {
    propertyPanel_->showTarget(targetId);
}


void ShellWindow::updateUndoActions() {
    if (undoAction_ == nullptr) {
        return;
    }
    undoAction_->setEnabled(controller_.commandManager().canUndo());
    redoAction_->setEnabled(controller_.commandManager().canRedo());
}

void ShellWindow::updateActionStates() {
    updateUndoActions();

    const auto& doc = controller_.document();
    bool hasProgramWithTargets = false;
    size_t totalTargets = 0;
    for (const auto& cu : doc.controllers) {
        for (const auto& prog : cu.programs) {
            for (const auto& op : prog.operations) {
                totalTargets += op.targets.size();
                if (!op.targets.empty()) {
                    hasProgramWithTargets = true;
                }
            }
        }
    }
    const bool hasSeams = !doc.seams.empty();
    const bool hasScene = !doc.sceneNodes.empty();

    if (exportMenuAction_ != nullptr) {
        exportMenuAction_->setEnabled(hasProgramWithTargets);
    }
    if (exportAction_ != nullptr) {
        exportAction_->setEnabled(hasProgramWithTargets);
    }
    if (exportPdfAction_ != nullptr) {
        exportPdfAction_->setEnabled(hasScene || hasSeams);
    }
    if (pathAction_ != nullptr) {
        pathAction_->setEnabled(hasSeams);
    }
    if (slerpAction_ != nullptr) {
        slerpAction_->setEnabled(totalTargets >= 2);
    }
    if (prevTargetAction_ != nullptr) {
        prevTargetAction_->setEnabled(totalTargets > 0);
    }
    if (nextTargetAction_ != nullptr) {
        nextTargetAction_->setEnabled(totalTargets > 0);
    }
}

void ShellWindow::fillIssues() {
    issueList_->refill(controller_);
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
                propertyPanel_->showOperationSummary(op.name, static_cast<int>(op.targets.size()));
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
    if (targetTable_ == nullptr) {
        return {};
    }
    return targetTable_->selectedTargetIds();
}


} // namespace olp::ui

