#include "olp/io/FanucLsPostProcessor.h"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace olp::io {

bool FanucLsPostProcessor::validateProgramName(const std::string& progName, std::string& outError) {
    if (progName.empty() || progName.size() > 16) {
        outError = "Program name must be 1..16 characters";
        return false;
    }
    if (!std::isupper(static_cast<unsigned char>(progName[0]))) {
        outError = "Program name must start with an uppercase letter (A-Z)";
        return false;
    }
    for (char c : progName) {
        if (!std::isupper(static_cast<unsigned char>(c)) &&
            !std::isdigit(static_cast<unsigned char>(c)) &&
            c != '_') {
            outError = "Program name may only contain uppercase A-Z, digits 0-9, and '_'";
            return false;
        }
    }
    return true;
}

std::string FanucLsPostProcessor::generateLsString(
    const core::Program& program,
    const LsPostConfig& config,
    std::string* errorOut) {
    std::ostringstream oss;

    oss << "/PROG  " << program.name << "\n";
    oss << "/ATTR\n";
    oss << "OWNER\t\t= " << config.owner << ";\n";
    oss << "COMMENT\t\t= \"" << program.comment << "\";\n";
    oss << "PROTECT\t\t= READ_WRITE;\n";
    oss << "DEFAULT_GROUP\t= " << config.defaultGroup << ";\n";

    if (config.includeArcWeldingAppl) {
        oss << "/APPL\n";
        oss << "  ARC Welding Equipment : " << config.defaultGroup << ";\n";
    }

    oss << "/MN\n";
    int lineNum = 1;
    auto emitLine = [&](const std::string& body) {
        oss << std::setw(4) << lineNum++ << ":" << body << " ;\n";
    };

    emitLine("  UFRAME_NUM=" + std::to_string(program.uframeNumber));
    emitLine("  UTOOL_NUM=" + std::to_string(program.utoolNumber));

    struct IndexedTarget {
        int pIndex{1};
        const core::Target* target{nullptr};
    };
    std::vector<IndexedTarget> posEntries;

    int nextPIndex = 1;
    for (const auto& op : program.operations) {
        if (!op.enabled) continue;

        emitLine("  ! --- Operation: " + op.name + " ---");
        for (const auto& t : op.targets) {
            if (!t.cache.valid) {
                if (errorOut) *errorOut = "Refusing to export " + t.name + ": inverse-kinematics cache is not valid";
                return {};
            }
            if (t.motion == core::MotionType::C) {
                if (errorOut) *errorOut = "Circular motion is not implemented; refusing to write it as a linear move";
                return {};
            }
            const int pIdx = nextPIndex++;
            posEntries.push_back({pIdx, &t});

            std::ostringstream motionLine;
            motionLine << (t.motion == core::MotionType::J ? "J" : "L")
                       << " P[" << pIdx;
            if (!t.name.empty()) {
                motionLine << ":" << t.name;
            }
            motionLine << "] ";

            if (t.useWeldSpeed) {
                motionLine << "WELD_SPEED ";
            } else if (t.motion == core::MotionType::J) {
                motionLine << static_cast<int>(t.speedValue) << "% ";
            } else {
                motionLine << static_cast<int>(t.speedValue) << "mm/sec ";
            }

            if (t.termination == core::TerminationType::FINE) {
                motionLine << "FINE   ";
            } else {
                motionLine << "CNT" << t.cntValue << "   ";
            }
            emitLine(motionLine.str());

            // Emit post-motion process events (Arc Start[n], Arc End[n], etc.)
            for (const auto& ev : t.events) {
                if (ev.type == core::ProcessEventType::ArcStart) {
                    emitLine("  Arc Start[" + std::to_string(ev.scheduleNumber) + "]");
                } else if (ev.type == core::ProcessEventType::ArcEnd) {
                    emitLine("  Arc End[" + std::to_string(ev.scheduleNumber) + "]");
                } else if (ev.type == core::ProcessEventType::Comment && !ev.comment.empty()) {
                    emitLine("  ! " + ev.comment);
                }
            }
        }
    }

    if (config.appendReturnHome) {
        std::ostringstream home;
        home << std::fixed << std::setprecision(3);
        home << "  ! HOME joints deg";
        for (double q : program.startState.homeJointsRad) {
            home << " " << core::rad2deg(q);
        }
        emitLine(home.str());
    }

    oss << "/POS\n";
    oss << std::fixed << std::setprecision(3);
    for (const auto& item : posEntries) {
        const core::Target& t = *item.target;
        const core::WprPose wpr = t.poseInUFrame.toWPR();
        oss << "P[" << item.pIndex;
        if (!t.name.empty()) {
            oss << ":\"" << t.name << "\"";
        }
        oss << "]{\n";
        oss << "   GP1:\n";
        oss << "\tUF : " << program.uframeNumber
            << ", UT : " << program.utoolNumber
            << ",\t\tCONFIG : '" << t.cache.solvedConfig.toString() << "',\n";
        oss << "\tX = " << std::setw(9) << wpr.x << "  mm,\t"
            << "Y = " << std::setw(9) << wpr.y << "  mm,\t"
            << "Z = " << std::setw(9) << wpr.z << "  mm,\n";
        oss << "\tW = " << std::setw(9) << wpr.wDeg << " deg,\t"
            << "P = " << std::setw(9) << wpr.pDeg << " deg,\t"
            << "R = " << std::setw(9) << wpr.rDeg << " deg\n";
        oss << "};\n";
    }

    oss << "/END\n";
    return oss.str();
}

bool FanucLsPostProcessor::exportToFile(
    const core::Program& program,
    const std::string& outputFilePath,
    const LsPostConfig& config) {
    std::string err;
    if (!validateProgramName(program.name, err)) {
        return false;
    }
    std::string genError;
    const std::string body = generateLsString(program, config, &genError);
    if (!genError.empty() || body.empty()) {
        return false;
    }
    std::ofstream ofs(outputFilePath, std::ios::binary);
    if (!ofs.is_open()) {
        return false;
    }
    ofs << body;
    return ofs.good();
}

} // namespace olp::io
