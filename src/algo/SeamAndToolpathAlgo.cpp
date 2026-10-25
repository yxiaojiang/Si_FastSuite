#include "olp/algo/SeamAndToolpathAlgo.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace olp::algo {

std::optional<core::Transform4d> SeamAndToolpathAlgo::computeSeamFrame(
    const core::Vec3& point,
    const core::Vec3& tangent,
    const core::Vec3& outwardNormal1,
    const core::Vec3& outwardNormal2) noexcept {
    const core::Vec3 X = tangent.normalized();
    if (X.normSq() < 1e-10) {
        return std::nullopt;
    }

    const core::Vec3 n1 = outwardNormal1.normalized();
    const core::Vec3 n2 = outwardNormal2.normalized();
    const core::Vec3 sumN = n1 + n2;

    // Section 6.1: If n1 + n2 ~ 0, face orientation is inverted -> report error
    if (sumN.norm() < 1e-5) {
        return std::nullopt;
    }

    const core::Vec3 b = sumN.normalized();
    const core::Vec3 Z0 = -b; // Torch Z points into the weld root (-b)
    core::Vec3 Y = Z0.cross(X).normalized();
    if (Y.normSq() < 1e-10) {
        return std::nullopt;
    }
    const core::Vec3 Z = X.cross(Y).normalized();

    core::Transform4d frame;
    frame.pos = point;
    frame.rot = core::Mat3::fromColumns(X, Y, Z);
    return frame;
}

core::Transform4d SeamAndToolpathAlgo::applyWeldingAngles(
    const core::Transform4d& seamFrame,
    double workAngleDeg,
    double travelAngleDeg,
    double twistAngleDeg) noexcept {
    core::Transform4d out = seamFrame;
    const core::Mat3 offsetRot =
        core::Mat3::rotX(core::deg2rad(workAngleDeg)) *
        core::Mat3::rotY(core::deg2rad(travelAngleDeg)) *
        core::Mat3::rotZ(core::deg2rad(twistAngleDeg));
    out.rot = seamFrame.rot * offsetRot;
    return out;
}

std::optional<core::SeamDefinition> SeamAndToolpathAlgo::buildSeamFromPickedEdge(
    const cad::ICadKernel& cadKernel,
    int sourceCadHandleId,
    int edgeIndex,
    const std::vector<int>& allWorkpieceCadHandles,
    double sampleStepMm) {
    const auto edges = cadKernel.getEdges(sourceCadHandleId);
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size())) {
        return std::nullopt;
    }
    const auto& pickedEdge = edges[edgeIndex];
    const auto faces = cadKernel.getFaces(sourceCadHandleId);

    // Determine the two participating weld flank faces:
    // For a T-joint fillet weld, the picked edge belongs to Vertical_Web_Plate (has 2 adjacent faces on Web:
    // one vertical web wall and one bottom thickness face), and the second weld flank is the mating top face
    // of Base_Plate (a different CAD solid).
    core::Vec3 normal1{0.0, 0.0, 1.0};
    core::Vec3 normal2{0.0, -1.0, 0.0};
    int face1Idx = -1;
    int face2Idx = -1;
    std::string face2PartName;

    auto crossMatch = cadKernel.findClosestMatingFaceAcrossParts(
        sourceCadHandleId, edgeIndex, allWorkpieceCadHandles, 1.0);

        if (crossMatch.has_value()) {
        // Cross-part T-joint detected! Pick the adjacent face on source solid that is NOT parallel to crossMatch normal
        normal2 = crossMatch->outwardNormal;
        face2Idx = crossMatch->faceIndex;
        face2PartName = crossMatch->partName;

        bool foundFlank = false;
        for (int adjIdx : pickedEdge.adjacentFaceIndices) {
            if (adjIdx >= 0 && adjIdx < static_cast<int>(faces.size())) {
                const core::Vec3 candN = faces[adjIdx].outwardNormal;
                if (std::abs(candN.dot(normal2)) < 0.85) {
                    normal1 = candN;
                    face1Idx = adjIdx;
                    foundFlank = true;
                    break;
                }
            }
        }
        if (!foundFlank) {
            return std::nullopt;
        }
    } else if (pickedEdge.adjacentFaceIndices.size() >= 2) {
        face1Idx = pickedEdge.adjacentFaceIndices[0];
        face2Idx = pickedEdge.adjacentFaceIndices[1];
        normal1 = faces[face1Idx].outwardNormal;
        normal2 = faces[face2Idx].outwardNormal;
    } else {
        return std::nullopt;
    }

    core::SeamDefinition seam;
    seam.id = "seam_" + std::to_string(sourceCadHandleId) + "_" + std::to_string(edgeIndex);
    seam.name = "FilletSeam_E" + std::to_string(edgeIndex);
    seam.seamType = core::SeamType::Fillet;
    seam.edgeChain.push_back(cadKernel.computeEdgeSignature(sourceCadHandleId, edgeIndex));
    seam.face1PartId = "part_" + std::to_string(sourceCadHandleId);
    seam.face1Index = face1Idx;
    seam.face2PartId = crossMatch.has_value()
        ? ("part_" + std::to_string(crossMatch->partHandleId))
        : seam.face1PartId;
    seam.face2Index = face2Idx;

    const double totalLen = pickedEdge.lengthMm;
    const double sStart = std::min(seam.startTrimMm, totalLen * 0.25);
    const double sEnd = std::max(sStart, totalLen - std::min(seam.endTrimMm, totalLen * 0.25));
    const double step = std::max(5.0, sampleStepMm);

    auto appendSample = [&](double s) -> bool {
        core::Vec3 pt{}, tan{};
        if (!cadKernel.evaluateEdgeAtArcLength(sourceCadHandleId, edgeIndex, s, pt, tan)) {
            return false;
        }
        if (seam.reversed) {
            tan = -tan;
        }
        auto frameOpt = computeSeamFrame(pt, tan, normal1, normal2);
        if (!frameOpt.has_value()) {
            return false;
        }
        core::SeamSampleFrame sf;
        sf.arcLengthMm = s;
        sf.position = pt;
        sf.tangentX = tan;
        sf.normalFace1 = normal1;
        sf.normalFace2 = normal2;
        sf.bisector = (normal1 + normal2).normalized();
        sf.frame = *frameOpt;
        seam.sampledFrames.push_back(sf);
        return true;
    };

    for (double s = sStart; s < sEnd - 1e-6; s += step) {
        if (!appendSample(s)) {
            return std::nullopt;
        }
    }
    if (!appendSample(sEnd)) {
        return std::nullopt;
    }

    return seam;
}

