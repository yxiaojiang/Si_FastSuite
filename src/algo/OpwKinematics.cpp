#include "olp/algo/OpwKinematics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace olp::algo {

namespace {

double axisSign(int sign) noexcept {
    return (sign < 0) ? -1.0 : 1.0;
}

// Teach-pendant / display angles -> URDF geometric joint angles.
// Coupling lives here, not inside the OPW +Y/+Y chain. On this URDF J3 is -Y,
// so adding +J2 cancels a pure J2 motion and the forearm stays put.
// The coefficient sign is NOT confirmed on ROBOGUIDE (phase-0 item S0-4).
std::array<double, 6> displayToUrdfJoints(const device::RobotModel& model, const std::array<double, 6>& displayJ) noexcept {
    std::array<double, 6> urdf = displayJ;
    if (model.hasJ2J3Coupling) {
        urdf[2] = displayJ[2] + model.j2J3CouplingCoeff * displayJ[1];
    }
    return urdf;
}

std::array<double, 6> urdfToOpwJoints(const device::RobotModel& model, const std::array<double, 6>& urdf) noexcept {
    // Stored convention (matches the M-10iA URDF flange at the zero pose):
    //   q = sign * J_urdf - offset
    // This is the same map as ROS-Industrial's `J = sign * q + offset_ros`
    // when offset_ros = sign * offset_stored. Do not drop the sign onto the offset.
    std::array<double, 6> q{};
    for (size_t i = 0; i < 6; ++i) {
        q[i] = axisSign(model.opw.signCorrections[i]) * urdf[i] - model.opw.offsetsRad[i];
    }
    return q;
}

std::array<double, 6> opwToUrdfJoints(const device::RobotModel& model, const std::array<double, 6>& q) noexcept {
    std::array<double, 6> urdf{};
    for (size_t i = 0; i < 6; ++i) {
        const double s = axisSign(model.opw.signCorrections[i]);
        urdf[i] = s * (q[i] + model.opw.offsetsRad[i]);
    }
    return urdf;
}

std::array<double, 6> urdfToDisplayJoints(const device::RobotModel& model, const std::array<double, 6>& urdf) noexcept {
    std::array<double, 6> display = urdf;
    if (model.hasJ2J3Coupling) {
        display[2] = urdf[2] - model.j2J3CouplingCoeff * urdf[1];
    }
    return display;
}

} // namespace

OpwKinematics::OpwKinematics(const device::RobotModel& model)
    : model_(model) {}

std::array<double, 6> OpwKinematics::modelToDisplayJoints(const std::array<double, 6>& modelQ) const noexcept {
    auto display = urdfToDisplayJoints(model_, opwToUrdfJoints(model_, modelQ));
    // OPW angles are folded into (-pi, pi]. J6's stored offset is +pi, so the
    // raw display angle lands in (-2pi, 0] and would be reported as turn -1
    // for a teach-pendant angle that is still inside (-pi, pi]. Fold once here.
    // ±360° turns stay the job of inverseKinematics(expandTurns=true).
    for (double& joint : display) {
        joint = core::normalizeAngleRad(joint);
    }
    return display;
}

std::array<double, 6> OpwKinematics::displayToModelJoints(const std::array<double, 6>& displayJ) const noexcept {
    return urdfToOpwJoints(model_, displayToUrdfJoints(model_, displayJ));
}

