#include "olp/ui/MainWindow.h"
#include "olp/algo/TrajectorySimulator.h"
#include "olp/io/HtmlAnimationExporter.h"
#include "olp/io/ProjectSerializer.h"

#include <algorithm>

namespace olp::ui {

MainWindow::MainWindow() {
    cmdMgr_.addDocumentChangedListener([this] {
        handleDocumentChanged();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::handleDocumentChanged() {
    if (inValidation_) return;
    inValidation_ = true;
    exportSimEnsured_ = false;

    // 1. Check if selected operation still exists
    if (!selectedOperationId_.empty()) {
        if (doc_.findOperation(selectedOperationId_) == nullptr) {
            selectedOperationId_.clear();
            selectedTargetId_.clear();
            lastSimulationResult_ = algo::SimulationResult{};
        }
    }

    // 2. Check if selected target still exists
    if (!selectedTargetId_.empty()) {
        if (doc_.findTarget(selectedTargetId_) == nullptr) {
            selectedTargetId_.clear();
        }
    }

    // 3. Determine which operation to validate
    std::string opToValidate = selectedOperationId_;
    if (opToValidate.empty() && !selectedTargetId_.empty()) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    for (const auto& t : op.targets) {
                        if (t.id == selectedTargetId_) {
                            opToValidate = op.id;
                            selectedOperationId_ = op.id;
                            break;
                        }
                    }
                    if (!opToValidate.empty()) break;
                }
                if (!opToValidate.empty()) break;
            }
            if (!opToValidate.empty()) break;
        }
    }

    if (!opToValidate.empty() && doc_.findOperation(opToValidate) != nullptr) {
        validateOperation(opToValidate);
    } else {
        bool validatedAny = false;
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                if (!prog.operations.empty()) {
                    validateOperation(prog.operations.front().id);
                    validatedAny = true;
                    break;
                }
            }
            if (validatedAny) break;
        }
        if (!validatedAny) {
            lastSimulationResult_ = algo::SimulationResult{};
        }
    }

    syncRobotJointsFromSelectedTarget();
    inValidation_ = false;
}

void MainWindow::syncRobotJointsFromSelectedTarget() {
    if (selectedTargetId_.empty()) return;
    const auto* t = doc_.findTarget(selectedTargetId_);
    if (t != nullptr && t->cache.valid) {
        for (auto& node : doc_.sceneNodes) {
            if (node.type == core::SceneNodeType::Robot) {
                node.currentJointsRad = t->cache.jointsRad;
                break;
            }
        }
    }
}

void MainWindow::selectSceneNode(const std::string& nodeId) {
    if (selectedSceneNodeId_ == nodeId && selectedTargetId_.empty()) {
        return;
    }
    selectedSceneNodeId_ = nodeId;
    selectedTargetId_.clear();
    notifySelectionChanged();
}

void MainWindow::selectOperation(const std::string& operationId) {
    if (selectedOperationId_ == operationId && selectedTargetId_.empty() && selectedSceneNodeId_.empty()) {
        return;
    }
    selectedOperationId_ = operationId;
    selectedTargetId_.clear();
    selectedSceneNodeId_.clear();
    notifySelectionChanged();
}

void MainWindow::selectTarget(const std::string& targetId, const std::string& operationId) {
    if (selectedTargetId_ == targetId && (operationId.empty() || selectedOperationId_ == operationId) && selectedSceneNodeId_.empty()) {
        return;
    }
    selectedTargetId_ = targetId;
    if (!operationId.empty()) {
        selectedOperationId_ = operationId;
    } else if (!targetId.empty()) {
        std::string foundOpId;
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    for (const auto& t : op.targets) {
                        if (t.id == targetId) {
                            foundOpId = op.id;
                            break;
                        }
                    }
                    if (!foundOpId.empty()) break;
                }
                if (!foundOpId.empty()) break;
            }
            if (!foundOpId.empty()) break;
        }
        if (!foundOpId.empty()) {
            selectedOperationId_ = foundOpId;
        }
    }
    selectedSceneNodeId_.clear();

    const auto* target = doc_.findTarget(targetId);
    if (target != nullptr && target->cache.valid) {
        for (auto& node : doc_.sceneNodes) {
            if (node.type == core::SceneNodeType::Robot) {
                node.currentJointsRad = target->cache.jointsRad;
                break;
            }
        }
    }

    notifySelectionChanged();
}

