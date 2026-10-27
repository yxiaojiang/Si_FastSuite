#pragma once

#include "olp/ui/MainWindow.h"

#include <QTableWidget>

#include <functional>
#include <string>
#include <vector>

class QTableWidgetItem;

namespace olp::ui {

class TargetWaypointTable : public QTableWidget {
public:
    TargetWaypointTable(MainWindow& controller,
                        bool& itemChangeFlag,
                        std::function<bool()> suppressEdits,
                        std::function<void()> refreshLater,
                        QWidget* parent = nullptr);

    void rebuild();
    std::vector<std::string> selectedTargetIds() const;

private:
    void onItemChanged(QTableWidgetItem* changed);
    void showStatus(const QString& message, int timeoutMs);

    MainWindow& controller_;
    bool& itemChangeFlag_;
    std::function<bool()> suppressEdits_;
    std::function<void()> refreshLater_;
};

} // namespace olp::ui
