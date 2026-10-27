#include "olp/ui/WorkcellPdfReportExporter.h"
#include "olp/app/CommandManager.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRect>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace olp::ui {

namespace {

const QColor kColorPrimary(26, 54, 93);       // Navy
const QColor kColorAccent(43, 108, 176);      // Blue
const QColor kColorHeaderBg(45, 55, 72);      // Slate header
const QColor kColorAltRow(247, 250, 252);     // Light table zebra
const QColor kColorBorder(226, 232, 240);     // Border gray
const QColor kColorText(26, 32, 44);          // Dark text
const QColor kColorMuted(113, 128, 150);      // Gray text
const QColor kColorGreen(47, 133, 90);        // Success
const QColor kColorOrange(192, 86, 33);       // Warning
const QColor kColorRed(197, 48, 48);          // Danger

void drawHeader(QPainter& p, int width, int pageNum, const PdfReportOptions& opts) {
    p.save();
    const int headerH = 65;
    p.fillRect(0, 0, width, headerH, kColorPrimary);
    p.setPen(Qt::white);
    QFont fLogo("Arial", 12, QFont::Bold);
    p.setFont(fLogo);
    p.drawText(QRect(24, 0, 1600, headerH), Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromStdString(opts.companyName));

    QFont fDoc("Arial", 9);
    p.setFont(fDoc);
    const QString topMeta = QString::fromStdString(opts.projectCode) + QStringLiteral(" | ") +
                            QString::fromStdString(opts.revision);
    p.drawText(QRect(width - 600, 0, 580, headerH), Qt::AlignVCenter | Qt::AlignRight, topMeta);
    p.restore();
}

void drawFooter(QPainter& p, int width, int height, int pageNum, const PdfReportOptions& opts) {
    p.save();
    p.setPen(QPen(kColorBorder, 2));
    p.drawLine(0, height - 55, width, height - 55);

    p.setPen(kColorMuted);
    QFont fFoot("Arial", 8);
    p.setFont(fFoot);

    const QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    p.drawText(QRect(0, height - 50, 800, 44), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("生成: ") + timeStr);

    p.drawText(QRect(width / 2 - 320, height - 50, 640, 44), Qt::AlignCenter,
               QStringLiteral("内部工业工艺指导文件 · 严格受控"));

    p.drawText(QRect(width - 400, height - 50, 400, 44), Qt::AlignVCenter | Qt::AlignRight,
               QStringLiteral("第 %1 页").arg(pageNum));
    p.restore();
}

void drawSectionTitle(QPainter& p, int x, int y, int width, const QString& title) {
    p.save();
    p.fillRect(x, y + 4, 8, 42, kColorAccent);
    p.setPen(kColorPrimary);
    QFont fSec("Arial", 11, QFont::Bold);
    p.setFont(fSec);
    p.drawText(QRect(x + 20, y, width - 24, 52), Qt::AlignVCenter | Qt::AlignLeft, title);
    p.setPen(QPen(kColorBorder, 1));
    p.drawLine(x, y + 52, x + width, y + 52);
    p.restore();
}

void drawMetricCard(QPainter& p, int x, int y, int w, int h,
                    const QString& label, const QString& val, const QColor& accent) {
    p.save();
    p.setPen(QPen(kColorBorder, 1));
    p.setBrush(QColor(250, 250, 252));
    p.drawRoundedRect(x, y, w, h, 6, 6);

    p.fillRect(x, y + 8, 6, h - 16, accent);

    p.setPen(kColorMuted);
    QFont fLbl("Arial", 9);
    p.setFont(fLbl);
    p.drawText(QRect(x + 20, y + 10, w - 30, 42), Qt::AlignLeft | Qt::AlignVCenter, label);

    p.setPen(kColorText);
    QFont fVal("Arial", 13, QFont::Bold);
    p.setFont(fVal);
    p.drawText(QRect(x + 20, y + 56, w - 30, 58), Qt::AlignLeft | Qt::AlignVCenter, val);
    p.restore();
}

} // namespace

