#include "olp/ui/IssueListPanel.h"

#include <QColor>
#include <QListWidgetItem>

#include <set>
#include <string>
#include <utility>

namespace olp::ui {

IssueListPanel::IssueListPanel(QWidget* parent) : QListWidget(parent) {}

void IssueListPanel::refill(MainWindow& controller) {
    this->clear();
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
        this->addItem(item);
    };

    // 1. SimulationResult issues (includes segment no-inverse, sweep collision, sweep clearance, ConfigJump)
    for (const auto& issue : controller.lastSimulationResult().issues) {
        const auto* op = controller.document().findOperation(issue.operationId);
        if (op == nullptr) {
            continue;
        }
        const auto* t = controller.document().findTarget(issue.targetId);
        if (t == nullptr) {
            continue;
        }
        std::string opName = op->name.empty() ? issue.operationId : op->name;
        std::string tName = issue.targetName.empty() ? t->name : issue.targetName;
        addIssueItem(issue.operationId, issue.targetId, opName, tName, issue.state, issue.description);
    }

    // 2. Direct TargetCache issues from Document
    for (const auto& cu : controller.document().controllers) {
        for (const auto& program : cu.programs) {
            for (const auto& op : program.operations) {
                for (const auto& target : op.targets) {
                    addIssueItem(op.id, target.id, op.name, target.name, target.cache.status, target.cache.issueMessage);
                }
            }
        }
    }

    if (this->count() == 0) {
        auto* cleanItem = new QListWidgetItem(QString::fromUtf8("未发现干涉或运动学异常 (全部正常)"));
        cleanItem->setForeground(QColor(50, 160, 80));
        cleanItem->setFlags(cleanItem->flags() & ~Qt::ItemIsSelectable);
        this->addItem(cleanItem);
    }
}

} // namespace olp::ui
