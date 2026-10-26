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

std::optional<core::SeamDefinition> SeamAndToolpathAlgo::buildSeamFromEdgeChain(
    const cad::ICadKernel& cadKernel,
    int sourceCadHandleId,
    const cad::EdgeChainResult& chain,
    const std::vector<int>& allWorkpieceCadHandles,
    double sampleStepMm,
    double startTrimMm,
    double endTrimMm) {
    if (chain.edges.empty() || chain.totalLengthMm <= 1e-4) {
        return std::nullopt;
    }

    const auto allEdges = cadKernel.getEdges(sourceCadHandleId);
    const auto allFaces = cadKernel.getFaces(sourceCadHandleId);

    struct FlankInfo {
        core::Vec3 normal1{0.0, 0.0, 1.0};
        core::Vec3 normal2{0.0, -1.0, 0.0};
        int face1Idx{-1};
        int face2Idx{-1};
        int face2HandleId{-1};
        std::string face2PartId{};
        bool valid{false};
    };

    auto determineFlanks = [&](int edgeIdx) -> FlankInfo {
        FlankInfo info;
        info.face2HandleId = sourceCadHandleId;
        if (edgeIdx < 0 || edgeIdx >= static_cast<int>(allEdges.size())) {
            return info;
        }
        const auto& edge = allEdges[edgeIdx];
        auto crossMatch = cadKernel.findClosestMatingFaceAcrossParts(
            sourceCadHandleId, edgeIdx, allWorkpieceCadHandles, 1.0);

        if (crossMatch.has_value()) {
            info.normal2 = crossMatch->outwardNormal;
            info.face2Idx = crossMatch->faceIndex;
            info.face2HandleId = crossMatch->partHandleId;
            info.face2PartId = "part_" + std::to_string(crossMatch->partHandleId);
            for (int adjIdx : edge.adjacentFaceIndices) {
                if (adjIdx >= 0 && adjIdx < static_cast<int>(allFaces.size())) {
                    const core::Vec3 candN = allFaces[adjIdx].outwardNormal;
                    if (std::abs(candN.dot(info.normal2)) < 0.85) {
                        info.normal1 = candN;
                        info.face1Idx = adjIdx;
                        info.valid = true;
                        break;
                    }
                }
            }
        } else if (edge.adjacentFaceIndices.size() >= 2) {
            info.face1Idx = edge.adjacentFaceIndices[0];
            info.face2Idx = edge.adjacentFaceIndices[1];
            info.face2HandleId = sourceCadHandleId;
            info.face2PartId = "part_" + std::to_string(sourceCadHandleId);
            if (info.face1Idx >= 0 && info.face1Idx < static_cast<int>(allFaces.size()) &&
                info.face2Idx >= 0 && info.face2Idx < static_cast<int>(allFaces.size())) {
                info.normal1 = allFaces[info.face1Idx].outwardNormal;
                info.normal2 = allFaces[info.face2Idx].outwardNormal;
                info.valid = true;
            }
        }
        return info;
    };

    std::vector<FlankInfo> edgeFlanks;
    edgeFlanks.reserve(chain.edges.size());
    for (const auto& ce : chain.edges) {
        edgeFlanks.push_back(determineFlanks(ce.edgeIndex));
    }

    for (size_t i = 0; i < edgeFlanks.size(); ++i) {
        if (!edgeFlanks[i].valid) {
            for (size_t offset = 1; offset < edgeFlanks.size(); ++offset) {
                if (i >= offset && edgeFlanks[i - offset].valid) {
                    edgeFlanks[i] = edgeFlanks[i - offset];
                    break;
                }
                if (i + offset < edgeFlanks.size() && edgeFlanks[i + offset].valid) {
                    edgeFlanks[i] = edgeFlanks[i + offset];
                    break;
                }
            }
        }
    }
    if (edgeFlanks.empty() || !edgeFlanks[0].valid) {
        return std::nullopt;
    }

    core::SeamDefinition seam;
    const int firstEdgeIdx = chain.edges.front().edgeIndex;
    seam.id = "seam_chain_" + std::to_string(sourceCadHandleId) + "_" + std::to_string(firstEdgeIdx);
    seam.name = "ChainSeam_E" + std::to_string(firstEdgeIdx) + "_" + std::to_string(chain.edges.size()) + "edges";
    seam.seamType = core::SeamType::Fillet;
    seam.face1PartId = "part_" + std::to_string(sourceCadHandleId);
    seam.face1Index = edgeFlanks[0].face1Idx;
    seam.face2PartId = edgeFlanks[0].face2PartId;
    seam.face2Index = edgeFlanks[0].face2Idx;
    seam.startTrimMm = std::max(0.0, startTrimMm);
    seam.endTrimMm = std::max(0.0, endTrimMm);
    seam.isClosed = chain.isClosed;

    for (const auto& ce : chain.edges) {
        seam.edgeChain.push_back(cadKernel.computeEdgeSignature(sourceCadHandleId, ce.edgeIndex));
    }

    const double totalLen = chain.totalLengthMm;
    const double sStart = std::min(seam.startTrimMm, totalLen * 0.25);
    const double sEnd = std::max(sStart, totalLen - std::min(seam.endTrimMm, totalLen * 0.25));
    const double step = std::max(2.0, sampleStepMm);

    auto sampleAtGlobalS = [&](double globalS) -> std::optional<core::SeamSampleFrame> {
        double accum = 0.0;
        for (size_t i = 0; i < chain.edges.size(); ++i) {
            const auto& ce = chain.edges[i];
            const double edgeLen = ce.lengthMm;
            const bool isLastEdge = (i + 1 == chain.edges.size());
            if (globalS <= accum + edgeLen || isLastEdge) {
                const double localS = std::clamp(globalS - accum, 0.0, edgeLen);
                const double edgeS = ce.isReversed ? (edgeLen - localS) : localS;

                core::Vec3 pt{}, tan{};
                if (!cadKernel.evaluateEdgeAtArcLength(sourceCadHandleId, ce.edgeIndex, edgeS, pt, tan)) {
                    return std::nullopt;
                }
                if (ce.isReversed) {
                    tan = -tan;
                }
                const auto& flank = edgeFlanks[i];
                core::Vec3 n1 = flank.normal1;
                core::Vec3 n2 = flank.normal2;
                cadKernel.evaluateFaceOutwardNormal(sourceCadHandleId, flank.face1Idx, pt, n1);
                cadKernel.evaluateFaceOutwardNormal(flank.face2HandleId, flank.face2Idx, pt, n2);

                auto frameOpt = computeSeamFrame(pt, tan, n1, n2);
                if (!frameOpt.has_value()) {
                    n1 = flank.normal1;
                    n2 = flank.normal2;
                    frameOpt = computeSeamFrame(pt, tan, n1, n2);
                }
                if (!frameOpt.has_value()) {
                    return std::nullopt;
                }

                core::SeamSampleFrame sf;
                sf.arcLengthMm = globalS;
                sf.position = pt;
                sf.tangentX = tan;
                sf.normalFace1 = n1;
                sf.normalFace2 = n2;
                sf.bisector = (n1 + n2).normalized();
                sf.frame = *frameOpt;
                return sf;
            }
            accum += edgeLen;
        }
        return std::nullopt;
    };

    for (double s = sStart; s < sEnd - 1e-6; s += step) {
        auto sfOpt = sampleAtGlobalS(s);
        if (!sfOpt.has_value()) {
            return std::nullopt;
        }
        seam.sampledFrames.push_back(*sfOpt);
    }
    auto sfEnd = sampleAtGlobalS(sEnd);
    if (!sfEnd.has_value()) {
        return std::nullopt;
    }
    seam.sampledFrames.push_back(*sfEnd);

    return seam;
}