ExtractedReportMetadata WorkcellPdfReportExporter::extractMetadata(
    const app::ProjectDocument& doc,
    const PdfReportOptions& options) {
    ExtractedReportMetadata meta;

    // 1. Programs & Operations & Targets
    std::vector<std::string> progNames;
    int uframeNum = 1;
    int utoolNum = 1;
    for (const auto& cu : doc.controllers) {
        for (const auto& prg : cu.programs) {
            if (!prg.name.empty()) {
                progNames.push_back(prg.name);
            }
            uframeNum = prg.uframeNumber;
            utoolNum = prg.utoolNumber;
            meta.opCount += static_cast<int>(prg.operations.size());
            for (const auto& op : prg.operations) {
                meta.targetCount += static_cast<int>(op.targets.size());
            }
        }
    }
    if (!progNames.empty()) {
        if (progNames.size() == 1) {
            meta.programName = progNames.front();
        } else {
            std::ostringstream oss;
            for (size_t i = 0; i < progNames.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << progNames[i];
            }
            meta.programName = oss.str();
        }
    } else {
        meta.programName = "无程序";
    }

    // 2. Controller & Robot & SceneNodes
    meta.sceneNodeCount = static_cast<int>(doc.sceneNodes.size());
    meta.seamCount = static_cast<int>(doc.seams.size());

    if (!doc.controllers.empty()) {
        const auto& cu = doc.controllers.front();
        meta.controllerDisplayName = cu.name.empty() ? (cu.id.empty() ? "FANUC_R30iB_Plus" : cu.id) : cu.name;
    }

    for (const auto& node : doc.sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            meta.hasRobotBase = true;
            meta.robotDisplayName = node.name.empty() ? (node.robotModelId.empty() ? "FANUC M-10iA" : node.robotModelId) : node.name;
            meta.robotBaseName = meta.robotDisplayName + " 底座安装基准";
            meta.robotBaseWpr = node.localTransform.toWPR();
        } else if (node.type == core::SceneNodeType::Frame && node.uframeNumber == uframeNum) {
            meta.hasUframe = true;
            meta.uframeName = node.name.empty() ? ("UFRAME[" + std::to_string(uframeNum) + "]") : node.name;
            meta.uframeWpr = node.localTransform.toWPR();
        } else if (node.type == core::SceneNodeType::Tool && node.utoolNumber == utoolNum) {
            meta.hasUtool = true;
            meta.utoolName = node.name.empty() ? ("UTOOL[" + std::to_string(utoolNum) + "]") : node.name;
            meta.utoolWpr = node.tcpOffset.toWPR();
        }
    }

    if (!meta.hasUframe) {
        for (const auto& node : doc.sceneNodes) {
            if (node.type == core::SceneNodeType::Frame) {
                meta.hasUframe = true;
                meta.uframeName = node.name;
                meta.uframeWpr = node.localTransform.toWPR();
                break;
            }
        }
    }

    // 3. Cycle Times & Welding Length
    if (options.cycleTimeSec > 0.0) {
        meta.isSimulated = true;
        meta.totalCycleTimeSec = options.cycleTimeSec;
        meta.weldTimeSec = std::max(0.0, options.weldTimeSec);
        meta.airTimeSec = (options.airTimeSec > 0.0)
            ? options.airTimeSec
            : std::max(0.0, meta.totalCycleTimeSec - meta.weldTimeSec);
        meta.dutyCyclePercent = (meta.totalCycleTimeSec > 0.0)
            ? (meta.weldTimeSec / meta.totalCycleTimeSec * 100.0)
            : 0.0;
    } else {
        meta.isSimulated = false;
        meta.totalCycleTimeSec = 0.0;
        meta.weldTimeSec = 0.0;
        meta.airTimeSec = 0.0;
        meta.dutyCyclePercent = 0.0;
    }

    // M13-007: Planning weld length equals back arc length minus front arc length (sampling span)
    if (options.totalWeldLengthMm > 0.0) {
        meta.totalWeldLengthMm = options.totalWeldLengthMm;
    } else {
        double calcLen = 0.0;
        for (const auto& seam : doc.seams) {
            if (seam.sampledFrames.size() >= 2) {
                calcLen += std::max(0.0, seam.sampledFrames.back().arcLengthMm - seam.sampledFrames.front().arcLengthMm);
            }
        }
        meta.totalWeldLengthMm = calcLen;
    }

    return meta;
}