bool MainWindow::stepTarget(bool forward, bool wrap) {
    const core::Operation* activeOp = nullptr;
    if (!selectedOperationId_.empty()) {
        activeOp = doc_.findOperation(selectedOperationId_);
    }
    if (activeOp == nullptr && !selectedTargetId_.empty()) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    for (const auto& t : op.targets) {
                        if (t.id == selectedTargetId_) {
                            activeOp = &op;
                            break;
                        }
                    }
                    if (activeOp) break;
                }
                if (activeOp) break;
            }
            if (activeOp) break;
        }
    }
    if (activeOp == nullptr) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                if (!prog.operations.empty()) {
                    activeOp = &prog.operations.front();
                    break;
                }
            }
            if (activeOp) break;
        }
    }
    if (activeOp == nullptr || activeOp->targets.empty()) {
        return false;
    }

    const auto& targets = activeOp->targets;
    int currIdx = -1;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].id == selectedTargetId_) {
            currIdx = static_cast<int>(i);
            break;
        }
    }

    int nextIdx = 0;
    if (currIdx < 0) {
        nextIdx = forward ? 0 : static_cast<int>(targets.size()) - 1;
    } else {
        if (forward) {
            if (currIdx + 1 < static_cast<int>(targets.size())) {
                nextIdx = currIdx + 1;
            } else {
                nextIdx = wrap ? 0 : currIdx;
            }
        } else {
            if (currIdx > 0) {
                nextIdx = currIdx - 1;
            } else {
                nextIdx = wrap ? (static_cast<int>(targets.size()) - 1) : 0;
            }
        }
    }

    if (nextIdx == currIdx && currIdx >= 0) {
        return false;
    }

    selectTarget(targets[static_cast<size_t>(nextIdx)].id, activeOp->id);
    return true;
}

bool MainWindow::generateOperationFromSelectedSeam(const core::ToolpathGenParams* params) {
    std::string seamId{};
    if (!selectedOperationId_.empty()) {
        const auto* op = doc_.findOperation(selectedOperationId_);
        if (op != nullptr && !op->seamId.empty()) {
            seamId = op->seamId;
        }
    }
    if (seamId.empty() && !doc_.seams.empty()) {
        seamId = doc_.seams.front().id;
    }
    if (seamId.empty()) {
        return false;
    }

    auto cmd = std::make_unique<app::GenerateOperationFromSeamCommand>(doc_, seamId, params);
    const std::string opId = cmd->operationId();
    cmdMgr_.executeCommand(std::move(cmd));
    selectOperation(opId);
    validateOperation(opId);
    return true;
}

bool MainWindow::smoothTargetOrientations(
    const std::string& startTargetId,
    const std::string& endTargetId,
    const std::string& operationId) {
    std::string opId = operationId.empty() ? selectedOperationId_ : operationId;
    if (opId.empty() && !startTargetId.empty()) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    for (const auto& t : op.targets) {
                        if (t.id == startTargetId) {
                            opId = op.id;
                            break;
                        }
                    }
                    if (!opId.empty()) break;
                }
                if (!opId.empty()) break;
            }
            if (!opId.empty()) break;
        }
    }
    if (opId.empty()) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                if (!prog.operations.empty()) {
                    opId = prog.operations.front().id;
                    break;
                }
            }
            if (!opId.empty()) break;
        }
    }
    if (opId.empty()) {
        return false;
    }

    auto cmd = std::make_unique<app::SmoothTargetOrientationsCommand>(
        doc_, opId, startTargetId, endTargetId);
    return cmdMgr_.executeCommand(std::move(cmd));
}

bool MainWindow::rotateTargetTcpZ(const std::string& targetId, double angleRad) {
    return rotateTargetsTcpZ({targetId}, angleRad);
}

