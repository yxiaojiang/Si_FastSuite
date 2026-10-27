#include "test_support.h"
#include "olp/app/CommandManager.h"
#include "olp/ui/MainWindow.h"
#include "olp/ui/SampleSession.h"
#include "olp/ui/WorkcellPdfReportExporter.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QStringList>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace olp;

void testWorkcellPdfReportExport() {
    std::cout << "\n--- Test 20: M13 Industrial Workcell 2D PDF Report Generation (QPdfWriter + QPainter) ---\n";

    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "sifastsuite_test_report_round2";
    std::filesystem::create_directories(tempDir);

    // =========================================================================
    // 1. M13-001: Font metrics vs Row Heights & Table Column Width Budget
    //    Bound directly to QPdfWriter (300 DPI)
    // =========================================================================
    {
        std::cout << "  [Check M13-001] Validating 300 DPI font metrics on QPdfWriter\n";

        const std::string dummyPdf = (tempDir / "font_metrics_test.pdf").string();
        QPdfWriter writer(QString::fromStdString(dummyPdf));
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageOrientation(QPageLayout::Portrait);
        writer.setResolution(300);
        writer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);

        const QRect paintRect = writer.pageLayout().paintRectPixels(writer.resolution());
        const int W = paintRect.width();
        const int H = paintRect.height();

        check(W >= 2200 && W <= 2300, "M13-001: Printable width at 300 DPI is ~2243px", std::to_string(W));
        check(H >= 3200 && H <= 3350, "M13-001: Printable height at 300 DPI is ~3272px", std::to_string(H));

        // Font metric checks bound to 300 DPI QPdfWriter paint device
        QFont f8("Arial", 8);
        QFontMetrics fm8(f8, &writer);
        const int h8 = fm8.height();

        QFont f8b("Arial", 8, QFont::Bold);
        QFontMetrics fm8b(f8b, &writer);
        const int h8b = fm8b.height();

        QFont f9("Arial", 9);
        QFontMetrics fm9(f9, &writer);
        const int h9 = fm9.height();

        QFont f9b("Arial", 9, QFont::Bold);
        QFontMetrics fm9b(f9b, &writer);
        const int h9b = fm9b.height();

        QFont f11b("Arial", 11, QFont::Bold);
        QFontMetrics fm11b(f11b, &writer);
        const int h11b = fm11b.height();

        QFont f12b("Arial", 12, QFont::Bold);
        QFontMetrics fm12b(f12b, &writer);
        const int h12b = fm12b.height();

        QFont f13b("Arial", 13, QFont::Bold);
        QFontMetrics fm13b(f13b, &writer);
        const int h13b = fm13b.height();

        QFont f18b("Arial", 18, QFont::Bold);
        QFontMetrics fm18b(f18b, &writer);
        const int h18b = fm18b.height();

        // 1. Table sub-rows: 8pt regular and 8pt bold font heights (33px) fit within 36px sub-row rect
        const int subRowH = 36;
        check(h8 <= subRowH, "M13-001: 8pt font height fits within 36px table sub-row rect",
              "actual font height=" + std::to_string(h8) + ", subRowH=" + std::to_string(subRowH));
        check(h8b <= subRowH, "M13-001: 8pt bold font height fits within 36px table sub-row rect",
              "actual font height=" + std::to_string(h8b) + ", subRowH=" + std::to_string(subRowH));

        // 2. Section title: 11pt bold font height (46px) fits in 52px title rect
        check(h11b <= 52, "M13-001: 11pt bold font height fits within 52px section title rect",
              "actual font height=" + std::to_string(h11b));

        // 3. Page header: 12pt bold font height (50px) fits in 65px header rect
        check(h12b < 65, "M13-001: 12pt bold font height fits within 65px header bar",
              "actual font height=" + std::to_string(h12b));

        // 4. Metadata key: 8pt font height (33px) fits in 38px key rect
        check(h8 < 38, "M13-001: 8pt metadata key font height fits within 38px key rect",
              "actual font height=" + std::to_string(h8));

        // 5. Metadata value: 9pt bold font height (38px) fits in 40px value rect
        check(h9b <= 40, "M13-001: 9pt bold metadata value font height fits within 40px value rect",
              "actual font height=" + std::to_string(h9b));

        // 6. Subtitle: 9pt font height (38px) fits in 45px subtitle rect
        check(h9 < 45, "M13-001: 9pt subtitle font height fits within 45px subtitle rect",
              "actual font height=" + std::to_string(h9));

        // 7. Metric card label: 9pt font height (38px) fits in 42px label rect
        check(h9 < 42, "M13-001: 9pt metric card label font height fits within 42px label rect",
              "actual font height=" + std::to_string(h9));

        // 8. Metric card value: 13pt bold font height (54px) fits in 58px value rect
        check(h13b < 58, "M13-001: 13pt bold metric card value font height fits within 58px value rect",
              "actual font height=" + std::to_string(h13b));

        // 9. Document title: 18pt bold font height (75px) fits in 85px title rect
        check(h18b < 85, "M13-001: 18pt bold title font height fits within 85px title rect",
              "actual font height=" + std::to_string(h18b));

        // Column width vs horizontal advance checks on 300 DPI device
        // M13-001 (Round 4): Assert all strings identified in REVIEW.md fit strictly within their drawing rects
        // 1. Page Header company name (12pt bold) in 1600px rect (prev 700px)
        const int advHeaderLogo = fm12b.horizontalAdvance(QStringLiteral("SiFastSuite Industrial Robotics"));
        check(advHeaderLogo <= 1600, "M13-001: 'SiFastSuite Industrial Robotics' advance fits in header rect (1600px)",
              "actual=" + std::to_string(advHeaderLogo));

        // 2. Robot model name (9pt bold) in 1156px rect (cw=1180 - 24, prev 536px)
        const int advRobotName = fm9b.horizontalAdvance(QStringLiteral("FANUC M-10iA / ARC Mate 100iC"));
        check(advRobotName <= 1156, "M13-001: 'FANUC M-10iA / ARC Mate 100iC' advance fits in robot cell (1156px)",
              "actual=" + std::to_string(advRobotName));

        // 3. Target WPR values use the renderer's formatter and fit the rebalanced sub-row.
        const QString standardWpr = QString::fromStdString(
            ui::WorkcellPdfReportExporter::formatTargetOrientation(-180.0, -90.0, 180.0));
        const int advWprVal = fm8.horizontalAdvance(standardWpr);
        check(advWprVal <= ui::WorkcellPdfReportExporter::kTargetWprColumnWidth,
              "M13-001: standard WPR value fits rebalanced WPR column",
              "actual=" + std::to_string(advWprVal));

        // 4. Target Motion / Speed CNT (8pt regular) in 540px rect (prev 195px)
        const int advMotionCnt = fm8.horizontalAdvance(QStringLiteral("L 200mm/s CNT100"));
        check(advMotionCnt <= 540, "M13-001: 'L 200mm/s CNT100' advance fits in motion rect (540px)",
              "actual=" + std::to_string(advMotionCnt));

        // 5. Target Motion / Speed FINE (8pt regular) in 540px rect (prev 195px)
        const int advMotionFine = fm8.horizontalAdvance(QStringLiteral("L WELD_SPD FINE"));
        check(advMotionFine <= 540, "M13-001: 'L WELD_SPD FINE' advance fits in motion rect (540px)",
              "actual=" + std::to_string(advMotionFine));

        // 6. Target solved config (8pt regular) in the rebalanced config column (580px)
        const int advConfigVal = fm8.horizontalAdvance(QStringLiteral("N U T, 0, 0, 0"));
        check(advConfigVal <= ui::WorkcellPdfReportExporter::kTargetConfigColumnWidth,
              "M13-001: 'N U T, 0, 0, 0' advance fits in config sub-row rect (580px)",
              "actual=" + std::to_string(advConfigVal));

        // 7. Schedule wire feed header (8pt bold) in 400px col (prev 240px)
        const int advWireFeed = fm8b.horizontalAdvance(QStringLiteral("送丝速度 (m/min)"));
        check(advWireFeed <= 400, "M13-001: '送丝速度 (m/min)' advance fits in sched col 4 (400px)",
              "actual=" + std::to_string(advWireFeed));

        // 8. Schedule gas flow header (8pt bold) in 400px col (prev 240px)
        const int advGasFlow = fm8b.horizontalAdvance(QStringLiteral("保护气体 (L/min)"));
        check(advGasFlow <= 400, "M13-001: '保护气体 (L/min)' advance fits in sched col 6 (400px)",
              "actual=" + std::to_string(advGasFlow));

        // 9. Target WPR header (8pt bold) in the rebalanced WPR column.
        const int advWprHdr = fm8b.horizontalAdvance(QStringLiteral("姿态 WPR (deg)"));
        check(advWprHdr <= ui::WorkcellPdfReportExporter::kTargetWprColumnWidth,
              "M13-001: WPR header fits rebalanced WPR column",
              "actual=" + std::to_string(advWprHdr));

        // 10. Schedule weld speed header (8pt bold) in 380px col (prev 240px)
        const int advWeldSpd = fm8b.horizontalAdvance(QStringLiteral("焊接速度 (mm/s)"));
        check(advWeldSpd <= 380, "M13-001: '焊接速度 (mm/s)' advance fits in sched col 5 (380px)",
              "actual=" + std::to_string(advWeldSpd));

        // 11. Target Config header (8pt bold) in the rebalanced config column.
        const int advConfigHdr = fm8b.horizontalAdvance(QStringLiteral("构型 CONFIG"));
        check(advConfigHdr <= ui::WorkcellPdfReportExporter::kTargetConfigColumnWidth,
              "M13-001: config header fits rebalanced config column",
              "actual=" + std::to_string(advConfigHdr));

        // 12. Target XYZ position header (8pt bold) in the 850px Cartesian column.
        const int advCol4 = fm8b.horizontalAdvance(QStringLiteral("笛卡尔位置 (X, Y, Z mm)"));
        check(advCol4 <= ui::WorkcellPdfReportExporter::kTargetXyzColumnWidth,
              "M13-001: XYZ header fits Cartesian column",
              "actual=" + std::to_string(advCol4));
        const QString standardXyz = QString::fromStdString(
            ui::WorkcellPdfReportExporter::formatTargetPosition(-180.0, -90.0, 180.0));
        const int advXyzVal = fm8.horizontalAdvance(standardXyz);
        check(advXyzVal <= ui::WorkcellPdfReportExporter::kTargetXyzColumnWidth,
              "M13-001: standard XYZ value fits Cartesian column",
              "actual=" + std::to_string(advXyzVal));

        // 13. Target status text (8pt regular) in (W - 1640)px rect (~603px, prev 305px)
        const int targetStatusColW = W - ui::WorkcellPdfReportExporter::kTargetStatusColumnStart;
        const int advCol7 = fm8.horizontalAdvance(QStringLiteral("超关节限位 (-)"));
        check(advCol7 <= targetStatusColW, "M13-001: '超关节限位 (-)' fits rebalanced status rect (" + std::to_string(targetStatusColW) + "px)",
              "actual=" + std::to_string(advCol7));

        // 14. Target process events (8pt regular) in (W - 1340)px rect (~903px, prev 423px)
        const int targetEventsColW = W - 1340;
        const int advCol8 = fm8.horizontalAdvance(QStringLiteral("起弧[1] 熄弧[1]"));
        check(advCol8 <= targetEventsColW, "M13-001: '起弧[1] 熄弧[1]' advance fits in events rect (" + std::to_string(targetEventsColW) + "px)",
              "actual=" + std::to_string(advCol8));

        // 15. Schedule descriptions (8pt regular) in (W - 24)px sub-row 2 rect (~2219px, prev 353px)
        const int schedDescColW = W - 24;
        const int advSchedDescSample = fm8.horizontalAdvance(QStringLiteral("Schedule 1: 180A, 22V, 50 cm/min"));
        check(advSchedDescSample <= schedDescColW,
              "M13-001: 'Schedule 1: 180A, 22V, 50 cm/min' advance fits in schedule description rect (" + std::to_string(schedDescColW) + "px)",
              "actual=" + std::to_string(advSchedDescSample));
        const int advSchedDescDefault = fm8.horizontalAdvance(QStringLiteral("Standard MAG Mild Steel"));
        check(advSchedDescDefault <= schedDescColW,
              "M13-001: 'Standard MAG Mild Steel' advance fits in schedule description rect (" + std::to_string(schedDescColW) + "px)",
              "actual=" + std::to_string(advSchedDescDefault));

        // 16. Coordinate frame origins (8pt regular) in 1140px sub-row 2 rect (prev 700px)
        const int advUframeOrigin = fm8.horizontalAdvance(QStringLiteral("X=500.00, Y=-300.00, Z=600.00 mm"));
        check(advUframeOrigin <= 1140,
              "M13-001: 'X=500.00, Y=-300.00, Z=600.00 mm' advance fits in frame origin rect (1140px)",
              "actual=" + std::to_string(advUframeOrigin));
        const int advUtoolOrigin = fm8.horizontalAdvance(QStringLiteral("X=0.00, Y=0.00, Z=150.00 mm"));
        check(advUtoolOrigin <= 1140,
              "M13-001: 'X=0.00, Y=0.00, Z=150.00 mm' advance fits in frame origin rect (1140px)",
              "actual=" + std::to_string(advUtoolOrigin));

        // 17. Coordinate frame rotation (8pt regular) in (W - 1186)px sub-row 2 rect (~1057px, prev 767px)
        const int frameRotColW = W - 1186;
        const int advFrameRot = fm8.horizontalAdvance(QStringLiteral("W=0.00, P=0.00, R=0.00 deg"));
        check(advFrameRot <= frameRotColW,
              "M13-001: 'W=0.00, P=0.00, R=0.00 deg' advance fits in frame rotation rect (" + std::to_string(frameRotColW) + "px)",
              "actual=" + std::to_string(advFrameRot));

        // 18. Tool name and robot base description (8pt regular) in (W - 346)px sub-row 1 rect (~1897px, prev 460px)
        const int frameDescColW = W - 346;
        const int advToolName = fm8.horizontalAdvance(QStringLiteral("Welding_Torch_150mm"));
        check(advToolName <= frameDescColW,
              "M13-001: 'Welding_Torch_150mm' advance fits in frame description rect (" + std::to_string(frameDescColW) + "px)",
              "actual=" + std::to_string(advToolName));
        const int advBaseDesc = fm8.horizontalAdvance(QStringLiteral("FANUC M-10iA / ARC Mate 100iC 底座安装基准"));
        check(advBaseDesc <= frameDescColW,
              "M13-001: 'FANUC M-10iA / ARC Mate 100iC 底座安装基准' advance fits in frame description rect (" + std::to_string(frameDescColW) + "px)",
              "actual=" + std::to_string(advBaseDesc));

        // 19. Overview grid controller model (9pt bold) in (W - 1580 - 24)px rect (~639px, prev 599px)
        const int controllerCellW = (W - 1580) - 24;
        const int advControllerName = fm9b.horizontalAdvance(QStringLiteral("FANUC_R30iB_Plus"));
        check(advControllerName <= controllerCellW,
              "M13-001: 'FANUC_R30iB_Plus' advance fits in controller cell (" + std::to_string(controllerCellW) + "px)",
              "actual=" + std::to_string(advControllerName));

        // 20. Overview grid revision & operator (9pt bold) in (W - 1240 - 24)px rect (~979px, prev 619px)
        const int revOperatorCellW = (W - 1240) - 24;
        const int advRevOperator = fm9b.horizontalAdvance(QStringLiteral("Rev 1.0 · SiFastSuite OLP"));
        check(advRevOperator <= revOperatorCellW,
              "M13-001: 'Rev 1.0 · SiFastSuite OLP' advance fits in revision/operator cell (" + std::to_string(revOperatorCellW) + "px)",
              "actual=" + std::to_string(advRevOperator));

        // 21. Target non-zero turn solved config (8pt regular) in the rebalanced config column.
        const int advConfigTurns = fm8.horizontalAdvance(QStringLiteral("F D B, -1, -1, -1"));
        check(advConfigTurns <= ui::WorkcellPdfReportExporter::kTargetConfigColumnWidth,
              "M13-001: maximum config value fits rebalanced config column",
              "actual=" + std::to_string(advConfigTurns));

        // 22. Schedule current and voltage headers (8pt bold) in 300px cols (prev 240px)
        const int advCurrentHdr = fm8b.horizontalAdvance(QStringLiteral("焊接电流 (A)"));
        check(advCurrentHdr <= 300,
              "M13-001: '焊接电流 (A)' advance fits in sched col 2 (300px)",
              "actual=" + std::to_string(advCurrentHdr));
        const int advVoltageHdr = fm8b.horizontalAdvance(QStringLiteral("电弧电压 (V)"));
        check(advVoltageHdr <= 300,
              "M13-001: '电弧电压 (V)' advance fits in sched col 3 (300px)",
              "actual=" + std::to_string(advVoltageHdr));

        // 23. Round 5 review follow-up: compact overview values retain their labels and fit unchanged cells.
        const int operationKeyCellW = 380 - 24;
        const int operationValueCellW = 380 - 24;
        const int sceneValueCellW = 320 - 24;
        const int seamValueCellW = 540 - 24;
        const int advOperationKey = fm8.horizontalAdvance(QStringLiteral("操作 / 点位:"));
        const int advOperationValue = fm9b.horizontalAdvance(QStringLiteral("1 / 40 点"));
        const int advSceneValue = fm9b.horizontalAdvance(QStringLiteral("16 实体"));
        const int advSeamValue = fm9b.horizontalAdvance(QString::fromStdString(
            ui::WorkcellPdfReportExporter::formatSeamSummary(1, 200.0)));
        check(advOperationKey <= operationKeyCellW, "M13-001: compact operation key fits overview cell",
              "actual=" + std::to_string(advOperationKey));
        check(advOperationValue <= operationValueCellW, "M13-001: compact operation value fits overview cell",
              "actual=" + std::to_string(advOperationValue));
        check(advSceneValue <= sceneValueCellW, "M13-001: compact scene value fits overview cell",
              "actual=" + std::to_string(advSceneValue));
        check(advSeamValue <= seamValueCellW, "M13-001: compact seam value fits overview cell",
              "actual=" + std::to_string(advSeamValue));

        // 24. The formatter used by the renderer must fit the supported +/-9999.9mm envelope.
        const QString fractionalXyz = QString::fromStdString(
            ui::WorkcellPdfReportExporter::formatTargetPosition(1000.5, -300.5, 600.5));
        const QString negativeBoundaryXyz = QString::fromStdString(
            ui::WorkcellPdfReportExporter::formatTargetPosition(-9999.9, -9999.9, -9999.9));
        check(fm8.horizontalAdvance(fractionalXyz) <= ui::WorkcellPdfReportExporter::kTargetXyzColumnWidth,
              "M13-001: fractional four-digit XYZ fits Cartesian column",
              "actual=" + std::to_string(fm8.horizontalAdvance(fractionalXyz)));
        check(fm8.horizontalAdvance(negativeBoundaryXyz) <= ui::WorkcellPdfReportExporter::kTargetXyzColumnWidth,
              "M13-001: supported negative XYZ boundary fits Cartesian column",
              "actual=" + std::to_string(fm8.horizontalAdvance(negativeBoundaryXyz)));

        // 25. The WPR, config, and event columns are strictly separated and cover full-angle orientations.
        const QString fullAngleWpr = QString::fromStdString(
            ui::WorkcellPdfReportExporter::formatTargetOrientation(-180.0, -180.0, -180.0));
        check(ui::WorkcellPdfReportExporter::kTargetWprColumnStart +
                  ui::WorkcellPdfReportExporter::kTargetWprColumnWidth <
                  ui::WorkcellPdfReportExporter::kTargetConfigColumnStart,
              "M13-001: WPR and config columns are separated and do not overlap");
        check(ui::WorkcellPdfReportExporter::kTargetConfigColumnStart +
                  ui::WorkcellPdfReportExporter::kTargetConfigColumnWidth <
                  ui::WorkcellPdfReportExporter::kTargetEventColumnStart,
              "M13-001: config and event columns are separated and do not overlap");
        check(fm8.horizontalAdvance(fullAngleWpr) <= ui::WorkcellPdfReportExporter::kTargetWprColumnWidth,
              "M13-001: full-angle WPR boundary (-180.0, -180.0, -180.0) fits rebalanced WPR column",
              "actual=" + std::to_string(fm8.horizontalAdvance(fullAngleWpr)));

        // 26. The compact seam summary is generated by the same helper used for the overview cell.
        const QString longSeamSummary = QString::fromStdString(
            ui::WorkcellPdfReportExporter::formatSeamSummary(999, 99999.9));
        check(fm9b.horizontalAdvance(longSeamSummary) <= seamValueCellW,
              "M13-001: supported long seam summary fits overview cell",
              "actual=" + std::to_string(fm9b.horizontalAdvance(longSeamSummary)));

        // 27. Every status string fits the rebalanced status column.
        const QStringList statusSamples = {
            QStringLiteral("未检查 (-)"), QStringLiteral("有效 (>100mm)"),
            QStringLiteral("有效 (998.0mm)"), QStringLiteral("近碰撞 (0.0mm)"),
            QStringLiteral("碰撞 (0.0mm)"), QStringLiteral("不可达 (-)"),
            QStringLiteral("超关节限位 (-)"), QStringLiteral("近关节限位"),
            QStringLiteral("奇异区告警"), QStringLiteral("构型突变告警")};
        for (const QString& statusText : statusSamples) {
            check(fm8.horizontalAdvance(statusText) <= targetStatusColW,
                  "M13-001: status text fits rebalanced status column",
                  statusText.toStdString() + ", actual=" + std::to_string(fm8.horizontalAdvance(statusText)));
        }

        // 28. Footer columns remain non-overlapping: the timestamp stays inside its original 800px cell.
        const int advFooterTimestamp = fm8.horizontalAdvance(QStringLiteral("生成: 2026-10-27 22:14:30"));
        check(advFooterTimestamp <= 800, "M13-001: compact footer timestamp fits left footer cell (800px)",
              "actual=" + std::to_string(advFooterTimestamp));

        // 29. Snapshot fallback uses three separately drawn lines, each inside the snapshot width.
        const int snapshotLineW = W - 40;
        const int advPlaceholderTitle = fm12b.horizontalAdvance(QStringLiteral("工位三维仿真模型渲染快照"));
        const int advPlaceholderRobot = fm12b.horizontalAdvance(QStringLiteral("[FANUC M-10iA / ARC Mate 100iC +"));
        const int advPlaceholderWorkpiece = fm12b.horizontalAdvance(QStringLiteral("Workpiece T-Joint / Fillet Weld Cell]"));
        check(advPlaceholderTitle <= snapshotLineW, "M13-001: snapshot fallback title fits its rect",
              "actual=" + std::to_string(advPlaceholderTitle));
        check(advPlaceholderRobot <= snapshotLineW, "M13-001: snapshot fallback robot line fits its rect",
              "actual=" + std::to_string(advPlaceholderRobot));
        check(advPlaceholderWorkpiece <= snapshotLineW, "M13-001: snapshot fallback workpiece line fits its rect",
              "actual=" + std::to_string(advPlaceholderWorkpiece));
    }

    // =========================================================================
    // 2. M13-002: Document Model vs Literals (UFRAME, UTOOL, Robot Base, Names)
    // =========================================================================
    {
        std::cout << "  [Check M13-002] Validating coordinate extraction and model names\n";

        // 2.1 Empty document test
        app::ProjectDocument emptyDoc;
        ui::PdfReportOptions emptyOpts;
        const auto emptyMeta = ui::WorkcellPdfReportExporter::extractMetadata(emptyDoc, emptyOpts);

        check(emptyMeta.programName == "无程序", "M13-002: Empty doc reports '无程序' (no hardcoded 'WELD_001')");
        check(emptyMeta.robotDisplayName == "未配置机器人", "M13-002: Empty doc reports '未配置机器人'");
        check(emptyMeta.controllerDisplayName == "未配置控制器", "M13-002: Empty doc reports '未配置控制器'");
        check(!emptyMeta.hasUtool, "M13-002: Empty doc has no calibrated UTOOL");
        check(emptyMeta.utoolName == "未标定", "M13-002: Empty doc reports UTOOL as '未标定'");
        check(!emptyMeta.hasUframe, "M13-002: Empty doc has no calibrated UFRAME");
        check(emptyMeta.uframeName == "未标定", "M13-002: Empty doc reports UFRAME as '未标定'");
        check(!emptyMeta.hasRobotBase, "M13-002: Empty doc has no robot base");
        check(emptyMeta.robotBaseName == "未配置", "M13-002: Empty doc reports robot base as '未配置'");

        // 2.2 Full sample workcell test
        ui::MainWindow win;
        const auto sessionResult = ui::runSampleWorkcell(win);
        check(sessionResult.exitCode == 0, "runSampleWorkcell succeeds for PDF metadata test");

        ui::PdfReportOptions sampleOpts;
        sampleOpts.cycleTimeSec = win.lastCycleTimeSec();
        sampleOpts.weldTimeSec = win.lastWeldTimeSec();

        const auto sampleMeta = ui::WorkcellPdfReportExporter::extractMetadata(win.document(), sampleOpts);

        check(sampleMeta.programName == "WELD_001", "M13-002: Sample doc reports program 'WELD_001'");
        check(sampleMeta.robotDisplayName == "FANUC M-10iA / ARC Mate 100iC",
              "M13-002: Sample doc reports robot 'FANUC M-10iA / ARC Mate 100iC'");
        check(sampleMeta.controllerDisplayName == "FANUC_R30iB_Plus",
              "M13-002: Sample doc reports controller 'FANUC_R30iB_Plus'");

        // UFRAME check: origin (500, -300, 600)
        check(sampleMeta.hasUframe, "M13-002: Sample doc has calibrated UFRAME");
        check(std::abs(sampleMeta.uframeWpr.x - 500.0) < 1e-3, "M13-002: UFRAME X is 500.00 mm");
        check(std::abs(sampleMeta.uframeWpr.y - (-300.0)) < 1e-3, "M13-002: UFRAME Y is -300.00 mm");
        check(std::abs(sampleMeta.uframeWpr.z - 600.0) < 1e-3, "M13-002: UFRAME Z is 600.00 mm");

        // UTOOL check: origin (0, 0, 150) - NOT 200 mm!
        check(sampleMeta.hasUtool, "M13-002: Sample doc has calibrated UTOOL");
        check(std::abs(sampleMeta.utoolWpr.x - 0.0) < 1e-3, "M13-002: UTOOL X is 0.00 mm");
        check(std::abs(sampleMeta.utoolWpr.y - 0.0) < 1e-3, "M13-002: UTOOL Y is 0.00 mm");
        check(std::abs(sampleMeta.utoolWpr.z - 150.0) < 1e-3, "M13-002: UTOOL Z is 150.00 mm (not 200.00 mm)");

        // Robot Base check: origin (0, 0, 500)
        check(sampleMeta.hasRobotBase, "M13-002: Sample doc has robot base node");
        check(std::abs(sampleMeta.robotBaseWpr.z - 500.0) < 1e-3, "M13-002: Robot Base Z is 500.00 mm");
    }

    // =========================================================================
    // 3. M13-003: Cycle Time Substitution vs '未仿真' & Weld Length
    // =========================================================================
    {
        std::cout << "  [Check M13-003] Validating cycle time behavior and weld length\n";

        app::ProjectDocument unsimDoc;
        ui::PdfReportOptions unsimOpts;
        unsimOpts.cycleTimeSec = 0.0;
        unsimOpts.weldTimeSec = 0.0;

        const auto unsimMeta = ui::WorkcellPdfReportExporter::extractMetadata(unsimDoc, unsimOpts);
        check(!unsimMeta.isSimulated, "M13-003: Unsimulated options flag isSimulated=false");
        check(unsimMeta.totalCycleTimeSec == 0.0,
              "M13-003: Unsimulated totalCycleTimeSec remains 0.0 (not replaced by 38.51)");
        check(unsimMeta.weldTimeSec == 0.0,
              "M13-003: Unsimulated weldTimeSec remains 0.0 (not replaced by 33.31)");
        check(unsimMeta.dutyCyclePercent == 0.0,
              "M13-003: Unsimulated duty cycle is 0.0%");

        // Simulated test with explicit times
        ui::PdfReportOptions simOpts;
        simOpts.cycleTimeSec = 40.0;
        simOpts.weldTimeSec = 30.0;
        simOpts.totalWeldLengthMm = 250.0;

        const auto simMeta = ui::WorkcellPdfReportExporter::extractMetadata(unsimDoc, simOpts);
        check(simMeta.isSimulated, "M13-003: Simulated options flag isSimulated=true");
        check(std::abs(simMeta.totalCycleTimeSec - 40.0) < 1e-3, "M13-003: Total cycle time is 40.00 s");
        check(std::abs(simMeta.weldTimeSec - 30.0) < 1e-3, "M13-003: Weld time is 30.00 s");
        check(std::abs(simMeta.airTimeSec - 10.0) < 1e-3, "M13-003: Air time is calculated as 10.00 s");
        check(std::abs(simMeta.dutyCyclePercent - 75.0) < 1e-3, "M13-003: Duty cycle is 75.0%");
        check(std::abs(simMeta.totalWeldLengthMm - 250.0) < 1e-3, "M13-003: Total weld length is 250.0 mm");
    }

    // =========================================================================
    // 4. M13-004: All 9 Target Validation States and Clearance Display
    // =========================================================================
    {
        std::cout << "  [Check M13-004] Validating target status text, colors, and clearance mm\n";

        const QColor colorGreen(47, 133, 90);
        const QColor colorOrange(192, 86, 33);
        const QColor colorRed(197, 48, 48);
        const QColor colorMuted(113, 128, 150);

        // 1) Unchecked
        core::Target tUnchecked;
        tUnchecked.cache.status = core::TargetValidationState::Unchecked;
        const auto strUnchecked = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tUnchecked);
        const auto colUnchecked = ui::WorkcellPdfReportExporter::getTargetStatusColor(tUnchecked.cache.status);
        check(strUnchecked == "未检查 (-)", "M13-004: Unchecked status formats as '未检查 (-)' (no default 1000mm)");
        check(colUnchecked == colorMuted, "M13-004: Unchecked color is muted gray (not green)");

        // 2) Valid
        core::Target tValid;
        tValid.cache.status = core::TargetValidationState::Valid;
        tValid.cache.minClearanceMm = 14.8;
        const auto strValid = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tValid);
        const auto colValid = ui::WorkcellPdfReportExporter::getTargetStatusColor(tValid.cache.status);
        check(strValid == "有效 (14.8mm)", "M13-004: Valid status includes measured clearance mm");
        check(colValid == colorGreen, "M13-004: Valid color is green");

        // 3) NearCollision
        core::Target tNearCol;
        tNearCol.cache.status = core::TargetValidationState::NearCollision;
        tNearCol.cache.minClearanceMm = 2.4;
        const auto strNearCol = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tNearCol);
        const auto colNearCol = ui::WorkcellPdfReportExporter::getTargetStatusColor(tNearCol.cache.status);
        check(strNearCol == "近碰撞 (2.4mm)", "M13-004: NearCollision status formats as '近碰撞 (2.4mm)'");
        check(colNearCol == colorOrange, "M13-004: NearCollision color is warning orange (not green)");

        // 4) Collision
        core::Target tCollision;
        tCollision.cache.status = core::TargetValidationState::Collision;
        const auto strCol = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tCollision);
        const auto colCol = ui::WorkcellPdfReportExporter::getTargetStatusColor(tCollision.cache.status);
        check(strCol == "碰撞 (0.0mm)", "M13-004: Collision status formats as '碰撞 (0.0mm)'");
        check(colCol == colorRed, "M13-004: Collision color is danger red");

        // 5) Unreachable
        core::Target tUnreach;
        tUnreach.cache.status = core::TargetValidationState::Unreachable;
        const auto strUnreach = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tUnreach);
        const auto colUnreach = ui::WorkcellPdfReportExporter::getTargetStatusColor(tUnreach.cache.status);
        check(strUnreach == "不可达 (-)", "M13-004: Unreachable status formats as '不可达 (-)'");
        check(colUnreach == colorRed, "M13-004: Unreachable color is danger red");

        // 6) JointLimitExceeded
        core::Target tJLimit;
        tJLimit.cache.status = core::TargetValidationState::JointLimitExceeded;
        const auto strJLimit = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tJLimit);
        const auto colJLimit = ui::WorkcellPdfReportExporter::getTargetStatusColor(tJLimit.cache.status);
        check(strJLimit == "超关节限位 (-)", "M13-004: JointLimitExceeded formats as '超关节限位 (-)'");
        check(colJLimit == colorRed, "M13-004: JointLimitExceeded color is danger red");

        // 7) NearJointLimit
        core::Target tNearJLimit;
        tNearJLimit.cache.status = core::TargetValidationState::NearJointLimit;
        const auto strNearJ = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tNearJLimit);
        const auto colNearJ = ui::WorkcellPdfReportExporter::getTargetStatusColor(tNearJLimit.cache.status);
        check(strNearJ == "近关节限位", "M13-004: NearJointLimit formats as '近关节限位'");
        check(colNearJ == colorOrange, "M13-004: NearJointLimit color is warning orange");

        // 8) Singularity
        core::Target tSing;
        tSing.cache.status = core::TargetValidationState::Singularity;
        const auto strSing = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tSing);
        const auto colSing = ui::WorkcellPdfReportExporter::getTargetStatusColor(tSing.cache.status);
        check(strSing == "奇异区告警", "M13-004: Singularity formats as '奇异区告警'");
        check(colSing == colorOrange, "M13-004: Singularity color is warning orange");

        // 9) ConfigJump
        core::Target tJump;
        tJump.cache.status = core::TargetValidationState::ConfigJump;
        const auto strJump = ui::WorkcellPdfReportExporter::formatTargetStatusAndClearance(tJump);
        const auto colJump = ui::WorkcellPdfReportExporter::getTargetStatusColor(tJump.cache.status);
        check(strJump == "构型突变告警", "M13-004: ConfigJump formats as '构型突变告警'");
        check(colJump == colorOrange, "M13-004: ConfigJump color is warning orange");
    }

    // =========================================================================
    // 5. M13-007: Weld Planning Length Sampling Span (Back - Front)
    // =========================================================================
    {
        std::cout << "  [Check M13-007] Validating weld length sampling span (back - front)\n";

        app::ProjectDocument seamDoc;
        core::SeamDefinition testSeam;
        testSeam.id = "seam_trim_test";
        testSeam.startTrimMm = 32.0;
        testSeam.endTrimMm = 5.0;

        // Construct 5 sample frames spanning 32.0 mm to 232.0 mm (actual span = 200.0 mm)
        for (int i = 0; i <= 4; ++i) {
            core::SeamSampleFrame sf;
            sf.arcLengthMm = 32.0 + i * 50.0; // 32.0, 82.0, 132.0, 182.0, 232.0
            testSeam.sampledFrames.push_back(sf);
        }
        seamDoc.seams.push_back(testSeam);

        ui::PdfReportOptions opts;
        const auto meta = ui::WorkcellPdfReportExporter::extractMetadata(seamDoc, opts);

        const double expectedSpan = testSeam.sampledFrames.back().arcLengthMm - testSeam.sampledFrames.front().arcLengthMm;
        const double flawedFormula = testSeam.sampledFrames.back().arcLengthMm - testSeam.startTrimMm - testSeam.endTrimMm;

        check(std::abs(expectedSpan - 200.0) < 1e-4, "M13-007: Test seam expected sampling span is 200.0 mm");
        check(std::abs(meta.totalWeldLengthMm - expectedSpan) < 0.1,
              "M13-007: Report planning weld length matches sampling span (back - front) within 0.1 mm",
              "actual=" + std::to_string(meta.totalWeldLengthMm));
        check(std::abs(meta.totalWeldLengthMm - flawedFormula) > 1.0,
              "M13-007: Report length is NOT flawed formula (does not double-subtract start/end trims)",
              "meta=" + std::to_string(meta.totalWeldLengthMm) + ", flawed=" + std::to_string(flawedFormula));
    }

    // =========================================================================
    // 6. Multi-Page Pagination with 32 Schedules and 40 Targets
    // =========================================================================
    {
        std::cout << "  [Check M13-001/Pagination] Testing multi-page pagination with 32 schedules and 40 targets\n";

        app::ProjectDocument stressDoc;
        core::ControllerUnit cu;
        cu.id = "cu_stress";
        core::Program prog;
        prog.id = "PROG_STRESS";
        prog.name = "WELD_STRESS";

        // Add 32 weld schedules
        for (int i = 1; i <= 32; ++i) {
            core::WeldScheduleDefinition sched;
            sched.scheduleNumber = i;
            sched.currentAmps = 150.0 + i * 2.0;
            sched.voltageVolts = 20.0 + i * 0.2;
            sched.wireFeedSpeedMPerMin = 4.0 + i * 0.1;
            sched.weldSpeedCmPerMin = 40.0 + i * 0.5;
            sched.gasFlowRateLPerMin = 15.0;
            sched.description = "Test Schedule " + std::to_string(i);
            stressDoc.weldSchedules.push_back(sched);
        }

        // Add 40 targets
        core::Operation op;
        op.id = "OP_STRESS";
        for (int i = 1; i <= 40; ++i) {
            core::Target t;
            t.id = "T_" + std::to_string(i);
            t.name = "P_" + std::to_string(i);
            t.motion = (i % 2 == 0) ? core::MotionType::L : core::MotionType::J;
            t.speedValue = (i % 2 == 0) ? 50.0 : 100.0;
            t.termination = core::TerminationType::CNT;
            t.cntValue = 50;
            t.poseInUFrame = core::Transform4d::fromTranslation(10.0 * i, 20.0 * i, 150.0);
            t.cache.valid = true;
            t.cache.status = core::TargetValidationState::Valid;
            t.cache.minClearanceMm = 12.0 + (i % 5);
            t.cache.solvedConfig = core::FanucConfig{false, true, true, {0, 0, 0}};

            if (i == 1) {
                core::ProcessEvent ev;
                ev.type = core::ProcessEventType::ArcStart;
                ev.scheduleNumber = 1;
                t.events.push_back(ev);
            } else if (i == 40) {
                core::ProcessEvent ev;
                ev.type = core::ProcessEventType::ArcEnd;
                ev.scheduleNumber = 1;
                t.events.push_back(ev);
            }
            op.targets.push_back(t);
        }
        prog.operations.push_back(op);
        cu.programs.push_back(prog);
        stressDoc.controllers.push_back(cu);

        const std::string stressPdf = (tempDir / "test_stress_pagination_report.pdf").string();
        std::filesystem::remove(stressPdf);

        ui::PdfReportOptions opts;
        opts.documentTitle = "32 Schedules and 40 Targets Stress Test";
        opts.includeViewportSnapshot = false;

        std::string err;
        const bool ok = ui::WorkcellPdfReportExporter::exportToPdf(stressDoc, stressPdf, opts, &err);
        check(ok, "Export of 32-schedule + 40-target document succeeds", err);
        check(std::filesystem::exists(stressPdf), "Stress PDF exists on disk");

        // Count /Page objects in PDF stream
        std::ifstream ifs(stressPdf, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        size_t pageCount = 0;
        size_t pos = 0;
        while ((pos = content.find("/Page\n", pos)) != std::string::npos ||
               (pos = content.find("/Page\r", pos)) != std::string::npos ||
               (pos = content.find("/Page ", pos)) != std::string::npos) {
            ++pageCount;
            pos += 5;
        }
        // Page 1: Overview, Page 2+: Schedules overflow + targets overflow -> must be at least 3 pages
        check(pageCount >= 3, "32 schedules + 40 targets generate at least 3 pages",
              "actual page count=" + std::to_string(pageCount));
    }

    // =========================================================================
    // 7. Robustness and Error Handling
    // =========================================================================
    {
        app::ProjectDocument doc;
        ui::PdfReportOptions opts;
        std::string err;
        const bool okEmpty = ui::WorkcellPdfReportExporter::exportToPdf(doc, "", opts, &err);
        check(!okEmpty, "Empty output path fails gracefully");
        check(!err.empty(), "Error message returned for empty output path");

        std::string err2;
        const bool okBadDir = ui::WorkcellPdfReportExporter::exportToPdf(doc, "Z:/non_existent_drive_dir/report.pdf", opts, &err2);
        check(!okBadDir, "Unwritable directory path fails gracefully");
        check(!err2.empty(), "Error message returned for unwritable directory path");
    }

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}