std::string WorkcellPdfReportExporter::formatTargetStatusAndClearance(const core::Target& t) {
    std::ostringstream oss;
    switch (t.cache.status) {
    case core::TargetValidationState::Unchecked:
        return "未检查 (-)";
    case core::TargetValidationState::Valid:
        if (t.cache.minClearanceMm < 999.0) {
            oss << "有效 (" << std::fixed << std::setprecision(1) << t.cache.minClearanceMm << "mm)";
        } else {
            oss << "有效 (>100mm)";
        }
        return oss.str();
    case core::TargetValidationState::NearCollision:
        oss << "近碰撞 (" << std::fixed << std::setprecision(1) << t.cache.minClearanceMm << "mm)";
        return oss.str();
    case core::TargetValidationState::Collision:
        return "碰撞 (0.0mm)";
    case core::TargetValidationState::Unreachable:
        return "不可达 (-)";
    case core::TargetValidationState::JointLimitExceeded:
        return "超关节限位 (-)";
    case core::TargetValidationState::NearJointLimit:
        return "近关节限位";
    case core::TargetValidationState::Singularity:
        return "奇异区告警";
    case core::TargetValidationState::ConfigJump:
        return "构型突变告警";
    }
    return "未知状态";
}

std::string WorkcellPdfReportExporter::formatTargetPosition(double xMm, double yMm, double zMm) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << xMm << ", " << yMm << ", " << zMm;
    return oss.str();
}

std::string WorkcellPdfReportExporter::formatTargetOrientation(double wDeg, double pDeg, double rDeg) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << wDeg << ", " << pDeg << ", " << rDeg;
    return oss.str();
}

std::string WorkcellPdfReportExporter::formatSeamSummary(int seamCount, double lengthMm) {
    std::ostringstream oss;
    oss << seamCount;
    if (lengthMm > 0.0) {
        oss << '/' << std::fixed << std::setprecision(1) << lengthMm << "mm";
    }
    return oss.str();
}

QColor WorkcellPdfReportExporter::getTargetStatusColor(core::TargetValidationState status) {
    switch (status) {
    case core::TargetValidationState::Valid:
        return kColorGreen;
    case core::TargetValidationState::NearCollision:
    case core::TargetValidationState::NearJointLimit:
    case core::TargetValidationState::Singularity:
    case core::TargetValidationState::ConfigJump:
        return kColorOrange;
    case core::TargetValidationState::Collision:
    case core::TargetValidationState::Unreachable:
    case core::TargetValidationState::JointLimitExceeded:
        return kColorRed;
    case core::TargetValidationState::Unchecked:
    default:
        return kColorMuted;
    }
}

