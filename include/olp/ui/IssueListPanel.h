#pragma once

#include "olp/ui/MainWindow.h"

#include <QListWidget>

namespace olp::ui {

class IssueListPanel : public QListWidget {
public:
    explicit IssueListPanel(QWidget* parent = nullptr);
    void refill(MainWindow& controller);
};

} // namespace olp::ui
