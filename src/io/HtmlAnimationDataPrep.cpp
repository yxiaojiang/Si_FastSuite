#include "HtmlAnimationDataPrep.h"
#include "olp/algo/FclCollisionQueries.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/io/Utf8Path.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <unordered_map>
#include <vector>

namespace olp::io {

std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c >= 0x20) out.push_back(static_cast<char>(c));
            break;
        }
    }
    return out;
}

std::string escapeHtmlText(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

void writeMeshArraysJson(std::ostream& os, const CompactMeshData& mesh) {
    os << "vertices:[";
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        if (i > 0) os << ",";
        os << mesh.vertices[i].x << "," << mesh.vertices[i].y << "," << mesh.vertices[i].z;
    }
    os << "],indices:[";
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
        if (i > 0) os << ",";
        os << mesh.triangles[i][0] << "," << mesh.triangles[i][1] << "," << mesh.triangles[i][2];
    }
    os << "]";
}

void writeTransform12Json(std::ostream& os, const core::Transform4d& tf) {
    const auto& r = tf.rot.m;
    const auto& p = tf.pos;
    os << "["
       << r[0][0] << "," << r[0][1] << "," << r[0][2] << "," << p.x << ","
       << r[1][0] << "," << r[1][1] << "," << r[1][2] << "," << p.y << ","
       << r[2][0] << "," << r[2][1] << "," << r[2][2] << "," << p.z << "]";
}

std::string roleNameString(core::CollisionRole role) {
    switch (role) {
    case core::CollisionRole::Workpiece: return "Workpiece";
    case core::CollisionRole::Fixture: return "Fixture";
    case core::CollisionRole::RobotLink: return "RobotLink";
    case core::CollisionRole::WeldingTorch: return "WeldingTorch";
    default: return "Environment";
    }
}

std::string targetRoleString(core::TargetRole role) {
    switch (role) {
    case core::TargetRole::HOME: return "HOME";
    case core::TargetRole::APPROACH: return "APPROACH";
    case core::TargetRole::WELD_START: return "WELD_START";
    case core::TargetRole::WELD_VIA: return "WELD_VIA";
    case core::TargetRole::WELD_END: return "WELD_END";
    case core::TargetRole::RETRACT: return "RETRACT";
    default: return "VIA";
    }
}

core::TriMesh makeBoxTriMesh(const core::Vec3& rawMin, const core::Vec3& rawMax) {
    core::Vec3 bmin{
        std::min(rawMin.x, rawMax.x),
        std::min(rawMin.y, rawMax.y),
        std::min(rawMin.z, rawMax.z)
    };
    core::Vec3 bmax{
        std::max(rawMin.x, rawMax.x),
        std::max(rawMin.y, rawMax.y),
        std::max(rawMin.z, rawMax.z)
    };
    if (bmax.x - bmin.x < 1.0) bmax.x = bmin.x + 1.0;
    if (bmax.y - bmin.y < 1.0) bmax.y = bmin.y + 1.0;
    if (bmax.z - bmin.z < 1.0) bmax.z = bmin.z + 1.0;

    core::TriMesh mesh;
    mesh.bboxMin = bmin;
    mesh.bboxMax = bmax;
    mesh.vertices = {
        {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z},
        {bmax.x, bmax.y, bmin.z}, {bmin.x, bmax.y, bmin.z},
        {bmin.x, bmin.y, bmax.z}, {bmax.x, bmin.y, bmax.z},
        {bmax.x, bmax.y, bmax.z}, {bmin.x, bmax.y, bmax.z}
    };
    mesh.triangles = {
        {0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4}, {2, 3, 7}, {2, 7, 6},
        {1, 2, 6}, {1, 6, 5}, {3, 0, 4}, {3, 4, 7}
    };
    return mesh;
}

