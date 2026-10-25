#include "olp/ui/MainWindow.h"

namespace olp::ui {

MainWindow::MainWindow()
    : viewport_(doc_, cmdMgr_) {}

MainWindow::~MainWindow() = default;

} // namespace olp::ui
