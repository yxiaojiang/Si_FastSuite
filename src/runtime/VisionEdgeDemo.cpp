#include "olp/runtime/VisionEdgeDemo.h"

#include "olp/runtime/BoardOutlineFit.h"
#include "olp/runtime/BoardPerception.h"

#include <utility>

namespace olp::runtime {
namespace {

VisionEdgeDemoRunReport faultAt(
    VisionEdgeDemoRunReport report,
    std::string reason) {
    report.stateReached = VisionEdgeDemoState::Fault;
    report.faultReason = std::move(reason);
    report.executedMock = false;
    report.mockDemoPassed = false;
    return report;
}

std::string programNameForPlacement(const std::string& placementId) {
    if (placementId == "placement_a") {
        return "EDGE_ROLL_A";
    }
    if (placementId == "placement_b") {
        return "EDGE_ROLL_B";
    }
    // FANUC-safe short fallback
    return "EDGE_ROLL";
}

} // namespace

const char* toString(VisionEdgeDemoState s) noexcept {
    switch (s) {
    case VisionEdgeDemoState::Idle: return "Idle";
    case VisionEdgeDemoState::Capture: return "Capture";
    case VisionEdgeDemoState::Perceive: return "Perceive";
    case VisionEdgeDemoState::Fit: return "Fit";
    case VisionEdgeDemoState::Plan: return "Plan";
    case VisionEdgeDemoState::Validate: return "Validate";
    case VisionEdgeDemoState::Execute: return "Execute";
    case VisionEdgeDemoState::Done: return "Done";
    case VisionEdgeDemoState::Fault: return "Fault";
    }
    return "Unknown";
}

DeviceResult<VisionEdgeDemoRunReport> runVisionEdgeDemoOnce(
    const std::string& placementId,
    VisionEdgeDemoDeps& deps) {
    VisionEdgeDemoRunReport report;
    report.placementId = placementId;
    report.stateReached = VisionEdgeDemoState::Idle;

    if (placementId.empty()) {
        return DeviceResult<VisionEdgeDemoRunReport>::success(
            faultAt(report, "empty placementId"));
    }

    // Capture
    report.stateReached = VisionEdgeDemoState::Capture;
    if (!deps.camera.isConnected()) {
        const auto st = deps.camera.connect();
        if (!st.ok()) {
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, "camera connect failed: " + st.message));
        }
    }
    {
        const auto sel = deps.camera.selectPlacement(placementId);
        if (!sel.ok()) {
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, "selectPlacement failed: " + sel.message));
        }
        const auto cap = deps.camera.captureContour();
        if (!cap.ok()) {
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, "captureContour failed: " + cap.status.message));
        }

        // Perceive
        report.stateReached = VisionEdgeDemoState::Perceive;
        const auto perc = perceiveFromContour(cap.value, deps.calib);
        if (!perc.ok()) {
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, "perceive failed: " + perc.status.message));
        }

        // Fit
        report.stateReached = VisionEdgeDemoState::Fit;
        const auto fit = fitBoardOutline(perc.value);
        if (!fit.ok()) {
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, "fit failed: " + fit.status.message));
        }

        // Plan
        report.stateReached = VisionEdgeDemoState::Plan;
        const auto path = planEdgeRollPath(fit.value, deps.pathParams);
        if (!path.ok()) {
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, "plan failed: " + path.status.message));
        }

        // Validate (+ gated Mock execute inside validate when gate on)
        report.stateReached = VisionEdgeDemoState::Validate;
        EdgeRollValidateOptions opt;
        opt.programName = programNameForPlacement(placementId);
        opt.runMock = true;
        opt.requireIkPass = false;
        opt.gateMockOnIkAndLsPass = true; // closes VE-D6-001 on one-click path
        opt.roller = deps.roller;
        opt.roller.boundToFcl = false;

        const auto val = validateExportAndMockEdgeRoll(
            path.value,
            deps.robotModel,
            deps.robot,
            deps.calib,
            opt,
            /*transientBoardMeshEmpty=*/true);
        if (!val.ok()) {
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, "validate DeviceResult failed: " + val.status.message));
        }
        report.validation = val.value;
        report.hasValidation = true;

        // Hard gate: ik or ls failure => Fault, no execution credit
        if (report.validation.ik != IkReportStatus::Passed ||
            report.validation.lsExport != StepReportStatus::Passed) {
            report.executedMock = false;
            report.mockDemoPassed = false;
            std::string why = "safety gate blocked execute: ";
            if (report.validation.ik != IkReportStatus::Passed) {
                why += "ik!=Passed; ";
            }
            if (report.validation.lsExport != StepReportStatus::Passed) {
                why += "lsExport!=Passed; ";
            }
            why += report.validation.ikFirstMessage;
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, why));
        }

        // Execute stage: Mock already performed under gate inside validate.
        report.stateReached = VisionEdgeDemoState::Execute;
        const bool mockOk =
            report.validation.mockUpload == StepReportStatus::Passed &&
            report.validation.mockExecute == StepReportStatus::Passed;
        if (!mockOk) {
            return DeviceResult<VisionEdgeDemoRunReport>::success(
                faultAt(report, "Mock upload/execute did not Pass under safety gate"));
        }
        report.executedMock = true;

        // Done -- Mock demo only; never site-safe
        report.stateReached = VisionEdgeDemoState::Done;
        report.mockDemoPassed = true;
        report.faultReason.clear();
        if (report.validation.claimsSiteSafe) {
            // Defensive: strip any accidental claim
            report.validation.claimsSiteSafe = false;
        }
        return DeviceResult<VisionEdgeDemoRunReport>::success(std::move(report));
    }
}

DeviceResult<VisionEdgeDemoABReport> runVisionEdgeDemoAB(VisionEdgeDemoDeps& deps) {
    VisionEdgeDemoABReport out;
    auto a = runVisionEdgeDemoOnce("placement_a", deps);
    if (!a.ok()) {
        return DeviceResult<VisionEdgeDemoABReport>::failure(
            a.status.code, a.status.message);
    }
    out.a = std::move(a.value);

    auto b = runVisionEdgeDemoOnce("placement_b", deps);
    if (!b.ok()) {
        return DeviceResult<VisionEdgeDemoABReport>::failure(
            b.status.code, b.status.message);
    }
    out.b = std::move(b.value);
    out.bothDone =
        out.a.stateReached == VisionEdgeDemoState::Done &&
        out.b.stateReached == VisionEdgeDemoState::Done &&
        out.a.mockDemoPassed && out.b.mockDemoPassed;
    return DeviceResult<VisionEdgeDemoABReport>::success(std::move(out));
}

} // namespace olp::runtime
