#pragma once

#include "olp/app/CommandManager.h"
#include "olp/cad/ICadKernel.h"
#include "olp/device/RobotModel.h"
#include <string>
#include <vector>

namespace olp::app {

struct SceneRobotReplaceResult {
    bool success{false};
    int totalSolidsImported{0};
    int staticRobotSolidsHidden{0};
    std::string activeRobotNodeId{};
    core::Transform4d detectedRobotBaseTransform{};
};

// M04: Workcell Layout & Section 3.4 Scheme A (Full Scene STEP Import + Robot Replacement)
class WorkcellLayoutService {
public:
    // Section 3.4 Scheme A: Import full workcell STEP (e.g. Full_Welding_Cell_With_Robot.step),
    // automatically identify static robot solids (FANUC_M10iA_Static_*), determine pedestal mounting base,
    // replace with movable library robot (FANUC M-10iA), and hide static robot solids.
    static SceneRobotReplaceResult importFullSceneAndReplaceRobot(
        ProjectDocument& doc,
        cad::ICadKernel& cadKernel,
        const std::string& stepFilePath,
        const device::RobotModel& libraryRobotModel);

    // M04: Define or update UFRAME by 3 picked points (Origin, +X direction point, XY plane point)
    static core::SceneNode& defineUFrameByThreePoints(
        ProjectDocument& doc,
        int uframeNumber,
        const std::string& frameName,
        const core::Vec3& originWorld,
        const core::Vec3& ptXWorld,
        const core::Vec3& ptXYWorld);
};

} // namespace olp::app
