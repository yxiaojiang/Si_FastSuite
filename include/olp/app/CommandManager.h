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
    core::UnitsSettings settings{};
    std::vector<core::SceneNode> sceneNodes{};
    std::vector<core::SeamDefinition> seams{};
    std::vector<core::ControllerUnit> controllers{};
    bool isDirty{false};

    core::Target* findTarget(const std::string& targetId);
    core::Operation* findOperation(const std::string& operationId);
    core::SceneNode* findSceneNode(const std::string& nodeId);
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

private:
    ProjectDocument& doc_;
    std::string targetId_;
    core::Transform4d oldPose_{};
    core::Transform4d newPose_{};
    core::Transform4d oldDelta_{};
    bool oldUserModified_{false};
    bool allowMerge_{false};
    bool initialized_{false};
};

// M08: Reverse Operation Welding Path Command (automatically swaps Arc Start[n] and Arc End[n])
class ReverseOperationPathCommand final : public ICommand {
public:
    ReverseOperationPathCommand(ProjectDocument& doc, std::string operationId);
    void execute() override;
    void undo() override;
    std::string description() const override { return "Reverse Welding Path (" + operationId_ + ")"; }

private:
    ProjectDocument& doc_;
    std::string operationId_;
    void toggleReverse();
};

// M01: Undo / Redo Command Stack Manager
class CommandManager {
public:
    explicit CommandManager(size_t maxHistoryDepth = 200);

    void executeCommand(std::unique_ptr<ICommand> cmd);
    bool canUndo() const noexcept { return !undoStack_.empty(); }
    bool canRedo() const noexcept { return !redoStack_.empty(); }
    bool undo();
    bool redo();
    void clear();

    size_t undoCount() const noexcept { return undoStack_.size(); }
    size_t redoCount() const noexcept { return redoStack_.size(); }

    void setOnDocumentChangedCallback(std::function<void()> cb) { onChanged_ = std::move(cb); }

private:
    size_t maxDepth_{200};
    std::vector<std::unique_ptr<ICommand>> undoStack_{};
    std::vector<std::unique_ptr<ICommand>> redoStack_{};
    std::function<void()> onChanged_{};
};

} // namespace olp::app
