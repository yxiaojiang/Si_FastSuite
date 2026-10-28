#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING 1
#endif

#include "olp/io/ProjectSerializer.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace olp::io {
namespace {

using json = nlohmann::json;

constexpr int kCurrentSchemaVersion = 1;
constexpr const char* kFileFormatTag = "SiFastSuite_Project";
constexpr const char* kGeneratorTag = "SiFastSuite OLP v0.1.0";

std::filesystem::path utf8ToPath(const std::string& utf8Str) {
#ifdef _WIN32
    if (utf8Str.empty()) return {};
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) return std::filesystem::u8path(utf8Str);
    std::wstring wideStr(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, wideStr.data(), wideLen);
    if (!wideStr.empty() && wideStr.back() == L'\0') wideStr.pop_back();
    return std::filesystem::path(wideStr);
#else
    return std::filesystem::u8path(utf8Str);
#endif
}

// --- Math & Vector Helpers ---

json serializeVec3(const core::Vec3& v) {
    return json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

core::Vec3 parseVec3(const json& j, const core::Vec3& def = {}) {
    if (j.is_object()) return {j.value("x", def.x), j.value("y", def.y), j.value("z", def.z)};
    if (j.is_array() && j.size() >= 3) {
        double x = j[0].is_number() ? j[0].get<double>() : def.x;
        double y = j[1].is_number() ? j[1].get<double>() : def.y;
        double z = j[2].is_number() ? j[2].get<double>() : def.z;
        return {x, y, z};
    }
    return def;
}

json serializeMat3(const core::Mat3& m) {
    return json{{m.m[0][0], m.m[0][1], m.m[0][2]},
                {m.m[1][0], m.m[1][1], m.m[1][2]},
                {m.m[2][0], m.m[2][1], m.m[2][2]}};
}

core::Mat3 parseMat3(const json& j, const core::Mat3& def = {}) {
    if (!j.is_array()) return def;
    core::Mat3 m = def;
    if (j.size() == 3 && j[0].is_array()) {
        for (int r = 0; r < 3; ++r) {
            if (j[r].is_array() && j[r].size() >= 3) {
                for (int c = 0; c < 3; ++c) {
                    if (j[r][c].is_number()) m.m[r][c] = j[r][c].get<double>();
                }
            }
        }
    } else if (j.size() >= 9) {
        for (int i = 0; i < 9; ++i) {
            if (j[i].is_number()) m.m[i / 3][i % 3] = j[i].get<double>();
        }
    }
    return m;
}

json serializeTransform(const core::Transform4d& t) {
    const auto w = t.toWPR();
    return json{
        {"pos", serializeVec3(t.pos)},
        {"rot", serializeMat3(t.rot)},
        {"wpr", {{"x", w.x}, {"y", w.y}, {"z", w.z}, {"wDeg", w.wDeg}, {"pDeg", w.pDeg}, {"rDeg", w.rDeg}}}
    };
}

core::Transform4d parseTransform(const json& j, const core::Transform4d& def = {}) {
    if (!j.is_object()) return def;
    core::Transform4d t = def;
    if (j.contains("pos")) t.pos = parseVec3(j["pos"], def.pos);
    if (j.contains("rot")) {
        t.rot = parseMat3(j["rot"], def.rot);
    } else if (j.contains("wpr") && j["wpr"].is_object()) {
        const auto& wj = j["wpr"];
        core::WprPose wpr{wj.value("x", t.pos.x), wj.value("y", t.pos.y), wj.value("z", t.pos.z),
                          wj.value("wDeg", 0.0), wj.value("pDeg", 0.0), wj.value("rDeg", 0.0)};
        t = core::Transform4d::fromWPR(wpr);
    }
    return t;
}

// --- UnitsSettings ---

json serializeUnits(const core::UnitsSettings& u) {
    return json{{"lengthUnit", u.lengthUnit}, {"angleUnit", u.angleUnit},
                {"chordToleranceMm", u.chordToleranceMm},
                {"collisionSafeClearanceMm", u.collisionSafeClearanceMm},
                {"wireTipExemptionRadiusMm", u.wireTipExemptionRadiusMm}};
}

core::UnitsSettings parseUnits(const json& j) {
    core::UnitsSettings u;
    if (!j.is_object()) return u;
    u.lengthUnit = j.value("lengthUnit", u.lengthUnit);
    u.angleUnit = j.value("angleUnit", u.angleUnit);
    u.chordToleranceMm = j.value("chordToleranceMm", u.chordToleranceMm);
    u.collisionSafeClearanceMm = j.value("collisionSafeClearanceMm", u.collisionSafeClearanceMm);
    u.wireTipExemptionRadiusMm = j.value("wireTipExemptionRadiusMm", u.wireTipExemptionRadiusMm);
    return u;
}

// --- TopologicalSignature & Mesh ---

json serializeTopoSig(const core::TopologicalSignature& s) {
    return json{{"cadFilePath", s.cadFilePath}, {"cadFileHash", s.cadFileHash},
                {"partName", s.partName}, {"topologyIndex", s.topologyIndex},
                {"startPoint", serializeVec3(s.startPoint)}, {"endPoint", serializeVec3(s.endPoint)},
                {"midPoint", serializeVec3(s.midPoint)}, {"midTangent", serializeVec3(s.midTangent)},
                {"arcLengthMm", s.arcLengthMm}};
}

core::TopologicalSignature parseTopoSig(const json& j) {
    core::TopologicalSignature s;
    if (!j.is_object()) return s;
    s.cadFilePath = j.value("cadFilePath", "");
    s.cadFileHash = j.value("cadFileHash", "");
    s.partName = j.value("partName", "");
    s.topologyIndex = j.value("topologyIndex", -1);
    if (j.contains("startPoint")) s.startPoint = parseVec3(j["startPoint"]);
    if (j.contains("endPoint")) s.endPoint = parseVec3(j["endPoint"]);
    if (j.contains("midPoint")) s.midPoint = parseVec3(j["midPoint"]);
    if (j.contains("midTangent")) s.midTangent = parseVec3(j["midTangent"]);
    s.arcLengthMm = j.value("arcLengthMm", 0.0);
    return s;
}

json serializeMesh(const core::TriMesh& m) {
    json j, vArr = json::array(), tArr = json::array();
    for (const auto& v : m.vertices) vArr.push_back({v.x, v.y, v.z});
    for (const auto& tri : m.triangles) tArr.push_back({tri[0], tri[1], tri[2]});
    j["vertices"] = std::move(vArr);
    j["triangles"] = std::move(tArr);
    j["bboxMin"] = serializeVec3(m.bboxMin);
    j["bboxMax"] = serializeVec3(m.bboxMax);
    return j;
}

core::TriMesh parseMesh(const json& j) {
    core::TriMesh m;
    if (!j.is_object()) return m;
    if (j.contains("vertices") && j["vertices"].is_array()) {
        m.vertices.reserve(j["vertices"].size());
        for (const auto& v : j["vertices"]) m.vertices.push_back(parseVec3(v));
    }
    if (j.contains("triangles") && j["triangles"].is_array()) {
        m.triangles.reserve(j["triangles"].size());
        for (const auto& t : j["triangles"]) {
            if (t.is_array() && t.size() >= 3 &&
                t[0].is_number() && t[1].is_number() && t[2].is_number()) {
                m.triangles.push_back({t[0].get<uint32_t>(), t[1].get<uint32_t>(), t[2].get<uint32_t>()});
            }
        }
    }
    if (j.contains("bboxMin")) m.bboxMin = parseVec3(j["bboxMin"]);
    if (j.contains("bboxMax")) m.bboxMax = parseVec3(j["bboxMax"]);
    return m;
}

// --- SceneNode & Enums ---

std::string sceneNodeTypeToString(core::SceneNodeType t) {
    switch (t) {
        case core::SceneNodeType::Part:  return "Part";
        case core::SceneNodeType::Robot: return "Robot";
        case core::SceneNodeType::Tool:  return "Tool";
        case core::SceneNodeType::Frame: return "Frame";
        default:                         return "Group";
    }
}

core::SceneNodeType parseSceneNodeType(const std::string& s) {
    if (s == "Part")  return core::SceneNodeType::Part;
    if (s == "Robot") return core::SceneNodeType::Robot;
    if (s == "Tool")  return core::SceneNodeType::Tool;
    if (s == "Frame") return core::SceneNodeType::Frame;
    return core::SceneNodeType::Group;
}

std::string collisionRoleToString(core::CollisionRole r) {
    switch (r) {
        case core::CollisionRole::Workpiece:    return "Workpiece";
        case core::CollisionRole::Fixture:      return "Fixture";
        case core::CollisionRole::RobotLink:    return "RobotLink";
        case core::CollisionRole::WeldingTorch: return "WeldingTorch";
        case core::CollisionRole::Ignored:      return "Ignored";
        default:                                return "Environment";
    }
}

core::CollisionRole parseCollisionRole(const std::string& s) {
    if (s == "Workpiece")    return core::CollisionRole::Workpiece;
    if (s == "Fixture")      return core::CollisionRole::Fixture;
    if (s == "RobotLink")    return core::CollisionRole::RobotLink;
    if (s == "WeldingTorch") return core::CollisionRole::WeldingTorch;
    if (s == "Ignored")      return core::CollisionRole::Ignored;
    return core::CollisionRole::Environment;
}

json serializeSceneNode(const core::SceneNode& n, const ProjectSerializeOptions& opt) {
    json j{{"id", n.id}, {"name", n.name}, {"type", sceneNodeTypeToString(n.type)},
           {"collisionRole", collisionRoleToString(n.collisionRole)},
           {"localTransform", serializeTransform(n.localTransform)},
           {"parentId", n.parentId}, {"visible", n.visible}, {"locked", n.locked}};
    if (n.type == core::SceneNodeType::Part) {
        j["sourceCadFile"] = n.sourceCadFile;
        j["cadHandleId"] = n.cadHandleId;
        if (opt.saveMeshes) {
            if (!n.renderMesh.vertices.empty()) j["renderMesh"] = serializeMesh(n.renderMesh);
            if (!n.collisionMesh.vertices.empty()) j["collisionMesh"] = serializeMesh(n.collisionMesh);
        }
    } else if (n.type == core::SceneNodeType::Robot) {
        j["robotModelId"] = n.robotModelId;
        j["currentJointsRad"] = n.currentJointsRad;
        j["mountedToolNodeId"] = n.mountedToolNodeId;
    } else if (n.type == core::SceneNodeType::Tool) {
        j["utoolNumber"] = n.utoolNumber;
        j["tcpOffset"] = serializeTransform(n.tcpOffset);
    } else if (n.type == core::SceneNodeType::Frame) {
        j["uframeNumber"] = n.uframeNumber;
    }
    return j;
}

core::SceneNode parseSceneNode(const json& j) {
    core::SceneNode n;
    if (!j.is_object()) return n;
    n.id = j.value("id", "");
    n.name = j.value("name", "");
    n.type = parseSceneNodeType(j.value("type", "Group"));
    n.collisionRole = parseCollisionRole(j.value("collisionRole", "Environment"));
    if (j.contains("localTransform")) n.localTransform = parseTransform(j["localTransform"]);
    n.parentId = j.value("parentId", "");
    n.visible = j.value("visible", true);
    n.locked = j.value("locked", false);
    n.sourceCadFile = j.value("sourceCadFile", "");
    n.cadHandleId = j.value("cadHandleId", -1);
    if (j.contains("renderMesh")) n.renderMesh = parseMesh(j["renderMesh"]);
    if (j.contains("collisionMesh")) n.collisionMesh = parseMesh(j["collisionMesh"]);
    n.robotModelId = j.value("robotModelId", "");
    if (j.contains("currentJointsRad") && j["currentJointsRad"].is_array() && j["currentJointsRad"].size() >= 6) {
        for (size_t i = 0; i < 6; ++i) {
            if (j["currentJointsRad"][i].is_number()) {
                n.currentJointsRad[i] = j["currentJointsRad"][i].get<double>();
            }
        }
    }
    n.mountedToolNodeId = j.value("mountedToolNodeId", "");
    n.utoolNumber = j.value("utoolNumber", 1);
    if (j.contains("tcpOffset")) n.tcpOffset = parseTransform(j["tcpOffset"]);
    n.uframeNumber = j.value("uframeNumber", 1);
    return n;
}

// --- SeamDefinition & Enums ---

std::string seamTypeToString(core::SeamType t) {
    if (t == core::SeamType::Lap) return "Lap";
    if (t == core::SeamType::Butt) return "Butt";
    return "Fillet";
}

core::SeamType parseSeamType(const std::string& s) {
    if (s == "Lap") return core::SeamType::Lap;
    if (s == "Butt") return core::SeamType::Butt;
    return core::SeamType::Fillet;
}

json serializeSeamSampleFrame(const core::SeamSampleFrame& f) {
    return json{{"arcLengthMm", f.arcLengthMm}, {"position", serializeVec3(f.position)},
                {"tangentX", serializeVec3(f.tangentX)}, {"normalFace1", serializeVec3(f.normalFace1)},
                {"normalFace2", serializeVec3(f.normalFace2)}, {"bisector", serializeVec3(f.bisector)},
                {"frame", serializeTransform(f.frame)}};
}

core::SeamSampleFrame parseSeamSampleFrame(const json& j) {
    core::SeamSampleFrame f;
    if (!j.is_object()) return f;
    f.arcLengthMm = j.value("arcLengthMm", 0.0);
    if (j.contains("position")) f.position = parseVec3(j["position"]);
    if (j.contains("tangentX")) f.tangentX = parseVec3(j["tangentX"]);
    if (j.contains("normalFace1")) f.normalFace1 = parseVec3(j["normalFace1"]);
    if (j.contains("normalFace2")) f.normalFace2 = parseVec3(j["normalFace2"]);
    if (j.contains("bisector")) f.bisector = parseVec3(j["bisector"]);
    if (j.contains("frame")) f.frame = parseTransform(j["frame"]);
    return f;
}

json serializeSeam(const core::SeamDefinition& s) {
    json edges = json::array(), frames = json::array();
    for (const auto& sig : s.edgeChain) edges.push_back(serializeTopoSig(sig));
    for (const auto& sf : s.sampledFrames) frames.push_back(serializeSeamSampleFrame(sf));
    return json{{"id", s.id}, {"name", s.name}, {"seamType", seamTypeToString(s.seamType)},
                {"filletSizeMm", s.filletSizeMm}, {"edgeChain", std::move(edges)},
                {"face1PartId", s.face1PartId}, {"face1Index", s.face1Index},
                {"face2PartId", s.face2PartId}, {"face2Index", s.face2Index},
                {"startTrimMm", s.startTrimMm}, {"endTrimMm", s.endTrimMm},
                {"isClosed", s.isClosed}, {"reversed", s.reversed},
                {"programmed", s.programmed}, {"sampledFrames", std::move(frames)}};
}

core::SeamDefinition parseSeam(const json& j) {
    core::SeamDefinition s;
    if (!j.is_object()) return s;
    s.id = j.value("id", "");
    s.name = j.value("name", "");
    s.seamType = parseSeamType(j.value("seamType", "Fillet"));
    s.filletSizeMm = j.value("filletSizeMm", 6.0);
    if (j.contains("edgeChain") && j["edgeChain"].is_array()) {
        s.edgeChain.reserve(j["edgeChain"].size());
        for (const auto& e : j["edgeChain"]) s.edgeChain.push_back(parseTopoSig(e));
    }
    s.face1PartId = j.value("face1PartId", "");
    s.face1Index = j.value("face1Index", -1);
    s.face2PartId = j.value("face2PartId", "");
    s.face2Index = j.value("face2Index", -1);
    s.startTrimMm = j.value("startTrimMm", 5.0);
    s.endTrimMm = j.value("endTrimMm", 5.0);
    s.isClosed = j.value("isClosed", false);
    s.reversed = j.value("reversed", false);
    s.programmed = j.value("programmed", false);
    if (j.contains("sampledFrames") && j["sampledFrames"].is_array()) {
        s.sampledFrames.reserve(j["sampledFrames"].size());
        for (const auto& sf : j["sampledFrames"]) s.sampledFrames.push_back(parseSeamSampleFrame(sf));
    }
    return s;
}

// --- WeldScheduleDefinition & FanucConfig ---

json serializeWeldSchedule(const core::WeldScheduleDefinition& w) {
    return json{{"scheduleNumber", w.scheduleNumber}, {"currentAmps", w.currentAmps},
                {"voltageVolts", w.voltageVolts}, {"wireFeedSpeedMPerMin", w.wireFeedSpeedMPerMin},
                {"weldSpeedCmPerMin", w.weldSpeedCmPerMin}, {"weldSpeedMmPerSec", w.weldSpeedMmPerSec},
                {"gasFlowRateLPerMin", w.gasFlowRateLPerMin}, {"description", w.description}};
}

core::WeldScheduleDefinition parseWeldSchedule(const json& j) {
    core::WeldScheduleDefinition w;
    if (!j.is_object()) return w;
    w.scheduleNumber = j.value("scheduleNumber", 1);
    w.currentAmps = j.value("currentAmps", 180.0);
    w.voltageVolts = j.value("voltageVolts", 22.0);
    w.wireFeedSpeedMPerMin = j.value("wireFeedSpeedMPerMin", 6.0);
    w.weldSpeedCmPerMin = j.value("weldSpeedCmPerMin", 50.0);
    w.weldSpeedMmPerSec = j.value("weldSpeedMmPerSec", 500.0 / 60.0);
    w.gasFlowRateLPerMin = j.value("gasFlowRateLPerMin", 15.0);
    w.description = j.value("description", "");
    return w;
}

json serializeFanucConfig(const core::FanucConfig& c) {
    return json{{"flip", c.flip}, {"up", c.up}, {"top", c.top}, {"turns", c.turns}, {"configString", c.toString()}};
}

core::FanucConfig parseFanucConfig(const json& j) {
    core::FanucConfig c;
    if (!j.is_object()) return c;
    c.flip = j.value("flip", false);
    c.up = j.value("up", true);
    c.top = j.value("top", true);
    if (j.contains("turns") && j["turns"].is_array() && j["turns"].size() >= 3) {
        if (j["turns"][0].is_number() && j["turns"][1].is_number() && j["turns"][2].is_number()) {
            c.turns = {j["turns"][0].get<int>(), j["turns"][1].get<int>(), j["turns"][2].get<int>()};
        }
    }
    return c;
}

// --- ProcessEvent & Enums ---

std::string processEventTypeToString(core::ProcessEventType t) {
    switch (t) {
        case core::ProcessEventType::ArcStart:   return "ArcStart";
        case core::ProcessEventType::ArcEnd:     return "ArcEnd";
        case core::ProcessEventType::WeaveStart: return "WeaveStart";
        case core::ProcessEventType::WeaveEnd:   return "WeaveEnd";
        case core::ProcessEventType::DigitalOut: return "DigitalOut";
        case core::ProcessEventType::Wait:       return "Wait";
        default:                                 return "Comment";
    }
}

core::ProcessEventType parseProcessEventType(const std::string& s) {
    if (s == "ArcStart")   return core::ProcessEventType::ArcStart;
    if (s == "ArcEnd")     return core::ProcessEventType::ArcEnd;
    if (s == "WeaveStart") return core::ProcessEventType::WeaveStart;
    if (s == "WeaveEnd")   return core::ProcessEventType::WeaveEnd;
    if (s == "DigitalOut") return core::ProcessEventType::DigitalOut;
    if (s == "Wait")       return core::ProcessEventType::Wait;
    return core::ProcessEventType::Comment;
}

json serializeProcessEvent(const core::ProcessEvent& e) {
    return json{{"type", processEventTypeToString(e.type)}, {"scheduleNumber", e.scheduleNumber},
                {"paramValue", e.paramValue}, {"comment", e.comment}};
}

core::ProcessEvent parseProcessEvent(const json& j) {
    core::ProcessEvent e;
    if (!j.is_object()) return e;
    e.type = parseProcessEventType(j.value("type", "Comment"));
    e.scheduleNumber = j.value("scheduleNumber", 1);
    e.paramValue = j.value("paramValue", 0.0);
    e.comment = j.value("comment", "");
    return e;
}

// --- Target & Enums ---

std::string targetRoleToString(core::TargetRole r) {
    switch (r) {
        case core::TargetRole::HOME:       return "HOME";
        case core::TargetRole::APPROACH:   return "APPROACH";
        case core::TargetRole::WELD_START: return "WELD_START";
        case core::TargetRole::WELD_VIA:   return "WELD_VIA";
        case core::TargetRole::WELD_END:   return "WELD_END";
        case core::TargetRole::RETRACT:    return "RETRACT";
        default:                           return "VIA";
    }
}

core::TargetRole parseTargetRole(const std::string& s) {
    if (s == "HOME")       return core::TargetRole::HOME;
    if (s == "APPROACH")   return core::TargetRole::APPROACH;
    if (s == "WELD_START") return core::TargetRole::WELD_START;
    if (s == "WELD_VIA")   return core::TargetRole::WELD_VIA;
    if (s == "WELD_END")   return core::TargetRole::WELD_END;
    if (s == "RETRACT")    return core::TargetRole::RETRACT;
    return core::TargetRole::VIA;
}

std::string motionTypeToString(core::MotionType m) {
    if (m == core::MotionType::J) return "J";
    if (m == core::MotionType::C) return "C";
    return "L";
}

core::MotionType parseMotionType(const std::string& s) {
    if (s == "J") return core::MotionType::J;
    if (s == "C") return core::MotionType::C;
    return core::MotionType::L;
}

std::string validationStateToString(core::TargetValidationState s) {
    switch (s) {
        case core::TargetValidationState::Valid:              return "Valid";
        case core::TargetValidationState::Unreachable:        return "Unreachable";
        case core::TargetValidationState::JointLimitExceeded: return "JointLimitExceeded";
        case core::TargetValidationState::NearJointLimit:     return "NearJointLimit";
        case core::TargetValidationState::Singularity:        return "Singularity";
        case core::TargetValidationState::ConfigJump:         return "ConfigJump";
        case core::TargetValidationState::Collision:          return "Collision";
        case core::TargetValidationState::NearCollision:      return "NearCollision";
        default:                                              return "Unchecked";
    }
}

core::TargetValidationState parseValidationState(const std::string& s) {
    if (s == "Valid")              return core::TargetValidationState::Valid;
    if (s == "Unreachable")        return core::TargetValidationState::Unreachable;
    if (s == "JointLimitExceeded") return core::TargetValidationState::JointLimitExceeded;
    if (s == "NearJointLimit")     return core::TargetValidationState::NearJointLimit;
    if (s == "Singularity")        return core::TargetValidationState::Singularity;
    if (s == "ConfigJump")         return core::TargetValidationState::ConfigJump;
    if (s == "Collision")          return core::TargetValidationState::Collision;
    if (s == "NearCollision")      return core::TargetValidationState::NearCollision;
    return core::TargetValidationState::Unchecked;
}

json serializeTargetCache(const core::TargetCache& c) {
    return json{{"valid", c.valid}, {"jointsRad", c.jointsRad},
                {"solvedConfig", serializeFanucConfig(c.solvedConfig)},
                {"status", validationStateToString(c.status)},
                {"minClearanceMm", c.minClearanceMm},
                {"manipulabilityIndex", c.manipulabilityIndex},
                {"issueMessage", c.issueMessage}};
}

core::TargetCache parseTargetCache(const json& j) {
    core::TargetCache c;
    if (!j.is_object()) return c;
    c.valid = j.value("valid", false);
    if (j.contains("jointsRad") && j["jointsRad"].is_array() && j["jointsRad"].size() >= 6) {
        for (size_t i = 0; i < 6; ++i) {
            if (j["jointsRad"][i].is_number()) c.jointsRad[i] = j["jointsRad"][i].get<double>();
        }
    }
    if (j.contains("solvedConfig")) c.solvedConfig = parseFanucConfig(j["solvedConfig"]);
    c.status = parseValidationState(j.value("status", "Unchecked"));
    c.minClearanceMm = j.value("minClearanceMm", 1000.0);
    c.manipulabilityIndex = j.value("manipulabilityIndex", 1.0);
    c.issueMessage = j.value("issueMessage", "");
    return c;
}

json serializeTarget(const core::Target& t) {
    json evArr = json::array();
    for (const auto& ev : t.events) evArr.push_back(serializeProcessEvent(ev));
    return json{{"id", t.id}, {"name", t.name},
                {"poseInUFrame", serializeTransform(t.poseInUFrame)},
                {"role", targetRoleToString(t.role)},
                {"motion", motionTypeToString(t.motion)},
                {"speedValue", t.speedValue},
                {"useWeldSpeed", t.useWeldSpeed},
                {"termination", (t.termination == core::TerminationType::CNT ? "CNT" : "FINE")},
                {"cntValue", t.cntValue},
                {"config", serializeFanucConfig(t.config)},
                {"lockConfig", t.lockConfig},
                {"events", std::move(evArr)},
                {"userModified", t.userModified},
                {"processModified", t.processModified},
                {"arcLengthRatio", t.arcLengthRatio},
                {"userDeltaOffset", serializeTransform(t.userDeltaOffset)},
                {"cache", serializeTargetCache(t.cache)}};
}

core::Target parseTarget(const json& j) {
    core::Target t;
    if (!j.is_object()) return t;
    t.id = j.value("id", "");
    t.name = j.value("name", "");
    if (j.contains("poseInUFrame")) t.poseInUFrame = parseTransform(j["poseInUFrame"]);
    t.role = parseTargetRole(j.value("role", "VIA"));
    t.motion = parseMotionType(j.value("motion", "L"));
    t.speedValue = j.value("speedValue", 200.0);
    t.useWeldSpeed = j.value("useWeldSpeed", false);
    t.termination = (j.value("termination", "FINE") == "CNT") ? core::TerminationType::CNT : core::TerminationType::FINE;
    t.cntValue = j.value("cntValue", 100);
    if (j.contains("config")) t.config = parseFanucConfig(j["config"]);
    t.lockConfig = j.value("lockConfig", false);
    if (j.contains("events") && j["events"].is_array()) {
        t.events.reserve(j["events"].size());
        for (const auto& ev : j["events"]) t.events.push_back(parseProcessEvent(ev));
    }
    t.userModified = j.value("userModified", false);
    t.processModified = j.value("processModified", false);
    t.arcLengthRatio = j.value("arcLengthRatio", 0.0);
    if (j.contains("userDeltaOffset")) t.userDeltaOffset = parseTransform(j["userDeltaOffset"]);
    if (j.contains("cache")) t.cache = parseTargetCache(j["cache"]);
    return t;
}

// --- ToolpathGenParams, Operation, Program, ControllerUnit ---

json serializeGenParams(const core::ToolpathGenParams& p) {
    return json{{"stepSizeMm", p.stepSizeMm}, {"endpointsOnlyForLine", p.endpointsOnlyForLine},
                {"workAngleDeg", p.workAngleDeg}, {"travelAngleDeg", p.travelAngleDeg},
                {"twistAngleDeg", p.twistAngleDeg}, {"approachDistMm", p.approachDistMm},
                {"retractDistMm", p.retractDistMm}, {"approachSpeedMmPerSec", p.approachSpeedMmPerSec},
                {"weldSpeedMmPerSec", p.weldSpeedMmPerSec}, {"weldScheduleNumber", p.weldScheduleNumber},
                {"approachMotion", motionTypeToString(p.approachMotion)},
                {"retractMotion", motionTypeToString(p.retractMotion)},
                {"retractSpeedMmPerSec", p.retractSpeedMmPerSec}};
}

core::ToolpathGenParams parseGenParams(const json& j) {
    core::ToolpathGenParams p;
    if (!j.is_object()) return p;
    p.stepSizeMm = j.value("stepSizeMm", p.stepSizeMm);
    p.endpointsOnlyForLine = j.value("endpointsOnlyForLine", p.endpointsOnlyForLine);
    p.workAngleDeg = j.value("workAngleDeg", p.workAngleDeg);
    p.travelAngleDeg = j.value("travelAngleDeg", p.travelAngleDeg);
    p.twistAngleDeg = j.value("twistAngleDeg", p.twistAngleDeg);
    p.approachDistMm = j.value("approachDistMm", p.approachDistMm);
    p.retractDistMm = j.value("retractDistMm", p.retractDistMm);
    p.approachSpeedMmPerSec = j.value("approachSpeedMmPerSec", p.approachSpeedMmPerSec);
    p.weldSpeedMmPerSec = j.value("weldSpeedMmPerSec", p.weldSpeedMmPerSec);
    p.weldScheduleNumber = j.value("weldScheduleNumber", p.weldScheduleNumber);
    if (j.contains("approachMotion")) p.approachMotion = parseMotionType(j["approachMotion"]);
    if (j.contains("retractMotion")) p.retractMotion = parseMotionType(j["retractMotion"]);
    p.retractSpeedMmPerSec = j.value("retractSpeedMmPerSec", p.retractSpeedMmPerSec);
    return p;
}

json serializeOperation(const core::Operation& op) {
    json targets = json::array();
    for (const auto& t : op.targets) targets.push_back(serializeTarget(t));
    return json{{"id", op.id}, {"name", op.name}, {"seamId", op.seamId},
                {"targetWorkpieceId", op.targetWorkpieceId},
                {"exemptionPartIds", op.exemptionPartIds},
                {"genParams", serializeGenParams(op.genParams)},
                {"enabled", op.enabled}, {"targets", std::move(targets)}};
}

core::Operation parseOperation(const json& j) {
    core::Operation op;
    if (!j.is_object()) return op;
    op.id = j.value("id", "");
    op.name = j.value("name", "");
    op.seamId = j.value("seamId", "");
    op.targetWorkpieceId = j.value("targetWorkpieceId", "");
    if (j.contains("exemptionPartIds") && j["exemptionPartIds"].is_array()) {
        op.exemptionPartIds = j["exemptionPartIds"].get<std::vector<std::string>>();
    }
    if (j.contains("genParams")) op.genParams = parseGenParams(j["genParams"]);
    op.enabled = j.value("enabled", true);
    if (j.contains("targets") && j["targets"].is_array()) {
        op.targets.reserve(j["targets"].size());
        for (const auto& tj : j["targets"]) op.targets.push_back(parseTarget(tj));
    }
    return op;
}

json serializeProgram(const core::Program& prog) {
    json ops = json::array();
    for (const auto& op : prog.operations) ops.push_back(serializeOperation(op));
    return json{{"id", prog.id}, {"name", prog.name}, {"comment", prog.comment},
                {"uframeNumber", prog.uframeNumber}, {"utoolNumber", prog.utoolNumber},
                {"startState", {{"homeJointsRad", prog.startState.homeJointsRad}}},
                {"operations", std::move(ops)}};
}

core::Program parseProgram(const json& j) {
    core::Program p;
    if (!j.is_object()) return p;
    p.id = j.value("id", "");
    p.name = j.value("name", "WELD_001");
    p.comment = j.value("comment", "");
    p.uframeNumber = j.value("uframeNumber", 1);
    p.utoolNumber = j.value("utoolNumber", 1);
    if (j.contains("startState") && j["startState"].is_object() &&
        j["startState"].contains("homeJointsRad") && j["startState"]["homeJointsRad"].is_array()) {
        for (size_t i = 0; i < 6 && i < j["startState"]["homeJointsRad"].size(); ++i) {
            if (j["startState"]["homeJointsRad"][i].is_number()) {
                p.startState.homeJointsRad[i] = j["startState"]["homeJointsRad"][i].get<double>();
            }
        }
    }
    if (j.contains("operations") && j["operations"].is_array()) {
        p.operations.reserve(j["operations"].size());
        for (const auto& opJ : j["operations"]) p.operations.push_back(parseOperation(opJ));
    }
    return p;
}

json serializeController(const core::ControllerUnit& cu) {
    json progs = json::array();
    for (const auto& p : cu.programs) progs.push_back(serializeProgram(p));
    return json{{"id", cu.id}, {"name", cu.name}, {"robotNodeId", cu.robotNodeId},
                {"activeUFrame", cu.activeUFrame}, {"activeUTool", cu.activeUTool},
                {"programs", std::move(progs)}};
}

core::ControllerUnit parseController(const json& j) {
    core::ControllerUnit cu;
    if (!j.is_object()) return cu;
    cu.id = j.value("id", "");
    cu.name = j.value("name", "FANUC_R30iB_Plus");
    cu.robotNodeId = j.value("robotNodeId", "");
    cu.activeUFrame = j.value("activeUFrame", 1);
    cu.activeUTool = j.value("activeUTool", 1);
    if (j.contains("programs") && j["programs"].is_array()) {
        cu.programs.reserve(j["programs"].size());
        for (const auto& pj : j["programs"]) cu.programs.push_back(parseProgram(pj));
    }
    return cu;
}

} // namespace