std::optional<core::SeamDefinition> SeamAndToolpathAlgo::buildSeamFromPickedEdge(
    const cad::ICadKernel& cadKernel,
    int sourceCadHandleId,
    int edgeIndex,
    const std::vector<int>& allWorkpieceCadHandles,
    double sampleStepMm,
    double startTrimMm) {
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
    seam.startTrimMm = std::max(0.0, startTrimMm);

    const double totalLen = pickedEdge.lengthMm;
    const double sStart = std::min(seam.startTrimMm, totalLen * 0.25);
    const double sEnd = std::max(sStart, totalLen - std::min(seam.endTrimMm, totalLen * 0.25));
    const double step = std::max(5.0, sampleStepMm);

    const int face2HandleId = crossMatch.has_value() ? crossMatch->partHandleId : sourceCadHandleId;

    auto appendSample = [&](double s) -> bool {
        core::Vec3 pt{}, tan{};
        if (!cadKernel.evaluateEdgeAtArcLength(sourceCadHandleId, edgeIndex, s, pt, tan)) {
            return false;
        }
        if (seam.reversed) {
            tan = -tan;
        }
        core::Vec3 n1 = normal1;
        core::Vec3 n2 = normal2;
        cadKernel.evaluateFaceOutwardNormal(sourceCadHandleId, face1Idx, pt, n1);
        cadKernel.evaluateFaceOutwardNormal(face2HandleId, face2Idx, pt, n2);

        auto frameOpt = computeSeamFrame(pt, tan, n1, n2);
        if (!frameOpt.has_value()) {
            n1 = normal1;
            n2 = normal2;
            frameOpt = computeSeamFrame(pt, tan, n1, n2);
        }
        if (!frameOpt.has_value()) {
            return false;
        }
        core::SeamSampleFrame sf;
        sf.arcLengthMm = s;
        sf.position = pt;
        sf.tangentX = tan;
        sf.normalFace1 = n1;
        sf.normalFace2 = n2;
        sf.bisector = (n1 + n2).normalized();
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
        bool poseModified{false};
        core::Transform4d deltaOffset{};
        bool processModified{false};
        core::MotionType motion{core::MotionType::L};
        double speedValue{200.0};
        bool useWeldSpeed{false};
        core::TerminationType termination{core::TerminationType::FINE};
        int cntValue{50};
        std::vector<core::ProcessEvent> events{};
        bool consumed{false};
    };
    std::vector<SavedUserModification> savedMods;
    for (const auto& oldT : inOutOperation.targets) {
        if (!oldT.userModified && !oldT.processModified) continue;
        const bool weldRole =
            oldT.role == core::TargetRole::WELD_START ||
            oldT.role == core::TargetRole::WELD_VIA ||
            oldT.role == core::TargetRole::WELD_END;
        const bool endRole =
            oldT.role == core::TargetRole::APPROACH ||
            oldT.role == core::TargetRole::RETRACT;
        if (weldRole || endRole) {
            SavedUserModification mod;
            mod.role = oldT.role;
            mod.ratio = oldT.arcLengthRatio;
            mod.poseModified = oldT.userModified;
            mod.deltaOffset = oldT.userDeltaOffset;
            mod.processModified = oldT.processModified;
            mod.motion = oldT.motion;
            mod.speedValue = oldT.speedValue;
            mod.useWeldSpeed = oldT.useWeldSpeed;
            mod.termination = oldT.termination;
            mod.cntValue = oldT.cntValue;
            mod.events = oldT.events;
            mod.consumed = false;
            savedMods.push_back(std::move(mod));
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

    // Determine whether seam is a single non-closed straight line segment
    bool isSingleStraightLine = (!seam.isClosed && seam.edgeChain.size() <= 1 && seam.sampledFrames.size() >= 2);
    if (isSingleStraightLine) {
        const auto& fFront = seam.sampledFrames.front();
        const auto& fBack = seam.sampledFrames.back();
        const core::Vec3 chord = fBack.position - fFront.position;
        const double chordLen = chord.norm();
        if (chordLen < 1e-4) {
            isSingleStraightLine = false;
        } else {
            const core::Vec3 dir = chord / chordLen;
            if (fFront.tangentX.dot(fBack.tangentX) < 0.99 || fFront.tangentX.dot(dir) < 0.98 || fBack.tangentX.dot(dir) < 0.98) {
                isSingleStraightLine = false;
            } else {
                for (const auto& sf : seam.sampledFrames) {
                    if (sf.tangentX.dot(dir) < 0.98) {
                        isSingleStraightLine = false;
                        break;
                    }
                    const core::Vec3 toPt = sf.position - fFront.position;
                    const double distToChord = toPt.cross(dir).norm();
                    if (distToChord > 0.05) {
                        isSingleStraightLine = false;
                        break;
                    }
                }
            }
        }
    }

    // Select active seam frames (if linear and endpointsOnlyForLine, use first & last;
    // composite edge chain / curved / closed seams retain all sampled frames)
    std::vector<core::SeamSampleFrame> activeFrames;
    if (params.endpointsOnlyForLine && isSingleStraightLine) {
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
        tApp.motion = params.approachMotion;
        tApp.speedValue = (params.approachMotion == core::MotionType::J) ? 50.0 : params.approachSpeedMmPerSec;
        tApp.termination = core::TerminationType::FINE;
        tApp.cntValue = 0;
        tApp.poseInUFrame = startWeldPoseUF;
        tApp.poseInUFrame.pos = startWeldPoseUF.pos - (startWeldPoseUF.rot.colZ() * params.approachDistMm);
        if (const auto* mod = takeEndEdit(core::TargetRole::APPROACH)) {
            if (mod->poseModified) {
                tApp.userModified = true;
                tApp.userDeltaOffset = mod->deltaOffset;
                tApp.poseInUFrame = tApp.poseInUFrame * mod->deltaOffset;
            }
            if (mod->processModified) {
                tApp.processModified = true;
                tApp.motion = mod->motion;
                tApp.speedValue = mod->speedValue;
                tApp.useWeldSpeed = mod->useWeldSpeed;
                tApp.termination = mod->termination;
                tApp.cntValue = mod->cntValue;
                tApp.events = mod->events;
            }
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
            tWeld.cntValue = 0;
            tWeld.events.push_back({core::ProcessEventType::ArcStart, params.weldScheduleNumber, 0.0, ""});
        } else if (isLast) {
            tWeld.name = "WELD_END";
            tWeld.role = core::TargetRole::WELD_END;
            tWeld.speedValue = params.weldSpeedMmPerSec;
            tWeld.useWeldSpeed = true;
            tWeld.termination = core::TerminationType::FINE;
            tWeld.cntValue = 0;
            tWeld.events.push_back({core::ProcessEventType::ArcEnd, params.weldScheduleNumber, 0.0, ""});
        } else {
            tWeld.name = "WELD_VIA_" + std::to_string(i);
            tWeld.role = core::TargetRole::WELD_VIA;
            tWeld.speedValue = params.weldSpeedMmPerSec;
            tWeld.useWeldSpeed = true;
            tWeld.termination = core::TerminationType::CNT;
            tWeld.cntValue = 50;
        }

        if (const auto* mod = takeWeldEdit(ratio)) {
            if (mod->poseModified) {
                tWeld.userModified = true;
                tWeld.userDeltaOffset = mod->deltaOffset;
                tWeld.poseInUFrame = tWeld.poseInUFrame * mod->deltaOffset;
            }
            if (mod->processModified) {
                tWeld.processModified = true;
                tWeld.motion = mod->motion;
                tWeld.speedValue = mod->speedValue;
                tWeld.useWeldSpeed = mod->useWeldSpeed;
                tWeld.termination = mod->termination;
                tWeld.cntValue = mod->cntValue;
                tWeld.events = mod->events;
            }
        }

        newTargets.push_back(std::move(tWeld));
    }

    // 3. RETRACT target (retreat along -Z_tcp by retractDistMm)
    {
        core::Target tRet;
        tRet.id = inOutOperation.id + "_T_RETRACT";
        tRet.name = "RETRACT";
        tRet.role = core::TargetRole::RETRACT;
        tRet.motion = params.retractMotion;
        tRet.speedValue = (params.retractMotion == core::MotionType::J) ? 50.0 : params.retractSpeedMmPerSec;
        tRet.termination = core::TerminationType::FINE;
        tRet.cntValue = 0;
        tRet.poseInUFrame = endWeldPoseUF;
        tRet.poseInUFrame.pos = endWeldPoseUF.pos - (endWeldPoseUF.rot.colZ() * params.retractDistMm);
        if (const auto* mod = takeEndEdit(core::TargetRole::RETRACT)) {
            if (mod->poseModified) {
                tRet.userModified = true;
                tRet.userDeltaOffset = mod->deltaOffset;
                tRet.poseInUFrame = tRet.poseInUFrame * mod->deltaOffset;
            }
            if (mod->processModified) {
                tRet.processModified = true;
                tRet.motion = mod->motion;
                tRet.speedValue = mod->speedValue;
                tRet.useWeldSpeed = mod->useWeldSpeed;
                tRet.termination = mod->termination;
                tRet.cntValue = mod->cntValue;
                tRet.events = mod->events;
            }
        }
        newTargets.push_back(std::move(tRet));
    }

    inOutOperation.targets = std::move(newTargets);
}

bool SeamAndToolpathAlgo::interpolateOrientationsSlerp(
    core::Operation& inOutOperation,
    const std::string& startTargetId,
    const std::string& endTargetId,
    const core::SeamDefinition* seam,
    const core::Transform4d& uframeInWorld) {
    (void)seam;
    (void)uframeInWorld;

    auto& targets = inOutOperation.targets;
    if (targets.size() < 3) {
        return false;
    }

    int idxStart = -1;
    int idxEnd = -1;

    if (!startTargetId.empty()) {
        for (size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].id == startTargetId) {
                idxStart = static_cast<int>(i);
                break;
            }
        }
    }
    if (!endTargetId.empty()) {
        for (size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].id == endTargetId) {
                idxEnd = static_cast<int>(i);
                break;
            }
        }
    }

    if (idxStart < 0) {
        for (size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].role == core::TargetRole::WELD_START ||
                targets[i].role == core::TargetRole::WELD_VIA) {
                idxStart = static_cast<int>(i);
                break;
            }
        }
        if (idxStart < 0) {
            idxStart = (targets.front().role == core::TargetRole::APPROACH && targets.size() > 1) ? 1 : 0;
        }
    }

    if (idxEnd < 0) {
        for (int i = static_cast<int>(targets.size()) - 1; i >= 0; --i) {
            if (targets[static_cast<size_t>(i)].role == core::TargetRole::WELD_END ||
                targets[static_cast<size_t>(i)].role == core::TargetRole::WELD_VIA) {
                idxEnd = i;
                break;
            }
        }
        if (idxEnd < 0) {
            idxEnd = (targets.back().role == core::TargetRole::RETRACT && targets.size() > 1)
                ? static_cast<int>(targets.size()) - 2
                : static_cast<int>(targets.size()) - 1;
        }
    }

    if (idxStart >= idxEnd || (idxEnd - idxStart) < 2) {
        return false;
    }

    auto computeBaseRotAtTarget = [&](const core::Target& t) -> core::Mat3 {
        if (t.userModified) {
            return t.poseInUFrame.rot * t.userDeltaOffset.rot.transposed();
        }
        return t.poseInUFrame.rot;
    };

    const auto& tA = targets[static_cast<size_t>(idxStart)];
    const auto& tB = targets[static_cast<size_t>(idxEnd)];

    const core::Mat3 baseRotA = computeBaseRotAtTarget(tA);
    const core::Mat3 baseRotB = computeBaseRotAtTarget(tB);

    core::Mat3 deltaA = baseRotA.transposed() * tA.poseInUFrame.rot;
    core::Mat3 deltaB = baseRotB.transposed() * tB.poseInUFrame.rot;
    if (tA.userModified) {
        deltaA = tA.userDeltaOffset.rot;
    }
    if (tB.userModified) {
        deltaB = tB.userDeltaOffset.rot;
    }

    const double sA = tA.arcLengthRatio;
    const double sB = tB.arcLengthRatio;

    bool updatedAny = false;
    for (int i = idxStart + 1; i < idxEnd; ++i) {
        auto& t = targets[static_cast<size_t>(i)];
        double tau = 0.0;
        if (std::abs(sB - sA) > 1e-6) {
            tau = (t.arcLengthRatio - sA) / (sB - sA);
        } else {
            tau = static_cast<double>(i - idxStart) / static_cast<double>(idxEnd - idxStart);
        }
        if (tau < 0.0) tau = 0.0;
        if (tau > 1.0) tau = 1.0;

        const core::Mat3 interpolatedDelta = core::Mat3::slerp(deltaA, deltaB, tau);
        const core::Mat3 baseRot = computeBaseRotAtTarget(t);
        const core::Mat3 newRot = baseRot * interpolatedDelta;

        if (t.poseInUFrame.rot.angularDistanceRad(newRot) > 1e-6) {
            t.poseInUFrame.rot = newRot;
            t.userDeltaOffset.rot = interpolatedDelta;
            t.userModified = true;
            t.cache.valid = false;
            updatedAny = true;
        }
    }

    return updatedAny;
}