bool loadBinaryRobotStlMm(const std::string& filePath, core::TriMesh& outMesh) {
    if (filePath.empty()) return false;
    std::ifstream ifs(utf8ToPath(filePath), std::ios::binary);
    if (!ifs.is_open()) return false;

    ifs.seekg(0, std::ios::end);
    const std::streamsize fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    if (fileSize < 84) return false;

    char header[80]{};
    ifs.read(header, 80);
    std::uint32_t triCount = 0;
    ifs.read(reinterpret_cast<char*>(&triCount), sizeof(std::uint32_t));
    const std::streamsize expectedSize = 84 + static_cast<std::streamsize>(triCount) * 50;
    if (expectedSize != fileSize || triCount == 0) return false;

    outMesh = core::TriMesh{};
    outMesh.bboxMin = {1e18, 1e18, 1e18};
    outMesh.bboxMax = {-1e18, -1e18, -1e18};
    outMesh.vertices.reserve(static_cast<std::size_t>(triCount) * 3);
    outMesh.triangles.reserve(triCount);

    constexpr double kMetersToMm = 1000.0;
    for (std::uint32_t i = 0; i < triCount; ++i) {
        float normal[3]{};
        float v[9]{};
        std::uint16_t attr = 0;
        ifs.read(reinterpret_cast<char*>(normal), 12);
        ifs.read(reinterpret_cast<char*>(v), 36);
        ifs.read(reinterpret_cast<char*>(&attr), 2);
        const auto baseIdx = static_cast<std::uint32_t>(outMesh.vertices.size());
        for (int k = 0; k < 3; ++k) {
            core::Vec3 pt{
                static_cast<double>(v[k * 3 + 0]) * kMetersToMm,
                static_cast<double>(v[k * 3 + 1]) * kMetersToMm,
                static_cast<double>(v[k * 3 + 2]) * kMetersToMm
            };
            outMesh.vertices.push_back(pt);
            outMesh.bboxMin.x = std::min(outMesh.bboxMin.x, pt.x);
            outMesh.bboxMin.y = std::min(outMesh.bboxMin.y, pt.y);
            outMesh.bboxMin.z = std::min(outMesh.bboxMin.z, pt.z);
            outMesh.bboxMax.x = std::max(outMesh.bboxMax.x, pt.x);
            outMesh.bboxMax.y = std::max(outMesh.bboxMax.y, pt.y);
            outMesh.bboxMax.z = std::max(outMesh.bboxMax.z, pt.z);
        }
        outMesh.triangles.push_back({baseIdx, baseIdx + 1, baseIdx + 2});
    }
    return true;
}

core::TriMesh makeFallbackRobotLinkMesh(std::size_t linkIdx) {
    static const std::array<std::pair<core::Vec3, core::Vec3>, 7> kLinkBoxes{{
        {{-170.0, -170.0,    0.0}, {170.0, 170.0, 240.0}},
        {{-150.0, -140.0, -210.0}, {190.0, 140.0, 110.0}},
        {{ -85.0,  -85.0,    0.0}, { 85.0,  85.0, 600.0}},
        {{ -95.0,  -95.0,  -70.0}, {170.0,  95.0, 170.0}},
        {{   0.0,  -60.0,  -60.0}, {640.0,  60.0,  60.0}},
        {{ -45.0,  -45.0,  -45.0}, {100.0,  45.0,  45.0}},
        {{ -38.0,  -38.0,  -15.0}, { 38.0,  38.0,  18.0}}
    }};
    const auto& b = kLinkBoxes[std::min<std::size_t>(linkIdx, 6)];
    return makeBoxTriMesh(b.first, b.second);
}

core::Transform4d computeFlangeToTool0(const device::RobotModel& robot) {
    if (robot.urdfLoaded) {
        return robot.urdfFlangeToTool0;
    }
    // FANUC URDF default: link_6 to tool0 has rpy="3.14159 -1.570796 0"
    // R = RotY(-pi/2) * RotX(pi) = [ [0,0,1], [0,-1,0], [1,0,0] ]
    core::Transform4d tf = core::Transform4d::identity();
    tf.rot.m[0][0] =  0.0; tf.rot.m[0][1] =  0.0; tf.rot.m[0][2] =  1.0;
    tf.rot.m[1][0] =  0.0; tf.rot.m[1][1] = -1.0; tf.rot.m[1][2] =  0.0;
    tf.rot.m[2][0] =  1.0; tf.rot.m[2][1] =  0.0; tf.rot.m[2][2] =  0.0;
    tf.pos = {0.0, 0.0, 0.0};
    return tf;
}

