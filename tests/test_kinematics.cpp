#include "test_support.h"
#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include "olp/app/CommandManager.h"
#include "olp/app/WorkcellLayoutService.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/device/RobotModel.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/ui/MainWindow.h"
#include "olp/ui/OcctViewport.h"
#include "olp/ui/TcpGizmo.h"

#ifdef OLP_HAS_QT6
#include "olp/ui/ShellWindow.h"
#include <QApplication>
#include <QListWidget>
#include <QStringList>
#endif

#ifdef OLP_HAS_OCCT
#include <TopoDS.hxx>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

void testWprAndGimbalLock() {
    std::cout << "\n--- Test 1: FANUC WPR Conversion & Gimbal Lock (Section 6.4) ---\n";
    const olp::core::WprPose inPose{450.25, -120.50, 310.00, 180.0, -45.0, 30.0};
    const auto T = olp::core::Transform4d::fromWPR(inPose);
    const auto outPose = T.toWPR();
    const auto T2 = olp::core::Transform4d::fromWPR(outPose);

    const double posErr = (T.pos - T2.pos).norm();
    const double rotErrDeg = olp::core::rad2deg(T.rot.angularDistanceRad(T2.rot));
    check(posErr < 1e-9 && rotErrDeg < 1e-7,
          "Standard WPR round-trip precision (< 1e-7 deg)",
          "rotErrDeg=" + std::to_string(rotErrDeg));

    // Gimbal lock at P = +90 deg and P = -90 deg
    const olp::core::WprPose gimbalPose{100.0, 200.0, 300.0, 25.0, 90.0, -40.0};
    const auto Tg1 = olp::core::Transform4d::fromWPR(gimbalPose);
    const auto Tg2 = olp::core::Transform4d::fromWPR(Tg1.toWPR());
    const double gimbalRotErrDeg = olp::core::rad2deg(Tg1.rot.angularDistanceRad(Tg2.rot));
    check(gimbalRotErrDeg < 1e-6,
          "Gimbal lock (P = +90 deg) rotation matrix round-trip",
          "gimbalRotErrDeg=" + std::to_string(gimbalRotErrDeg));
}


