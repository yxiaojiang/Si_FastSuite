#include "olp/device/RobotModel.h"

#include <fstream>
#include <regex>
#include <sstream>

namespace olp::device {

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

    // Parse joint limits from URDF
    std::regex reJoint(
        R"REGEX(<joint\s+name="joint_([1-6])"[\s\S]*?<limit\s+lower="([-\d.]+)"\s+upper="([-\d.]+)"[^>]*velocity="([-\d.]+)")REGEX");
    for (std::sregex_iterator it(content.begin(), content.end(), reJoint), end; it != end; ++it) {
        const int idx = std::stoi((*it)[1].str()) - 1;
        if (idx >= 0 && idx < 6) {
            m.jointLimits[idx].lowerRad = std::stod((*it)[2].str());
            m.jointLimits[idx].upperRad = std::stod((*it)[3].str());
            m.jointLimits[idx].maxVelRadPerSec = std::stod((*it)[4].str());
        }
    }

    return m;
}

} // namespace olp::device