bool MainWindow::rotateTargetsTcpZ(const std::vector<std::string>& targetIds, double angleRad) {
    if (targetIds.empty() || std::abs(angleRad) < 1e-6) {
        return false;
    }
    auto cmd = std::make_unique<app::RotateTargetAroundTcpZCommand>(doc_, targetIds, angleRad);
    return cmdMgr_.executeCommand(std::move(cmd));
}

bool MainWindow::modifyTargetProcessParams(
    const std::string& targetId,
    const app::TargetProcessParams& params) {
    return modifyTargetsProcessParams({targetId}, params);
}

bool MainWindow::modifyTargetsProcessParams(
    const std::vector<std::string>& targetIds,
    const app::TargetProcessParams& params) {
    if (targetIds.empty()) {
        return false;
    }
    auto cmd = std::make_unique<app::ModifyTargetProcessParamsCommand>(doc_, targetIds, params);
    return cmdMgr_.executeCommand(std::move(cmd));
}

algo::SimulationResult MainWindow::validateOperation(const std::string& operationId) {
    algo::SimulationResult result;
    core::Operation* op = doc_.findOperation(operationId);
    if (!op) return result;

    core::Program* prog = nullptr;
    for (auto& cu : doc_.controllers) {
        for (auto& p : cu.programs) {
            for (auto& o : p.operations) {
                if (o.id == operationId) {
                    prog = &p;
                    break;
                }
            }
            if (prog) break;
        }
        if (prog) break;
    }
    if (!prog) return result;

    core::Transform4d robotBaseInWorld = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            robotBaseInWorld = node.localTransform;
            break;
        }
    }

    const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(prog->uframeNumber);
    core::Transform4d utoolOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Tool) {
            utoolOffset = node.localTransform;
            break;
        }
    }

    algo::OpwKinematics kin(robotModel_);
    algo::CollisionAndSimEngine simEngine(kin, doc_.settings);

    result = simEngine.validateAndSimulateProgram(
        *prog,
        robotBaseInWorld,
        uframeInWorld,
        utoolOffset,
        doc_.sceneNodes,
        0.05);

    lastSimulationResult_ = result;
    return result;
}

algo::SimulationResult MainWindow::validateCurrentOperation() {
    if (selectedOperationId_.empty()) return {};
    return validateOperation(selectedOperationId_);
}

std::optional<MainWindow::TwistScanTask> MainWindow::prepareTwistScanTask(double stepDeg) const {
    if (selectedTargetId_.empty()) return std::nullopt;
    const core::Target* target = doc_.findTarget(selectedTargetId_);
    if (!target) return std::nullopt;

    const core::Operation* op = nullptr;
    const core::Program* prog = nullptr;
    for (const auto& cu : doc_.controllers) {
        for (const auto& p : cu.programs) {
            for (const auto& o : p.operations) {
                for (const auto& t : o.targets) {
                    if (t.id == selectedTargetId_) {
                        op = &o;
                        prog = &p;
                        break;
                    }
                }
                if (op) break;
            }
            if (op) break;
        }
        if (op) break;
    }
    if (!op || !prog) return std::nullopt;

    TwistScanTask task;
    task.stepDeg = stepDeg;
    task.robotModel = robotModel_;
    task.settings = doc_.settings;
    task.sceneNodes = doc_.sceneNodes;

    task.robotBaseInWorld = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    for (const auto& node : task.sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            task.robotBaseInWorld = node.localTransform;
            break;
        }
    }

    const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(prog->uframeNumber);
    task.utoolOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    for (const auto& node : task.sceneNodes) {
        if (node.type == core::SceneNodeType::Tool) {
            task.utoolOffset = node.localTransform;
            break;
        }
    }

    task.baseTargetWorldPose = uframeInWorld * target->poseInUFrame;
    task.requiredConfig = std::nullopt;
    task.refJointsRad = prog->startState.homeJointsRad;

    const bool hasPriorSolution =
        target->cache.valid ||
        target->cache.status != core::TargetValidationState::Unchecked;

    if (hasPriorSolution) {
        task.requiredConfig = target->cache.solvedConfig;
        task.refJointsRad = target->cache.jointsRad;
    }

    task.isWeldRole =
        target->role == core::TargetRole::WELD_START ||
        target->role == core::TargetRole::WELD_VIA ||
        target->role == core::TargetRole::WELD_END;

    task.targetWorkpieceId = op->targetWorkpieceId;
    task.exemptionPartIds = op->exemptionPartIds;

    return task;
}