core::TriMesh makeWeldingTorchMeshInLink6(
    const core::Transform4d& utoolOffset,
    const core::Transform4d& flangeToTool0,
    core::Vec3* outTcpLocalInLink6) {
    // 1. Build torch body in tool0 coordinate frame along utoolOffset.pos
    const core::Vec3 tcpInTool0 = (utoolOffset.pos.norm() > 1.0) ? utoolOffset.pos : core::Vec3{0.0, 0.0, 150.0};
    const core::Vec3 dir = tcpInTool0.normalized();
    const double len = tcpInTool0.norm();
    core::Vec3 up{1.0, 0.0, 0.0};
    if (std::abs(dir.dot(up)) > 0.9) up = {0.0, 1.0, 0.0};
    const core::Vec3 u = dir.cross(up).normalized();
    const core::Vec3 v = dir.cross(u).normalized();

    struct RingSpec { double zRatio; double radiusMm; };
    constexpr std::array<RingSpec, 5> rings{{{0.00, 24.0}, {0.40, 20.0}, {0.72, 15.0}, {0.90, 8.0}, {1.00, 1.8}}};
    constexpr int kSegs = 8;

    std::vector<core::Vec3> rawVertsInTool0;
    rawVertsInTool0.reserve(rings.size() * kSegs);
    for (const auto& ring : rings) {
        const core::Vec3 center = dir * (len * ring.zRatio);
        for (int s = 0; s < kSegs; ++s) {
            const double ang = (2.0 * core::kPi * s) / kSegs;
            rawVertsInTool0.push_back(center + u * (ring.radiusMm * std::cos(ang)) + v * (ring.radiusMm * std::sin(ang)));
        }
    }

    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (std::size_t r = 0; r + 1 < rings.size(); ++r) {
        const auto b0 = static_cast<std::uint32_t>(r * kSegs);
        const auto b1 = static_cast<std::uint32_t>((r + 1) * kSegs);
        for (int s = 0; s < kSegs; ++s) {
            const auto sNext = static_cast<std::uint32_t>((s + 1) % kSegs);
            triangles.push_back({b0 + s, b0 + sNext, b1 + sNext});
            triangles.push_back({b0 + s, b1 + sNext, b1 + s});
        }
    }

    // 2. Transform all torch vertices to link_6 coordinate space via flangeToTool0
    // so torchMesh mounts on link_6 flange and tip coincides exactly with f.tcp
    core::TriMesh meshInLink6;
    meshInLink6.bboxMin = {1e18, 1e18, 1e18};
    meshInLink6.bboxMax = {-1e18, -1e18, -1e18};
    meshInLink6.vertices.reserve(rawVertsInTool0.size());
    meshInLink6.triangles = std::move(triangles);

    for (const auto& ptTool0 : rawVertsInTool0) {
        const core::Vec3 ptLink6 = flangeToTool0.transformPoint(ptTool0);
        meshInLink6.vertices.push_back(ptLink6);
        meshInLink6.bboxMin.x = std::min(meshInLink6.bboxMin.x, ptLink6.x);
        meshInLink6.bboxMin.y = std::min(meshInLink6.bboxMin.y, ptLink6.y);
        meshInLink6.bboxMin.z = std::min(meshInLink6.bboxMin.z, ptLink6.z);
        meshInLink6.bboxMax.x = std::max(meshInLink6.bboxMax.x, ptLink6.x);
        meshInLink6.bboxMax.y = std::max(meshInLink6.bboxMax.y, ptLink6.y);
        meshInLink6.bboxMax.z = std::max(meshInLink6.bboxMax.z, ptLink6.z);
    }

    if (outTcpLocalInLink6 != nullptr) {
        *outTcpLocalInLink6 = flangeToTool0.transformPoint(tcpInTool0);
    }

    return meshInLink6;
}