std::string ProjectSerializer::toJsonString(
    const app::ProjectDocument& doc,
    const ProjectSerializeOptions& options) {
    json root;
    root["fileFormat"] = kFileFormatTag;
    root["schemaVersion"] = kCurrentSchemaVersion;
    root["generator"] = kGeneratorTag;

    root["settings"] = serializeUnits(doc.settings);

    json nodesArr = json::array();
    for (const auto& n : doc.sceneNodes) nodesArr.push_back(serializeSceneNode(n, options));
    root["sceneNodes"] = std::move(nodesArr);

    json seamsArr = json::array();
    for (const auto& s : doc.seams) seamsArr.push_back(serializeSeam(s));
    root["seams"] = std::move(seamsArr);

    json schedulesArr = json::array();
    for (const auto& ws : doc.weldSchedules) schedulesArr.push_back(serializeWeldSchedule(ws));
    root["weldSchedules"] = std::move(schedulesArr);

    json controllersArr = json::array();
    for (const auto& cu : doc.controllers) controllersArr.push_back(serializeController(cu));
    root["controllers"] = std::move(controllersArr);

    return options.pretty ? root.dump(options.indentSpaces) : root.dump();
}

bool ProjectSerializer::fromJsonString(
    const std::string& jsonStr,
    app::ProjectDocument& outDoc,
    std::string* errorOut) {
    auto setError = [errorOut](const std::string& msg) {
        if (errorOut != nullptr) *errorOut = msg;
    };

    if (jsonStr.empty()) {
        setError("JSON 字符串为空。");
        return false;
    }

    try {
        json root = json::parse(jsonStr, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            setError("JSON 内容解析失败或格式不合法。");
            return false;
        }

        if (!root.contains("fileFormat") || !root["fileFormat"].is_string()) {
            setError("工程文件缺少有效的 fileFormat 标识。");
            return false;
        }
        const std::string fmt = root["fileFormat"].get<std::string>();
        if (fmt != kFileFormatTag) {
            setError("不支持的工程文件类型: " + fmt);
            return false;
        }

        if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_integer()) {
            setError("工程文件缺少有效的 schemaVersion 版本号。");
            return false;
        }
        const int version = root["schemaVersion"].get<int>();
        if (version > kCurrentSchemaVersion) {
            setError("工程文件 Schema 版本 (" + std::to_string(version) + ") 高于当前支持的版本 (" +
                     std::to_string(kCurrentSchemaVersion) + ")。");
            return false;
        }

        app::ProjectDocument loaded;
        loaded.sceneNodes.clear();
        loaded.seams.clear();
        loaded.weldSchedules.clear();
        loaded.controllers.clear();

        if (root.contains("settings")) loaded.settings = parseUnits(root["settings"]);
        if (root.contains("sceneNodes") && root["sceneNodes"].is_array()) {
            loaded.sceneNodes.reserve(root["sceneNodes"].size());
            for (const auto& nj : root["sceneNodes"]) loaded.sceneNodes.push_back(parseSceneNode(nj));
        }
        if (root.contains("seams") && root["seams"].is_array()) {
            loaded.seams.reserve(root["seams"].size());
            for (const auto& sj : root["seams"]) loaded.seams.push_back(parseSeam(sj));
        }
        if (root.contains("weldSchedules") && root["weldSchedules"].is_array()) {
            loaded.weldSchedules.reserve(root["weldSchedules"].size());
            for (const auto& wj : root["weldSchedules"]) loaded.weldSchedules.push_back(parseWeldSchedule(wj));
        }
        if (root.contains("controllers") && root["controllers"].is_array()) {
            loaded.controllers.reserve(root["controllers"].size());
            for (const auto& cj : root["controllers"]) loaded.controllers.push_back(parseController(cj));
        }

        loaded.isDirty = false;
        outDoc = std::move(loaded);
        return true;
    } catch (const std::exception& e) {
        setError(std::string("工程数据解析异常: ") + e.what());
        return false;
    }
}

