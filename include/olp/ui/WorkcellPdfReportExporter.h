#pragma once

#include "olp/core/MathTypes.h"
#include "olp/core/ProjectModel.h"
#include <QColor>
#include <QImage>
#include <string>
#include <vector>

namespace olp::app {
class ProjectDocument;
}

namespace olp::ui {

struct PdfReportOptions {
    std::string documentTitle{"工业机器人焊接离线编程工艺指导书 (SOP)"};
    std::string projectCode{"FS-WELD-2026"};
    std::string operatorName{"SiFastSuite OLP"};
    std::string companyName{"SiFastSuite Industrial Robotics"};
    std::string revision{"Rev 1.0"};
    bool includeViewportSnapshot{true};
    QImage snapshotImage{};
    double cycleTimeSec{0.0};
    double weldTimeSec{0.0};
    double airTimeSec{0.0};
    double totalWeldLengthMm{0.0};
};

struct ExtractedReportMetadata {
    std::string programName{"无程序"};
    std::string robotDisplayName{"未配置机器人"};
    std::string controllerDisplayName{"未配置控制器"};
    int opCount{0};
    int targetCount{0};
    int sceneNodeCount{0};
    int seamCount{0};

    bool hasUtool{false};
    std::string utoolName{"未标定"};
    core::WprPose utoolWpr{};

    bool hasUframe{false};
    std::string uframeName{"未标定"};
    core::WprPose uframeWpr{};

    bool hasRobotBase{false};
    std::string robotBaseName{"未配置"};
    core::WprPose robotBaseWpr{};

    bool isSimulated{false};
    double totalCycleTimeSec{0.0};
    double weldTimeSec{0.0};
    double airTimeSec{0.0};
    double dutyCyclePercent{0.0};
    double totalWeldLengthMm{0.0};
};

// M13: Industrial Workcell 2D PDF SOP Report Exporter
class WorkcellPdfReportExporter {
public:
    // The target table supports signed Cartesian coordinates through +/-9999.9 mm.
    static constexpr int kTargetXyzColumnWidth = 850;
    static constexpr int kTargetStatusColumnStart = 1830;
    static constexpr int kTargetWprColumnStart = 12;
    static constexpr int kTargetWprColumnWidth = 730;
    static constexpr int kTargetConfigColumnStart = 746;
    static constexpr int kTargetConfigColumnWidth = 580;
    static constexpr int kTargetEventColumnStart = 1330;

    static ExtractedReportMetadata extractMetadata(
        const app::ProjectDocument& doc,
        const PdfReportOptions& options);

    static std::string formatTargetPosition(double xMm, double yMm, double zMm);
    static std::string formatTargetOrientation(double wDeg, double pDeg, double rDeg);
    static std::string formatSeamSummary(int seamCount, double lengthMm);
    static std::string formatTargetStatusAndClearance(const core::Target& t);
    static QColor getTargetStatusColor(core::TargetValidationState status);

    static bool exportToPdf(
        const app::ProjectDocument& doc,
        const std::string& outputFilePath,
        const PdfReportOptions& options = {},
        std::string* errorOut = nullptr);
};

} // namespace olp::ui
