#include "olp/io/FanucLsPostProcessor.h"
#include "olp/io/Utf8Path.h"

#include <cctype>
#include <filesystem>
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

LsValidationResult FanucLsPostProcessor::validateProgram(
    const core::Program& program,
    const LsPostConfig& config) {
    LsValidationResult result;

    std::string nameErr;
    if (!validateProgramName(program.name, nameErr)) {
        result.issues.push_back({
            LsValidationIssue::Severity::Error,
            "",
            "",
            "Invalid program name '" + program.name + "': " + nameErr
        });
    }

    if (program.comment.size() > static_cast<size_t>(config.maxProgramCommentLength)) {
        result.issues.push_back({
            LsValidationIssue::Severity::Error,
            "",
            "",
            "Program comment length (" + std::to_string(program.comment.size()) +
                ") exceeds FANUC limit of " + std::to_string(config.maxProgramCommentLength) + " characters"
        });
    }

    size_t totalTargets = 0;
    for (const auto& op : program.operations) {
        if (!op.enabled) continue;
        for (const auto& t : op.targets) {
            totalTargets++;
            const std::string tLabel = t.name.empty() ? t.id : t.name;

            if (t.name.size() > static_cast<size_t>(config.maxCommentLength)) {
                result.issues.push_back({
                    LsValidationIssue::Severity::Error,
                    op.id,
                    t.id,
                    "Target '" + tLabel + "' comment length (" + std::to_string(t.name.size()) +
                        ") exceeds FANUC limit of " + std::to_string(config.maxCommentLength) + " characters"
                });
            }

            if (!t.cache.valid) {
                result.issues.push_back({
                    LsValidationIssue::Severity::Error,
                    op.id,
                    t.id,
                    "Refusing to export " + tLabel + ": inverse-kinematics cache is not valid"
                });
            }

            if (t.motion == core::MotionType::C) {
                result.issues.push_back({
                    LsValidationIssue::Severity::Error,
                    op.id,
                    t.id,
                    "Circular motion is not implemented; refusing to write it as a linear move"
                });
            }

            if (config.blockOnUnreachable && t.cache.status == core::TargetValidationState::Unreachable) {
                result.issues.push_back({
                    LsValidationIssue::Severity::Error,
                    op.id,
                    t.id,
                    "Target '" + tLabel + "' is unreachable"
                });
            }

            if (t.cache.status == core::TargetValidationState::JointLimitExceeded) {
                result.issues.push_back({
                    LsValidationIssue::Severity::Error,
                    op.id,
                    t.id,
                    "Target '" + tLabel + "' exceeds robot joint limits"
                });
            }

            // FIX-EXPORT-GATE-005 / OPEN-P1-3: always surface Collision — Error when gate on,
            // Warning when gate off (never silent pure-green with hard hits).
            if (t.cache.status == core::TargetValidationState::Collision) {
                result.issues.push_back({
                    config.blockOnCollision
                        ? LsValidationIssue::Severity::Error
                        : LsValidationIssue::Severity::Warning,
                    op.id,
                    t.id,
                    "Target '" + tLabel + "' has hard collision"
                });
            }

            if (t.cache.status == core::TargetValidationState::ConfigJump) {
                result.issues.push_back({
                    config.blockOnConfigJump
                        ? LsValidationIssue::Severity::Error
                        : LsValidationIssue::Severity::Warning,
                    op.id,
                    t.id,
                    "Target '" + tLabel + "' has arm configuration jump"
                });
            }

            if (t.cache.status == core::TargetValidationState::NearCollision) {
                result.issues.push_back({
                    LsValidationIssue::Severity::Warning,
                    op.id,
                    t.id,
                    "Target '" + tLabel + "' has near-collision clearance warning"
                });
            }

            if (t.cache.status == core::TargetValidationState::Singularity) {
                result.issues.push_back({
                    LsValidationIssue::Severity::Warning,
                    op.id,
                    t.id,
                    "Target '" + tLabel + "' is near a singularity"
                });
            }

            if (t.motion == core::MotionType::J) {
                if (t.useWeldSpeed) {
                    result.issues.push_back({
                        LsValidationIssue::Severity::Error,
                        op.id,
                        t.id,
                        "Target '" + tLabel + "' is a Joint motion (J) but specifies WELD_SPEED; joint moves must use percentage speed (1..100%)"
                    });
                } else {
                    const int spd = static_cast<int>(std::round(t.speedValue));
                    if (spd < 1 || spd > 100) {
                        result.issues.push_back({
                            LsValidationIssue::Severity::Error,
                            op.id,
                            t.id,
                            "Target '" + tLabel + "' joint speed (" + std::to_string(spd) + "%) is outside valid FANUC range 1..100%"
                        });
                    }
                }
            }

            if (t.motion == core::MotionType::L && !t.useWeldSpeed) {
                const int spd = static_cast<int>(std::round(t.speedValue));
                if (spd < 1) {
                    result.issues.push_back({
                        LsValidationIssue::Severity::Error,
                        op.id,
                        t.id,
                        "Target '" + tLabel + "' linear speed (" + std::to_string(spd) + " mm/sec) must be at least 1 mm/sec"
                    });
                }
            }

            if (t.termination == core::TerminationType::CNT) {
                if (t.cntValue < 0 || t.cntValue > 100) {
                    result.issues.push_back({
                        LsValidationIssue::Severity::Error,
                        op.id,
                        t.id,
                        "Target '" + tLabel + "' CNT value (" + std::to_string(t.cntValue) + ") is outside valid FANUC range 0..100"
                    });
                }
            }
        }
    }

    if (totalTargets == 0) {
        result.issues.push_back({
            LsValidationIssue::Severity::Error,
            "",
            "",
            "Program contains no enabled targets to export"
        });
    }

    result.ok = !result.hasErrors();
    return result;
}

