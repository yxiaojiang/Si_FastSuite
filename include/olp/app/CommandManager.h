#pragma once

#include "olp/core/ProjectModel.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace olp::app {

// M01: Unified Project Document
class ProjectDocument {
public:
    ProjectDocument();

    core::UnitsSettings settings{};
    std::vector<core::SceneNode> sceneNodes{};
    std::vector<core::SeamDefinition> seams{};
    std::vector<core::ControllerUnit> controllers{};
    std::vector<core::WeldScheduleDefinition> weldSchedules{};
    bool isDirty{false};

    core::Target* findTarget(const std::string& targetId);
    const core::Target* findTarget(const std::string& targetId) const;
    core::Operation* findOperation(const std::string& operationId);
    const core::Operation* findOperation(const std::string& operationId) const;
    core::SceneNode* findSceneNode(const std::string& nodeId);
    const core::SceneNode* findSceneNode(const std::string& nodeId) const;
    core::SeamDefinition* findSeam(const std::string& seamId);
    const core::SeamDefinition* findSeam(const std::string& seamId) const;
    core::WeldScheduleDefinition* findWeldSchedule(int scheduleNumber);
    const core::WeldScheduleDefinition* findWeldSchedule(int scheduleNumber) const;
    core::Transform4d computeUFrameInWorld(int uframeNumber = 1) const;
};

// M01: Abstract Command Interface (all document mutations MUST go through commands)
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string description() const = 0;
    virtual int commandTypeId() const noexcept { return 0; }
    virtual bool mergeWith(const ICommand* /*other*/) { return false; }
    // False when execute changed nothing (missing target, empty drag). Such commands are not stacked.
    virtual bool didMutate() const noexcept { return true; }
};

// M01 & M03: MacroCommand for multi-selection Gizmo drag
class MacroCommand final : public ICommand {
public:
    explicit MacroCommand(std::string desc);
    void addSubCommand(std::unique_ptr<ICommand> cmd);
    void execute() override;
    void undo() override;
    std::string description() const override { return desc_; }

private:
    std::string desc_;
    std::vector<std::unique_ptr<ICommand>> subCommands_;
};

// M03 & M08: Modify Target Pose Command (marks userModified = true)
class ModifyTargetPoseCommand final : public ICommand {
public:
    ModifyTargetPoseCommand(
        ProjectDocument& doc,
        std::string targetId,
        const core::Transform4d& newPoseInUFrame,
        bool allowMerge = false);

    void execute() override;
    void undo() override;
    std::string description() const override { return "Modify Target Pose (" + targetId_ + ")"; }
    int commandTypeId() const noexcept override { return 1001; }
    bool mergeWith(const ICommand* other) override;

    bool didMutate() const noexcept override { return mutated_; }

private:
    ProjectDocument& doc_;
    std::string targetId_;
    core::Transform4d oldPose_{};
    core::Transform4d newPose_{};
    core::Transform4d oldDelta_{};
    bool oldUserModified_{false};
    bool allowMerge_{false};
    bool initialized_{false};
    bool mutated_{false};
};

// M08: Reverse Operation Welding Path Command (automatically swaps Arc Start[n] and Arc End[n])
class ReverseOperationPathCommand final : public ICommand {
public:
    ReverseOperationPathCommand(ProjectDocument& doc, std::string operationId);
    void execute() override;
    void undo() override;
    std::string description() const override { return "Reverse Welding Path (" + operationId_ + ")"; }
    bool didMutate() const noexcept override { return mutated_; }

private:
    ProjectDocument& doc_;
    std::string operationId_;
    std::vector<core::Target> before_{};
    std::vector<core::Target> after_{};
    bool snapshotTaken_{false};
    bool mutated_{false};

    static void applyReversed(std::vector<core::Target>& targets);
};

// M08: Generate Operation from Seam Command (creates or regenerates Operation from SeamDefinition)
class GenerateOperationFromSeamCommand final : public ICommand {
public:
    GenerateOperationFromSeamCommand(
        ProjectDocument& doc,
        std::string seamId,
        const core::ToolpathGenParams* params = nullptr,
        std::string programId = "");

    void execute() override;
    void undo() override;
    std::string description() const override;
    bool didMutate() const noexcept override { return mutated_; }
    const std::string& operationId() const noexcept { return operationId_; }

private:
    ProjectDocument& doc_;
    std::string seamId_;
    std::string programId_;
    bool hasCustomParams_{false};
    core::ToolpathGenParams customParams_{};

    std::string operationId_{};
    bool isNewOperation_{false};
    core::Operation beforeOp_{};
    bool beforeSeamProgrammed_{false};
    bool beforeDirty_{false};
    bool mutated_{false};
    bool initialized_{false};
    bool createdController_{false};
    bool createdProgram_{false};
};

// M08: Regenerate Operation Targets Command
class RegenerateOperationTargetsCommand final : public ICommand {
public:
    RegenerateOperationTargetsCommand(ProjectDocument& doc, std::string operationId);

    void execute() override;
    void undo() override;
    std::string description() const override { return "Regenerate Operation Targets (" + operationId_ + ")"; }
    bool didMutate() const noexcept override { return mutated_; }

private:
    ProjectDocument& doc_;
    std::string operationId_;
    std::string seamId_{};
    std::vector<core::Target> beforeTargets_{};
    bool beforeSeamProgrammed_{false};
    bool beforeDirty_{false};
    bool initialized_{false};
    bool mutated_{false};
};

