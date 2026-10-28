#include "olp/algo/FclCollisionQueries.h"
#include "olp/cad/OcctCadKernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef OLP_HAS_FCL
#include "fcl/geometry/bvh/BVH_model.h"
#include "fcl/geometry/shape/sphere.h"
#include "fcl/narrowphase/distance.h"
#endif

namespace olp::algo {

namespace {

core::Mat3 axisRotation(const core::Vec3& axis, double angle) noexcept {
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



#ifdef OLP_HAS_FCL

fcl::Transform3d toFclTransform(const core::Transform4d& pose) {
    fcl::Transform3d out = fcl::Transform3d::Identity();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out.linear()(row, col) = pose.rot.m[row][col];
        }
    }
    out.translation() = fcl::Vector3d(pose.pos.x, pose.pos.y, pose.pos.z);
    return out;
}

std::shared_ptr<fcl::BVHModel<fcl::OBBRSSd>> buildBvhFromTriMesh(const core::TriMesh& mesh) {
    auto model = std::make_shared<fcl::BVHModel<fcl::OBBRSSd>>();
    model->beginModel(static_cast<unsigned int>(mesh.triangles.size()));
    for (const auto& tri : mesh.triangles) {
        const core::Vec3& a = mesh.vertices[tri[0]];
        const core::Vec3& b = mesh.vertices[tri[1]];
        const core::Vec3& c = mesh.vertices[tri[2]];
        model->addTriangle(
            fcl::Vector3d(a.x, a.y, a.z),
            fcl::Vector3d(b.x, b.y, b.z),
            fcl::Vector3d(c.x, c.y, c.z));
    }
    model->endModel();
    return model;
}

core::TriMesh createBoxTriMesh(const core::Vec3& bmin, const core::Vec3& bmax) {
    core::TriMesh m;
    m.bboxMin = bmin;
    m.bboxMax = bmax;
    m.vertices = {
        {bmin.x, bmin.y, bmin.z}, // 0
        {bmax.x, bmin.y, bmin.z}, // 1
        {bmax.x, bmax.y, bmin.z}, // 2
        {bmin.x, bmax.y, bmin.z}, // 3
        {bmin.x, bmin.y, bmax.z}, // 4
        {bmax.x, bmin.y, bmax.z}, // 5
        {bmax.x, bmax.y, bmax.z}, // 6
        {bmin.x, bmax.y, bmax.z}  // 7
    };
    m.triangles = {
        {0, 2, 1}, {0, 3, 2},
        {4, 5, 6}, {4, 6, 7},
        {0, 7, 3}, {0, 4, 7},
        {1, 2, 6}, {1, 6, 5},
        {0, 1, 5}, {0, 5, 4},
        {3, 6, 2}, {3, 7, 6}
    };
    return m;
}

double queryDistanceMm(
    const fcl::CollisionGeometryd* a,
    const fcl::Transform3d& aTf,
    const fcl::CollisionGeometryd* b,
    const fcl::Transform3d& bTf) {
    fcl::DistanceRequestd request;
    request.enable_signed_distance = true;
    fcl::DistanceResultd result;
    return fcl::distance(a, aTf, b, bTf, request, result);
}

double computeAabbMinDistance(
    const core::Vec3& minA, const core::Vec3& maxA,
    const core::Vec3& minB, const core::Vec3& maxB) noexcept {
    const double dx = std::max({minA.x - maxB.x, 0.0, minB.x - maxA.x});
    const double dy = std::max({minA.y - maxB.y, 0.0, minB.y - maxA.y});
    const double dz = std::max({minA.z - maxB.z, 0.0, minB.z - maxA.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void computeWorldAabb(
    const core::Vec3& localMin, const core::Vec3& localMax,
    const core::Transform4d& tf,
    core::Vec3& outMin, core::Vec3& outMax) noexcept {
    const core::Vec3 center = (localMin + localMax) * 0.5;
    const core::Vec3 ext = (localMax - localMin) * 0.5;
    const core::Vec3 worldCenter = tf.pos + tf.rot * center;
    const double ex = std::abs(tf.rot.m[0][0]) * ext.x + std::abs(tf.rot.m[0][1]) * ext.y + std::abs(tf.rot.m[0][2]) * ext.z;
    const double ey = std::abs(tf.rot.m[1][0]) * ext.x + std::abs(tf.rot.m[1][1]) * ext.y + std::abs(tf.rot.m[1][2]) * ext.z;
    const double ez = std::abs(tf.rot.m[2][0]) * ext.x + std::abs(tf.rot.m[2][1]) * ext.y + std::abs(tf.rot.m[2][2]) * ext.z;
    outMin = {worldCenter.x - ex, worldCenter.y - ey, worldCenter.z - ez};
    outMax = {worldCenter.x + ex, worldCenter.y + ey, worldCenter.z + ez};
}


struct RobotMeshAsset {
    std::array<std::shared_ptr<fcl::BVHModel<fcl::OBBRSSd>>, 7> linkBvhs{};
    std::array<core::Vec3, 7> bboxMin{};
    std::array<core::Vec3, 7> bboxMax{};
    bool loaded{false};
};

RobotMeshAsset loadRobotMeshAsset(const device::RobotModel& robot) {
    static std::mutex s_robotMutex;
    static std::unordered_map<std::string, RobotMeshAsset> s_robotCache;
    std::lock_guard<std::mutex> lock(s_robotMutex);

    auto it = s_robotCache.find(robot.modelId);
    if (it != s_robotCache.end()) {
        return it->second;
    }

    RobotMeshAsset asset;
    cad::OcctCadKernel cad;
    bool allOk = true;
    for (size_t i = 0; i < 7; ++i) {
        core::TriMesh mesh;
        if (cad.importRobotStlMillimeters(robot.collisionMeshFiles[i], mesh) && !mesh.triangles.empty()) {
            asset.linkBvhs[i] = buildBvhFromTriMesh(mesh);
            asset.bboxMin[i] = mesh.bboxMin;
            asset.bboxMax[i] = mesh.bboxMax;
        } else {
            allOk = false;
        }
    }
    asset.loaded = allOk;
    s_robotCache[robot.modelId] = asset;
    return asset;
}

struct SceneBvhItem {
    std::shared_ptr<fcl::BVHModel<fcl::OBBRSSd>> bvh{};
    core::Vec3 localBboxMin{};
    core::Vec3 localBboxMax{};
    bool valid{false};
};

SceneBvhItem getSceneBvh(const core::SceneNode& node) {
    static std::mutex s_sceneMutex;
    static std::unordered_map<std::string, SceneBvhItem> s_sceneCache;
    std::lock_guard<std::mutex> lock(s_sceneMutex);

    const std::string key = node.id + "_" + std::to_string(node.cadHandleId) + "_" +
                            std::to_string(node.collisionMesh.triangles.size());
    auto it = s_sceneCache.find(key);
    if (it != s_sceneCache.end()) {
        return it->second;
    }

    SceneBvhItem item;
    if (!node.collisionMesh.triangles.empty()) {
        item.bvh = buildBvhFromTriMesh(node.collisionMesh);
        item.localBboxMin = node.collisionMesh.bboxMin;
        item.localBboxMax = node.collisionMesh.bboxMax;
        item.valid = true;
    } else if (node.collisionMesh.bboxMax.x > node.collisionMesh.bboxMin.x &&
               node.collisionMesh.bboxMax.y > node.collisionMesh.bboxMin.y &&
               node.collisionMesh.bboxMax.z > node.collisionMesh.bboxMin.z) {
        item.bvh = buildBvhFromTriMesh(createBoxTriMesh(node.collisionMesh.bboxMin, node.collisionMesh.bboxMax));
        item.localBboxMin = node.collisionMesh.bboxMin;
        item.localBboxMax = node.collisionMesh.bboxMax;
        item.valid = true;
    }
    s_sceneCache[key] = item;
    return item;
}

#else

double pointToAabbSignedDistance(
    const core::Vec3& pt,
    const core::Vec3& boxMin,
    const core::Vec3& boxMax) noexcept {
    const double dxOut = std::max({boxMin.x - pt.x, 0.0, pt.x - boxMax.x});
    const double dyOut = std::max({boxMin.y - pt.y, 0.0, pt.y - boxMax.y});
    const double dzOut = std::max({boxMin.z - pt.z, 0.0, pt.z - boxMax.z});
    const double outsideDist = std::sqrt(dxOut * dxOut + dyOut * dyOut + dzOut * dzOut);
    if (outsideDist > 0.0) {
        return outsideDist;
    }
    const double dxIn = std::min(pt.x - boxMin.x, boxMax.x - pt.x);
    const double dyIn = std::min(pt.y - boxMin.y, boxMax.y - pt.y);
    const double dzIn = std::min(pt.z - boxMin.z, boxMax.z - pt.z);
    return -std::min({dxIn, dyIn, dzIn});
}

#endif

} // namespace

std::array<core::Transform4d, 7> forwardRobotLinkWorldFrames(
    const std::array<double, 6>& displayJointsRad,
    const core::Transform4d& robotBaseInWorld) noexcept {
    const core::Vec3 origins[6] = {
        {0, 0, 450}, {150, 0, 0}, {0, 0, 600}, {0, 0, 150}, {640, 0, 0}, {100, 0, 0}
    };
    const core::Vec3 axes[6] = {
        {0, 0, 1}, {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {0, -1, 0}, {-1, 0, 0}
    };

    std::array<double, 6> urdfQ = displayJointsRad;
    urdfQ[2] = displayJointsRad[2] + displayJointsRad[1]; // FANUC J2/J3 coupling

    std::array<core::Transform4d, 7> linkWorld{};
    linkWorld[0] = robotBaseInWorld;
    core::Transform4d cursor = robotBaseInWorld;
    for (int j = 0; j < 6; ++j) {
        core::Transform4d step;
        step.pos = origins[j];
        step.rot = axisRotation(axes[j], urdfQ[static_cast<size_t>(j)]);
        cursor = cursor * step;
        linkWorld[static_cast<size_t>(j + 1)] = cursor;
    }
    return linkWorld;
}

bool queryRobotAndTorchCollision(
    const OpwKinematics& kinematics,
    const core::UnitsSettings& settings,
    const core::Transform4d& tcpWorldPose,
    const std::optional<std::array<double, 6>>& jointsRad,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& /*utoolOffset*/,
    const std::vector<core::SceneNode>& sceneNodes,
    const std::string& activeWorkpieceId,
    bool isWeldingRole,
    double& outMinClearanceMm,
    std::string& outCollidingPartName,
    const std::vector<std::string>& exemptionPartIds) noexcept {
    outMinClearanceMm = 10000.0;
    outCollidingPartName.clear();

    struct TorchSphere {
        double offsetAlongNegZMm;
        double radiusMm;
        bool isWireTip;
    };
    constexpr std::array<TorchSphere, 4> torchSpheres{{
        {0.0,   2.0,  true},
        {45.0,  16.0, false},
        {95.0,  22.0, false},
        {150.0, 25.0, false}
    }};

    const core::Vec3 retreatDir = -tcpWorldPose.rot.colZ();

#ifdef OLP_HAS_FCL
    const auto robotAsset = loadRobotMeshAsset(kinematics.robotModel());
    const bool checkLinks = jointsRad.has_value() && robotAsset.loaded;
    std::array<core::Transform4d, 7> linkWorld{};
    std::array<core::Vec3, 7> linkAabbMin{};
    std::array<core::Vec3, 7> linkAabbMax{};

    if (checkLinks) {
        linkWorld = forwardRobotLinkWorldFrames(*jointsRad, robotBaseInWorld);
        for (size_t i = 1; i <= 6; ++i) {
            computeWorldAabb(robotAsset.bboxMin[i], robotAsset.bboxMax[i], linkWorld[i], linkAabbMin[i], linkAabbMax[i]);
        }
    }

    const std::array<std::shared_ptr<fcl::Sphered>, 4> fclSpheres{{
        std::make_shared<fcl::Sphered>(torchSpheres[0].radiusMm),
        std::make_shared<fcl::Sphered>(torchSpheres[1].radiusMm),
        std::make_shared<fcl::Sphered>(torchSpheres[2].radiusMm),
        std::make_shared<fcl::Sphered>(torchSpheres[3].radiusMm)
    }};

    std::array<core::Transform4d, 4> sphereTfs{};
    std::array<core::Vec3, 4> sphereAabbMin{};
    std::array<core::Vec3, 4> sphereAabbMax{};
    for (size_t s = 0; s < 4; ++s) {
        sphereTfs[s].pos = tcpWorldPose.pos + retreatDir * torchSpheres[s].offsetAlongNegZMm;
        const double r = torchSpheres[s].radiusMm;
        sphereAabbMin[s] = sphereTfs[s].pos - core::Vec3{r, r, r};
        sphereAabbMax[s] = sphereTfs[s].pos + core::Vec3{r, r, r};
    }

    for (const auto& node : sceneNodes) {
        if (!node.visible || node.type != core::SceneNodeType::Part) {
            continue;
        }
        if (node.collisionRole == core::CollisionRole::Ignored ||
            node.collisionRole == core::CollisionRole::RobotLink ||
            node.collisionRole == core::CollisionRole::WeldingTorch) {
            continue;
        }
        if (node.collisionRole == core::CollisionRole::Environment &&
            (node.name.rfind("FANUC_M10iA_Static", 0) == 0 || node.name.rfind("Robot_", 0) == 0)) {
            continue;
        }

        const bool isExemptBaseMetal = [&]() {
            if (!activeWorkpieceId.empty() && node.id == activeWorkpieceId) return true;
            for (const auto& id : exemptionPartIds) {
                if (node.id == id) return true;
            }
            return false;
        }();

        const auto obs = getSceneBvh(node);
        if (!obs.valid) continue;

        core::Vec3 obsAabbMin = obs.localBboxMin;
        core::Vec3 obsAabbMax = obs.localBboxMax;
        computeWorldAabb(obs.localBboxMin, obs.localBboxMax, node.localTransform, obsAabbMin, obsAabbMax);
        const fcl::Transform3d obsTf = toFclTransform(node.localTransform);

        // 1. Check robot links 1..6 (Link 0 on pedestal is excluded; adjacent links/flange-torch shielded)
        if (checkLinks) {
            for (size_t link = 1; link <= 6; ++link) {
                const double aabbDist = computeAabbMinDistance(linkAabbMin[link], linkAabbMax[link], obsAabbMin, obsAabbMax);
                if (aabbDist > std::max(outMinClearanceMm, 100.0)) {
                    continue; // Broadphase culling: discard obviously far apart pairs
                }
                const double dist = queryDistanceMm(
                    robotAsset.linkBvhs[link].get(), toFclTransform(linkWorld[link]),
                    obs.bvh.get(), obsTf);
                // Retain FCL exact distance (M10-FCL-001: no overwriting with pointToAabbSignedDistance)
                if (dist < outMinClearanceMm) {
                    outMinClearanceMm = dist;
                    outCollidingPartName = node.name;
                }
            }
        }

        // 2. Check 4 torch spheres along -Z_tcp
        for (size_t s = 0; s < 4; ++s) {
            const double aabbDist = computeAabbMinDistance(sphereAabbMin[s], sphereAabbMax[s], obsAabbMin, obsAabbMax);
            if (aabbDist > std::max(outMinClearanceMm, 100.0)) {
                continue; // Broadphase culling
            }
            const double dist = queryDistanceMm(
                fclSpheres[s].get(), toFclTransform(sphereTfs[s]),
                obs.bvh.get(), obsTf);

            // Wire-tip exemption on active workpiece during welding
            if (torchSpheres[s].isWireTip && isWeldingRole && isExemptBaseMetal &&
                dist >= -settings.wireTipExemptionRadiusMm) {
                continue;
            }

            // Retain FCL exact distance (M10-FCL-001: no overwriting with pointToAabbSignedDistance)
            if (dist < outMinClearanceMm) {
                outMinClearanceMm = dist;
                outCollidingPartName = node.name;
            }
        }
    }
#else
    for (const auto& node : sceneNodes) {
        if (!node.visible || node.type != core::SceneNodeType::Part) {
            continue;
        }
        if (node.collisionRole == core::CollisionRole::Ignored ||
            node.collisionRole == core::CollisionRole::Environment) {
            continue;
        }

        const bool isExemptBaseMetal = [&]() {
            if (node.id == activeWorkpieceId) return true;
            for (const auto& id : exemptionPartIds) {
                if (node.id == id) return true;
            }
            return false;
        }();

        for (const auto& sph : torchSpheres) {
            const core::Vec3 center = tcpWorldPose.pos + retreatDir * sph.offsetAlongNegZMm;
            const double rawDist = pointToAabbSignedDistance(
                center, node.collisionMesh.bboxMin, node.collisionMesh.bboxMax);
            if (sph.isWireTip && isWeldingRole && isExemptBaseMetal &&
                rawDist >= -settings.wireTipExemptionRadiusMm) {
                continue;
            }

            const double dist = rawDist - sph.radiusMm;
            if (dist < outMinClearanceMm) {
                outMinClearanceMm = dist;
                outCollidingPartName = node.name;
            }
        }
    }
#endif

    return (outMinClearanceMm <= 0.0);
}

} // namespace olp::algo