bool ProjectSerializer::saveToFile(
    const app::ProjectDocument& doc,
    const std::string& filePath,
    const ProjectSerializeOptions& options,
    std::string* errorOut) {
    auto setError = [errorOut](const std::string& msg) {
        if (errorOut != nullptr) *errorOut = msg;
    };

    if (filePath.empty()) {
        setError("保存工程文件路径不能为空。");
        return false;
    }

    std::string content;
    try {
        content = toJsonString(doc, options);
    } catch (const std::exception& e) {
        setError(std::string("生成工程 JSON 异常: ") + e.what());
        return false;
    }

    const std::filesystem::path targetPath = utf8ToPath(filePath);
    const std::filesystem::path parentDir = targetPath.parent_path();
    if (!parentDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);
    }

    std::filesystem::path tempPath = targetPath;
    tempPath += ".tmp";

    {
        std::ofstream ofs(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            setError("无法创建临时工程文件: " + tempPath.string());
            return false;
        }
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.flush();
        if (!ofs.good()) {
            setError("写入工程文件数据时发生 I/O 错误: " + filePath);
            ofs.close();
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return false;
        }
        ofs.close();
        if (ofs.fail()) {
            setError("关闭工程文件时发生 I/O 错误: " + filePath);
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(tempPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD lastErr = GetLastError();
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        setError("替换目标工程文件失败 (Win32 Error " + std::to_string(lastErr) + "): " + filePath);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(tempPath, targetPath, ec);
    if (ec) {
        std::error_code rmEc;
        std::filesystem::remove(tempPath, rmEc);
        setError("替换目标工程文件失败: " + filePath + " (" + ec.message() + ")");
        return false;
    }
#endif

    return true;
}

bool ProjectSerializer::loadFromFile(
    const std::string& filePath,
    app::ProjectDocument& outDoc,
    std::string* errorOut) {
    auto setError = [errorOut](const std::string& msg) {
        if (errorOut != nullptr) *errorOut = msg;
    };

    if (filePath.empty()) {
        setError("打开工程文件路径不能为空。");
        return false;
    }

    const std::filesystem::path targetPath = utf8ToPath(filePath);
    std::ifstream ifs(targetPath, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        setError("工程文件不存在或无法打开: " + filePath);
        return false;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    const std::string content = buffer.str();
    return fromJsonString(content, outDoc, errorOut);
}

} // namespace olp::io
