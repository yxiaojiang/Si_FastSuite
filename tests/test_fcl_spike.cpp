#include "test_support.h"

#include "olp/app/WorkcellLayoutService.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/core/ProjectModel.h"
#include "olp/device/RobotModel.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

#ifdef OLP_HAS_FCL
#include "fcl/geometry/bvh/BVH_model.h"
#include "fcl/geometry/shape/sphere.h"
#include "fcl/narrowphase/distance.h"
#endif

namespace {

#ifdef OLP_HAS_FCL

fcl::Transform3d toFcl(const olp::core::Transform4d& pose) {
    fcl::Transform3d out = fcl::Transform3d::Identity();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out.linear()(row, col) = pose.rot.m[row][col];
        }
    }
    out.translation() = fcl::Vector3d(pose.pos.x, pose.pos.y, pose.pos.z);
    return out;
}

std::shared_ptr<fcl::BVHModel<fcl::OBBRSSd>> buildBvh(const olp::core::TriMesh& mesh) {
    auto model = std::make_shared<fcl::BVHModel<fcl::OBBRSSd>>();
    model->beginModel(static_cast<unsigned int>(mesh.triangles.size()));
    for (const auto& tri : mesh.triangles) {
        const olp::core::Vec3& a = mesh.vertices[tri[0]];
        const olp::core::Vec3& b = mesh.vertices[tri[1]];
        const olp::core::Vec3& c = mesh.vertices[tri[2]];
        model->addTriangle(
            fcl::Vector3d(a.x, a.y, a.z),
            fcl::Vector3d(b.x, b.y, b.z),
            fcl::Vector3d(c.x, c.y, c.z));
    }
    model->endModel();
    return model;
}

double distanceMm(
    const fcl::CollisionGeometryd* a,
    const fcl::Transform3d& aTf,
    const fcl::CollisionGeometryd* b,
    const fcl::Transform3d& bTf) {
    fcl::DistanceRequestd request;
    request.enable_signed_distance = true;
    fcl::DistanceResultd result;
    return fcl::distance(a, aTf, b, bTf, request, result);
}

#endif

} // namespace

void testFclSinglePoseTiming() {
    std::cout << "\n--- Test 21: FCL single-pose distance timing ---\n";
#if !defined(OLP_HAS_FCL) || !defined(OLP_HAS_OCCT)
    check(true, "FCL single-pose timing skipped; FCL or OpenCASCADE is not linked");
    return;
#else
    olp::app::ProjectDocument doc;
    olp::cad::OcctCadKernel cad;
    const auto robot = olp::device::RobotModel::createFanucM10iA();
    const std::string stepPath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step";
    const auto imported = olp::app::WorkcellLayoutService::importFullSceneAndReplaceRobot(
        doc, cad, stepPath, robot);
    check(imported.success, "Workcell import succeeded before the FCL timing query");
    if (!imported.success) return;

    const olp::core::SceneNode* fixture = nullptr;
    for (const auto& node : doc.sceneNodes) {
        if (node.collisionRole != olp::core::CollisionRole::Fixture) continue;
        if (node.collisionMesh.triangles.empty()) continue;
        if (fixture == nullptr || node.collisionMesh.triangles.size() > fixture->collisionMesh.triangles.size()) {
            fixture = &node;
        }
    }
    check(fixture != nullptr, "A fixture collision mesh with triangles is available");
    if (fixture == nullptr) return;

    std::array<olp::core::TriMesh, 7> links{};
    bool linksOk = true;
    for (size_t i = 0; i < links.size(); ++i) {
        if (!cad.importRobotStlMillimeters(robot.collisionMeshFiles[i], links[i]) ||
            links[i].triangles.empty()) {
            linksOk = false;
        }
    }
    check(linksOk, "Seven robot collision STLs convert from meters to millimeters");
    if (!linksOk) return;

    const auto buildStart = std::chrono::steady_clock::now();
    const auto fixtureBvh = buildBvh(fixture->collisionMesh);
    std::array<std::shared_ptr<fcl::BVHModel<fcl::OBBRSSd>>, 7> linkBvhs{};
    for (size_t i = 0; i < links.size(); ++i) {
        linkBvhs[i] = buildBvh(links[i]);
    }
    const std::array<std::shared_ptr<fcl::Sphered>, 4> spheres{{
        std::make_shared<fcl::Sphered>(2.0),
        std::make_shared<fcl::Sphered>(16.0),
        std::make_shared<fcl::Sphered>(22.0),
        std::make_shared<fcl::Sphered>(25.0)
    }};
    const double buildMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - buildStart).count();

    // URDF joint origins in millimeters. Zero joint angles, base on the pedestal.
    // These frames follow the URDF chain, not the OPW link frames used for drawing.
    const olp::core::Vec3 origins[6] = {
        {0, 0, 450}, {150, 0, 0}, {0, 0, 600}, {0, 0, 150}, {640, 0, 0}, {100, 0, 0}
    };
    std::array<olp::core::Transform4d, 7> linkWorld{};
    linkWorld[0] = olp::core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    olp::core::Transform4d cursor = linkWorld[0];
    for (int joint = 0; joint < 6; ++joint) {
        olp::core::Transform4d step;
        step.pos = origins[joint];
        cursor = cursor * step;
        linkWorld[static_cast<size_t>(joint + 1)] = cursor;
    }

    const olp::core::Vec3 clampCenter = (fixture->collisionMesh.bboxMin + fixture->collisionMesh.bboxMax) * 0.5;
    const olp::core::Vec3 ontoClamp = clampCenter - linkWorld[6].pos;
    std::array<olp::core::Transform4d, 7> closeWorld = linkWorld;
    for (auto& pose : closeWorld) {
        pose.pos = pose.pos + ontoClamp;
    }

    const double sphereOffsetMm[4] = {0.0, 45.0, 95.0, 150.0};
    auto query = [&](const std::array<olp::core::Transform4d, 7>& poses, double& minDist) {
        minDist = std::numeric_limits<double>::infinity();
        const auto started = std::chrono::steady_clock::now();
        for (size_t i = 0; i < linkBvhs.size(); ++i) {
            const double dist = distanceMm(
                linkBvhs[i].get(), toFcl(poses[i]), fixtureBvh.get(), fcl::Transform3d::Identity());
            minDist = std::min(minDist, dist);
        }
        const olp::core::Vec3 toolZ = poses[6].rot.colZ();
        for (size_t s = 0; s < spheres.size(); ++s) {
            olp::core::Transform4d spherePose = poses[6];
            spherePose.pos = poses[6].pos - toolZ * sphereOffsetMm[s];
            const double dist = distanceMm(
                spheres[s].get(), toFcl(spherePose), fixtureBvh.get(), fcl::Transform3d::Identity());
            minDist = std::min(minDist, dist);
        }
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    };

    double homeDist = 0.0;
    double closeDist = 0.0;
    const double homeMs = query(linkWorld, homeDist);
    const double closeMs = query(closeWorld, closeDist);
    const bool finite = std::isfinite(homeDist) && std::isfinite(closeDist) &&
        std::isfinite(homeMs) && std::isfinite(closeMs);

    std::ostringstream detail;
    detail << "fixture=" << fixture->name
           << " tris=" << fixture->collisionMesh.triangles.size()
           << " buildMs=" << buildMs
           << " homeMs=" << homeMs << " homeDistMm=" << homeDist
           << " closeMs=" << closeMs << " closeDistMm=" << closeDist;
    check(finite && homeMs >= 0.0 && closeMs >= 0.0,
          "FCL one-pose query finished (" + detail.str() + ")",
          detail.str());
#endif
}