// M08: Smooth Target Orientations Command (Undoable SLERP orientation smoothing)
class SmoothTargetOrientationsCommand final : public ICommand {
public:
    SmoothTargetOrientationsCommand(
        ProjectDocument& doc,
        std::string operationId,
        std::string startTargetId = "",
        std::string endTargetId = "");

    void execute() override;
    void undo() override;
    std::string description() const override;
    bool didMutate() const noexcept override { return mutated_; }

private:
    ProjectDocument& doc_;
    std::string operationId_;
    std::string startTargetId_;
    std::string endTargetId_;
    std::vector<core::Target> beforeTargets_{};
    std::vector<core::Target> afterTargets_{};
    bool beforeDirty_{false};
    bool initialized_{false};
    bool mutated_{false};
};

// ------------------------------------------------------------------------------
// M09: Process Parameters & Target Editing Commands
// ------------------------------------------------------------------------------
struct TargetProcessParams {
    std::optional<core::MotionType> motion{};
    std::optional<double> speedValue{};
    std::optional<bool> useWeldSpeed{};
    std::optional<core::TerminationType> termination{};
    std::optional<int> cntValue{};
    std::optional<std::vector<core::ProcessEvent>> events{};
};

class ModifyTargetProcessParamsCommand final : public ICommand {
public:
    ModifyTargetProcessParamsCommand(
        ProjectDocument& doc,
        std::string targetId,
        const TargetProcessParams& params);

    ModifyTargetProcessParamsCommand(
        ProjectDocument& doc,
        std::vector<std::string> targetIds,
        const TargetProcessParams& params);

    ModifyTargetProcessParamsCommand(
        ProjectDocument& doc,
        std::string targetId,
        core::MotionType motion,
        double speedValue,
        bool useWeldSpeed,
        core::TerminationType termination,
        int cntValue,
        std::vector<core::ProcessEvent> events = {});

    void execute() override;
    void undo() override;
    std::string description() const override;
    int commandTypeId() const noexcept override { return 1002; }
    bool didMutate() const noexcept override { return mutated_; }

private:
    struct TargetParamsSnapshot {
        std::string targetId{};
        core::MotionType motion{core::MotionType::L};
        double speedValue{200.0};
        bool useWeldSpeed{false};
        core::TerminationType termination{core::TerminationType::FINE};
        int cntValue{100};
        std::vector<core::ProcessEvent> events{};
        bool processModified{false};
        core::TargetCache cache{};
    };

    ProjectDocument& doc_;
    std::vector<std::string> targetIds_{};
    TargetProcessParams newParams_{};

    std::vector<TargetParamsSnapshot> beforeSnapshots_{};
    std::vector<TargetParamsSnapshot> afterSnapshots_{};
    bool beforeDirty_{false};
    bool initialized_{false};
    bool mutated_{false};
};

class RotateTargetAroundTcpZCommand final : public ICommand {
public:
    RotateTargetAroundTcpZCommand(
        ProjectDocument& doc,
        std::string targetId,
        double angleRad,
        bool allowMerge = false);

    RotateTargetAroundTcpZCommand(
        ProjectDocument& doc,
        std::vector<std::string> targetIds,
        double angleRad,
        bool allowMerge = false);

    void execute() override;
    void undo() override;
    std::string description() const override;
    int commandTypeId() const noexcept override { return 1003; }
    bool mergeWith(const ICommand* other) override;
    bool didMutate() const noexcept override { return mutated_; }

    double angleRad() const noexcept { return angleRad_; }

private:
    struct TargetPoseSnapshot {
        std::string targetId{};
        core::Transform4d poseInUFrame{};
        core::Transform4d userDeltaOffset{};
        bool userModified{false};
        core::TargetCache cache{};
    };

    ProjectDocument& doc_;
    std::vector<std::string> targetIds_{};
    double angleRad_{0.0};
    bool allowMerge_{false};

    std::vector<TargetPoseSnapshot> beforeSnapshots_{};
    std::vector<TargetPoseSnapshot> afterSnapshots_{};
    bool beforeDirty_{false};
    bool initialized_{false};
    bool mutated_{false};
};

// M01: Undo / Redo Command Stack Manager
class CommandManager {
public:
    explicit CommandManager(size_t maxHistoryDepth = 200);

    bool executeCommand(std::unique_ptr<ICommand> cmd);
    bool canUndo() const noexcept { return !undoStack_.empty(); }
    bool canRedo() const noexcept { return !redoStack_.empty(); }
    bool undo();
    bool redo();
    void clear();
    void setMergeSession(bool enabled) noexcept { mergeSession_ = enabled; }

    size_t undoCount() const noexcept { return undoStack_.size(); }
    size_t redoCount() const noexcept { return redoStack_.size(); }

    void setOnDocumentChangedCallback(std::function<void()> cb) { onChanged_ = std::move(cb); }
    int addDocumentChangedListener(std::function<void()> listener);
    void removeDocumentChangedListener(int listenerId);
    void notifyDocumentChanged();

private:

    size_t maxDepth_{200};
    std::vector<std::unique_ptr<ICommand>> undoStack_{};
    std::vector<std::unique_ptr<ICommand>> redoStack_{};
    std::function<void()> onChanged_{};
    struct DocumentListener {
        int id{0};
        std::function<void()> fn{};
    };
    std::vector<DocumentListener> listeners_{};
    int nextListenerId_{1};
    bool mergeSession_{false};
};

} // namespace olp::app