bool WorkcellPdfReportExporter::exportToPdf(
    const app::ProjectDocument& doc,
    const std::string& outputFilePath,
    const PdfReportOptions& options,
    std::string* errorOut) {

    QFileInfo fi(QString::fromStdString(outputFilePath));
    QDir dir = fi.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorOut) *errorOut = "Could not create output directory: " + dir.absolutePath().toStdString();
        return false;
    }

    QPdfWriter writer(QString::fromStdString(outputFilePath));
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Portrait);
    writer.setResolution(300);
    writer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);

    QPainter p(&writer);
    if (!p.isActive()) {
        if (errorOut) *errorOut = "Could not initialize QPainter on QPdfWriter for " + outputFilePath;
        return false;
    }

    const QRect paintRect = writer.pageLayout().paintRectPixels(writer.resolution());
    const int W = paintRect.width();
    const int H = paintRect.height();

    const auto meta = extractMetadata(doc, options);

    // --------------------------------------------------------------------------
    // PAGE 1: 工位总览、3D视口快照、节拍评估与坐标系
    // --------------------------------------------------------------------------
    int pageNum = 1;
    drawHeader(p, W, pageNum, options);

    p.setPen(kColorPrimary);
    QFont fDocTitle("Arial", 18, QFont::Bold);
    p.setFont(fDocTitle);
    p.drawText(QRect(0, 75, W, 85), Qt::AlignCenter, QString::fromStdString(options.documentTitle));

    p.setPen(kColorMuted);
    QFont fDocSub("Arial", 9);
    p.setFont(fDocSub);
    p.drawText(QRect(0, 165, W, 45), Qt::AlignCenter,
               QStringLiteral("Industrial Robot Welding Offline Programming Process Specification"));

    // Overview Metadata Grid Table (cellH = 90, gridH = 180)
    int curY = 225;
    const int gridH = 180;
    p.setPen(QPen(kColorBorder, 1));
    p.setBrush(QColor(248, 250, 252));
    p.drawRoundedRect(0, curY, W, gridH, 4, 4);

    auto drawCell = [&](int cx, int cy, int cw, int ch, const QString& k, const QString& v) {
        p.setPen(kColorMuted);
        p.setFont(QFont("Arial", 8));
        p.drawText(QRect(cx + 12, cy + 6, cw - 24, 38), Qt::AlignLeft | Qt::AlignVCenter, k);
        p.setPen(kColorText);
        p.setFont(QFont("Arial", 9, QFont::Bold));
        p.drawText(QRect(cx + 12, cy + 46, cw - 24, 40), Qt::AlignLeft | Qt::AlignVCenter, v);
    };

    const int cellH = 90;
    // Row 1: Program (400), Robot Model (1180), Controller (W - 1580)
    drawCell(0, curY, 400, cellH, QStringLiteral("焊接程序名称:"), QString::fromStdString(meta.programName));
    drawCell(400, curY, 1180, cellH, QStringLiteral("机器人型号:"), QString::fromStdString(meta.robotDisplayName));
    drawCell(1580, curY, W - 1580, cellH, QStringLiteral("控制器型号:"), QString::fromStdString(meta.controllerDisplayName));

    // Row 2: Operations (380), Scene Nodes (320), Seams (540), Revision & Operator (W - 1240)
    drawCell(0, curY + cellH, 380, cellH, QStringLiteral("操作 / 点位:"),
             QStringLiteral("%1 / %2 点").arg(meta.opCount).arg(meta.targetCount));
    drawCell(380, curY + cellH, 320, cellH, QStringLiteral("工位实体数:"),
             QStringLiteral("%1 实体").arg(meta.sceneNodeCount));

    const QString seamStr = QString::fromStdString(
        formatSeamSummary(meta.seamCount, meta.totalWeldLengthMm));
    drawCell(700, curY + cellH, 540, cellH, QStringLiteral("焊缝定义 / 规划长度:"), seamStr);
    drawCell(1240, curY + cellH, W - 1240, cellH, QStringLiteral("文档版本 / 编制人员:"),
             QStringLiteral("%1 · %2").arg(QString::fromStdString(options.revision)).arg(QString::fromStdString(options.operatorName)));

    // Section 1: 3D Workcell Layout Snapshot
    curY += gridH + 20;
    drawSectionTitle(p, 0, curY, W, QStringLiteral("一、 焊接工位三维空间布局与可达性透视图"));

    curY += 65;
    const int snapH = 700;
    QRect snapRect(0, curY, W, snapH);
    p.setPen(QPen(kColorBorder, 1));
    p.setBrush(QColor(245, 245, 247));
    p.drawRoundedRect(snapRect, 6, 6);

    if (options.includeViewportSnapshot && !options.snapshotImage.isNull()) {
        const QImage scaledSnap = options.snapshotImage.scaled(
            snapRect.size() - QSize(20, 20), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int px = snapRect.x() + (snapRect.width() - scaledSnap.width()) / 2;
        const int py = snapRect.y() + (snapRect.height() - scaledSnap.height()) / 2;
        p.drawImage(px, py, scaledSnap);
    } else {
        p.setPen(QPen(kColorBorder, 2, Qt::DashLine));
        p.drawLine(snapRect.left() + 60, snapRect.top() + snapH / 2, snapRect.right() - 60, snapRect.top() + snapH / 2);
        p.setPen(kColorPrimary);
        p.setFont(QFont("Arial", 12, QFont::Bold));
        const int placeholderY = snapRect.center().y() - 75;
        const QRect placeholderLine1(snapRect.x() + 20, placeholderY, snapRect.width() - 40, 50);
        const QRect placeholderLine2(snapRect.x() + 20, placeholderY + 55, snapRect.width() - 40, 50);
        const QRect placeholderLine3(snapRect.x() + 20, placeholderY + 110, snapRect.width() - 40, 50);
        p.drawText(placeholderLine1, Qt::AlignCenter, QStringLiteral("工位三维仿真模型渲染快照"));
        p.drawText(placeholderLine2, Qt::AlignCenter,
                   QStringLiteral("[%1 +").arg(QString::fromStdString(meta.robotDisplayName)));
        p.drawText(placeholderLine3, Qt::AlignCenter, QStringLiteral("Workpiece T-Joint / Fillet Weld Cell]"));
    }

    curY += snapH + 25;

    // Section 2: Process Cycle Times
    const QString sec2Title = (meta.totalWeldLengthMm > 0.0)
        ? QStringLiteral("二、 焊接节拍与工艺效率统计 (规划焊缝总长: %1 mm)").arg(meta.totalWeldLengthMm, 0, 'f', 1)
        : QStringLiteral("二、 焊接节拍与工艺效率统计");
    drawSectionTitle(p, 0, curY, W, sec2Title);
    curY += 65;

    const QString totalCycleStr = meta.isSimulated ? QStringLiteral("%1 s").arg(meta.totalCycleTimeSec, 0, 'f', 2) : QStringLiteral("未仿真");
    const QString weldTimeStr = meta.isSimulated ? QStringLiteral("%1 s").arg(meta.weldTimeSec, 0, 'f', 2) : QStringLiteral("未仿真");
    const QString airTimeStr = meta.isSimulated ? QStringLiteral("%1 s").arg(meta.airTimeSec, 0, 'f', 2) : QStringLiteral("未仿真");
    const QString dutyCycleStr = meta.isSimulated ? QStringLiteral("%1 %").arg(meta.dutyCyclePercent, 0, 'f', 1) : QStringLiteral("0.0 %");

    const int cardGap = 12;
    const int cardW = (W - 3 * cardGap) / 4;
    const int cardH = 120;
    drawMetricCard(p, 0, curY, cardW, cardH, QStringLiteral("预估生产节拍"), totalCycleStr, kColorPrimary);
    drawMetricCard(p, cardW + cardGap, curY, cardW, cardH, QStringLiteral("纯起弧焊接时间"), weldTimeStr, kColorGreen);
    drawMetricCard(p, (cardW + cardGap) * 2, curY, cardW, cardH, QStringLiteral("快速空走移动时间"), airTimeStr, kColorAccent);
    drawMetricCard(p, (cardW + cardGap) * 3, curY, cardW, cardH, QStringLiteral("焊接时间占空比"), dutyCycleStr, kColorOrange);

    curY += cardH + 25;

    // Section 3: Tool & User Coordinate Systems (2 sub-rows per entry, sub-row height 36px >= 8pt 33px)
    drawSectionTitle(p, 0, curY, W, QStringLiteral("三、 工具坐标系 (UTOOL) 与用户坐标系 (UFRAME) 标定参数"));
    curY += 65;

    const int frameRowH = 84;
    p.fillRect(0, curY, W, frameRowH, kColorHeaderBg);
    p.setPen(Qt::white);
    p.setFont(QFont("Arial", 8, QFont::Bold));
    p.drawText(QRect(16, curY + 5, 300, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("坐标系编号"));
    p.drawText(QRect(330, curY + 5, W - 346, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("类型 / 描述"));
    p.drawText(QRect(16, curY + 43, 1140, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("空间原点 (X, Y, Z mm)"));
    p.drawText(QRect(1170, curY + 43, W - 1186, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("旋转姿态 (W, P, R deg)"));
    curY += frameRowH;

    auto drawFrameRow = [&](int y, const QString& num, const QString& desc, const QString& pos, const QString& rot, bool alt) {
        if (alt) p.fillRect(0, y, W, frameRowH, kColorAltRow);
        p.setPen(QPen(kColorBorder, 1));
        p.drawLine(0, y + frameRowH, W, y + frameRowH);
        p.setPen(kColorText);
        p.setFont(QFont("Arial", 8));
        p.drawText(QRect(16, y + 5, 300, 36), Qt::AlignVCenter | Qt::AlignLeft, num);
        p.drawText(QRect(330, y + 5, W - 346, 36), Qt::AlignVCenter | Qt::AlignLeft, desc);
        p.drawText(QRect(16, y + 43, 1140, 36), Qt::AlignVCenter | Qt::AlignLeft, pos);
        p.drawText(QRect(1170, y + 43, W - 1186, 36), Qt::AlignVCenter | Qt::AlignLeft, rot);
    };

    QString utoolPos = QStringLiteral("未标定");
    QString utoolRot = QStringLiteral("-");
    if (meta.hasUtool) {
        utoolPos = QStringLiteral("X=%1, Y=%2, Z=%3 mm")
            .arg(meta.utoolWpr.x, 0, 'f', 2)
            .arg(meta.utoolWpr.y, 0, 'f', 2)
            .arg(meta.utoolWpr.z, 0, 'f', 2);
        utoolRot = QStringLiteral("W=%1, P=%2, R=%3 deg")
            .arg(meta.utoolWpr.wDeg, 0, 'f', 2)
            .arg(meta.utoolWpr.pDeg, 0, 'f', 2)
            .arg(meta.utoolWpr.rDeg, 0, 'f', 2);
    }
    drawFrameRow(curY, QStringLiteral("UTOOL[1]"), QString::fromStdString(meta.utoolName), utoolPos, utoolRot, false);
    curY += frameRowH;

    QString uframePos = QStringLiteral("未标定");
    QString uframeRot = QStringLiteral("-");
    if (meta.hasUframe) {
        uframePos = QStringLiteral("X=%1, Y=%2, Z=%3 mm")
            .arg(meta.uframeWpr.x, 0, 'f', 2)
            .arg(meta.uframeWpr.y, 0, 'f', 2)
            .arg(meta.uframeWpr.z, 0, 'f', 2);
        uframeRot = QStringLiteral("W=%1, P=%2, R=%3 deg")
            .arg(meta.uframeWpr.wDeg, 0, 'f', 2)
            .arg(meta.uframeWpr.pDeg, 0, 'f', 2)
            .arg(meta.uframeWpr.rDeg, 0, 'f', 2);
    }
    drawFrameRow(curY, QStringLiteral("UFRAME[1]"), QString::fromStdString(meta.uframeName), uframePos, uframeRot, true);
    curY += frameRowH;

    QString basePos = QStringLiteral("未配置");
    QString baseRot = QStringLiteral("-");
    if (meta.hasRobotBase) {
        basePos = QStringLiteral("X=%1, Y=%2, Z=%3 mm")
            .arg(meta.robotBaseWpr.x, 0, 'f', 2)
            .arg(meta.robotBaseWpr.y, 0, 'f', 2)
            .arg(meta.robotBaseWpr.z, 0, 'f', 2);
        baseRot = QStringLiteral("W=%1, P=%2, R=%3 deg")
            .arg(meta.robotBaseWpr.wDeg, 0, 'f', 2)
            .arg(meta.robotBaseWpr.pDeg, 0, 'f', 2)
            .arg(meta.robotBaseWpr.rDeg, 0, 'f', 2);
    }
    drawFrameRow(curY, QStringLiteral("ROBOT_BASE"), QString::fromStdString(meta.robotBaseName), basePos, baseRot, false);

    drawFooter(p, W, H, pageNum, options);

    // --------------------------------------------------------------------------
    // PAGE 2+: 焊接工艺规范卡与点位清单
    // --------------------------------------------------------------------------
    writer.newPage();
    pageNum++;
    drawHeader(p, W, pageNum, options);

    curY = 80;
    drawSectionTitle(p, 0, curY, W, QStringLiteral("四、 焊接工艺参数条件表 (Weld Schedules)"));
    curY += 65;

    const int schedH = 84;
    auto drawSchedHeader = [&]() {
        p.fillRect(0, curY, W, schedH, kColorHeaderBg);
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", 8, QFont::Bold));
        // Sub-row 1
        p.drawText(QRect(12, curY + 5, 180, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("条件号"));
        p.drawText(QRect(202, curY + 5, 300, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("焊接电流 (A)"));
        p.drawText(QRect(512, curY + 5, 300, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("电弧电压 (V)"));
        p.drawText(QRect(822, curY + 5, 400, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("送丝速度 (m/min)"));
        p.drawText(QRect(1232, curY + 5, 380, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("焊接速度 (mm/s)"));
        p.drawText(QRect(1622, curY + 5, 400, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("保护气体 (L/min)"));
        // Sub-row 2
        p.drawText(QRect(12, curY + 43, W - 24, 36), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("工艺描述"));
        curY += schedH;
    };

    drawSchedHeader();

    auto drawSchedRow = [&](int y, const core::WeldScheduleDefinition& sc, bool alt) {
        if (alt) p.fillRect(0, y, W, schedH, kColorAltRow);
        p.setPen(QPen(kColorBorder, 1));
        p.drawLine(0, y + schedH, W, y + schedH);
        p.setPen(kColorText);
        p.setFont(QFont("Arial", 8));
        // Sub-row 1
        p.drawText(QRect(12, y + 5, 180, 36), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Cond[%1]").arg(sc.scheduleNumber));
        p.drawText(QRect(202, y + 5, 300, 36), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1 A").arg(sc.currentAmps, 0, 'f', 1));
        p.drawText(QRect(512, y + 5, 300, 36), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1 V").arg(sc.voltageVolts, 0, 'f', 1));
        p.drawText(QRect(822, y + 5, 400, 36), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1 m/min").arg(sc.wireFeedSpeedMPerMin, 0, 'f', 1));
        p.drawText(QRect(1232, y + 5, 380, 36), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1 mm/s").arg(sc.getWeldSpeedMmPerSec(), 0, 'f', 1));
        p.drawText(QRect(1622, y + 5, 400, 36), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1 L/min").arg(sc.gasFlowRateLPerMin, 0, 'f', 1));
        // Sub-row 2
        p.drawText(QRect(12, y + 43, W - 24, 36), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromStdString(sc.description));
    };

    if (doc.weldSchedules.empty()) {
        core::WeldScheduleDefinition def;
        drawSchedRow(curY, def, false);
        curY += schedH;
    } else {
        bool alt = false;
        for (const auto& sc : doc.weldSchedules) {
            if (curY + schedH > H - 75) {
                drawFooter(p, W, H, pageNum, options);
                writer.newPage();
                pageNum++;
                drawHeader(p, W, pageNum, options);
                curY = 80;
                drawSectionTitle(p, 0, curY, W, QStringLiteral("四、 焊接工艺参数条件表 (续)"));
                curY += 65;
                drawSchedHeader();
            }
            drawSchedRow(curY, sc, alt);
            alt = !alt;
            curY += schedH;
        }
    }

    curY += 35;

    // Section 5: Target Waypoints Detailed List (2 lines per waypoint)
    if (curY + 190 > H - 75) {
        drawFooter(p, W, H, pageNum, options);
        writer.newPage();
        pageNum++;
        drawHeader(p, W, pageNum, options);
        curY = 80;
    }

    drawSectionTitle(p, 0, curY, W, QStringLiteral("五、 机器人程序点位详细清单 (Target Waypoints)"));
    curY += 65;

    const int targetH = 84;
    const int subRowH = 36;
    auto drawTargetHeader = [&]() {
        p.fillRect(0, curY, W, targetH, kColorHeaderBg);
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", 8, QFont::Bold));
        // Sub-row 1
        p.drawText(QRect(4, curY + 5, 110, subRowH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("点位"));
        p.drawText(QRect(116, curY + 5, 320, subRowH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("点位标识"));
        p.drawText(QRect(438, curY + 5, 540, subRowH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("运动 / 编程速度"));
        p.drawText(QRect(980, curY + 5, kTargetXyzColumnWidth, subRowH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("笛卡尔位置 (X, Y, Z mm)"));
        p.drawText(QRect(kTargetStatusColumnStart, curY + 5, W - kTargetStatusColumnStart, subRowH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("状态 / 间隙"));

        // Sub-row 2
        p.drawText(QRect(kTargetWprColumnStart, curY + 43, kTargetWprColumnWidth, subRowH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("姿态 WPR (deg)"));
        p.drawText(QRect(kTargetConfigColumnStart, curY + 43, kTargetConfigColumnWidth, subRowH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("构型 CONFIG"));
        p.drawText(QRect(kTargetEventColumnStart, curY + 43, W - kTargetEventColumnStart - 10, subRowH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("工艺事件"));
        curY += targetH;
    };

    drawTargetHeader();

    int ptIdx = 1;
    bool altTarget = false;
    for (const auto& cu : doc.controllers) {
        for (const auto& prg : cu.programs) {
            for (const auto& op : prg.operations) {
                for (const auto& t : op.targets) {
                    if (curY + targetH > H - 75) {
                        drawFooter(p, W, H, pageNum, options);
                        writer.newPage();
                        pageNum++;
                        drawHeader(p, W, pageNum, options);
                        curY = 80;
                        drawSectionTitle(p, 0, curY, W, QStringLiteral("五、 机器人程序点位详细清单 (续)"));
                        curY += 65;
                        drawTargetHeader();
                    }

                    if (altTarget) p.fillRect(0, curY, W, targetH, kColorAltRow);
                    p.setPen(QPen(kColorBorder, 1));
                    p.drawLine(0, curY + targetH, W, curY + targetH);

                    p.setPen(kColorText);
                    p.setFont(QFont("Arial", 8));

                    // Sub-row 1: Point index, Name, Motion/Speed, Position XYZ, Status
                    p.drawText(QRect(4, curY + 5, 110, subRowH), Qt::AlignVCenter | Qt::AlignLeft,
                               QStringLiteral("P[%1]").arg(ptIdx++));

                    p.drawText(QRect(116, curY + 5, 320, subRowH), Qt::AlignVCenter | Qt::AlignLeft,
                               QString::fromStdString(t.name.empty() ? t.id : t.name));

                    QString motionStr = (t.motion == core::MotionType::J ? QStringLiteral("J ") : QStringLiteral("L "));
                    if (t.useWeldSpeed) {
                        motionStr += QStringLiteral("WELD_SPD");
                    } else if (t.motion == core::MotionType::J) {
                        motionStr += QStringLiteral("%1%").arg(static_cast<int>(std::round(t.speedValue)));
                    } else {
                        motionStr += QStringLiteral("%1mm/s").arg(static_cast<int>(std::round(t.speedValue)));
                    }
                    if (t.termination == core::TerminationType::FINE) {
                        motionStr += QStringLiteral(" FINE");
                    } else {
                        motionStr += QStringLiteral(" CNT%1").arg(t.cntValue);
                    }
                    p.drawText(QRect(438, curY + 5, 540, subRowH), Qt::AlignVCenter | Qt::AlignLeft, motionStr);

                    const auto wpr = t.poseInUFrame.toWPR();
                    p.drawText(QRect(980, curY + 5, kTargetXyzColumnWidth, subRowH), Qt::AlignVCenter | Qt::AlignLeft,
                               QString::fromStdString(formatTargetPosition(wpr.x, wpr.y, wpr.z)));

                    p.setPen(getTargetStatusColor(t.cache.status));
                    p.drawText(QRect(kTargetStatusColumnStart, curY + 5, W - kTargetStatusColumnStart, subRowH), Qt::AlignVCenter | Qt::AlignLeft,
                               QString::fromStdString(formatTargetStatusAndClearance(t)));
                    p.setPen(kColorText);

                    // Sub-row 2: Pose WPR, CONFIG, Events
                    p.drawText(QRect(kTargetWprColumnStart, curY + 43, kTargetWprColumnWidth, subRowH), Qt::AlignVCenter | Qt::AlignLeft,
                               QString::fromStdString(formatTargetOrientation(wpr.wDeg, wpr.pDeg, wpr.rDeg)));

                    p.drawText(QRect(kTargetConfigColumnStart, curY + 43, kTargetConfigColumnWidth, subRowH), Qt::AlignVCenter | Qt::AlignLeft,
                               QString::fromStdString(t.cache.solvedConfig.toString()));

                    QString evStr;
                    for (const auto& ev : t.events) {
                        if (ev.type == core::ProcessEventType::ArcStart) evStr += QStringLiteral("起弧[%1] ").arg(ev.scheduleNumber);
                        else if (ev.type == core::ProcessEventType::ArcEnd) evStr += QStringLiteral("熄弧[%1] ").arg(ev.scheduleNumber);
                        else if (ev.type == core::ProcessEventType::WeaveStart) evStr += QStringLiteral("摆焊[%1] ").arg(ev.scheduleNumber);
                    }
                    p.drawText(QRect(kTargetEventColumnStart, curY + 43, W - kTargetEventColumnStart - 10, subRowH), Qt::AlignVCenter | Qt::AlignLeft, evStr);

                    altTarget = !altTarget;
                    curY += targetH;
                }
            }
        }
    }

    drawFooter(p, W, H, pageNum, options);
    p.end();

    QFileInfo outFi(QString::fromStdString(outputFilePath));
    if (!outFi.exists() || outFi.size() < 100) {
        if (errorOut) *errorOut = "Generated PDF file does not exist or is empty: " + outputFilePath;
        return false;
    }

    return true;
}

} // namespace olp::ui