CompactMeshData compactAndDecimateMesh(
    const core::TriMesh& src,
    const core::Transform4d& worldTf,
    std::size_t maxTriangles) {
    CompactMeshData out;
    if (src.vertices.empty() || src.triangles.empty()) return out;

    out.bboxMin = {1e18, 1e18, 1e18};
    out.bboxMax = {-1e18, -1e18, -1e18};

    const std::size_t totalTris = src.triangles.size();
    const std::size_t cap = std::max<std::size_t>(12, maxTriangles);
    const double stride = (totalTris > cap) ? (static_cast<double>(totalTris) / static_cast<double>(cap)) : 1.0;
    const std::size_t targetCount = std::min(totalTris, cap);

    std::unordered_map<std::uint64_t, std::uint32_t> vertMap;
    vertMap.reserve(targetCount * 2);

    auto quantizeKey = [](const core::Vec3& p) noexcept -> std::uint64_t {
        const auto qx = static_cast<std::int64_t>(std::llround(p.x * 10.0)) + 1000000LL;
        const auto qy = static_cast<std::int64_t>(std::llround(p.y * 10.0)) + 1000000LL;
        const auto qz = static_cast<std::int64_t>(std::llround(p.z * 10.0)) + 1000000LL;
        return (static_cast<std::uint64_t>(qx & 0x1FFFFF) << 42) |
               (static_cast<std::uint64_t>(qy & 0x1FFFFF) << 21) |
               static_cast<std::uint64_t>(qz & 0x1FFFFF);
    };

    auto addVertex = [&](const core::Vec3& localPt) -> std::uint32_t {
        const core::Vec3 wp = worldTf.transformPoint(localPt);
        out.bboxMin.x = std::min(out.bboxMin.x, wp.x);
        out.bboxMin.y = std::min(out.bboxMin.y, wp.y);
        out.bboxMin.z = std::min(out.bboxMin.z, wp.z);
        out.bboxMax.x = std::max(out.bboxMax.x, wp.x);
        out.bboxMax.y = std::max(out.bboxMax.y, wp.y);
        out.bboxMax.z = std::max(out.bboxMax.z, wp.z);

        const std::uint64_t key = quantizeKey(wp);
        auto it = vertMap.find(key);
        if (it != vertMap.end()) return it->second;
        const auto idx = static_cast<std::uint32_t>(out.vertices.size());
        out.vertices.push_back(wp);
        vertMap.emplace(key, idx);
        return idx;
    };

    for (std::size_t k = 0; k < targetCount; ++k) {
        const std::size_t srcIdx = std::min(totalTris - 1, static_cast<std::size_t>(std::floor(k * stride)));
        const auto& tri = src.triangles[srcIdx];
        if (tri[0] >= src.vertices.size() || tri[1] >= src.vertices.size() || tri[2] >= src.vertices.size()) continue;
        const std::uint32_t i0 = addVertex(src.vertices[tri[0]]);
        const std::uint32_t i1 = addVertex(src.vertices[tri[1]]);
        const std::uint32_t i2 = addVertex(src.vertices[tri[2]]);
        if (i0 != i1 && i1 != i2 && i2 != i0) {
            out.triangles.push_back({i0, i1, i2});
        }
    }
    return out;
}

namespace {
bool linkTransformsNeedUrdfRebuild(const std::array<core::Transform4d, 7>& tfs) noexcept {
    bool allIdentity = true;
    for (const auto& tf : tfs) {
        if (tf.pos.norm() > 1e-6 ||
            std::abs(tf.rot.m[0][0] - 1.0) > 1e-6 ||
            std::abs(tf.rot.m[1][1] - 1.0) > 1e-6 ||
            std::abs(tf.rot.m[2][2] - 1.0) > 1e-6) {
            allIdentity = false;
            break;
        }
    }
    if (allIdentity) return true;
    const double d45 = (tfs[4].pos - tfs[5].pos).norm();
    const double d23 = (tfs[2].pos - tfs[3].pos).norm();
    return (d45 < 1e-3 && d23 > 100.0);
}
} // namespace

std::vector<algo::BakedFrame> normalizeAndDecimateFrames(
    const std::vector<algo::BakedFrame>& rawFrames,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& utoolOffset,
    const device::RobotModel& robotModel,
    const HtmlExportOptions& options) {
    std::vector<algo::BakedFrame> normalized;
    normalized.reserve(rawFrames.size());
    algo::OpwKinematics kin(robotModel);

    for (const auto& f : rawFrames) {
        algo::BakedFrame nf = f;
        if (linkTransformsNeedUrdfRebuild(nf.linkWorldTransforms)) {
            nf.linkWorldTransforms = algo::forwardRobotLinkWorldFrames(nf.jointsRad, robotBaseInWorld);
        }
        if (nf.tcpWorldPos.norm() < 1e-6) {
            const auto fk = kin.forwardKinematics(nf.jointsRad, utoolOffset);
            nf.tcpWorldPos = (robotBaseInWorld * fk.tcpPoseInBase).pos;
        }
        normalized.push_back(nf);
    }
    if (normalized.size() <= 2) return normalized;

    std::vector<algo::BakedFrame> kept;
    kept.reserve(normalized.size());
    kept.push_back(normalized.front());

    const double posTol = std::max(0.01, options.keyframePosTolMm);
    const double jointTolRad = core::deg2rad(std::max(0.01, options.keyframeJointTolDeg));

    for (std::size_t i = 1; i + 1 < normalized.size(); ++i) {
        const auto& prev = kept.back();
        const auto& curr = normalized[i];
        const auto& next = normalized[i + 1];

        if (curr.arcActive != prev.arcActive || curr.arcActive != next.arcActive) {
            kept.push_back(curr);
            continue;
        }

        const double dPrevPos = (curr.tcpWorldPos - prev.tcpWorldPos).norm();
        const double dNextPos = (next.tcpWorldPos - prev.tcpWorldPos).norm();
        double maxJPrev = 0.0, maxJNext = 0.0;
        for (std::size_t j = 0; j < 6; ++j) {
            maxJPrev = std::max(maxJPrev, std::abs(curr.jointsRad[j] - prev.jointsRad[j]));
            maxJNext = std::max(maxJNext, std::abs(next.jointsRad[j] - prev.jointsRad[j]));
        }
        if (dPrevPos < 0.05 && dNextPos < 0.05 && maxJPrev < 1e-4 && maxJNext < 1e-4) {
            continue;
        }

        const double dtSpan = next.timeSec - prev.timeSec;
        if (dtSpan > 1e-6 && dtSpan <= 0.25) {
            const double alpha = std::clamp((curr.timeSec - prev.timeSec) / dtSpan, 0.0, 1.0);
            const core::Vec3 interpTcp = prev.tcpWorldPos + (next.tcpWorldPos - prev.tcpWorldPos) * alpha;
            const double lineDevMm = (curr.tcpWorldPos - interpTcp).norm();
            double maxJointLineDev = 0.0;
            for (std::size_t j = 0; j < 6; ++j) {
                const double ij = prev.jointsRad[j] + alpha * (next.jointsRad[j] - prev.jointsRad[j]);
                maxJointLineDev = std::max(maxJointLineDev, std::abs(curr.jointsRad[j] - ij));
            }
            if (lineDevMm <= posTol && maxJointLineDev <= jointTolRad) {
                continue;
            }
        }
        kept.push_back(curr);
    }
    kept.push_back(normalized.back());
    return kept;
}

