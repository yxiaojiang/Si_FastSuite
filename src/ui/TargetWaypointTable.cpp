#include "olp/ui/TargetWaypointTable.h"

#include "olp/ui/ShellWindow.h"
#include "ShellLabels.h"

#include <QHeaderView>
#include <QMainWindow>
#include <QStatusBar>
#include <QTableWidgetItem>
#include <QTimer>

#include <algorithm>
#include <set>

namespace olp::ui {

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


TargetWaypointTable::TargetWaypointTable(MainWindow& controller,
                                         bool& itemChangeFlag,
                                         std::function<bool()> suppressEdits,
                                         std::function<void()> refreshLater,
                                         QWidget* parent)
    : QTableWidget(0, 7, parent),
      controller_(controller),
      itemChangeFlag_(itemChangeFlag),
      suppressEdits_(std::move(suppressEdits)),
      refreshLater_(std::move(refreshLater)) {
    setHorizontalHeaderLabels({
        QString::fromUtf8("名称"),
        QString::fromUtf8("运动"),
        QString::fromUtf8("速度"),
        QString::fromUtf8("平滑度"),
        QString::fromUtf8("工艺事件"),
        QString::fromUtf8("状态"),
        QString::fromUtf8("间隙 mm")
    });
    horizontalHeader()->setStretchLastSection(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    connect(this, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* changed) {
        onItemChanged(changed);
    });
}

void TargetWaypointTable::rebuild() {
    this->setRowCount(0);
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
        this->insertRow(row);
        auto* name = new QTableWidgetItem(QString::fromStdString(target.name));
        name->setData(Qt::UserRole, QString::fromStdString(target.id));
        name->setData(Qt::UserRole + 1, QString::fromStdString(operation->id));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        this->setItem(row, 0, name);

        this->setItem(row, 1, new QTableWidgetItem(motionText(target.motion)));

        this->setItem(row, 2, new QTableWidgetItem(formatSpeedCellText(target)));

        const QString term = (target.termination == core::TerminationType::FINE)
            ? QString("FINE")
            : QString("CNT %1").arg(target.cntValue);
        this->setItem(row, 3, new QTableWidgetItem(term));

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
        this->setItem(row, 4, new QTableWidgetItem(evStr));

        auto* statusItem = new QTableWidgetItem(statusText(target.cache.status));
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
        this->setItem(row, 5, statusItem);

        auto* clearanceItem = new QTableWidgetItem(
            QString::number(target.cache.minClearanceMm, 'f', 1));
        clearanceItem->setFlags(clearanceItem->flags() & ~Qt::ItemIsEditable);
        this->setItem(row, 6, clearanceItem);

        ++row;
    }
}

void TargetWaypointTable::onItemChanged(QTableWidgetItem* item) {
        if (item == nullptr || suppressEdits_()) {
            return;
        }
        const int row = item->row();
        const int col = item->column();
        auto* nameItem = this->item(row, 0);
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
                refreshLater_();
                return;
            }
        } else if (col == 2) { // 速度: e.g. "200", "200 mm/s", "50%", "50 cm/min", "WELD_SPEED (80 mm/s)"
            const auto parsed = parseSpeedCellText(text, target->speedValue);
            if (parsed.valid) {
                params.speedValue = parsed.speedValue;
                params.useWeldSpeed = parsed.useWeldSpeed;
            } else {
                refreshLater_();
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
                    refreshLater_();
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
                refreshLater_();
                return;
            }
        } else {
            return;
        }

        struct ItemChangeScope {
            bool& flag;
            explicit ItemChangeScope(bool& f) : flag(f) { flag = true; }
            ~ItemChangeScope() { flag = false; }
        } itemScope(itemChangeFlag_);

        if (controller_.modifyTargetProcessParams(targetId, params)) {
            showStatus(QString::fromUtf8("已更新目标点工艺参数。"), 3000);
        } else {
            refreshLater_();
        }
}

void TargetWaypointTable::showStatus(const QString& message, int timeoutMs) {
    if (auto* mainWindow = qobject_cast<QMainWindow*>(window())) {
        mainWindow->statusBar()->showMessage(message, timeoutMs);
    }
}

std::vector<std::string> TargetWaypointTable::selectedTargetIds() const {
    std::vector<std::string> ids;

    std::set<int> rowSet;
    const auto selectedIndexes = this->selectionModel() ? this->selectionModel()->selectedIndexes() : QModelIndexList();
    for (const auto& idx : selectedIndexes) {
        rowSet.insert(idx.row());
    }

    for (int r : rowSet) {
        auto* item = this->item(r, 0);
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