std::vector<algo::TwistScanSample> MainWindow::executeTwistScanTask(
    const TwistScanTask& task,
    const std::shared_ptr<std::atomic<bool>>& cancelToken) {
    algo::OpwKinematics kin(task.robotModel);
    algo::CollisionAndSimEngine simEngine(kin, task.settings);

    return simEngine.scanTwistAngles(
        task.baseTargetWorldPose,
        task.robotBaseInWorld,
        task.utoolOffset,
        task.refJointsRad,
        task.requiredConfig,
        task.sceneNodes,
        task.targetWorkpieceId,
        task.stepDeg,
        task.exemptionPartIds,
        task.isWeldRole,
        cancelToken);
}

std::vector<algo::TwistScanSample> MainWindow::scanSelectedTargetTwistAngles(
    double stepDeg,
    const std::shared_ptr<std::atomic<bool>>& cancelToken) const {
    const auto task = prepareTwistScanTask(stepDeg);
    if (!task) return {};
    return executeTwistScanTask(*task, cancelToken);
}

int MainWindow::addSelectionListener(std::function<void()> listener) {
    const int id = nextSelectionListenerId_++;
    selectionListeners_.push_back(SelectionListener{id, std::move(listener)});
    return id;
}

void MainWindow::removeSelectionListener(int listenerId) {
    selectionListeners_.erase(
        std::remove_if(selectionListeners_.begin(), selectionListeners_.end(),
            [&](const SelectionListener& listener) { return listener.id == listenerId; }),
        selectionListeners_.end());
}

void MainWindow::noteSampleRun(int exitCode, int frameCount, double cycleTimeSec, double weldTimeSec) noexcept {
    lastSampleExitCode_ = exitCode;
    lastFrameCount_ = frameCount;
    lastCycleTimeSec_ = cycleTimeSec;
    lastWeldTimeSec_ = weldTimeSec;
}

void MainWindow::notifySelectionChanged() {
    const auto listeners = selectionListeners_;
    for (const auto& listener : listeners) {
        if (listener.fn) {
            listener.fn();
        }
    }
}

static const core::Program* findProgramInDoc(const app::ProjectDocument& doc, const std::string& programId) {
    for (const auto& cu : doc.controllers) {
        for (const auto& prog : cu.programs) {
            if (programId.empty() || prog.id == programId || prog.name == programId) {
                return &prog;
            }
        }
    }
    return nullptr;
}

static core::Program* findProgramInDocMutable(app::ProjectDocument& doc, const std::string& programId) {
    for (auto& cu : doc.controllers) {
        for (auto& prog : cu.programs) {
            if (programId.empty() || prog.id == programId || prog.name == programId) {
                return &prog;
            }
        }
    }
    return nullptr;
}

bool MainWindow::documentHasCollisionRelevantGeometry() const noexcept {
    for (const auto& node : doc_.sceneNodes) {
        if (node.collisionRole == core::CollisionRole::Ignored) {
            continue;
        }
        // Any non-Ignored role with a usable AABB (or Robot/Part/Fixture-like type) can
        // change mid-segment collision outcomes relative to a prior Valid cache.
        const bool hasAabb =
            (node.collisionMesh.bboxMax.x > node.collisionMesh.bboxMin.x) ||
            (node.collisionMesh.bboxMax.y > node.collisionMesh.bboxMin.y) ||
            (node.collisionMesh.bboxMax.z > node.collisionMesh.bboxMin.z);
        if (hasAabb || node.type == core::SceneNodeType::Robot ||
            node.collisionRole == core::CollisionRole::Fixture ||
            node.collisionRole == core::CollisionRole::Workpiece ||
            node.collisionRole == core::CollisionRole::Environment) {
            return true;
        }
    }
    return false;
}

