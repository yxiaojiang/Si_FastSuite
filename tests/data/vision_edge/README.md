# Vision Edge Mock Dataset (VE-D1)

Synthetic contour placements for headless Vision_Edge regression without a
real camera or robot.

## Files

| File | Description |
| :--- | :--- |
| `placement_a.json` | Board outline at nominal pose (axis-aligned 200 x 200 mm square). |
| `placement_b.json` | Same board after planar translate + rotate (demo second placement). |
| `placement_a.csv` | CSV mirror of placement A (x_mm,y_mm,z_mm). |
| `placement_b.csv` | CSV mirror of placement B. |
| `calib_demo.json` | Demo Eye-to-Hand extrinsic (`T_cam_base`) + optional `T_flange_tcp`. |

Coordinates are millimeters in the fixed eye-to-hand `camera` frame.
`fakePointCount` is metadata only; VE-D1 does not ship real depth buffers.

## Usage

`MockCamera` loads this directory via `ContourDataset::loadFromDirectory`.
Select `placement_a` or `placement_b`, then call `captureContour()`.

## Scope

Demo Mode 2 (no CAD, capture-and-compute). Not a claim of real-hardware
acceptance. Vacuum / dip-paint / multi-panel fixtures are out of VE-D1 scope.

## Calibration fixture (VE-D2)

`calib_demo.json` is a **synthetic** hand-eye / TCP pose for offline regression.
It is **not** a field 9-point, AX=XB, or ChArUco calibration result and must not be
reported as site acceptance.

## Perception (VE-D3)

Mode 2 demo perception consumes these **synthetic** contours (plus `calib_demo.json`):
capture -> transform to base -> CCW upper-edge ring + centroid/yaw.
This is **not** real 3D vision acceptance (no Open3D/PCL/OpenCV, no depth RANSAC).

## Outline fit / transient workpiece (VE-D4)

`fitBoardOutline` turns a perceived CCW polygon into straight line segments only.
`makeTransientBoardWorkpiece` wraps that into a `SceneNode` (Part / Workpiece,
`cadHandleId=-1`, empty meshes). No OCCT prism is required for the headless gate.
Synthetic fit is **not** industrial contour fitting or real CAD import.

## Edge roll path (VE-D5)

`planEdgeRollPath` builds a base-frame Approach / Contact / Retract dense-L path with
constant offset d = R - delta. Demo defaults only. This is **not** field process
acceptance, and empty transient meshes do **not** mean FCL board collision passed.

## Validate / export / Mock (VE-D6)

`validateExportAndMockEdgeRoll` runs OPW IK, optional honest FCL status, Fanuc `.ls`
text, and Mock upload/execute. Empty transient meshes and builds without FCL report
`NotChecked*` / `NotBuilt` -- never "board cleared". Mock success is **not** site
acceptance; RISK-001 stays OPEN. RollerToolSpec is declared unbound; welding torch
spheres are never used as the roller model on this path.

## One-click demo A/B (VE-D7)

`runVisionEdgeDemoOnce` / `runVisionEdgeDemoAB` run Idle->Capture->...->Done|Fault.
IK or LS failure Faults the run and **blocks all Mock execute** (closes VE-D6-001 on
the one-click path). `mockDemoPassed` means simulated acceptance only -- **not**
site / real-robot acceptance. RISK-001 stays OPEN.