void SeamAndToolpathAlgo::generateOperationTargets(
    const core::SeamDefinition& seam,
    const core::Transform4d& uframeInWorld,
    core::Operation& inOutOperation) {
    if (seam.sampledFrames.empty()) {
        return;
    }

    // Keep manual edits on the point they belong to. Approach/retract are matched
    // by role. Weld points are matched by arc-length ratio and each edit is consumed once.
    struct SavedUserModification {
        core::TargetRole role{core::TargetRole::VIA};
        double ratio{0.0};
        core::Transform4d deltaOffset{};
        bool consumed{false};
    };
    std::vector<SavedUserModification> savedMods;
    for (const auto& oldT : inOutOperation.targets) {
        if (!oldT.userModified) continue;
        const bool weldRole =
            oldT.role == core::TargetRole::WELD_START ||
            oldT.role == core::TargetRole::WELD_VIA ||
            oldT.role == core::TargetRole::WELD_END;
        const bool endRole =
            oldT.role == core::TargetRole::APPROACH ||
            oldT.role == core::TargetRole::RETRACT;
        if (weldRole || endRole) {
            savedMods.push_back({oldT.role, oldT.arcLengthRatio, oldT.userDeltaOffset, false});
        }
    }

    auto takeEndEdit = [&](core::TargetRole role) -> const SavedUserModification* {
        for (auto& mod : savedMods) {
            if (!mod.consumed && mod.role == role) {
                mod.consumed = true;
                return &mod;
            }
        }
        return nullptr;
    };
    auto takeWeldEdit = [&](double ratio) -> const SavedUserModification* {
        for (auto& mod : savedMods) {
            const bool weldRole =
                mod.role == core::TargetRole::WELD_START ||
                mod.role == core::TargetRole::WELD_VIA ||
                mod.role == core::TargetRole::WELD_END;
            if (!mod.consumed && weldRole && std::abs(mod.ratio - ratio) < 0.05) {
                mod.consumed = true;
                return &mod;
            }
        }
        return nullptr;
    };

    const core::Transform4d worldToUFrame = uframeInWorld.inverse();
    const auto& params = inOutOperation.genParams;

    // Select active seam frames (if linear and endpointsOnlyForLine, use first & last)
    std::vector<core::SeamSampleFrame> activeFrames;
    if (params.endpointsOnlyForLine && seam.sampledFrames.size() >= 2) {
        activeFrames.push_back(seam.sampledFrames.front());
        activeFrames.push_back(seam.sampledFrames.back());
    } else {
        activeFrames = seam.sampledFrames;
    }

    const double sMin = activeFrames.front().arcLengthMm;
    const double sMax = activeFrames.back().arcLengthMm;

    std::vector<core::Target> newTargets;

    auto makeWeldPoseInUFrame = [&](const core::SeamSampleFrame& sf) -> core::Transform4d {
        const core::Transform4d worldPose = applyWeldingAngles(
            sf.frame, params.workAngleDeg, params.travelAngleDeg, params.twistAngleDeg);
        return worldToUFrame * worldPose;
    };

    if (activeFrames.size() == 1) {
        // A single sample is both the arc-start point and the arc-end point.
        // One target would keep the arc on through retract.
        activeFrames.push_back(activeFrames.front());
    }

    const core::Transform4d startWeldPoseUF = makeWeldPoseInUFrame(activeFrames.front());
    const core::Transform4d endWeldPoseUF = makeWeldPoseInUFrame(activeFrames.back());

    // 1. APPROACH target (retreat along -Z_tcp by approachDistMm)
    {
        core::Target tApp;
        tApp.id = inOutOperation.id + "_T_APPROACH";
        tApp.name = "APPROACH";
        tApp.role = core::TargetRole::APPROACH;
        tApp.motion = core::MotionType::J;
        tApp.speedValue = 50.0; // 50%
        tApp.termination = core::TerminationType::CNT;
        tApp.cntValue = 50;
        tApp.poseInUFrame = startWeldPoseUF;
        tApp.poseInUFrame.pos = startWeldPoseUF.pos - (startWeldPoseUF.rot.colZ() * params.approachDistMm);
        if (const auto* mod = takeEndEdit(core::TargetRole::APPROACH)) {
            tApp.userModified = true;
            tApp.userDeltaOffset = mod->deltaOffset;
            tApp.poseInUFrame = tApp.poseInUFrame * mod->deltaOffset;
        }
        newTargets.push_back(std::move(tApp));
    }

    // 2. WELD_START, WELD_VIA*, WELD_END targets
    for (size_t i = 0; i < activeFrames.size(); ++i) {
        const bool isFirst = (i == 0);
        const bool isLast = (i + 1 == activeFrames.size());
        double ratio = 0.0;
        if (std::abs(sMax - sMin) < 1e-6) {
            ratio = isFirst ? 0.0 : 1.0;
        } else {
            ratio = (activeFrames[i].arcLengthMm - sMin) / (sMax - sMin);
        }

        core::Target tWeld;
        tWeld.id = inOutOperation.id + "_T_WELD_" + std::to_string(i + 1);
        tWeld.arcLengthRatio = ratio;
        tWeld.poseInUFrame = makeWeldPoseInUFrame(activeFrames[i]);
        tWeld.motion = core::MotionType::L;

        if (isFirst) {
            tWeld.name = "WELD_START";
            tWeld.role = core::TargetRole::WELD_START;
            tWeld.speedValue = params.approachSpeedMmPerSec;
            tWeld.useWeldSpeed = false;
            tWeld.termination = core::TerminationType::FINE;
            tWeld.events.push_back({core::ProcessEventType::ArcStart, params.weldScheduleNumber, 0.0, ""});
        } else if (isLast) {
            tWeld.name = "WELD_END";
            tWeld.role = core::TargetRole::WELD_END;
            tWeld.speedValue = params.weldSpeedMmPerSec;
            tWeld.useWeldSpeed = true;
            tWeld.termination = core::TerminationType::FINE;
            tWeld.events.push_back({core::ProcessEventType::ArcEnd, params.weldScheduleNumber, 0.0, ""});
        } else {
            tWeld.name = "WELD_VIA_" + std::to_string(i);
            tWeld.role = core::TargetRole::WELD_VIA;
            tWeld.speedValue = params.weldSpeedMmPerSec;
            tWeld.useWeldSpeed = true;
            tWeld.termination = core::TerminationType::CNT;
            tWeld.cntValue = 100;
        }

        if (const auto* mod = takeWeldEdit(ratio)) {
            tWeld.userModified = true;
            tWeld.userDeltaOffset = mod->deltaOffset;
            tWeld.poseInUFrame = tWeld.poseInUFrame * mod->deltaOffset;
        }

        newTargets.push_back(std::move(tWeld));
    }

    // 3. RETRACT target (retreat along -Z_tcp by retractDistMm)
    {
        core::Target tRet;
        tRet.id = inOutOperation.id + "_T_RETRACT";
        tRet.name = "RETRACT";
        tRet.role = core::TargetRole::RETRACT;
        tRet.motion = core::MotionType::L;
        tRet.speedValue = params.approachSpeedMmPerSec;
        tRet.termination = core::TerminationType::CNT;
        tRet.cntValue = 50;
        tRet.poseInUFrame = endWeldPoseUF;
        tRet.poseInUFrame.pos = endWeldPoseUF.pos - (endWeldPoseUF.rot.colZ() * params.retractDistMm);
        if (const auto* mod = takeEndEdit(core::TargetRole::RETRACT)) {
            tRet.userModified = true;
            tRet.userDeltaOffset = mod->deltaOffset;
            tRet.poseInUFrame = tRet.poseInUFrame * mod->deltaOffset;
        }
        newTargets.push_back(std::move(tRet));
    }

    inOutOperation.targets = std::move(newTargets);
}

} // namespace olp::algo