bool MainWindow::exportSimulationMissingOrStale() const noexcept {
    // Freshness rule (FIX-EXPORT-GATE-005 / OPEN-P1-2):
    // When collision-relevant geometry exists, export must be backed by either:
    //   (a) a non-empty lastSimulationResult_.frames from validateOperation / ensure, or
    //   (b) exportSimEnsured_ after ensureProgramSimulatedForExport (covers empty-frame edge).
    // Otherwise Valid caches are treated as untrusted/stale for const validate/preview.
    if (!documentHasCollisionRelevantGeometry()) {
        return false;
    }
    if (exportSimEnsured_) {
        return false;
    }
    return lastSimulationResult_.frames.empty();
}

bool MainWindow::ensureProgramSimulatedForExport(const std::string& programId) {
    core::Program* prog = findProgramInDocMutable(doc_, programId);
    if (!prog) {
        return false;
    }
    // No collision geometry → nothing to re-sim for export safety (unit-test path).
    if (!documentHasCollisionRelevantGeometry()) {
        exportSimEnsured_ = true;
        return true;
    }

    core::Transform4d robotBaseInWorld = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            robotBaseInWorld = node.localTransform;
            break;
        }
    }
    const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(prog->uframeNumber);
    core::Transform4d utoolOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Tool) {
            utoolOffset = node.localTransform;
            break;
        }
    }

    algo::OpwKinematics kin(robotModel_);
    algo::CollisionAndSimEngine simEngine(kin, doc_.settings);
    lastSimulationResult_ = simEngine.validateAndSimulateProgram(
        *prog, robotBaseInWorld, uframeInWorld, utoolOffset, doc_.sceneNodes, 0.05);
    exportSimEnsured_ = true;
    return true;
}

io::LsValidationResult MainWindow::validateProgramForExport(const std::string& programId) const {
    const auto* prog = findProgramInDoc(doc_, programId);
    if (!prog) {
        io::LsValidationResult res;
        res.ok = false;
        res.issues.push_back({io::LsValidationIssue::Severity::Error, "", "", "No program found to validate"});
        return res;
    }
    // Const API cannot re-sim; refuse pure trust of Valid caches when sim is missing.
    if (exportSimulationMissingOrStale()) {
        io::LsValidationResult res;
        res.ok = false;
        res.issues.push_back({
            io::LsValidationIssue::Severity::Error,
            "",
            "",
            "Refusing export validation: collision geometry present but no fresh simulation "
            "(run validation/simulation, or use exportFanucLs / ensureProgramSimulatedForExport)"
        });
        return res;
    }
    return io::FanucLsPostProcessor::validateProgram(*prog);
}

std::string MainWindow::previewFanucLs(const std::string& programId, std::string* errorOut) const {
    const auto* prog = findProgramInDoc(doc_, programId);
    if (!prog) {
        if (errorOut) *errorOut = "No program found for preview";
        return {};
    }
    if (exportSimulationMissingOrStale()) {
        if (errorOut) {
            *errorOut =
                "Refusing .ls preview: collision geometry present but no fresh simulation "
                "(run validation/simulation first)";
        }
        return {};
    }
    return io::FanucLsPostProcessor::generateLsString(*prog, {}, errorOut);
}

bool MainWindow::exportFanucLs(
    const std::string& outputFilePath,
    const std::string& programId,
    const io::LsPostConfig& config,
    std::string* errorOut) {
    if (!ensureProgramSimulatedForExport(programId)) {
        if (errorOut) *errorOut = "No program found for export";
        return false;
    }
    const auto* prog = findProgramInDoc(doc_, programId);
    if (!prog) {
        if (errorOut) *errorOut = "No program found for export";
        return false;
    }
    return io::FanucLsPostProcessor::exportToFile(*prog, outputFilePath, config, errorOut);
}