void testOpwKinematics100Samples() {
    std::cout << "\n--- Test 3: OPW FK/IK 100-Sample Closed-Loop & J2/J3 Coupling (Section 11.1) ---\n";
    const auto model = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(model);
    const auto utool = olp::core::Transform4d::fromWPR({0.0, 0.0, 160.0, 0.0, 35.0, 0.0});

    double maxPosErrMm = 0.0;
    double maxRotErrDeg = 0.0;
    double maxJointRecoveryErrDeg = 0.0;
    int successCount = 0;

    for (int i = 0; i < 100; ++i) {
        const double t = static_cast<double>(i) / 100.0;
        const std::array<double, 6> testJointsRad = {
            -1.2 + 2.4 * t,
            -0.2 + 0.5 * std::sin(i * 0.37),
            -0.2 + 0.4 * std::cos(i * 0.29),
            2.5 * std::sin(i * 0.53),
            -1.2 - 0.3 * std::cos(i * 0.41), // Keep J5 away from 0 to test non-singular posture recovery
            -2.0 + 4.0 * t
        };

        const auto fk = kin.forwardKinematics(testJointsRad, utool);
        const auto ikSols = kin.inverseKinematics(fk.tcpPoseInBase, utool, false);
        if (ikSols.empty()) continue;

        // Verify all IK solutions map back to the exact target TCP pose
        for (const auto& sol : ikSols) {
            const auto fkCheck = kin.forwardKinematics(sol.jointsRad, utool);
            const double pErr = (fkCheck.tcpPoseInBase.pos - fk.tcpPoseInBase.pos).norm();
            const double rErr = olp::core::rad2deg(
                fkCheck.tcpPoseInBase.rot.angularDistanceRad(fk.tcpPoseInBase.rot));
            maxPosErrMm = std::max(maxPosErrMm, pErr);
            maxRotErrDeg = std::max(maxRotErrDeg, rErr);
        }

        // Verify selectBestSolution recovers the exact original joint angles
        const auto recovered = kin.selectBestSolution(ikSols, testJointsRad, fk.config, true);
        if (recovered.has_value()) {
            ++successCount;
            for (size_t k = 0; k < 6; ++k) {
                const double jErrDeg = olp::core::rad2deg(
                    std::abs(olp::core::normalizeAngleRad(recovered->jointsRad[k] - testJointsRad[k])));
                maxJointRecoveryErrDeg = std::max(maxJointRecoveryErrDeg, jErrDeg);
            }
        }
    }

    check(successCount == 100 && maxPosErrMm < 0.01 && maxRotErrDeg < 0.001 && maxJointRecoveryErrDeg < 0.001,
          "100-Sample FK(IK(x)) = x (maxPosErr=" + std::to_string(maxPosErrMm) +
          "mm, maxRotErr=" + std::to_string(maxRotErrDeg) + "deg)",
          "successCount=" + std::to_string(successCount) +
          ", maxJointRecoveryErrDeg=" + std::to_string(maxJointRecoveryErrDeg));

    auto rotAbout = [](olp::core::Vec3 axis, double angle) {
        const double n = axis.norm();
        axis = axis * (1.0 / n);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const double t = 1.0 - c;
        olp::core::Mat3 R;
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
    };
    const olp::core::Mat3 tool0Rot =
        olp::core::Mat3::rotZ(0.0) * olp::core::Mat3::rotY(-1.570796) * olp::core::Mat3::rotX(3.14159);
    auto urdfFlange = [&](const std::array<double, 6>& urdfQ) {
        const olp::core::Vec3 origins[6] = {
            {0, 0, 450}, {150, 0, 0}, {0, 0, 600}, {0, 0, 150}, {640, 0, 0}, {100, 0, 0}
        };
        const olp::core::Vec3 axes[6] = {
            {0, 0, 1}, {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {0, -1, 0}, {-1, 0, 0}
        };
        olp::core::Transform4d T = olp::core::Transform4d::identity();
        for (int j = 0; j < 6; ++j) {
            olp::core::Transform4d joint;
            joint.pos = origins[j];
            joint.rot = rotAbout(axes[j], urdfQ[static_cast<size_t>(j)]);
            T = T * joint;
        }
        T.rot = T.rot * tool0Rot;
        return T;
    };

    double maxGoldMm = 0.0;
    double maxGoldAxis = 0.0;
    unsigned seed = 2026u;
    for (int i = 0; i < 200; ++i) {
        std::array<double, 6> display{};
        if (i == 0) {
            display = {0, 0, 0, 0, 0, 0};
        } else if (i == 1) {
            display = {0, olp::core::deg2rad(10.0), 0, 0, 0, 0};
        } else {
            for (double& q : display) {
                seed = seed * 1664525u + 1013904223u;
                const double unit = static_cast<double>(seed % 10000) / 10000.0;
                q = olp::core::deg2rad(-80.0 + 160.0 * unit);
            }
            display[4] = olp::core::deg2rad(-60.0);
        }
        std::array<double, 6> urdf = display;
        urdf[2] = display[2] + display[1];
        const auto gold = urdfFlange(urdf);
        const auto fk = kin.forwardKinematics(display);
        maxGoldMm = std::max(maxGoldMm, (fk.flangePoseInBase.pos - gold.pos).norm());
        maxGoldAxis = std::max(maxGoldAxis, (fk.flangePoseInBase.rot.colZ() - gold.rot.colZ()).norm());
    }
    check(maxGoldMm < 0.01 && maxGoldAxis < 1e-4,
          "URDF-chain gold: flange position and tool0 Z stay within 0.01 mm",
          "maxPos=" + std::to_string(maxGoldMm) + " maxAxis=" + std::to_string(maxGoldAxis));
}


