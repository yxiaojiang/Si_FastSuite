#pragma once

#include "olp/core/MathTypes.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace olp::core {

// ------------------------------------------------------------------------------
// Units & Global Settings (Section 4.3: Internal unified to mm, rad, Z-up)
// ------------------------------------------------------------------------------
struct UnitsSettings {
    std::string lengthUnit{"mm"};
    std::string angleUnit{"rad"};
    double chordToleranceMm{0.1};
    double collisionSafeClearanceMm{5.0};
    double wireTipExemptionRadiusMm{10.0};
};

// ------------------------------------------------------------------------------
// Persistent B-Rep Topological Signature (Section 4.3 Rule 3 & M02)
// ------------------------------------------------------------------------------
struct TopologicalSignature {
    std::string cadFilePath{};
    std::string cadFileHash{};
    std::string partName{};
    int topologyIndex{-1};
    Vec3 startPoint{};
    Vec3 endPoint{};
    Vec3 midPoint{};
    Vec3 midTangent{};
    double arcLengthMm{0.0};

    bool matchesGeometry(const TopologicalSignature& other, double posTolMm = 0.5) const noexcept;
};

// ------------------------------------------------------------------------------
// Triangle Mesh (Render fine mesh / Collision coarse mesh)
// ------------------------------------------------------------------------------
struct TriMesh {
    std::vector<Vec3> vertices{};
    std::vector<std::array<uint32_t, 3>> triangles{};
    Vec3 bboxMin{0.0, 0.0, 0.0};
    Vec3 bboxMax{0.0, 0.0, 0.0};
};

// ------------------------------------------------------------------------------
// Scene Graph Nodes (Section 4.3: PartNode, RobotNode, ToolNode, FrameNode)
// ------------------------------------------------------------------------------
enum class SceneNodeType {
    Group,
    Part,
    Robot,
    Tool,
    Frame
};

enum class CollisionRole {
    Workpiece,
    Fixture,
    Environment,
    RobotLink,
    WeldingTorch,
    Ignored
};

struct SceneNode {
    std::string id{};
    std::string name{};
    SceneNodeType type{SceneNodeType::Group};
    CollisionRole collisionRole{CollisionRole::Environment};
    Transform4d localTransform{};
    std::string parentId{};
    bool visible{true};
    bool locked{false};

    // PartNode data
    std::string sourceCadFile{};
    int cadHandleId{-1};
    TriMesh renderMesh{};
    TriMesh collisionMesh{};

    // RobotNode data
    std::string robotModelId{};
    std::array<double, 6> currentJointsRad{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::string mountedToolNodeId{};

    // ToolNode data (UTOOL list)
    int utoolNumber{1};
    Transform4d tcpOffset{}; // Flange -> TCP transform

    // FrameNode data (UFRAME)
    int uframeNumber{1};
};

// ------------------------------------------------------------------------------
// M07: SeamDefinition (Process Geometry - decoupled from Toolpath Targets)
// ------------------------------------------------------------------------------
enum class SeamType {
    Fillet,  // T-joint / corner fillet weld (角焊)
    Lap,     // Lap weld (搭接焊)
    Butt     // Butt weld (对接焊)
};

struct SeamSampleFrame {
    double arcLengthMm{0.0};
    Vec3 position{};
    Vec3 tangentX{};     // Forward welding direction X
    Vec3 normalFace1{};  // Outward normal of flank face 1
    Vec3 normalFace2{};  // Outward normal of flank face 2
    Vec3 bisector{};     // Unit angle bisector b = normalize(n1 + n2)
    Transform4d frame{}; // Seam local coordinate frame (X=t, Z=-b pointing into seam root)
};

struct SeamDefinition {
    std::string id{};
    std::string name{};
    SeamType seamType{SeamType::Fillet};
    double filletSizeMm{6.0};
    std::vector<TopologicalSignature> edgeChain{};
    std::string face1PartId{};
    int face1Index{-1};
    std::string face2PartId{};
    int face2Index{-1};
    double startTrimMm{5.0};
    double endTrimMm{5.0};
    bool reversed{false};
    bool programmed{false};
    std::vector<SeamSampleFrame> sampledFrames{};
};

// ------------------------------------------------------------------------------
// M06 / M12: FANUC Configuration String ('N U T, 0, 0, 0')
// ------------------------------------------------------------------------------
struct FanucConfig {
    bool flip{false};      // F (true) / N (false: non-flip)
    bool up{true};         // U (true) / D (false)
    bool top{true};        // T (true) / B (false)
    std::array<int, 3> turns{0, 0, 0}; // J1, J4, J6 turn numbers