bool MainWindow::exportAllPrograms(
    const std::string& outputDir,
    const io::LsPostConfig& config,
    std::vector<std::string>* exportedFiles,
    std::vector<std::string>* errors) {
    if (doc_.controllers.empty()) {
        if (errors) errors->push_back("Document has no controllers");
        return false;
    }
    bool allSuccess = true;
    for (const auto& cu : doc_.controllers) {
        // FIX-EXPORT-GATE-006 / OPEN-P2-GATE005-1: match exportFanucLs — with
        // collision-relevant geometry, re-sim each program before bulk export;
        // refuse this controller's export and collect errors if ensure fails.
        bool controllerEnsureOk = true;
        for (const auto& prog : cu.programs) {
            const std::string key = !prog.id.empty() ? prog.id : prog.name;
            if (!ensureProgramSimulatedForExport(key)) {
                controllerEnsureOk = false;
                allSuccess = false;
                if (errors) {
                    const std::string label = !prog.name.empty() ? prog.name
                        : (!key.empty() ? key : std::string("<unnamed>"));
                    errors->push_back(
                        "No program found for export (ensure failed): " + label);
                }
            }
        }
        if (!controllerEnsureOk) {
            continue;
        }
        if (!io::FanucLsPostProcessor::exportControllerPrograms(cu, outputDir, config, exportedFiles, errors)) {
            allSuccess = false;
        }
    }
    return allSuccess;
}

bool MainWindow::exportWorkcellPdfReport(
    const std::string& outputFilePath,
    const PdfReportOptions& options,
    std::string* errorOut) {
    return WorkcellPdfReportExporter::exportToPdf(doc_, outputFilePath, options, errorOut);
}

bool MainWindow::exportHtmlAnimation(
    const std::string& outputFilePath,
    const std::string& programId,
    std::string* errorOut) {
    const core::Program* prog = findProgramInDoc(doc_, programId);
    if (!prog) {
        if (errorOut) *errorOut = "No program found for 3D HTML animation export";
        return false;
    }

    core::Transform4d robotBaseInWorld = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    core::Transform4d utoolOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            robotBaseInWorld = node.localTransform;
        } else if (node.type == core::SceneNodeType::Tool && node.utoolNumber == prog->utoolNumber) {
            utoolOffset = (node.tcpOffset.pos.norm() > 1e-3) ? node.tcpOffset : node.localTransform;
        }
    }
    const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(prog->uframeNumber);

    algo::OpwKinematics kin(robotModel_);
    algo::TrajectorySimulator simulator(kin, robotModel_);
    const auto plan = simulator.plan(*prog, robotBaseInWorld, uframeInWorld, utoolOffset, 30.0);

    algo::SimulationResult simRes;
    simRes.totalCycleTimeSec = plan.totalCycleTimeSec;
    simRes.arcWeldTimeSec = plan.arcWeldTimeSec;
    simRes.airCutTimeSec = plan.airCutTimeSec;
    simRes.hasConfigJumpInLinear = plan.hasConfigJumpInLinear;
    simRes.frames = plan.frames;
    simRes.issues = plan.issues;

    return io::HtmlAnimationExporter::exportSingleFileHtml(
        outputFilePath, *prog, doc_.sceneNodes, simRes, &robotModel_, {}, errorOut);
}

bool MainWindow::saveProject(const std::string& filePath, std::string* errorOut) const {
    const bool ok = io::ProjectSerializer::saveToFile(doc_, filePath, {}, errorOut);
    if (ok) {
        const_cast<MainWindow*>(this)->doc_.isDirty = false;
    }
    return ok;
}

bool MainWindow::openProject(const std::string& filePath, std::string* errorOut) {
    app::ProjectDocument loadedDoc;
    if (!io::ProjectSerializer::loadFromFile(filePath, loadedDoc, errorOut)) {
        return false;
    }
    doc_ = std::move(loadedDoc);
    cmdMgr_.clear();
    selectedSceneNodeId_.clear();
    selectedOperationId_.clear();
    selectedTargetId_.clear();
    handleDocumentChanged();
    cmdMgr_.notifyDocumentChanged();
    return true;
}

} // namespace olp::ui
