#include "olp/ui/ShellWindow.h"
#include "olp/app/WorkcellLayoutService.h"
#include "olp/ui/GizmoDragPreviewHandler.h"
#include "olp/ui/IssueListPanel.h"
#include "olp/ui/TargetPropertyPanel.h"
#include "olp/ui/TargetWaypointTable.h"
#include "olp/ui/TimelinePanel.h"
#include "ShellChromeHelpers.h"
#include "ShellLabels.h"
#include "olp/ui/OcctViewport.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>

#include <cmath>
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
    statusBar()->showMessage(QString::fromUtf8("布局工作台。场景、属性和点动保持打开。用工具栏加载示例工位或导入 STEP。"));
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
    if (!confirmDiscardIfDirty()) {
        return;
    }
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

void ShellWindow::setConfirmDiscardCallback(ConfirmDiscardCallback callback) {
    confirmDiscardCallback_ = std::move(callback);
}

bool ShellWindow::confirmDiscardIfDirty() {
    if (!controller_.document().isDirty) {
        return true;
    }
    if (confirmDiscardCallback_) {
        return confirmDiscardCallback_();
    }
    const auto btn = QMessageBox::question(
        this,
        QString::fromUtf8("未保存的修改"),
        QString::fromUtf8("当前工程有未保存的修改。如果继续，这些修改将会丢失。\n是否放弃未保存的修改？"),
        QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel);
    return btn == QMessageBox::Discard;
}

void ShellWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscardIfDirty()) {
        event->ignore();
        return;
    }
    event->accept();
    QMainWindow::closeEvent(event);
}

void ShellWindow::importStepFile() {
    importStepFile(QString());
}

void ShellWindow::importStepFile(const QString& explicitPath) {
    if (!confirmDiscardIfDirty()) {
        return;
    }
    const QString path = explicitPath.isEmpty()
        ? QFileDialog::getOpenFileName(
              this, QString::fromUtf8("导入 STEP 文件"),
              QString(),
              QStringLiteral("STEP 文件 (*.step *.stp);;所有文件 (*.*)"))
        : explicitPath;
    if (path.isEmpty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    app::SceneRobotReplaceResult repRes;
    const bool ok = controller_.importStepWorkcell(path.toStdString(), &repRes);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        statusBar()->showMessage(QString::fromUtf8("导入 STEP 失败: ") + path);
        return;
    }
    refreshFromDocument();
    if (viewport_ != nullptr) {
        viewport_->syncScene();
        viewport_->fitAll();
    }
    statusBar()->showMessage(QString::fromUtf8("已成功导入 STEP 工位 (%1 个实体): %2")
        .arg(QString::number(repRes.totalSolidsImported), path));
}

void ShellWindow::openProjectFile() {
    openProjectFile(QString());
}

void ShellWindow::openProjectFile(const QString& explicitPath) {
    if (!confirmDiscardIfDirty()) {
        return;
    }
    const QString path = explicitPath.isEmpty()
        ? QFileDialog::getOpenFileName(
              this, QString::fromUtf8("打开工程文件"),
              QString(),
              QStringLiteral("JSON 工程文件 (*.json);;所有文件 (*.*)"))
        : explicitPath;
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
        std::string sceneFingerprint;
        sceneFingerprint.reserve(doc.sceneNodes.size() * 24 + doc.seams.size() * 16);
        for (const auto& node : doc.sceneNodes) {
            sceneFingerprint += node.id;
            sceneFingerprint += ':';
            sceneFingerprint += std::to_string(node.cadHandleId);
            sceneFingerprint += ';';
        }
        for (const auto& seam : doc.seams) {
            sceneFingerprint += seam.id;
            sceneFingerprint += ';';
        }

        const bool sceneChanged = (sceneFingerprint != lastSceneFingerprint_);
        lastSceneFingerprint_ = std::move(sceneFingerprint);
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
            auto* seamItem = new QTreeWidgetItem(seams, {
                QString::fromStdString(seam.name),
                QString::fromUtf8("起点后移 %1 mm").arg(seam.startTrimMm, 0, 'f', 0)
            });
            seamItem->setData(0, Qt::UserRole, "seam");
            seamItem->setData(0, Qt::UserRole + 1, QString::fromStdString(seam.id));
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
    if (exportHtmlAction_ != nullptr) {
        exportHtmlAction_->setEnabled(hasProgramWithTargets);
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