    std::string toString() const;
    bool samePosture(const FanucConfig& other) const noexcept {
        return flip == other.flip && up == other.up && top == other.top && turns == other.turns;
    }
};

// ------------------------------------------------------------------------------
// M09: Process Events & Target Data Model (Section 4.3)
// ------------------------------------------------------------------------------
enum class ProcessEventType {
    ArcStart,
    ArcEnd,
    DigitalOut,
    Wait,
    Comment
};

struct ProcessEvent {
    ProcessEventType type{ProcessEventType::Comment};
    int scheduleNumber{1};   // e.g. Arc Start[1], Arc End[1]
    double paramValue{0.0};
    std::string comment{};
};

enum class TargetRole {
    HOME,
    APPROACH,
    WELD_START,
    WELD_VIA,
    WELD_END,
    RETRACT,
    VIA
};

enum class MotionType {
    J, // Joint motion
    L, // Linear motion
    C  // Circular motion (P1)
};

enum class TerminationType {
    FINE,
    CNT
};

enum class TargetValidationState {
    Unchecked,
    Valid,
    Unreachable,
    JointLimitExceeded,
    NearJointLimit,
    Singularity,
    ConfigJump,
    Collision,
    NearCollision
};

struct TargetCache {
    bool valid{false};
    std::array<double, 6> jointsRad{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    FanucConfig solvedConfig{};
    TargetValidationState status{TargetValidationState::Unchecked};
    double minClearanceMm{1000.0};
    double manipulabilityIndex{1.0};
    std::string issueMessage{};
};

struct Target {
    std::string id{};
    std::string name{};
    Transform4d poseInUFrame{}; // Stored relative to UFRAME (Rule 1)
    TargetRole role{TargetRole::VIA};
    MotionType motion{MotionType::L};
    double speedValue{200.0};   // % for J, mm/sec for L
    bool useWeldSpeed{false};   // Output WELD_SPEED in .ls
    TerminationType termination{TerminationType::FINE};
    int cntValue{100};          // 0..100 when termination == CNT
    FanucConfig config{};
    bool lockConfig{false};
    std::vector<ProcessEvent> events{};

    // Rule 2: Coexistence of auto-generation & manual edits
    bool userModified{false};
    double arcLengthRatio{0.0};
    Transform4d userDeltaOffset{};

    // Derived cache (not serialized as source truth)
    TargetCache cache{};
};

// ------------------------------------------------------------------------------
// M08: Operation, Program & ControllerUnit
// ------------------------------------------------------------------------------
struct ToolpathGenParams {
    double stepSizeMm{20.0};
    bool endpointsOnlyForLine{true};
    double workAngleDeg{0.0};   // Tool rotation around seam X
    double travelAngleDeg{10.0}; // Push/pull angle around seam Y
    double twistAngleDeg{0.0};  // Rotation around wire axis Z
    double approachDistMm{50.0};
    double retractDistMm{50.0};
    double approachSpeedMmPerSec{200.0};
    double weldSpeedMmPerSec{10.0};
    int weldScheduleNumber{1};
};

struct Operation {
    std::string id{};
    std::string name{};
    std::string seamId{};           // Empty for non-welding air-move operations
    std::string targetWorkpieceId{}; // Legacy single id. Exemption uses exemptionPartIds when set.
    std::vector<std::string> exemptionPartIds{}; // Weld base metals. Wire tip is exempt only against these, and only inside the tip radius.
    ToolpathGenParams genParams{};
    bool enabled{true};
    std::vector<Target> targets{};
};

struct StartState {
    std::array<double, 6> homeJointsRad{0.0, 0.0, 0.0, 0.0, -kPi * 0.5, 0.0};
};

struct Program {
    std::string id{};
    std::string name{"WELD_001"};
    std::string comment{"Generated by SiFastSuite OLP"};
    int uframeNumber{1};
    int utoolNumber{1};
    StartState startState{};
    std::vector<Operation> operations{};
};

struct ControllerUnit {
    std::string id{};
    std::string name{"FANUC_R30iB_Plus"};
    std::string robotNodeId{};
    int activeUFrame{1};
    int activeUTool{1};
    std::vector<Program> programs{};
};

} // namespace olp::core
