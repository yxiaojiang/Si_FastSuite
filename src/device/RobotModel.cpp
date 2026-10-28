#include "olp/device/RobotModel.h"

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

namespace olp::device {

namespace {

core::Mat3 rotAboutAxis(core::Vec3 axis, double angle) noexcept {
    const double n = axis.norm();
    if (n < 1e-12) return core::Mat3::identity();
    axis = axis * (1.0 / n);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    core::Mat3 R;
    R.m[0][0] = t * axis.x * axis.x + c;
    R.m[0][1] = t * axis.x * axis.y - s * axis.z;
    R.m[0][2] = t * axis.x * axis.z + s * axis.y;
    R.m[1][0] = t * axis.x * axis.y + s * axis.z;
    R.m[1][1] = t * axis.y * axis.y + c;
    R.m[1][2] = t * axis.y * axis.z - s * axis.x;
    R.m[2][0] = t * axis.x * axis.z - s * axis.y;
    R.m[2][1] = t * axis.y * axis.z + s * axis.x;
    R.m[2][2] = t * axis.z * axis.z + c;
    return R;
}

} // namespace

core::Transform4d RobotModel::forwardUrdfFlange(const std::array<double, 6>& urdfJointsRad) const noexcept {
    if (!urdfLoaded) {
        return core::Transform4d::identity();
    }
    core::Transform4d T = core::Transform4d::identity();
    for (size_t j = 0; j < 6; ++j) {
        const auto& jd = urdfJoints[j];
        core::Transform4d step;
        step.pos = jd.originXyzMm;
        const core::Mat3 rpyRot =
            core::Mat3::rotZ(jd.originRpyRad.z) *
            core::Mat3::rotY(jd.originRpyRad.y) *
            core::Mat3::rotX(jd.originRpyRad.x);
        step.rot = rpyRot * rotAboutAxis(jd.axis, urdfJointsRad[j]);
        T = T * step;
    }
    return T;
}

core::Transform4d RobotModel::forwardUrdfTool0(const std::array<double, 6>& urdfJointsRad) const noexcept {
    if (!urdfLoaded) {
        return core::Transform4d::identity();
    }
    return forwardUrdfFlange(urdfJointsRad) * urdfFlangeToTool0;
}

RobotModel RobotModel::createFanucM10iA(const std::string& workspaceRoot) {
    RobotModel m;
    m.modelId = "fanuc_m10ia";
    m.displayName = "FANUC M-10iA / ARC Mate 100iC";
    m.manufacturer = "FANUC";

    // OPW Parameters from fanuc_m10ia_standalone.urdf:
    // a1=150mm, a2=-150mm, b=0, c1=450mm, c2=600mm, c3=640mm, c4=100mm
    m.opw.a1 = 150.0;
    m.opw.a2 = -150.0;
    m.opw.b  = 0.0;
    m.opw.c1 = 450.0;
    m.opw.c2 = 600.0;
    m.opw.c3 = 640.0;
    m.opw.c4 = 100.0;
    m.opw.offsetsRad = {0.0, 0.0, -core::kPi * 0.5, 0.0, 0.0, core::kPi};
    m.opw.signCorrections = {1, 1, -1, -1, -1, -1};

    m.hasJ2J3Coupling = true;
    m.j2J3CouplingCoeff = 1.0;
    m.homeJointsRad = {0.0, 0.0, 0.0, 0.0, -core::kPi * 0.5, 0.0};

    m.jointLimits[0] = {"J1", -2.967, 2.967, 3.665, 10.0};
    m.jointLimits[1] = {"J2", -1.570, 2.792, 3.316, 9.5};
    m.jointLimits[2] = {"J3", -2.967, 4.014, 3.665, 10.0};
    m.jointLimits[3] = {"J4", -3.316, 3.316, 6.981, 20.0};
    m.jointLimits[4] = {"J5", -3.316, 3.316, 6.981, 20.0};
    m.jointLimits[5] = {"J6", -6.283, 6.283, 10.471, 30.0};

    // URDF joint chain definitions are populated when loadFromUrdf() parses a URDF file
    m.urdfLoaded = false;

    const std::string baseDir = workspaceRoot.empty()
        ? std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Robot_FANUC_M10iA/meshes"
        : workspaceRoot + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Robot_FANUC_M10iA/meshes";

    const std::array<std::string, 7> linkFiles = {
        "base_link.stl", "link_1.stl", "link_2.stl",
        "link_3.stl", "link_4.stl", "link_5.stl", "link_6.stl"
    };
    for (size_t i = 0; i < 7; ++i) {
        m.visualMeshFiles[i] = baseDir + "/visual/" + linkFiles[i];
        m.collisionMeshFiles[i] = baseDir + "/collision/" + linkFiles[i];
    }

    const std::string root = workspaceRoot.empty() ? std::string(OLP_SOURCE_DIR) : workspaceRoot;
    const std::string jsonPath = root + "/resources/robots/fanuc_m10ia.json";
    std::ifstream jin(jsonPath);
    if (jin) {
        std::stringstream buffer;
        buffer << jin.rdbuf();
        const std::string text = buffer.str();
        auto readSix = [&](const std::string& key, std::array<double, 6>& out, bool asDegrees) -> bool {
            const std::regex re("\"" + key + "\"\\s*:\\s*\\[([^\\]]+)\\]");
            std::smatch match;
            if (!std::regex_search(text, match, re)) return false;
            std::stringstream values(match[1].str());
            for (int i = 0; i < 6; ++i) {
                double number = 0.0;
                values >> number;
                if (values.fail()) return false;
                char separator = 0;
                values >> separator;
                out[static_cast<size_t>(i)] = asDegrees ? core::deg2rad(number) : number;
            }
            return true;
        };
        std::array<double, 6> offsets{};
        std::array<double, 6> signs{};
        if (readSix("joint_offsets_deg", offsets, true)) {
            m.opw.offsetsRad = offsets;
        }
        if (readSix("joint_sign_corrections", signs, false)) {
            for (int i = 0; i < 6; ++i) {
                m.opw.signCorrections[static_cast<size_t>(i)] = (signs[static_cast<size_t>(i)] < 0.0) ? -1 : 1;
            }
        }
    }
    return m;
}

std::optional<RobotModel> RobotModel::loadFromUrdf(const std::string& urdfFilePath) {
    std::ifstream ifs(urdfFilePath);
    if (!ifs.is_open()) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    const std::string content = buffer.str();

    RobotModel m = createFanucM10iA();

    std::regex reRobotName(R"REGEX(<robot\s+name="([^"]+)")REGEX");
    std::smatch mr;
    if (std::regex_search(content, mr, reRobotName)) {
        m.modelId = mr[1].str();
    }

    auto parseVec3Attr = [](const std::string& text, const std::string& attrName) -> std::optional<core::Vec3> {
        const std::regex re(attrName + R"REGEX(\s*=\s*"([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)")REGEX");
        std::smatch sm;
        if (std::regex_search(text, sm, re)) {
            try {
                return core::Vec3{std::stod(sm[1].str()), std::stod(sm[2].str()), std::stod(sm[3].str())};
            } catch (...) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    };

    auto parseStringAttr = [](const std::string& text, const std::string& attrName) -> std::string {
        const std::regex re(attrName + R"REGEX(\s*=\s*"([^"]+)")REGEX");
        std::smatch sm;
        if (std::regex_search(text, sm, re)) {
            return sm[1].str();
        }
        return "";
    };

    auto parseDoubleAttr = [](const std::string& text, const std::string& attrName) -> std::optional<double> {
        const std::regex re(attrName + R"REGEX(\s*=\s*"([-\d.eE+]+)")REGEX");
        std::smatch sm;
        if (std::regex_search(text, sm, re)) {
            try {
                return std::stod(sm[1].str());
            } catch (...) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    };

    int revoluteParsedCount = 0;
    bool tool0Parsed = false;

    // Parse each <joint ...> ... </joint> block
    std::regex reJointBlock(R"REGEX(<joint\s+([^>]+)>([\s\S]*?)</joint>)REGEX");
    for (std::sregex_iterator it(content.begin(), content.end(), reJointBlock), end; it != end; ++it) {
        const std::string jointHeader = (*it)[1].str();
        const std::string jointBody = (*it)[2].str();
        const std::string jointName = parseStringAttr(jointHeader, "name");
        const std::string jointType = parseStringAttr(jointHeader, "type");

        core::Vec3 originXyzMm{0.0, 0.0, 0.0};
        core::Vec3 originRpyRad{0.0, 0.0, 0.0};
        core::Vec3 jointAxis{0.0, 0.0, 1.0};

        std::string parentLink;
        std::regex reParent(R"REGEX(<parent\s+link\s*=\s*"([^"]+)")REGEX");
        std::smatch smP;
        if (std::regex_search(jointBody, smP, reParent)) parentLink = smP[1].str();

        std::string childLink;
        std::regex reChild(R"REGEX(<child\s+link\s*=\s*"([^"]+)")REGEX");
        std::smatch smC;
        if (std::regex_search(jointBody, smC, reChild)) childLink = smC[1].str();

        // Parse <origin ... />
        std::regex reOrigin(R"REGEX(<origin\s+([^>]*)/?>)REGEX");
        std::smatch originSm;
        if (std::regex_search(jointBody, originSm, reOrigin)) {
            const std::string originTag = originSm[1].str();
            if (auto xyzMeters = parseVec3Attr(originTag, "xyz")) {
                // URDF distances are in meters, convert to millimeters
                originXyzMm = {xyzMeters->x * 1000.0, xyzMeters->y * 1000.0, xyzMeters->z * 1000.0};
            }
            if (auto rpy = parseVec3Attr(originTag, "rpy")) {
                originRpyRad = *rpy;
            }
        }

        // Parse <axis ... />
        std::regex reAxis(R"REGEX(<axis\s+([^>]*)/?>)REGEX");
        std::smatch axisSm;
        if (std::regex_search(jointBody, axisSm, reAxis)) {
            if (auto ax = parseVec3Attr(axisSm[1].str(), "xyz")) {
                jointAxis = *ax;
            }
        }

        // Parse <limit ... />
        double lowerRad = -core::kPi;
        double upperRad = core::kPi;
        double vel = 3.0;
        std::regex reLimit(R"REGEX(<limit\s+([^>]*)/?>)REGEX");
        std::smatch limitSm;
        if (std::regex_search(jointBody, limitSm, reLimit)) {
            const std::string limitTag = limitSm[1].str();
            if (auto l = parseDoubleAttr(limitTag, "lower")) lowerRad = *l;
            if (auto u = parseDoubleAttr(limitTag, "upper")) upperRad = *u;
            if (auto v = parseDoubleAttr(limitTag, "velocity")) vel = *v;
        }

        // Revolute joints joint_1 .. joint_6
        std::regex reJointIdx(R"REGEX(^joint_([1-6])$)REGEX");
        std::smatch idxSm;
        if (std::regex_match(jointName, idxSm, reJointIdx)) {
            const int idx = std::stoi(idxSm[1].str()) - 1;
            if (idx >= 0 && idx < 6) {
                UrdfJointDef def;
                def.name = jointName;
                def.type = jointType.empty() ? "revolute" : jointType;
                def.originXyzMm = originXyzMm;
                def.originRpyRad = originRpyRad;
                def.axis = jointAxis;
                def.parentLink = parentLink;
                def.childLink = childLink;
                def.lowerLimitRad = lowerRad;
                def.upperLimitRad = upperRad;
                def.velocityLimitRadPerSec = vel;

                m.urdfJoints[static_cast<size_t>(idx)] = def;
                m.jointLimits[static_cast<size_t>(idx)].name = "J" + std::to_string(idx + 1);
                m.jointLimits[static_cast<size_t>(idx)].lowerRad = lowerRad;
                m.jointLimits[static_cast<size_t>(idx)].upperRad = upperRad;
                m.jointLimits[static_cast<size_t>(idx)].maxVelRadPerSec = vel;
                revoluteParsedCount++;
            }
        } else if (childLink == "tool0" || jointName == "link_6-tool0") {
            // Fixed joint to tool0 (flange -> tool0)
            m.urdfFlangeToTool0.pos = originXyzMm;
            m.urdfFlangeToTool0.rot =
                core::Mat3::rotZ(originRpyRad.z) *
                core::Mat3::rotY(originRpyRad.y) *
                core::Mat3::rotX(originRpyRad.x);
            tool0Parsed = true;
        }
    }

    if (revoluteParsedCount == 6 && tool0Parsed) {
        m.urdfLoaded = true;
    }

    return m;
}

} // namespace olp::device