FkResult OpwKinematics::forwardKinematics(
    const std::array<double, 6>& displayJointsRad,
    const core::Transform4d& utoolOffset) const noexcept {
    FkResult res;
    const auto q = displayToModelJoints(displayJointsRad);
    const auto& p = model_.opw;

    const double q1 = q[0];
    const double q2 = q[1];
    const double q3 = q[2];
    const double q4 = q[3];
    const double q5 = q[4];
    const double q6 = q[5];
    const double q23 = q2 + q3;

    // Base link (Link 0)
    res.linkTransforms[0] = core::Transform4d::identity();

    // Link 1 (Turret rotating around Z0 by q1)
    core::Transform4d T1;
    T1.rot = core::Mat3::rotZ(q1);
    T1.pos = {0.0, 0.0, p.c1};
    res.linkTransforms[1] = T1;

    // Link 2 (Lower arm origin at J2)
    core::Transform4d T2;
    T2.rot = core::Mat3::rotZ(q1) * core::Mat3::rotY(q2);
    T2.pos = T1.rot * core::Vec3{p.a1, p.b, p.c1};
    res.linkTransforms[2] = T2;

    // Link 3 (Upper arm origin at J3)
    core::Transform4d T3;
    T3.rot = core::Mat3::rotZ(q1) * core::Mat3::rotY(q23);
    T3.pos = T2.pos + (T2.rot * core::Vec3{0.0, 0.0, p.c2});
    res.linkTransforms[3] = T3;

    // Wrist center C
    const core::Vec3 wristCenter = T3.pos + (T3.rot * core::Vec3{p.a2, 0.0, p.c3});

    // Link 4 (Forearm rotating around Z3 by q4)
    core::Transform4d T4;
    T4.rot = T3.rot * core::Mat3::rotZ(q4);
    T4.pos = wristCenter;
    res.linkTransforms[4] = T4;

    // Link 5 (Wrist pitch rotating around Y4 by q5)
    core::Transform4d T5;
    T5.rot = T4.rot * core::Mat3::rotY(q5);
    T5.pos = wristCenter;
    res.linkTransforms[5] = T5;

    // Link 6 / Flange (Wrist roll rotating around Z5 by q6, translated by c4 along Z6)
    core::Transform4d T6;
    T6.rot = T5.rot * core::Mat3::rotZ(q6);
    T6.pos = wristCenter + (T6.rot * core::Vec3{0.0, 0.0, p.c4});
    res.linkTransforms[6] = T6;

    res.flangePoseInBase = T6;
    res.tcpPoseInBase = T6 * utoolOffset;
    res.config = computeConfig(displayJointsRad);
    return res;
}