std::string FanucLsPostProcessor::generateLsString(
    const core::Program& program,
    const LsPostConfig& config,
    std::string* errorOut) {
    const LsValidationResult valResult = validateProgram(program, config);
    if (!valResult.ok) {
        if (errorOut) {
            for (const auto& is : valResult.issues) {
                if (is.severity == LsValidationIssue::Severity::Error) {
                    *errorOut = is.message;
                    break;
                }
            }
        }
        return {};
    }

    struct IndexedPos {
        int pIndex{1};
        const core::Target* target{nullptr};
        bool isHomeJoint{false};
    };
    std::vector<IndexedPos> posEntries;

    std::vector<std::string> mnLines;
    auto addMnLine = [&](const std::string& body) {
        mnLines.push_back(body);
    };

    // RISK-001 draft gate: always watermark /MN — do not treat export as production TP download.
    addMnLine("  ! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)");
    addMnLine("  UFRAME_NUM=" + std::to_string(program.uframeNumber));
    addMnLine("  UTOOL_NUM=" + std::to_string(program.utoolNumber));

    int nextPIndex = 1;
    for (const auto& op : program.operations) {
        if (!op.enabled) continue;

        addMnLine("  ! --- Operation: " + op.name + " ---");
        for (const auto& t : op.targets) {
            const int pIdx = nextPIndex++;
            posEntries.push_back({pIdx, &t, false});

            std::ostringstream motionLine;
            motionLine << (t.motion == core::MotionType::J ? "J" : "L")
                       << " P[" << pIdx;
            if (!t.name.empty()) {
                motionLine << ":" << t.name;
            }
            motionLine << "] ";

            if (t.motion == core::MotionType::J) {
                const int spd = static_cast<int>(std::round(t.speedValue));
                motionLine << spd << "% ";
            } else if (t.useWeldSpeed) {
                motionLine << "WELD_SPEED ";
            } else {
                const int spd = static_cast<int>(std::round(t.speedValue));
                motionLine << spd << "mm/sec ";
            }

            if (t.termination == core::TerminationType::FINE) {
                motionLine << "FINE   ";
            } else {
                motionLine << "CNT" << t.cntValue << "   ";
            }
            addMnLine(motionLine.str());

            for (const auto& ev : t.events) {
                if (ev.type == core::ProcessEventType::ArcStart) {
                    addMnLine("  Arc Start[" + std::to_string(ev.scheduleNumber) + "]");
                } else if (ev.type == core::ProcessEventType::ArcEnd) {
                    addMnLine("  Arc End[" + std::to_string(ev.scheduleNumber) + "]");
                } else if (ev.type == core::ProcessEventType::WeaveStart) {
                    addMnLine("  Weave Sine[" + std::to_string(ev.scheduleNumber) + "]");
                } else if (ev.type == core::ProcessEventType::WeaveEnd) {
                    addMnLine("  Weave End");
                } else if (ev.type == core::ProcessEventType::Wait) {
                    std::ostringstream woss;
                    woss << std::fixed << std::setprecision(2);
                    woss << "  WAIT   " << ev.paramValue << "(sec)";
                    addMnLine(woss.str());
                } else if (ev.type == core::ProcessEventType::DigitalOut) {
                    addMnLine("  DO[" + std::to_string(ev.scheduleNumber) + "]=" + (ev.paramValue > 0.5 ? "ON" : "OFF"));
                } else if (ev.type == core::ProcessEventType::Comment && !ev.comment.empty()) {
                    addMnLine("  ! " + ev.comment);
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
        addMnLine(home.str());

        if (config.emitHomeAsJointTarget) {
            const int homePIdx = nextPIndex++;
            posEntries.push_back({homePIdx, nullptr, true});
            addMnLine("J P[" + std::to_string(homePIdx) + ":HOME] 30% FINE");
        }
    }

    std::ostringstream oss;
    oss << "/PROG  " << program.name << "\n";
    oss << "/ATTR\n";
    oss << "OWNER\t\t= " << config.owner << ";\n";
    oss << "COMMENT\t\t= \"" << program.comment << "\";\n";
    oss << "PROG_SIZE\t= 0;\n";
    oss << "CREATE\t\t= DATE 26-10-27  TIME 12:00:00;\n";
    oss << "MODIFIED\t= DATE 26-10-27  TIME 12:00:00;\n";
    oss << "FILE_NAME\t= ;\n";
    oss << "VERSION\t\t= 0;\n";
    oss << "LINE_COUNT\t= " << mnLines.size() << ";\n";
    oss << "MEMORY_SIZE\t= " << (500 + mnLines.size() * 32 + posEntries.size() * 48) << ";\n";
    oss << "PROTECT\t\t= READ_WRITE;\n";
    oss << "TCD:  STACK_SIZE\t= 0, TASK_PRIORITY\t= 50, TIME_SLICE\t= 0,\n";
    oss << "      BUSY_LAMP_OFF\t= 0, ABORT_REQUEST\t= 0, PAUSE_REQUEST\t= 0;\n";
    oss << "DEFAULT_GROUP\t= " << config.defaultGroup << ";\n";
    oss << "CONTROL_CODE\t= 00000000 00000000;\n";

    if (config.includeArcWeldingAppl) {
        oss << "/APPL\n";
        oss << "  ARC Welding Equipment : " << config.defaultGroup << ";\n";
    }

    oss << "/MN\n";
    for (size_t i = 0; i < mnLines.size(); ++i) {
        oss << std::setw(4) << (i + 1) << ":" << mnLines[i] << " ;\n";
    }

    oss << "/POS\n";
    oss << std::fixed << std::setprecision(3);
    for (const auto& item : posEntries) {
        if (item.isHomeJoint) {
            oss << "P[" << item.pIndex << ":\"HOME\"]{\n";
            oss << "   GP1:\n";
            oss << "\tUF : 0, UT : " << program.utoolNumber << ",\n";
            oss << "\tJ1 = " << std::setw(9) << core::rad2deg(program.startState.homeJointsRad[0]) << " deg,\t"
                << "J2 = " << std::setw(9) << core::rad2deg(program.startState.homeJointsRad[1]) << " deg,\t"
                << "J3 = " << std::setw(9) << core::rad2deg(program.startState.homeJointsRad[2]) << " deg,\n";
            oss << "\tJ4 = " << std::setw(9) << core::rad2deg(program.startState.homeJointsRad[3]) << " deg,\t"
                << "J5 = " << std::setw(9) << core::rad2deg(program.startState.homeJointsRad[4]) << " deg,\t"
                << "J6 = " << std::setw(9) << core::rad2deg(program.startState.homeJointsRad[5]) << " deg\n";
            oss << "};\n";
            continue;
        }

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
    const LsPostConfig& config,
    std::string* errorOut) {
    std::string genError;
    const std::string body = generateLsString(program, config, &genError);
    if (!genError.empty() || body.empty()) {
        if (errorOut) *errorOut = genError.empty() ? "Generated .ls body is empty" : genError;
        return false;
    }

    // Same UTF-8 path semantics as ProjectSerializer (Windows CP936-safe).
    const std::filesystem::path p = utf8ToPath(outputFilePath);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream ofs(p, std::ios::binary);
    if (!ofs.is_open()) {
        if (errorOut) *errorOut = "Could not open file for writing: " + outputFilePath;
        return false;
    }
    ofs << body;
    if (!ofs.good()) {
        if (errorOut) *errorOut = "Error occurred while writing to file: " + outputFilePath;
        return false;
    }
    return true;
}

bool FanucLsPostProcessor::exportControllerPrograms(
    const core::ControllerUnit& controller,
    const std::string& outputDir,
    const LsPostConfig& config,
    std::vector<std::string>* exportedFiles,
    std::vector<std::string>* errors) {
    if (controller.programs.empty()) {
        if (errors) errors->push_back("Controller contains no programs to export");
        return false;
    }

    // Keep UTF-8 directory string; exportToFile applies utf8ToPath.
    const std::filesystem::path dir = utf8ToPath(outputDir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    bool allSuccess = true;
    for (const auto& prog : controller.programs) {
        std::string filePath = outputDir;
        if (!filePath.empty()) {
            const char last = filePath.back();
            if (last != '/' && last != '\\') filePath.push_back('/');
        }
        filePath += prog.name + ".ls";
        std::string err;
        if (exportToFile(prog, filePath, config, &err)) {
            if (exportedFiles) exportedFiles->push_back(filePath);
        } else {
            allSuccess = false;
            if (errors) errors->push_back("Failed to export " + prog.name + ": " + err);
        }
    }
    return allSuccess;
}

} // namespace olp::io