bool SeamAndToolpathAlgo::extendStartTrimUntilTorchClears(
    const cad::ICadKernel& cadKernel,
    int sourceCadHandleId,
    int edgeIndex,
    const std::vector<int>& allWorkpieceCadHandles,
    double sampleStepMm,
    const CollisionAndSimEngine& collision,
    const core::Transform4d& uframeInWorld,
    const std::vector<core::SceneNode>& sceneNodes,
    core::SeamDefinition& inOutSeam,
    core::Operation& inOutOperation) {
    const auto edges = cadKernel.getEdges(sourceCadHandleId);
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size())) {
        return false;
    }
    const double maxTrim = edges[edgeIndex].lengthMm * 0.25;
    constexpr double kStepMm = 1.0;

    auto tryTrim = [&](double trimMm) -> bool {
        auto rebuilt = buildSeamFromPickedEdge(
            cadKernel, sourceCadHandleId, edgeIndex, allWorkpieceCadHandles, sampleStepMm, trimMm);
        if (!rebuilt.has_value() || rebuilt->sampledFrames.empty()) {
            return false;
        }
        inOutSeam = *rebuilt;
        generateOperationTargets(inOutSeam, uframeInWorld, inOutOperation);
        if (inOutOperation.targets.empty()) {
            return false;
        }
        const auto clearance = collision.measureOperationTorchClearance(
            inOutOperation, uframeInWorld, sceneNodes);
        return clearance.meetsSafeDistance;
    };

    const double first = std::min(std::max(0.0, inOutSeam.startTrimMm), maxTrim);
    for (double trim = first; trim <= maxTrim + 1e-6; trim += kStepMm) {
        if (tryTrim(trim)) {
            return true;
        }
    }
    return false;
}

bool SeamAndToolpathAlgo::rotateTargetAroundTcpZ(core::Target& target, double angleRad) {
    if (std::abs(angleRad) < 1e-6) {
        return false;
    }

    const core::Mat3 rotZ = core::Mat3::rotZ(angleRad);
    // Multiply target orientation in UFrame on the right by rotZ to rotate around local TCP Z
    const core::Mat3 newRot = core::Mat3::orthonormalize(target.poseInUFrame.rot * rotZ);
    target.poseInUFrame.rot = newRot;

    // TCP position is unchanged (rotating around TCP's own origin)
    // Update userDeltaOffset = userDeltaOffset * stepDelta
    core::Transform4d stepDelta;
    stepDelta.rot = rotZ;
    stepDelta.pos = {0.0, 0.0, 0.0};
    target.userDeltaOffset = target.userDeltaOffset * stepDelta;
    target.userModified = true;
    target.cache.valid = false;
    return true;
}

} // namespace olp::algo