std::vector<IkSolution> OpwKinematics::inverseKinematics(
    const core::Transform4d& targetTcpInBase,
    const core::Transform4d& utoolOffset,
    bool expandTurns) const {
    std::vector<IkSolution> solutions;
    solutions.reserve(expandTurns ? 32 : 8);

    // 1. Recover Flange pose from TCP pose: T_flange = T_tcp * T_utool^-1
    const core::Transform4d flangePose = targetTcpInBase * utoolOffset.inverse();
    const auto& p = model_.opw;

    // 2. Recover Wrist Center C = P_flange - R_flange * [0, 0, c4]^T
    const core::Vec3 C = flangePose.pos - (flangePose.rot * core::Vec3{0.0, 0.0, p.c4});

    const double rxySq = C.x * C.x + C.y * C.y - p.b * p.b;
    if (rxySq < -1e-6) {
        return solutions; // Unreachable (inside shoulder lateral offset cylinder)
    }
    const double rxyMag = std::sqrt(std::max(0.0, rxySq));
    const double L3 = std::sqrt(p.a2 * p.a2 + p.c3 * p.c3);
    const double psi = std::atan2(p.a2, p.c3);

    // Iterate over 2 shoulder branches (s1 = +1 Front / -1 Back)
    for (int s1 : {+1, -1}) {
        const double rPlanar = s1 * rxyMag;
        const double q1 = core::normalizeAngleRad(std::atan2(C.y, C.x) - std::atan2(p.b, rPlanar));

        const double u = rPlanar - p.a1;
        const double v = C.z - p.c1;
        const double dSq = u * u + v * v;

        const double denom = 2.0 * p.c2 * L3;
        if (denom < 1e-9) continue;

        double cosQ3Psi = (dSq - p.c2 * p.c2 - L3 * L3) / denom;
        if (cosQ3Psi > 1.0 + 1e-6 || cosQ3Psi < -1.0 - 1e-6) {
            continue; // Target beyond arm reach
        }
        cosQ3Psi = std::clamp(cosQ3Psi, -1.0, 1.0);
        const double sinMag = std::sqrt(std::max(0.0, 1.0 - cosQ3Psi * cosQ3Psi));

        // Iterate over 2 elbow branches (s2 = +1 Up / -1 Down)
        for (int s2 : {+1, -1}) {
            const double sinQ3Psi = s2 * sinMag;
            const double q3 = core::normalizeAngleRad(std::atan2(sinQ3Psi, cosQ3Psi) - psi);

            const double alpha = std::atan2(u, v);
            const double beta = std::atan2(L3 * sinQ3Psi, p.c2 + L3 * cosQ3Psi);
            const double q2 = core::normalizeAngleRad(alpha - beta);

            // Compute R03 and R36 = R03^T * R_flange
            const core::Mat3 R03 = core::Mat3::rotZ(q1) * core::Mat3::rotY(q2 + q3);
            const core::Mat3 R36 = R03.transposed() * flangePose.rot;

            const double sinQ5Mag = std::sqrt(R36.m[0][2] * R36.m[0][2] + R36.m[1][2] * R36.m[1][2]);
            const double cosQ5 = R36.m[2][2];

            double q4Base = 0.0;
            const double q5Base = std::atan2(sinQ5Mag, cosQ5);
            double q6Base = 0.0;

            if (sinQ5Mag > 1e-7) {
                q4Base = std::atan2(R36.m[1][2], R36.m[0][2]);
                q6Base = std::atan2(R36.m[2][1], -R36.m[2][0]);
            } else {
                // Wrist singularity (q5 ~ 0 or pi)
                q4Base = 0.0;
                q6Base = std::atan2(-R36.m[0][1], R36.m[0][0]);
            }

            // Iterate over 2 wrist branches (s3 = +1 Non-Flip / -1 Flip)
            for (int s3 : {+1, -1}) {
                double q4 = q4Base;
                double q5 = q5Base;
                double q6 = q6Base;
                if (s3 < 0) {
                    q4 = core::normalizeAngleRad(q4Base + core::kPi);
                    q5 = -q5Base;
                    q6 = core::normalizeAngleRad(q6Base + core::kPi);
                }

                const std::array<double, 6> modelQ{q1, q2, q3, q4, q5, q6};
                const std::array<double, 6> dispJ = modelToDisplayJoints(modelQ);

                auto addCandidate = [&](const std::array<double, 6>& candidateJ) {
                    IkSolution sol;
                    sol.jointsRad = candidateJ;
                    sol.config = computeConfig(candidateJ);
                    checkJointLimits(candidateJ, sol.withinLimits, sol.nearLimits);
                    sol.isSingular = checkSingularity(candidateJ, sol.manipulability);
                    solutions.push_back(sol);
                };

                addCandidate(dispJ);

                if (expandTurns) {
                    // Plan §6.2: unfold J1, J4 and J6 by ±360°. Which FANUC turn
                    // registers these map to ($TURN_AXIS, often J4/J5/J6) is unverified.
                    for (int t1 : {-1, 0, +1}) {
                        for (int t4 : {-1, 0, +1}) {
                            for (int t6 : {-1, 0, +1}) {
                                if (t1 == 0 && t4 == 0 && t6 == 0) continue;
                                auto extJ = dispJ;
                                extJ[0] += t1 * 2.0 * core::kPi;
                                extJ[3] += t4 * 2.0 * core::kPi;
                                extJ[5] += t6 * 2.0 * core::kPi;
                                bool inside = true;
                                for (int axis : {0, 3, 5}) {
                                    const auto& lim = model_.jointLimits[static_cast<size_t>(axis)];
                                    const double urdfAxis = displayToUrdfJoints(model_, extJ)[static_cast<size_t>(axis)];
                                    if (urdfAxis < lim.lowerRad - 1e-5 || urdfAxis > lim.upperRad + 1e-5) {
                                        inside = false;
                                        break;
                                    }
                                }
                                if (inside) addCandidate(extJ);
                            }
                        }
                    }
                }
            }
        }
    }

    return solutions;
}

std::optional<IkSolution> OpwKinematics::selectBestSolution(
    const std::vector<IkSolution>& candidates,
    const std::array<double, 6>& prevDisplayJointsRad,
    const std::optional<core::FanucConfig>& lockedConfig,
    bool forbidConfigChangeInWeld) const {
    if (candidates.empty()) {
        return std::nullopt;
    }

    const IkSolution* best = nullptr;
    double bestCost = std::numeric_limits<double>::infinity();

    for (const auto& sol : candidates) {
        if (!sol.withinLimits) {
            continue;
        }
        // A supplied config is a hard lock. The previous +100 penalty let simulation
        // replace a user-locked posture and then write that posture back.
        (void)forbidConfigChangeInWeld;
        if (lockedConfig.has_value() && !sol.config.samePosture(*lockedConfig)) {
            continue;
        }

        double cost = weightedJointDistance(sol.jointsRad, prevDisplayJointsRad);
        if (sol.nearLimits) {
            cost += 2.0;
        }
        if (sol.isSingular) {
            cost += 10.0;
        }

        if (cost < bestCost) {
            bestCost = cost;
            best = &sol;
        }
    }

    if (best != nullptr) {
        return *best;
    }
    return std::nullopt;
}

