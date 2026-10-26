#include "olp/ui/MainWindow.h"
#include "olp/ui/SampleSession.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef OLP_HAS_QT6
#include "olp/ui/ShellWindow.h"
#include <QApplication>
#include <QTimer>
#endif

namespace {

void hideConsoleWindow() {
#ifdef _WIN32
    HWND console = GetConsoleWindow();
    if (console == nullptr) {
        return;
    }
    DWORD ownerPid = 0;
    GetWindowThreadProcessId(console, &ownerPid);
    if (ownerPid == GetCurrentProcessId()) {
        ShowWindow(console, SW_HIDE);
    }
#else
    (void)0;
#endif
}

} // namespace

int main(int argc, char* argv[]) {
    bool verify = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--verify") {
            verify = true;
        }
    }

#ifndef OLP_HAS_QT6
    verify = true;
#endif

    if (verify) {
        olp::ui::MainWindow window;
        return olp::ui::runSampleWorkcell(window).exitCode;
    }
    hideConsoleWindow();

#ifdef OLP_HAS_QT6
    QApplication app(argc, argv);
    app.setApplicationName("SiFastSuite OLP");
    app.setOrganizationName("SiFastSuite");
    olp::ui::MainWindow window;
    olp::ui::ShellWindow shell(window);
    shell.setSampleLoader([&window] { olp::ui::runSampleWorkcell(window); });
    shell.show();
    shell.raise();
    shell.activateWindow();
    QTimer::singleShot(0, &shell, &olp::ui::ShellWindow::loadSample);
    return app.exec();
#else
    return 0;
#endif
}
