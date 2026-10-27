#include "test_support.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/ui/MainWindow.h"
#include "olp/algo/SeamAndToolpathAlgo.h"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace olp;

namespace {

core::Target makeValidTarget(
    const std::string& id,
    const std::string& name,
    core::MotionType motion,
    double speed,
    core::TerminationType term,
    int cnt) {
    core::Target t;
    t.id = id;
    t.name = name;
    t.motion = motion;
    t.speedValue = speed;
    t.termination = term;
    t.cntValue = cnt;
    t.poseInUFrame = core::Transform4d::fromTranslation(100.0, 200.0, 300.0);
    t.cache.valid = true;
    t.cache.status = core::TargetValidationState::Valid;
    t.cache.jointsRad = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6};
    t.cache.solvedConfig = core::FanucConfig{false, true, true, {0, 0, 0}};
    return t;
}

} // namespace

void testFanucLsPostProcessorValidationAndExport() {
    std::cout << "\n--- Test 19: M12 FANUC .ls post-processor validation, events, multi-program export & UI linkage ---\n";

    // 1. Program name validation & boundaries
    std::string err;
    check(io::FanucLsPostProcessor::validateProgramName("WELD_001", err),
          "Valid program name 'WELD_001' accepted");
    check(io::FanucLsPostProcessor::validateProgramName("A", err),
          "Single uppercase char 'A' accepted");
    const std::string name16 = "PROGRAM_NAME_16C"; // exactly 16 characters
    check(name16.size() == 16 && io::FanucLsPostProcessor::validateProgramName(name16, err),
          "Exactly 16-char program name accepted");

    const std::string name17 = "PROGRAM_NAME_17CH"; // exactly 17 characters
    check(name17.size() == 17 && !io::FanucLsPostProcessor::validateProgramName(name17, err),
          "17-char program name rejected");
    check(!io::FanucLsPostProcessor::validateProgramName("weld_001", err),
          "Lowercase start 'weld_001' rejected");
    check(!io::FanucLsPostProcessor::validateProgramName("1WELD", err),
          "Digit start '1WELD' rejected");
    check(!io::FanucLsPostProcessor::validateProgramName("", err),
          "Empty program name rejected");
    check(!io::FanucLsPostProcessor::validateProgramName("WELD-001", err),
          "Program name with hyphen 'WELD-001' rejected");

    // 2. Program validation checks
    core::Program emptyProg;
    emptyProg.name = "EMPTY_PROG";
    auto valEmpty = io::FanucLsPostProcessor::validateProgram(emptyProg);
    check(!valEmpty.ok && valEmpty.hasErrors(),
          "Program with no targets reports validation errors");

    core::Program testProg;
    testProg.name = "TEST_PROG";
    testProg.comment = "Short comment";
    core::Operation op1;
    op1.id = "op_1";
    op1.name = "FirstWeld";
    op1.enabled = true;

    auto t1 = makeValidTarget("t_1", "APPROACH", core::MotionType::J, 60.0, core::TerminationType::CNT, 100);
    auto t2 = makeValidTarget("t_2", "WELD_START", core::MotionType::L, 10.0, core::TerminationType::FINE, 0);
    t2.useWeldSpeed = true;
    t2.events.push_back({core::ProcessEventType::ArcStart, 2, 0.0, ""});
    t2.events.push_back({core::ProcessEventType::WeaveStart, 1, 0.0, ""});

    auto t3 = makeValidTarget("t_3", "WELD_END", core::MotionType::L, 10.0, core::TerminationType::FINE, 0);
    t3.useWeldSpeed = true;
    t3.events.push_back({core::ProcessEventType::WeaveEnd, 0, 0.0, ""});
    t3.events.push_back({core::ProcessEventType::ArcEnd, 2, 0.0, ""});
    t3.events.push_back({core::ProcessEventType::Wait, 0, 1.5, ""});
    t3.events.push_back({core::ProcessEventType::DigitalOut, 4, 1.0, ""});
    t3.events.push_back({core::ProcessEventType::Comment, 0, 0.0, "Torch cleaned"});

    op1.targets = {t1, t2, t3};
    testProg.operations.push_back(op1);

    auto valValid = io::FanucLsPostProcessor::validateProgram(testProg);
    check(valValid.ok && !valValid.hasErrors(),
          "Well-formed program passes validation with no errors");

    // M12-002: Comment boundaries (16 chars vs 17 chars for targets, 32 vs 33 for program)
    core::Program tName16Prog = testProg;
    tName16Prog.operations[0].targets[0].name = "TARGET_NAME_16CH"; // 16 chars
    check(tName16Prog.operations[0].targets[0].name.size() == 16, "Target name is 16 chars");
    auto valT16 = io::FanucLsPostProcessor::validateProgram(tName16Prog);
    check(valT16.ok, "16-character target name is accepted");

    core::Program tName17Prog = testProg;
    tName17Prog.operations[0].targets[0].name = "TARGET_NAME_17CHR"; // 17 chars
    check(tName17Prog.operations[0].targets[0].name.size() == 17, "Target name is 17 chars");
    auto valT17 = io::FanucLsPostProcessor::validateProgram(tName17Prog);
    std::string errT17;
    std::string lsT17 = io::FanucLsPostProcessor::generateLsString(tName17Prog, {}, &errT17);
    check(!valT17.ok && valT17.hasErrors() && lsT17.empty() && !errT17.empty(),
          "M12-002: 17-character target name records error and refuses export (not truncated)");

    core::Program pComment32Prog = testProg;
    pComment32Prog.comment = std::string(32, 'C'); // 32 chars
    check(pComment32Prog.comment.size() == 32, "Program comment is 32 chars");
    auto valP32 = io::FanucLsPostProcessor::validateProgram(pComment32Prog);
    check(valP32.ok, "32-character program comment is accepted");

    core::Program pComment33Prog = testProg;
    pComment33Prog.comment = std::string(33, 'C'); // 33 chars
    check(pComment33Prog.comment.size() == 33, "Program comment is 33 chars");
    auto valP33 = io::FanucLsPostProcessor::validateProgram(pComment33Prog);
    std::string errP33;
    std::string lsP33 = io::FanucLsPostProcessor::generateLsString(pComment33Prog, {}, &errP33);
    check(!valP33.ok && valP33.hasErrors() && lsP33.empty() && !errP33.empty(),
          "M12-002: 33-character program comment records error and refuses export");

    // M12-001: Speed & CNT range tests (no silent rewriting)
    core::Program jSpeedZeroProg = testProg;
    jSpeedZeroProg.operations[0].targets[0].speedValue = 0.0;
    auto valJ0 = io::FanucLsPostProcessor::validateProgram(jSpeedZeroProg);
    check(!valJ0.ok && valJ0.hasErrors(), "M12-001: Joint speed 0% records error");

    core::Program jSpeedSmallProg = testProg;
    jSpeedSmallProg.operations[0].targets[0].speedValue = 0.4; // rounds to 0
    auto valJSmall = io::FanucLsPostProcessor::validateProgram(jSpeedSmallProg);
    std::string errJSmall;
    std::string lsJSmall = io::FanucLsPostProcessor::generateLsString(jSpeedSmallProg, {}, &errJSmall);
    check(!valJSmall.ok && lsJSmall.empty(),
          "M12-001: Joint speed 0.4% records error and refuses export (not changed to 50%)");

    core::Program jSpeed150Prog = testProg;
    jSpeed150Prog.operations[0].targets[0].speedValue = 150.0;
    auto valJ150 = io::FanucLsPostProcessor::validateProgram(jSpeed150Prog);
    std::string errJ150;
    std::string lsJ150 = io::FanucLsPostProcessor::generateLsString(jSpeed150Prog, {}, &errJ150);
    check(!valJ150.ok && lsJ150.empty(),
          "M12-001: Joint speed 150% records error and refuses export (not clamped to 100%)");

    core::Program lSpeedZeroProg = testProg;
    lSpeedZeroProg.operations[0].targets[1].useWeldSpeed = false;
    lSpeedZeroProg.operations[0].targets[1].speedValue = 0.0;
    auto valL0 = io::FanucLsPostProcessor::validateProgram(lSpeedZeroProg);
    check(!valL0.ok && valL0.hasErrors(), "M12-001: Linear speed 0 mm/sec records error");

    core::Program lSpeedSmallProg = testProg;
    lSpeedSmallProg.operations[0].targets[1].useWeldSpeed = false;
    lSpeedSmallProg.operations[0].targets[1].speedValue = 0.4; // rounds to 0
    auto valLSmall = io::FanucLsPostProcessor::validateProgram(lSpeedSmallProg);
    std::string errLSmall;
    std::string lsLSmall = io::FanucLsPostProcessor::generateLsString(lSpeedSmallProg, {}, &errLSmall);
    check(!valLSmall.ok && lsLSmall.empty(),
          "M12-001: Linear speed 0.4 mm/sec records error and refuses export (not changed to 200 mm/sec)");

    // Non-weld linear speed rounding to nearest integer
    core::Program lSpeedRoundProg = testProg;
    lSpeedRoundProg.operations[0].targets[1].useWeldSpeed = false;
    lSpeedRoundProg.operations[0].targets[1].speedValue = 8.6;
    std::string lsLRound = io::FanucLsPostProcessor::generateLsString(lSpeedRoundProg);
    check(lsLRound.find("9mm/sec") != std::string::npos,
          "M12-001: Linear speed 8.6 mm/sec formats to nearest integer 9mm/sec");

    core::Program cntNegProg = testProg;
    cntNegProg.operations[0].targets[0].cntValue = -1;
    auto valCntNeg = io::FanucLsPostProcessor::validateProgram(cntNegProg);
    check(!valCntNeg.ok && valCntNeg.hasErrors(), "M12-001: Negative CNT value records error");

    core::Program cnt250Prog = testProg;
    cnt250Prog.operations[0].targets[0].cntValue = 250;
    auto valCnt250 = io::FanucLsPostProcessor::validateProgram(cnt250Prog);
    std::string errCnt250;
    std::string lsCnt250 = io::FanucLsPostProcessor::generateLsString(cnt250Prog, {}, &errCnt250);
    check(!valCnt250.ok && lsCnt250.empty(),
          "M12-001: CNT 250 records error and refuses export (not clamped to CNT100)");

    // M12-003: Joint motion with useWeldSpeed must be rejected
    core::Program jWeldSpeedProg = testProg;
    jWeldSpeedProg.operations[0].targets[0].motion = core::MotionType::J;
    jWeldSpeedProg.operations[0].targets[0].useWeldSpeed = true;
    auto valJWeld = io::FanucLsPostProcessor::validateProgram(jWeldSpeedProg);
    std::string errJWeld;
    std::string lsJWeld = io::FanucLsPostProcessor::generateLsString(jWeldSpeedProg, {}, &errJWeld);
    check(!valJWeld.ok && valJWeld.hasErrors() && lsJWeld.empty() && !errJWeld.empty(),
          "M12-003: Joint motion with useWeldSpeed=true records error and refuses export");

    // Collision & Unreachable blocking
    core::Program issueProg = testProg;
    issueProg.operations[0].targets[1].cache.status = core::TargetValidationState::Collision;
    io::LsPostConfig blockCollConfig;
    blockCollConfig.blockOnCollision = true;
    auto valColl = io::FanucLsPostProcessor::validateProgram(issueProg, blockCollConfig);
    check(!valColl.ok, "blockOnCollision=true rejects program with collided targets");

    // 3. String generation & structure
    std::string genErr;
    std::string lsText = io::FanucLsPostProcessor::generateLsString(testProg, {}, &genErr);
    check(!lsText.empty() && genErr.empty(), "generateLsString generates non-empty .ls text");
    check(lsText.find("/PROG  TEST_PROG") != std::string::npos, "Header contains /PROG TEST_PROG");
    check(lsText.find("LINE_COUNT\t=") != std::string::npos, "Header contains LINE_COUNT");
    check(lsText.find("MEMORY_SIZE\t=") != std::string::npos, "Header contains MEMORY_SIZE");
    check(lsText.find("/APPL") != std::string::npos, "Contains /APPL section");
    check(lsText.find("/MN") != std::string::npos, "Contains /MN section");
    check(lsText.find("J P[1:APPROACH] 60% CNT100") != std::string::npos, "MN has J move with speed% and CNT");
    check(lsText.find("L P[2:WELD_START] WELD_SPEED FINE") != std::string::npos, "MN has L move with WELD_SPEED FINE");
    check(lsText.find("J P[") != std::string::npos && lsText.find("J P[1:APPROACH] WELD_SPEED") == std::string::npos,
          "M12-003: Generated text contains no J moves with WELD_SPEED");
    check(lsText.find("Arc Start[2]") != std::string::npos, "MN has Arc Start[2]");
    check(lsText.find("Weave Sine[1]") != std::string::npos, "MN has Weave Sine[1]");
    check(lsText.find("Weave End") != std::string::npos, "MN has Weave End");
    check(lsText.find("Arc End[2]") != std::string::npos, "MN has Arc End[2]");
    check(lsText.find("WAIT   1.50(sec)") != std::string::npos, "MN has WAIT instruction");
    check(lsText.find("DO[4]=ON") != std::string::npos, "MN has DO instruction");
    check(lsText.find("! Torch cleaned") != std::string::npos, "MN has custom comment instruction");
    check(lsText.find("/POS") != std::string::npos, "Contains /POS section");
    check(lsText.find("/END") != std::string::npos, "Contains /END section");

    // 4. emitHomeAsJointTarget option
    io::LsPostConfig jointHomeConfig;
    jointHomeConfig.appendReturnHome = true;
    jointHomeConfig.emitHomeAsJointTarget = true;
    std::string jointHomeLs = io::FanucLsPostProcessor::generateLsString(testProg, jointHomeConfig);
    check(jointHomeLs.find("J P[4:HOME] 30% FINE") != std::string::npos,
          "emitHomeAsJointTarget outputs J P[HOME] motion instruction");
    check(jointHomeLs.find("P[4:\"HOME\"]{\n   GP1:\n\tUF : 0, UT : 1,\n\tJ1 =") != std::string::npos,
          "emitHomeAsJointTarget outputs GP1 Joint position block in /POS");

    // 5. Multi-operation export
    core::Program multiOpProg = testProg;
    core::Operation op2;
    op2.id = "op_2";
    op2.name = "SecondWeld";
    op2.enabled = true;
    op2.targets = {
        makeValidTarget("t_21", "OP2_START", core::MotionType::L, 10.0, core::TerminationType::FINE, 0),
        makeValidTarget("t_22", "OP2_END", core::MotionType::L, 10.0, core::TerminationType::FINE, 0)
    };
    multiOpProg.operations.push_back(op2);
    std::string multiOpLs = io::FanucLsPostProcessor::generateLsString(multiOpProg);
    check(multiOpLs.find("! --- Operation: FirstWeld ---") != std::string::npos,
          "Multi-operation outputs first operation separator");
    check(multiOpLs.find("! --- Operation: SecondWeld ---") != std::string::npos,
          "Multi-operation outputs second operation separator");
    check(multiOpLs.find("P[5:\"OP2_END\"]") != std::string::npos,
          "Multi-operation point numbering is contiguous across operations");

    // 6. Multi-program controller export
    core::ControllerUnit controllerUnit;
    controllerUnit.id = "ctrl_1";
    controllerUnit.name = "FANUC_R30iB";
    core::Program progA = testProg;
    progA.name = "PART_A";
    core::Program progB = testProg;
    progB.name = "PART_B";
    controllerUnit.programs = {progA, progB};

    const std::string tempDir = (std::filesystem::temp_directory_path() / "olp_ls_test_output").string();
    std::vector<std::string> exportedFiles;
    std::vector<std::string> exportErrors;
    bool exportCtrlOk = io::FanucLsPostProcessor::exportControllerPrograms(
        controllerUnit, tempDir, {}, &exportedFiles, &exportErrors);
    check(exportCtrlOk && exportedFiles.size() == 2,
          "exportControllerPrograms successfully exported 2 .ls programs");
    check(std::filesystem::exists(tempDir + "/PART_A.ls"), "PART_A.ls file exists on disk");
    check(std::filesystem::exists(tempDir + "/PART_B.ls"), "PART_B.ls file exists on disk");
    std::filesystem::remove_all(tempDir);

    // 7. MainWindow controller export linkage
    ui::MainWindow win;
    auto& doc = win.document();
    doc.controllers.clear();
    doc.controllers.push_back(controllerUnit);

    auto valWin = win.validateProgramForExport("PART_A");
    check(valWin.ok, "MainWindow::validateProgramForExport validates program");

    std::string winPreviewErr;
    std::string winPreview = win.previewFanucLs("PART_A", &winPreviewErr);
    check(!winPreview.empty() && winPreviewErr.empty(),
          "MainWindow::previewFanucLs returns generated text");

    const std::string winOutFile = (std::filesystem::temp_directory_path() / "PART_A_win.ls").string();
    std::string winExportErr;
    bool winExportOk = win.exportFanucLs(winOutFile, "PART_A", {}, &winExportErr);
    check(winExportOk && std::filesystem::exists(winOutFile),
          "MainWindow::exportFanucLs writes .ls file to disk");
    std::filesystem::remove(winOutFile);
}