core::FanucConfig OpwKinematics::computeConfig(const std::array<double, 6>& displayJointsRad) const noexcept {
    const auto q = displayToModelJoints(displayJointsRad);
    const auto& p = model_.opw;
    core::FanucConfig cfg;
    // Flip (F) if q5 < 0, Non-flip (N) if q5 >= 0 (matches s3 branch)
    cfg.flip = (q[4] < -1e-7);
    // Up (U) vs Down (D) based on elbow branch sin(q3 + psi) >= 0 (matches s2 branch)
    const double psi = std::atan2(p.a2, p.c3);
    cfg.up = (std::sin(q[2] + psi) >= -1e-7);
    // Top/Front (T) vs Bottom/Back (B) based on planar reach rPlanar >= 0 (matches s1 branch)
    const double rPlanar =
        p.a1 + p.c2 * std::sin(q[1]) +
        p.a2 * std::cos(q[1] + q[2]) +
        p.c3 * std::sin(q[1] + q[2]);
    cfg.top = (rPlanar >= -1e-7);

    auto calcTurn = [](double angleRad) -> int {
        return static_cast<int>(std::floor((angleRad + core::kPi) / (2.0 * core::kPi)));
    };
    // Turn registers below are J1 / J4 / J6, matching Plan §6.2.
    // FANUC's default $TURN_AXIS set is often J4 / J5 / J6. Unverified until S0-4.
    cfg.turns[0] = calcTurn(displayJointsRad[0]);
    cfg.turns[1] = calcTurn(displayJointsRad[3]);
    cfg.turns[2] = calcTurn(displayJointsRad[5]);
    return cfg;
}

void OpwKinematics::checkJointLimits(
    const std::array<double, 6>& displayJointsRad,
    bool& outWithin,
    bool& outNear) const noexcept {
    outWithin = true;
    outNear = false;
    // jointLimits were copied from the URDF, so they bound URDF geometric angles.
    // Whether the teach pendant uses that same number for coupled J3 is unverified.
    const auto urdf = displayToUrdfJoints(model_, displayJointsRad);
    for (size_t i = 0; i < 6; ++i) {
        const double val = urdf[i];
        const auto& lim = model_.jointLimits[i];
        if (val < lim.lowerRad - 1e-5 || val > lim.upperRad + 1e-5) {
            outWithin = false;
        } else if (val - lim.lowerRad < lim.warningMarginRad ||
                   lim.upperRad - val < lim.warningMarginRad) {
            outNear = true;
        }
    }
}

bool OpwKinematics::checkSingularity(
    const std::array<double, 6>& displayJointsRad,
    double& outManipulability) const noexcept {
    const auto q = displayToModelJoints(displayJointsRad);
    // Geometric proxies, not the Jacobian minimum singular value.
    // Wrist: |sin(q5)| -> 0. Elbow: arm fully stretched. Shoulder: wrist on the J1 axis.
    const double wristFactor = std::abs(std::sin(q[4]));
    const double psi = std::atan2(model_.opw.a2, model_.opw.c3);
    const double elbowFactor = std::abs(std::sin(q[2] + psi));
    const double rPlanar =
        model_.opw.a1 + model_.opw.c2 * std::sin(q[1]) +
        model_.opw.a2 * std::cos(q[1] + q[2]) +
        model_.opw.c3 * std::sin(q[1] + q[2]);
    const double shoulderFactor = std::min(1.0, std::abs(rPlanar) / 50.0);
    outManipulability = wristFactor * elbowFactor * shoulderFactor;
    return (wristFactor < core::deg2rad(3.0)) ||
           (elbowFactor < core::deg2rad(3.0)) ||
           (std::abs(rPlanar) < 5.0);
}

double OpwKinematics::weightedJointDistance(
    const std::array<double, 6>& j1,
    const std::array<double, 6>& j2) noexcept {
    // Section 6.2: Base/arm axes weighted higher than wrist axes
    constexpr std::array<double, 6> weights{1.5, 1.5, 1.2, 0.6, 0.6, 0.4};
    double sum = 0.0;
    for (size_t i = 0; i < 6; ++i) {
        const double d = j1[i] - j2[i];
        sum += weights[i] * d * d;
    }
    return std::sqrt(sum);
}

} // namespace olp::algo