void extractOperationsAndWaypoints(
    const core::Program& program,
    const core::Transform4d& uframeInWorld,
    const std::vector<algo::BakedFrame>& decimatedFrames,
    std::vector<OperationExportRecord>& outOps,
    std::vector<WaypointExportRecord>& outWps) {
    outOps.clear();
    outWps.clear();

    std::size_t globalPtIndex = 0;
    std::size_t searchFrameCursor = 0;

    for (std::size_t opIdx = 0; opIdx < program.operations.size(); ++opIdx) {
        const auto& op = program.operations[opIdx];
        if (!op.enabled) continue;
        std::size_t opStartFrame = searchFrameCursor;
        std::size_t weldStartFrame = searchFrameCursor;
        bool sawOpStart = false;
        bool sawWeldStart = false;

        for (const auto& t : op.targets) {
            ++globalPtIndex;
            const core::Transform4d wpPose = uframeInWorld * t.poseInUFrame;
            const core::Vec3 wpPos = wpPose.pos;
            const core::Vec3 wpDirZ = wpPose.rot.colZ();

            std::size_t bestIdx = searchFrameCursor;
            double bestDist = 1e18;
            for (std::size_t fi = searchFrameCursor; fi < decimatedFrames.size(); ++fi) {
                const double d = (decimatedFrames[fi].tcpWorldPos - wpPos).norm();
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = fi;
                    if (d < 1.5) break;
                }
            }
            searchFrameCursor = bestIdx;
            if (!sawOpStart) {
                opStartFrame = bestIdx;
                sawOpStart = true;
            }
            const bool isWeld =
                (t.role == core::TargetRole::WELD_START ||
                 t.role == core::TargetRole::WELD_VIA ||
                 t.role == core::TargetRole::WELD_END);
            if (!sawWeldStart && (t.role == core::TargetRole::WELD_START || isWeld)) {
                weldStartFrame = bestIdx;
                sawWeldStart = true;
            }

            outWps.push_back({
                globalPtIndex,
                t.id,
                t.name.empty() ? ("P" + std::to_string(globalPtIndex)) : t.name,
                op.name.empty() ? op.id : op.name,
                targetRoleString(t.role),
                isWeld,
                wpPos,
                wpDirZ,
                bestIdx
            });
        }
        if (!sawWeldStart) weldStartFrame = opStartFrame;
        const double startTime = (!decimatedFrames.empty() && opStartFrame < decimatedFrames.size())
            ? decimatedFrames[opStartFrame].timeSec : 0.0;
        const double weldTime = (!decimatedFrames.empty() && weldStartFrame < decimatedFrames.size())
            ? decimatedFrames[weldStartFrame].timeSec : startTime;

        outOps.push_back({
            opIdx,
            op.id,
            op.name.empty() ? op.id : op.name,
            op.seamId,
            opStartFrame,
            weldStartFrame,
            startTime,
            weldTime
        });
    }
}

} // namespace olp::io
