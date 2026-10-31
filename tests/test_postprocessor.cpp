#include "test_support.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/io/Utf8Path.h"
#include "olp/ui/MainWindow.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/device/RobotModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

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

    // FIX-EXPORT-GATE-005 / OPEN-P1-3: gate off → Collision is Warning (exportable) but NOT silent.
    io::LsPostConfig allowCollConfig;
    allowCollConfig.blockOnCollision = false;
    auto valCollWarn = io::FanucLsPostProcessor::validateProgram(issueProg, allowCollConfig);
    check(valCollWarn.ok && !valCollWarn.hasErrors() && !valCollWarn.issues.empty(),
          "FIX-EXPORT-GATE-005: blockOnCollision=false keeps Collision as Warning (not silent)");
    bool sawCollWarn = false;
    for (const auto& is : valCollWarn.issues) {
        if (is.severity == io::LsValidationIssue::Severity::Warning &&
            is.message.find("hard collision") != std::string::npos) {
            sawCollWarn = true;
        }
    }
    check(sawCollWarn,
          "FIX-EXPORT-GATE-005: gate-off Collision warning message is present for yellow UI path");
    std::string errAllowColl;
    std::string lsAllowColl = io::FanucLsPostProcessor::generateLsString(issueProg, allowCollConfig, &errAllowColl);
    check(!lsAllowColl.empty() && errAllowColl.empty(),
          "FIX-EXPORT-GATE-005: gate-off Collision remains exportable (Warning, ok==true)");

    // FIX-EXPORT-GATE-001: default LsPostConfig{} must reject Collision and ConfigJump
    check(io::LsPostConfig{}.blockOnCollision && io::LsPostConfig{}.blockOnConfigJump,
          "FIX-EXPORT-GATE-001: default LsPostConfig enables blockOnCollision and blockOnConfigJump");
    auto valCollDefault = io::FanucLsPostProcessor::validateProgram(issueProg);
    std::string errCollDefault;
    std::string lsCollDefault = io::FanucLsPostProcessor::generateLsString(issueProg, {}, &errCollDefault);
    check(!valCollDefault.ok && valCollDefault.hasErrors() && lsCollDefault.empty() && !errCollDefault.empty(),
          "FIX-EXPORT-GATE-001: default LsPostConfig rejects Collision targets and refuses generateLsString");

    core::Program jumpProg = testProg;
    jumpProg.operations[0].targets[1].cache.status = core::TargetValidationState::ConfigJump;
    auto valJumpDefault = io::FanucLsPostProcessor::validateProgram(jumpProg);
    std::string errJumpDefault;
    std::string lsJumpDefault = io::FanucLsPostProcessor::generateLsString(jumpProg, {}, &errJumpDefault);
    check(!valJumpDefault.ok && valJumpDefault.hasErrors() && lsJumpDefault.empty() && !errJumpDefault.empty(),
          "FIX-EXPORT-GATE-001: default LsPostConfig rejects ConfigJump as Error and refuses export");

    io::LsPostConfig allowJumpConfig;
    allowJumpConfig.blockOnConfigJump = false;
    auto valJumpWarn = io::FanucLsPostProcessor::validateProgram(jumpProg, allowJumpConfig);
    check(valJumpWarn.ok && !valJumpWarn.hasErrors() && !valJumpWarn.issues.empty(),
          "blockOnConfigJump=false keeps ConfigJump as Warning and allows export");

    core::Program nearProg = testProg;
    nearProg.operations[0].targets[1].cache.status = core::TargetValidationState::NearCollision;
    auto valNear = io::FanucLsPostProcessor::validateProgram(nearProg);
    check(valNear.ok && !valNear.hasErrors() && !valNear.issues.empty(),
          "NearCollision produces Warning and remains exportable under default config");

    core::Program singProg = testProg;
    singProg.operations[0].targets[1].cache.status = core::TargetValidationState::Singularity;
    auto valSing = io::FanucLsPostProcessor::validateProgram(singProg);
    check(valSing.ok && !valSing.hasErrors() && !valSing.issues.empty(),
          "Singularity produces Warning and remains exportable under default config");

    // FIX-EXPORT-GATE-003: lock default LsPostConfig{} gates in test_postprocessor
    // (endpoint Collision, mid-segment writeback Collision, ConfigJump).
    {
        check(io::LsPostConfig{}.blockOnCollision && io::LsPostConfig{}.blockOnConfigJump,
              "FIX-EXPORT-GATE-003: default LsPostConfig keeps blockOnCollision and blockOnConfigJump");

        core::Program endpointColl = testProg;
        endpointColl.operations[0].targets[1].cache.valid = true;
        endpointColl.operations[0].targets[1].cache.status = core::TargetValidationState::Collision;
        auto valEndpoint = io::FanucLsPostProcessor::validateProgram(endpointColl);
        std::string errEndpoint;
        std::string lsEndpoint = io::FanucLsPostProcessor::generateLsString(endpointColl, {}, &errEndpoint);
        check(!valEndpoint.ok && valEndpoint.hasErrors() && lsEndpoint.empty() && !errEndpoint.empty(),
              "FIX-EXPORT-GATE-003: default LsPostConfig rejects endpoint Collision and refuses generateLsString");

        core::Program jumpLock = testProg;
        jumpLock.operations[0].targets[1].cache.valid = true;
        jumpLock.operations[0].targets[1].cache.status = core::TargetValidationState::ConfigJump;
        auto valJump = io::FanucLsPostProcessor::validateProgram(jumpLock);
        std::string errJump;
        std::string lsJump = io::FanucLsPostProcessor::generateLsString(jumpLock, {}, &errJump);
        check(!valJump.ok && valJump.hasErrors() && lsJump.empty() && !errJump.empty(),
              "FIX-EXPORT-GATE-003: default LsPostConfig rejects ConfigJump and refuses generateLsString");

        // Mid-segment writeback shape (item 2): IK may remain valid while status=Collision.
        core::Program writebackShape = testProg;
        writebackShape.operations[0].targets[0].cache.valid = true;
        writebackShape.operations[0].targets[0].cache.status = core::TargetValidationState::Valid;
        writebackShape.operations[0].targets[1].cache.valid = true;
        writebackShape.operations[0].targets[1].cache.status = core::TargetValidationState::Collision;
        writebackShape.operations[0].targets[1].cache.issueMessage = "Sweep collides (cache writeback)";
        auto valWb = io::FanucLsPostProcessor::validateProgram(writebackShape);
        std::string errWb;
        std::string lsWb = io::FanucLsPostProcessor::generateLsString(writebackShape, {}, &errWb);
        check(!valWb.ok && valWb.hasErrors() && lsWb.empty() && !errWb.empty(),
              "FIX-EXPORT-GATE-003: default validateProgram rejects mid-segment Collision writeback shape (valid=true, status=Collision)");

        // Live sweep: validateAndSimulateProgram must write Collision, then default post rejects.
        {
            const auto robot = device::RobotModel::createFanucM10iA();
            algo::OpwKinematics kin(robot);
            core::UnitsSettings settings;
            algo::CollisionAndSimEngine engine(kin, settings);
            const auto utool = core::Transform4d::identity();
            const auto home = robot.homeJointsRad;
            auto endJoints = home;
            endJoints[0] += 0.4;

            const double jDist = algo::OpwKinematics::weightedJointDistance(home, endJoints);
            const double sampleStep = 0.05;
            const double segDuration = std::max(0.2, jDist / 1.5);
            const int numSteps = std::max(2, static_cast<int>(std::ceil(segDuration / sampleStep)));
            const int midStep = std::max(1, numSteps / 2);
            const double alpha = static_cast<double>(midStep) / static_cast<double>(numSteps);
            std::array<double, 6> midJoints{};
            for (size_t k = 0; k < 6; ++k) {
                midJoints[k] = home[k] + alpha * (endJoints[k] - home[k]);
            }
            const auto midTcp = kin.forwardKinematics(midJoints, utool).tcpPoseInBase;
            const core::Vec3 gateCenter = midTcp.pos + (-midTcp.rot.colZ()) * 95.0;
            constexpr double half = 6.0;

            core::SceneNode gate;
            gate.id = "gate003_sweep_gate";
            gate.name = "GATE003_Sweep_Gate";
            gate.type = core::SceneNodeType::Part;
            gate.visible = true;
            gate.collisionRole = core::CollisionRole::Fixture;
            gate.collisionMesh.bboxMin = gateCenter - core::Vec3{half, half, half};
            gate.collisionMesh.bboxMax = gateCenter + core::Vec3{half, half, half};
            const std::vector<core::SceneNode> scene{gate};

            const auto endTcp = kin.forwardKinematics(endJoints, utool).tcpPoseInBase;
            core::Program sweepProg;
            sweepProg.id = "prog_gate003_sweep";
            sweepProg.name = "GATE003";
            sweepProg.startState.homeJointsRad = home;
            core::Operation op;
            op.id = "op_gate003";
            core::Target target;
            target.id = "via_end";
            target.name = "Joint_End";
            target.role = core::TargetRole::VIA;
            target.motion = core::MotionType::J;
            target.poseInUFrame = endTcp;
            op.targets.push_back(target);
            sweepProg.operations.push_back(op);

            (void)engine.validateAndSimulateProgram(
                sweepProg,
                core::Transform4d::identity(),
                core::Transform4d::identity(),
                utool,
                scene,
                sampleStep);
            const auto& endCache = sweepProg.operations[0].targets[0].cache;
            check(endCache.status == core::TargetValidationState::Collision,
                  "FIX-EXPORT-GATE-003: validateAndSimulateProgram writes mid-segment Collision to cache",
                  "status=" + std::to_string(static_cast<int>(endCache.status)) + " msg=" + endCache.issueMessage);
            check(endCache.valid,
                  "FIX-EXPORT-GATE-003: mid-segment Collision writeback keeps cache.valid true");
            const auto lsVal = io::FanucLsPostProcessor::validateProgram(sweepProg);
            std::string lsErr;
            const std::string lsBody = io::FanucLsPostProcessor::generateLsString(sweepProg, {}, &lsErr);
            check(!lsVal.ok && lsVal.hasErrors() && lsBody.empty() && !lsErr.empty(),
                  "FIX-EXPORT-GATE-003: after mid-segment sweep writeback, default LsPostConfig rejects export");
        }
    }

    // FIX-EXPORT-GATE-003: UTF-8 Chinese path .ls export (same utf8ToPath as ProjectSerializer)
    {
        // Keep Chinese segments as UTF-8 string concat (do not round-trip via path::string()).
        const std::string tempRoot = std::filesystem::temp_directory_path().string();
        const std::string chineseDir = tempRoot + "/olp_ls_中文路径导出_001";
        const std::string chinesePath = chineseDir + "/工位导出_WELD_001.ls";
        std::string chineseErr;
        const bool chineseOk = io::FanucLsPostProcessor::exportToFile(testProg, chinesePath, {}, &chineseErr);
        const auto chineseFs = io::utf8ToPath(chinesePath);
        check(chineseOk && std::filesystem::exists(chineseFs),
              "FIX-EXPORT-GATE-003: exportToFile writes .ls under UTF-8 Chinese path",
              chineseErr);
        if (chineseOk) {
            std::ifstream ifs(chineseFs, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            check(content.find("/PROG  TEST_PROG") != std::string::npos,
                  "FIX-EXPORT-GATE-003: Chinese-path .ls content contains /PROG header");
            check(content.find("! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)") != std::string::npos,
                  "FIX-EXPORT-GATE-004: Chinese-path exportToFile .ls contains RISK-001 draft watermark");
        }
        std::error_code rmEc;
        std::filesystem::remove_all(io::utf8ToPath(chineseDir), rmEc);
    }

    // 3. String generation & structure
    std::string genErr;
    std::string lsText = io::FanucLsPostProcessor::generateLsString(testProg, {}, &genErr);
    check(!lsText.empty() && genErr.empty(), "generateLsString generates non-empty .ls text");
    check(lsText.find("/PROG  TEST_PROG") != std::string::npos, "Header contains /PROG TEST_PROG");
    check(lsText.find("LINE_COUNT\t=") != std::string::npos, "Header contains LINE_COUNT");
    check(lsText.find("MEMORY_SIZE\t=") != std::string::npos, "Header contains MEMORY_SIZE");
    check(lsText.find("/APPL") != std::string::npos, "Contains /APPL section");
    check(lsText.find("/MN") != std::string::npos, "Contains /MN section");
    check(lsText.find("! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)") != std::string::npos,
          "FIX-EXPORT-GATE-004: generateLsString embeds RISK-001 draft watermark in /MN");
    {
        const auto mnPos = lsText.find("/MN");
        const auto draftPos = lsText.find("! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)");
        check(mnPos != std::string::npos && draftPos != std::string::npos && draftPos > mnPos,
              "FIX-EXPORT-GATE-004: draft watermark appears after /MN header");
    }
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

    // FIX-EXPORT-GATE-005 / OPEN-P1-2: with collision geometry and empty lastSimulationResult,
    // const validate/preview must refuse (no blind trust of Valid caches).
    {
        ui::MainWindow winStale;
        auto& staleDoc = winStale.document();
        staleDoc.controllers.clear();
        staleDoc.controllers.push_back(controllerUnit);

        core::SceneNode fixture;
        fixture.id = "stale_fixture";
        fixture.name = "StaleFixture";
        fixture.type = core::SceneNodeType::Part;
        fixture.collisionRole = core::CollisionRole::Fixture;
        fixture.collisionMesh.bboxMin = {-10.0, -10.0, -10.0};
        fixture.collisionMesh.bboxMax = {10.0, 10.0, 10.0};
        staleDoc.sceneNodes.push_back(fixture);

        check(winStale.lastSimulationResult().frames.empty(),
              "FIX-EXPORT-GATE-005: setup has empty lastSimulationResult before export gate");
        auto staleVal = winStale.validateProgramForExport("PART_A");
        check(!staleVal.ok && staleVal.hasErrors(),
              "FIX-EXPORT-GATE-005: validateProgramForExport refuses when collision geometry present but sim missing");
        std::string stalePreviewErr;
        std::string stalePreview = winStale.previewFanucLs("PART_A", &stalePreviewErr);
        check(stalePreview.empty() && !stalePreviewErr.empty(),
              "FIX-EXPORT-GATE-005: previewFanucLs refuses with explicit message when sim missing");

        // Non-const export path force-resims; with only a far fixture and Valid IK caches
        // the re-sim may still succeed or fail depending on IK — either way it must not
        // blindly export without attempting ensureProgramSimulatedForExport.
        check(winStale.ensureProgramSimulatedForExport("PART_A"),
              "FIX-EXPORT-GATE-005: ensureProgramSimulatedForExport runs against current scene");
        // After ensure, const validate must no longer take the "sim missing" refuse path
        // (either ok from clean caches or real Errors from re-sim — not the stale-cache message).
        auto afterEnsure = winStale.validateProgramForExport("PART_A");
        bool refusedForStale = false;
        for (const auto& is : afterEnsure.issues) {
            if (is.message.find("no fresh simulation") != std::string::npos) {
                refusedForStale = true;
            }
        }
        check(!refusedForStale,
              "FIX-EXPORT-GATE-005: after ensureProgramSimulatedForExport, stale-sim refuse is cleared");

        // FIX-EXPORT-P2-A reinforcement: ensureProgramSimulatedForExport("PART_A") only ensures PART_A;
        // un-simulated PART_B under the same controller must still be refused with stale/missing error.
        auto staleValB = winStale.validateProgramForExport("PART_B");
        bool refusedForStaleBBefore = false;
        for (const auto& is : staleValB.issues) {
            if (is.message.find("no fresh simulation") != std::string::npos) {
                refusedForStaleBBefore = true;
            }
        }
        check(!staleValB.ok && refusedForStaleBBefore,
              "FIX-EXPORT-P2: un-simulated PART_B is still refused after PART_A ensure");

        std::string stalePreviewErrB;
        std::string stalePreviewB = winStale.previewFanucLs("PART_B", &stalePreviewErrB);
        check(stalePreviewB.empty() && stalePreviewErrB.find("no fresh simulation") != std::string::npos,
              "FIX-EXPORT-P2: un-simulated PART_B preview refuses with stale-sim error");

        // MAINT-001 regression: programs A and B have different IDs (prog_1, prog_2) but identical default name ("WELD_001").
        // Ensuring prog_1 must NOT grant freshness to un-simulated prog_2.
        {
            ui::MainWindow winSharedName;
            auto& doc = winSharedName.document();
            doc.controllers.clear();

            core::ControllerUnit cu;
            cu.id = "ctrl_shared";
            cu.name = "FANUC_R30iB_Shared";

            core::Program sharedA = testProg;
            sharedA.id = "prog_1";
            sharedA.name = "WELD_001";

            core::Program sharedB = testProg;
            sharedB.id = "prog_2";
            sharedB.name = "WELD_001";

            cu.programs = {sharedA, sharedB};
            doc.controllers.push_back(cu);

            core::SceneNode fixtureShared;
            fixtureShared.id = "shared_fixture";
            fixtureShared.name = "SharedFixture";
            fixtureShared.type = core::SceneNodeType::Part;
            fixtureShared.collisionRole = core::CollisionRole::Fixture;
            fixtureShared.collisionMesh.bboxMin = {-10.0, -10.0, -10.0};
            fixtureShared.collisionMesh.bboxMax = {10.0, 10.0, 10.0};
            doc.sceneNodes.push_back(fixtureShared);

            check(winSharedName.ensureProgramSimulatedForExport("prog_1"),
                  "MAINT-001: ensure succeeds for prog_1");

            auto afterA = winSharedName.validateProgramForExport("prog_1");
            bool refusedA = false;
            for (const auto& is : afterA.issues) {
                if (is.message.find("no fresh simulation") != std::string::npos) {
                    refusedA = true;
                }
            }
            check(!refusedA, "MAINT-001: prog_1 stale error is cleared after ensure");

            // prog_2 shares name WELD_001 with prog_1, but has id prog_2 and was never simulated.
            // const validateProgramForExport("prog_2") and previewFanucLs("prog_2") MUST refuse for stale sim.
            auto staleValSharedB = winSharedName.validateProgramForExport("prog_2");
            bool refusedB = false;
            for (const auto& is : staleValSharedB.issues) {
                if (is.message.find("no fresh simulation") != std::string::npos) {
                    refusedB = true;
                }
            }
            check(!staleValSharedB.ok && refusedB,
                  "MAINT-001: un-simulated prog_2 with shared name WELD_001 is still refused by validate");

            std::string previewErrSharedB;
            std::string previewSharedB = winSharedName.previewFanucLs("prog_2", &previewErrSharedB);
            check(previewSharedB.empty() && previewErrSharedB.find("no fresh simulation") != std::string::npos,
                  "MAINT-001: un-simulated prog_2 with shared name WELD_001 is still refused by preview");
        }
    }

    // FIX-EXPORT-GATE-006 / OPEN-P2-GATE005-1: exportAllPrograms must ensure each program
    // before FanucLsPostProcessor::exportControllerPrograms (same safety as exportFanucLs).
    {
        ui::MainWindow winAll;
        auto& allDoc = winAll.document();
        allDoc.controllers.clear();
        allDoc.controllers.push_back(controllerUnit);

        core::SceneNode fixture;
        fixture.id = "all_export_fixture";
        fixture.name = "AllExportFixture";
        fixture.type = core::SceneNodeType::Part;
        fixture.collisionRole = core::CollisionRole::Fixture;
        fixture.collisionMesh.bboxMin = {-10.0, -10.0, -10.0};
        fixture.collisionMesh.bboxMax = {10.0, 10.0, 10.0};
        allDoc.sceneNodes.push_back(fixture);

        check(winAll.lastSimulationResult().frames.empty(),
              "FIX-EXPORT-GATE-006: setup has empty lastSimulationResult before exportAllPrograms");

        auto staleBefore = winAll.validateProgramForExport("PART_A");
        check(!staleBefore.ok && staleBefore.hasErrors(),
              "FIX-EXPORT-GATE-006: validate refuses pre-exportAllPrograms when sim missing");

        auto staleBeforeB = winAll.validateProgramForExport("PART_B");
        check(!staleBeforeB.ok && staleBeforeB.hasErrors(),
              "FIX-EXPORT-P2: validate refuses PART_B pre-exportAllPrograms when sim missing");

        const std::string allDir =
            (std::filesystem::temp_directory_path() / "olp_ls_export_all_gate006").string();
        std::filesystem::remove_all(allDir);
        std::vector<std::string> allFiles;
        std::vector<std::string> allErrs;
        (void)winAll.exportAllPrograms(allDir, {}, &allFiles, &allErrs);

        // Regardless of write success (IK/collision may still fail validation), ensure must
        // have run so const validate no longer takes the stale-sim refuse path.
        auto afterAll = winAll.validateProgramForExport("PART_A");
        bool refusedForStaleAll = false;
        for (const auto& is : afterAll.issues) {
            if (is.message.find("no fresh simulation") != std::string::npos) {
                refusedForStaleAll = true;
            }
        }
        check(!refusedForStaleAll,
              "FIX-EXPORT-GATE-006: exportAllPrograms clears stale-sim refuse via ensure");

        // Also cover PART_B (second program under same controller) was ensured.
        auto afterAllB = winAll.validateProgramForExport("PART_B");
        bool refusedForStaleB = false;
        for (const auto& is : afterAllB.issues) {
            if (is.message.find("no fresh simulation") != std::string::npos) {
                refusedForStaleB = true;
            }
        }
        check(!refusedForStaleB,
              "FIX-EXPORT-GATE-006: exportAllPrograms ensures each program id under controller");

        ui::MainWindow winEmpty;
        std::vector<std::string> emptyErrs;
        bool emptyOk = winEmpty.exportAllPrograms(allDir, {}, nullptr, &emptyErrs);
        check(!emptyOk && !emptyErrs.empty(),
              "FIX-EXPORT-GATE-006: exportAllPrograms refuses when document has no controllers");

        std::filesystem::remove_all(allDir);
    }
}
