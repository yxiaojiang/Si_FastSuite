#include "olp/io/HtmlAnimationExporter.h"
#include "HtmlAnimationDataPrep.h"
#include "olp/io/Utf8Path.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <vector>

namespace olp::io {

bool HtmlAnimationExporter::exportSingleFileHtml(
    const std::string& outputFilePath,
    const core::Program& program,
    const std::vector<core::SceneNode>& sceneNodes,
    const algo::SimulationResult& simResult,
    const device::RobotModel* robotModel,
    const HtmlExportOptions& options,
    std::string* errorOut) {
    if (outputFilePath.empty()) {
        if (errorOut) *errorOut = "HTML export output path is empty";
        return false;
    }

    const std::filesystem::path fsPath = utf8ToPath(outputFilePath);
    if (fsPath.has_parent_path()) {
        std::error_code ec;
        if (!std::filesystem::exists(fsPath.parent_path(), ec)) {
            std::filesystem::create_directories(fsPath.parent_path(), ec);
        }
    }

    std::ofstream ofs(fsPath, std::ios::binary);
    if (!ofs.is_open()) {
        if (errorOut) *errorOut = "Failed to open output file for HTML export: " + outputFilePath;
        return false;
    }

    const device::RobotModel activeRobot =
        (robotModel != nullptr) ? *robotModel : device::RobotModel::createFanucM10iA();

    core::Transform4d robotBaseInWorld = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    core::Transform4d uframeInWorld = core::Transform4d::identity();
    core::Transform4d utoolOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);

    for (const auto& node : sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            robotBaseInWorld = node.localTransform;
        } else if (node.type == core::SceneNodeType::Frame && node.uframeNumber == program.uframeNumber) {
            uframeInWorld = node.localTransform;
        } else if (node.type == core::SceneNodeType::Tool && node.utoolNumber == program.utoolNumber) {
            utoolOffset = (node.tcpOffset.pos.norm() > 1e-3) ? node.tcpOffset : node.localTransform;
        }
    }

    const auto decimatedFrames = normalizeAndDecimateFrames(
        simResult.frames, robotBaseInWorld, utoolOffset, activeRobot, options);

    const core::Transform4d flangeToTool0 = computeFlangeToTool0(activeRobot);

    const std::string progTitle = program.name.empty() ? "WELD_001" : program.name;
    ofs << std::fixed << std::setprecision(4);
    ofs << R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>SiFastSuite OLP - 3D Offline Animation ()" << escapeHtmlText(progTitle) << R"()</title>
<style>
  * { box-sizing: border-box; user-select: none; }
  body { margin: 0; background: #0f172a; color: #f1f5f9; font-family: "Microsoft YaHei", "Segoe UI", sans-serif; overflow: hidden; }
  #viewport { display: block; width: 100vw; height: 100vh; cursor: grab; }
  #viewport:active { cursor: grabbing; }
  #hud {
    position: absolute; top: 14px; left: 14px; z-index: 10;
    background: rgba(15, 23, 42, 0.88); backdrop-filter: blur(6px);
    padding: 14px 18px; border-radius: 10px; border: 1px solid #334155;
    box-shadow: 0 8px 24px rgba(0,0,0,0.45); min-width: 360px; max-width: 520px;
  }
  #hud h3 { margin: 0 0 6px 0; font-size: 16px; color: #38bdf8; display: flex; justify-content: space-between; align-items: center; }
  .badge { font-size: 11px; padding: 2px 8px; border-radius: 4px; background: #1e293b; color: #94a3b8; border: 1px solid #475569; }
  .badge-arc-on { background: rgba(239, 68, 68, 0.25); color: #fca5a5; border-color: #ef4444; font-weight: bold; }
  .badge-arc-off { background: rgba(59, 130, 246, 0.2); color: #93c5fd; border-color: #3b82f6; }
  .hud-row { font-size: 13px; color: #cbd5e1; margin: 4px 0; }
  #frameInfo {
    margin-top: 8px; padding-top: 8px; border-top: 1px solid #334155;
    font-family: Consolas, "Courier New", monospace; font-size: 12.5px; line-height: 1.5; color: #e2e8f0;
  }
  #legend {
    position: absolute; top: 14px; right: 14px; z-index: 10;
    background: rgba(15, 23, 42, 0.85); padding: 10px 14px; border-radius: 8px;
    border: 1px solid #334155; font-size: 12px; color: #cbd5e1; line-height: 1.7;
  }
  .dot { display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin-right: 6px; vertical-align: middle; }
  #controls {
    position: absolute; bottom: 18px; left: 50%; transform: translateX(-50%); z-index: 10;
    background: rgba(15, 23, 42, 0.92); backdrop-filter: blur(6px);
    padding: 10px 16px; border-radius: 10px; border: 1px solid #334155;
    display: flex; flex-wrap: wrap; gap: 10px; align-items: center; width: min(94vw, 1120px);
    box-shadow: 0 8px 24px rgba(0,0,0,0.5);
  }
  button, select {
    background: #1e293b; color: #f8fafc; border: 1px solid #475569;
    padding: 6px 12px; border-radius: 6px; cursor: pointer; font-size: 12.5px; font-family: inherit;
  }
  button:hover, select:hover { background: #334155; border-color: #64748b; }
  button.primary { background: #2563eb; border-color: #3b82f6; font-weight: 600; }
  button.primary:hover { background: #1d4ed8; }
  input[type=range] { flex: 1; min-width: 180px; accent-color: #38bdf8; cursor: pointer; }
  label.toggle { display: inline-flex; align-items: center; gap: 5px; font-size: 12.5px; color: #cbd5e1; cursor: pointer; }
  #timeLabel { font-family: Consolas, monospace; font-size: 13px; min-width: 115px; text-align: right; color: #38bdf8; }
</style>
</head>
<body>
<div id="hud">
  <h3>
    <span>SiFastSuite OLP 3D 离线仿真 — )" << escapeHtmlText(progTitle) << R"(</span>
    <span id="arcBadge" class="badge badge-arc-off">Arc OFF</span>
  </h3>
  <div class="hud-row">机器人: <b>)" << escapeHtmlText(activeRobot.displayName) << R"(</b></div>
  <div class="hud-row">总节拍估算: <b>)" << simResult.totalCycleTimeSec << R"( s</b> (焊接: <span style="color:#f87171;font-weight:bold;">)"
        << simResult.arcWeldTimeSec << R"( s</span> / 空走: <span style="color:#60a5fa;font-weight:bold;">)"
        << simResult.airCutTimeSec << R"( s</span>) | 关键帧: <b>)"
        << decimatedFrames.size() << "/" << simResult.frames.size() << R"(</b></div>
  <div id="frameInfo">正在初始化三维场景...</div>
</div>
<div id="legend">
  <div><b>三维视图交互 (完全离线)</b></div>
  <div>• 鼠标左键拖拽：转盘旋转 (Turntable Orbit)</div>
  <div>• 鼠标右键/中键：平移视角 (Pan)</div>
  <div>• 鼠标滚轮：缩放 (Zoom) | 双击画布：视角复位</div>
  <div style="margin-top:4px;border-top:1px solid #334155;padding-top:4px;">
    <span class="dot" style="background:#ef4444;"></span>焊接轨迹 (Arc ON)
    <span class="dot" style="background:#3b82f6;margin-left:8px;"></span>空走轨迹 (Arc OFF)<br/>
    <span class="dot" style="background:#eab308;"></span>机器人连杆 (7轴网格)
    <span class="dot" style="background:#3b82f6;margin-left:8px;"></span>工件
    <span class="dot" style="background:#d97706;margin-left:8px;"></span>夹具
  </div>
</div>
<div id="controls">
  <button id="btnPlay" class="primary">播放 / 暂停</button>
  <button id="btnStop">停止</button>
  <select id="speedSelect" title="回放倍速切换">
    <option value="0.5">0.5x 慢放</option>
    <option value="1.0" selected>1.0x 原速</option>
    <option value="2.0">2.0x 倍速</option>
    <option value="4.0">4.0x 快进</option>
  </select>
  <select id="opJumpSelect" title="按操作/焊缝起点跳转">
    <option value="-1">跳转焊缝/操作起点...</option>
  </select>
  <input id="slider" type="range" min="0" max=")" << (decimatedFrames.empty() ? 0 : decimatedFrames.size() - 1) << R"(" step="1" value="0" />
  <span id="timeLabel">0.00 / )" << simResult.totalCycleTimeSec << R"( s</span>
  <label class="toggle"><input type="checkbox" id="chkWaypoints" checked />路径点序号</label>
  <label class="toggle"><input type="checkbox" id="chkTransparent" />工位半透明</label>
  <button id="btnResetCam">视角复位</button>
</div>
<canvas id="viewport"></canvas>
<script>
const workcellMeta = {
  programName: ")" << escapeJson(progTitle) << R"(",
  robotName: ")" << escapeJson(activeRobot.displayName) << R"(",
  totalCycleTimeSec: )" << simResult.totalCycleTimeSec << R"(,
  arcWeldTimeSec: )" << simResult.arcWeldTimeSec << R"(,
  airCutTimeSec: )" << simResult.airCutTimeSec << R"(,
  rawFrameCount: )" << simResult.frames.size() << R"(,
  keyframeCount: )" << decimatedFrames.size() << R"(,
  robotBaseTransform: )";
    writeTransform12Json(ofs, robotBaseInWorld);
    ofs << ",\n  uframeTransform: ";
    writeTransform12Json(ofs, uframeInWorld);
    ofs << ",\n  utoolTransform: ";
    writeTransform12Json(ofs, utoolOffset);
    ofs << ",\n  flangeToTool0Transform: ";
    writeTransform12Json(ofs, flangeToTool0);
    ofs << "\n};\n";

    // 1. Export 3D Scene Part Meshes (Workpiece / Fixture / Environment)
    ofs << "const sceneParts = [\n";
    for (const auto& node : sceneNodes) {
        if (!node.visible || node.type != core::SceneNodeType::Part) continue;
        if (node.collisionRole == core::CollisionRole::Ignored) continue;
        if (node.collisionRole == core::CollisionRole::Environment &&
            (node.name.rfind("FANUC_M10iA_Static", 0) == 0 || node.name.rfind("Robot_", 0) == 0)) {
            continue;
        }

        const core::TriMesh* srcMesh = nullptr;
        core::TriMesh fallbackBox;
        if (!node.renderMesh.vertices.empty() && !node.renderMesh.triangles.empty()) {
            srcMesh = &node.renderMesh;
        } else if (!node.collisionMesh.vertices.empty() && !node.collisionMesh.triangles.empty()) {
            srcMesh = &node.collisionMesh;
        } else {
            const core::Vec3& lo = (node.renderMesh.bboxMax - node.renderMesh.bboxMin).norm() > 1e-3
                ? node.renderMesh.bboxMin : node.collisionMesh.bboxMin;
            const core::Vec3& hi = (node.renderMesh.bboxMax - node.renderMesh.bboxMin).norm() > 1e-3
                ? node.renderMesh.bboxMax : node.collisionMesh.bboxMax;
            fallbackBox = makeBoxTriMesh(lo, hi);
            srcMesh = &fallbackBox;
        }

        const CompactMeshData cm = compactAndDecimateMesh(
            *srcMesh, node.localTransform, options.maxTrianglesPerPartMesh);
        if (cm.vertices.empty() || cm.triangles.empty()) continue;

        std::string colorHex = "#64748b";
        double opacity = 0.72;
        if (node.collisionRole == core::CollisionRole::Workpiece) {
            colorHex = "#3b82f6";
            opacity = 0.94;
        } else if (node.collisionRole == core::CollisionRole::Fixture) {
            colorHex = "#d97706";
            opacity = 0.88;
        }

        ofs << "  { id:\"" << escapeJson(node.id)
            << "\", name:\"" << escapeJson(node.name)
            << "\", role:\"" << roleNameString(node.collisionRole)
            << "\", color:\"" << colorHex
            << "\", opacity:" << opacity
            << ", min:[" << cm.bboxMin.x << "," << cm.bboxMin.y << "," << cm.bboxMin.z << "]"
            << ", max:[" << cm.bboxMax.x << "," << cm.bboxMax.y << "," << cm.bboxMax.z << "], ";
        writeMeshArraysJson(ofs, cm);
        ofs << " },\n";
    }
    ofs << "];\n";

    // 2. Export 7 Robot Link Meshes (base_link + link_1..link_6)
    ofs << "const robotLinkMeshes = [\n";
    static const std::array<const char*, 7> kLinkNames{
        "base_link", "link_1", "link_2", "link_3", "link_4", "link_5", "link_6"
    };
    static const std::array<const char*, 7> kLinkColors{
        "#334155", "#eab308", "#facc15", "#eab308", "#facc15", "#eab308", "#94a3b8"
    };
    for (std::size_t k = 0; k < 7; ++k) {
        core::TriMesh linkMesh;
        bool loaded = false;
        if (options.includeRobotLinkMeshes) {
            loaded = loadBinaryRobotStlMm(activeRobot.visualMeshFiles[k], linkMesh);
            if (!loaded) loaded = loadBinaryRobotStlMm(activeRobot.collisionMeshFiles[k], linkMesh);
        }
        if (!loaded || linkMesh.triangles.empty()) {
            linkMesh = makeFallbackRobotLinkMesh(k);
        }
        const CompactMeshData cm = compactAndDecimateMesh(
            linkMesh, core::Transform4d::identity(), options.maxTrianglesPerRobotLink);
        ofs << "  { linkIndex:" << k
            << ", name:\"" << kLinkNames[k]
            << "\", color:\"" << kLinkColors[k] << "\", ";
        writeMeshArraysJson(ofs, cm);
        ofs << " },\n";
    }
    ofs << "];\n";

    // 3. Export Welding Torch 3D Mesh in link_6 space (mounted on flange)
    core::Vec3 torchTipInLink6{0.0, 0.0, 0.0};
    const core::TriMesh rawTorch = makeWeldingTorchMeshInLink6(utoolOffset, flangeToTool0, &torchTipInLink6);
    const CompactMeshData compactTorch = compactAndDecimateMesh(rawTorch, core::Transform4d::identity(), 256);
    ofs << "const torchMesh = { name:\"Welding_Torch\", color:\"#cbd5e1\", tcpLocal:["
        << torchTipInLink6.x << "," << torchTipInLink6.y << "," << torchTipInLink6.z << "], ";
    writeMeshArraysJson(ofs, compactTorch);
    ofs << " };\n";

    // 4. Export Waypoints and Operations
    std::vector<OperationExportRecord> opRecords;
    std::vector<WaypointExportRecord> wpRecords;
    extractOperationsAndWaypoints(program, uframeInWorld, decimatedFrames, opRecords, wpRecords);

    ofs << "const operations = [\n";
    for (const auto& op : opRecords) {
        ofs << "  { index:" << op.index
            << ", id:\"" << escapeJson(op.id)
            << "\", name:\"" << escapeJson(op.name)
            << "\", seamId:\"" << escapeJson(op.seamId)
            << "\", startFrameIdx:" << op.startFrameIdx
            << ", weldStartFrameIdx:" << op.weldStartFrameIdx
            << ", startTimeSec:" << op.startTimeSec
            << ", weldStartTimeSec:" << op.weldStartTimeSec << " },\n";
    }
    ofs << "];\n";

    ofs << "const waypoints = [\n";
    for (const auto& wp : wpRecords) {
        ofs << "  { idx:" << wp.index
            << ", id:\"" << escapeJson(wp.id)
            << "\", name:\"" << escapeJson(wp.name)
            << "\", opName:\"" << escapeJson(wp.opName)
            << "\", role:\"" << wp.role
            << "\", isWeld:" << (wp.isWeld ? "true" : "false")
            << ", frameIdx:" << wp.frameIdx
            << ", pos:[" << wp.worldPos.x << "," << wp.worldPos.y << "," << wp.worldPos.z << "]"
            << ", dirZ:[" << wp.dirZ.x << "," << wp.dirZ.y << "," << wp.dirZ.z << "] },\n";
    }
    ofs << "];\n";

    // 5. Export Decimated 3D Baked Frames
    ofs << "const bakedFrames = [\n";
    for (const auto& f : decimatedFrames) {
        ofs << "  { t: " << f.timeSec
            << ", tcp: [" << f.tcpWorldPos.x << "," << f.tcpWorldPos.y << "," << f.tcpWorldPos.z << "]"
            << ", arc: " << (f.arcActive ? "true" : "false")
            << ", jDeg: ["
            << core::rad2deg(f.jointsRad[0]) << "," << core::rad2deg(f.jointsRad[1]) << ","
            << core::rad2deg(f.jointsRad[2]) << "," << core::rad2deg(f.jointsRad[3]) << ","
            << core::rad2deg(f.jointsRad[4]) << "," << core::rad2deg(f.jointsRad[5]) << "]"
            << ", linkWorldTransforms: [";
        for (std::size_t k = 0; k < 7; ++k) {
            if (k > 0) ofs << ",";
            writeTransform12Json(ofs, f.linkWorldTransforms[k]);
        }
        ofs << "] },\n";
    }

    // Split embedded JS into compact chunks (< 6 KB each) to respect MSVC C2026 limit
    ofs << R"JS1(];
const canvas = document.getElementById('viewport'), ctx = canvas.getContext('2d');
const slider = document.getElementById('slider'), timeLabel = document.getElementById('timeLabel');
const frameInfo = document.getElementById('frameInfo'), arcBadge = document.getElementById('arcBadge');
const speedSelect = document.getElementById('speedSelect'), opJumpSelect = document.getElementById('opJumpSelect');
const chkWaypoints = document.getElementById('chkWaypoints'), chkTransparent = document.getElementById('chkTransparent');
let curIdx = 0, playing = true, playbackSpeed = 1.0;
let simTimeSec = bakedFrames.length > 0 ? bakedFrames[0].t : 0.0, lastWallMs = performance.now();

operations.forEach((op, idx) => {
  const optStart = document.createElement('option');
  optStart.value = String(op.startFrameIdx);
  optStart.textContent = `[操作 ${idx + 1}] ${op.name} - 起点 (${op.startTimeSec.toFixed(2)}s)`;
  opJumpSelect.appendChild(optStart);
  if (op.weldStartFrameIdx !== op.startFrameIdx) {
    const optWeld = document.createElement('option');
    optWeld.value = String(op.weldStartFrameIdx);
    optWeld.textContent = `  ↳ ${op.name} - 起弧点 WELD_START (${op.weldStartTimeSec.toFixed(2)}s)`;
    opJumpSelect.appendChild(optWeld);
  }
});

const camera = { yaw: -0.82, pitch: 0.48, distance: 2400.0, target: [400.0, -100.0, 650.0], fovRad: 42.0 * Math.PI / 180.0 };
function computeSceneBounds() {
  let minX = 1e9, minY = 1e9, minZ = 1e9, maxX = -1e9, maxY = -1e9, maxZ = -1e9;
  const includePt = (x, y, z) => {
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (y < minY) minY = y; if (y > maxY) maxY = y;
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
  };
  sceneParts.forEach(p => { includePt(p.min[0], p.min[1], p.min[2]); includePt(p.max[0], p.max[1], p.max[2]); });
  bakedFrames.forEach(f => includePt(f.tcp[0], f.tcp[1], f.tcp[2]));
  if (minX > maxX) { minX = -500; maxX = 1000; minY = -800; maxY = 800; minZ = 0; maxZ = 1400; }
  return { center: [(minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5], radius: Math.max(400.0, Math.hypot(maxX - minX, maxY - minY, maxZ - minZ) * 0.55) };
}
const sceneBounds = computeSceneBounds();

function resetCamera() {
  camera.yaw = -0.82; camera.pitch = 0.48;
  camera.target = sceneBounds.center.slice();
  camera.distance = Math.max(900.0, sceneBounds.radius * 2.25);
  render();
}
function resize() { canvas.width = window.innerWidth; canvas.height = window.innerHeight; render(); }
window.addEventListener('resize', resize);

let dragMode = null, lastMouse = [0, 0];
canvas.addEventListener('mousedown', (e) => {
  lastMouse = [e.clientX, e.clientY];
  if (e.button === 0) dragMode = 'orbit';
  else if (e.button === 1 || e.button === 2) dragMode = 'pan';
});
window.addEventListener('mousemove', (e) => {
  if (!dragMode) return;
  const dx = e.clientX - lastMouse[0], dy = e.clientY - lastMouse[1];
  lastMouse = [e.clientX, e.clientY];
  if (dragMode === 'orbit') {
    camera.yaw -= dx * 0.0065;
    camera.pitch = Math.max(-1.48, Math.min(1.48, camera.pitch + dy * 0.0065));
  } else if (dragMode === 'pan') {
    const panScale = camera.distance * 0.0011;
    const rightX = -Math.sin(camera.yaw), rightY = Math.cos(camera.yaw);
    const upX = -Math.sin(camera.pitch) * Math.cos(camera.yaw), upY = -Math.sin(camera.pitch) * Math.sin(camera.yaw), upZ = Math.cos(camera.pitch);
    camera.target[0] -= (dx * rightX - dy * upX) * panScale;
    camera.target[1] -= (dx * rightY - dy * upY) * panScale;
    camera.target[2] -= (-dy * upZ) * panScale;
  }
  if (!playing) render();
});
window.addEventListener('mouseup', () => { dragMode = null; });
canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('dblclick', resetCamera);
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  const factor = e.deltaY > 0 ? 1.12 : (1.0 / 1.12);
  camera.distance = Math.max(200.0, Math.min(15000.0, camera.distance * factor));
  if (!playing) render();
}, { passive: false });
)JS1";

    ofs << R"JS2(
document.getElementById('btnPlay').onclick = () => {
  playing = !playing;
  if (playing && bakedFrames.length > 0 && curIdx >= bakedFrames.length - 1) { curIdx = 0; simTimeSec = bakedFrames[0].t; }
  lastWallMs = performance.now(); render();
};
document.getElementById('btnStop').onclick = () => {
  playing = false; curIdx = 0; simTimeSec = bakedFrames.length > 0 ? bakedFrames[0].t : 0.0; slider.value = 0; render();
};
speedSelect.onchange = (e) => { playbackSpeed = parseFloat(e.target.value) || 1.0; };
opJumpSelect.onchange = (e) => {
  const targetFrame = parseInt(e.target.value, 10);
  if (targetFrame >= 0 && targetFrame < bakedFrames.length) { curIdx = targetFrame; simTimeSec = bakedFrames[curIdx].t; slider.value = curIdx; render(); }
  opJumpSelect.value = "-1";
};
slider.oninput = (e) => {
  playing = false; curIdx = parseInt(e.target.value, 10) || 0;
  if (bakedFrames.length > 0) simTimeSec = bakedFrames[curIdx].t;
  render();
};
chkWaypoints.onchange = () => render();
chkTransparent.onchange = () => render();
document.getElementById('btnResetCam').onclick = resetCamera;

function parseHexRgb(hex) { const v = parseInt(hex.replace('#', ''), 16); return [(v >> 16) & 255, (v >> 8) & 255, v & 255]; }
sceneParts.forEach(p => { p.rgb = parseHexRgb(p.color); });
robotLinkMeshes.forEach(m => { m.rgb = parseHexRgb(m.color); });
torchMesh.rgb = parseHexRgb(torchMesh.color);

function transformPoint12(tf, x, y, z) {
  return [tf[0]*x + tf[1]*y + tf[2]*z + tf[3], tf[4]*x + tf[5]*y + tf[6]*z + tf[7], tf[8]*x + tf[9]*y + tf[10]*z + tf[11]];
}
function buildCameraBasis() {
  const cp = Math.cos(camera.pitch), sp = Math.sin(camera.pitch), cy = Math.cos(camera.yaw), sy = Math.sin(camera.yaw);
  const fwd = [-cp * cy, -cp * sy, -sp];
  const eye = [camera.target[0] - fwd[0] * camera.distance, camera.target[1] - fwd[1] * camera.distance, camera.target[2] - fwd[2] * camera.distance];
  const right = [-sy, cy, 0.0], up = [-sp * cy, -sp * sy, cp];
  const focal = (canvas.height * 0.5) / Math.tan(camera.fovRad * 0.5);
  return { eye, fwd, right, up, focal, cx: canvas.width * 0.5, cy: canvas.height * 0.5 };
}
function projectPoint(cam, pt) {
  const dx = pt[0] - cam.eye[0], dy = pt[1] - cam.eye[1], dz = pt[2] - cam.eye[2];
  const zCam = dx * cam.fwd[0] + dy * cam.fwd[1] + dz * cam.fwd[2];
  if (zCam <= 20.0) return null;
  const xCam = dx * cam.right[0] + dy * cam.right[1] + dz * cam.right[2];
  const yCam = dx * cam.up[0] + dy * cam.up[1] + dz * cam.up[2];
  const invZ = cam.focal / zCam;
  return [cam.cx + xCam * invZ, cam.cy - yCam * invZ, zCam];
}
function appendMeshTriangles(drawList, cam, verts, indices, tf12, rgb, alpha, cullBackface) {
  const nVerts = (verts.length / 3) | 0, proj = new Array(nVerts), worldPts = new Array(nVerts);
  for (let i = 0; i < nVerts; ++i) {
    const k = i * 3;
    const wp = tf12 ? transformPoint12(tf12, verts[k], verts[k+1], verts[k+2]) : [verts[k], verts[k+1], verts[k+2]];
    worldPts[i] = wp; proj[i] = projectPoint(cam, wp);
  }
  const lightDir = [0.45, -0.38, 0.81];
  for (let t = 0; t < indices.length; t += 3) {
    const i0 = indices[t], i1 = indices[t+1], i2 = indices[t+2];
    const p0 = proj[i0], p1 = proj[i1], p2 = proj[i2];
    if (!p0 || !p1 || !p2) continue;
    const area2 = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0]);
    if (cullBackface && area2 >= 0) continue;
    const w0 = worldPts[i0], w1 = worldPts[i1], w2 = worldPts[i2];
    const ux = w1[0] - w0[0], uy = w1[1] - w0[1], uz = w1[2] - w0[2];
    const vx = w2[0] - w0[0], vy = w2[1] - w0[1], vz = w2[2] - w0[2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const nLen = Math.hypot(nx, ny, nz) || 1.0;
    const ndotl = Math.abs((nx * lightDir[0] + ny * lightDir[1] + nz * lightDir[2]) / nLen);
    const shade = 0.38 + 0.62 * ndotl;
    const r = Math.min(255, (rgb[0] * shade) | 0), g = Math.min(255, (rgb[1] * shade) | 0), b = Math.min(255, (rgb[2] * shade) | 0);
    drawList.push({ z: (p0[2] + p1[2] + p2[2]) * 0.333333, x0: p0[0], y0: p0[1], x1: p1[0], y1: p1[1], x2: p2[0], y2: p2[1], fill: `rgba(${r},${g},${b},${alpha})` });
  }
}
function draw3DLine(cam, a, b, strokeStyle, lineWidth) {
  const pa = projectPoint(cam, a), pb = projectPoint(cam, b);
  if (!pa || !pb) return;
  ctx.strokeStyle = strokeStyle; ctx.lineWidth = lineWidth;
  ctx.beginPath(); ctx.moveTo(pa[0], pa[1]); ctx.lineTo(pb[0], pb[1]); ctx.stroke();
}
function drawCoordinateTriad(cam, tf12, scaleMm, label) {
  const origin = tf12 ? [tf12[3], tf12[7], tf12[11]] : [0, 0, 0];
  draw3DLine(cam, origin, tf12 ? transformPoint12(tf12, scaleMm, 0, 0) : [scaleMm, 0, 0], '#ef4444', 2.2);
  draw3DLine(cam, origin, tf12 ? transformPoint12(tf12, 0, scaleMm, 0) : [0, scaleMm, 0], '#22c55e', 2.2);
  draw3DLine(cam, origin, tf12 ? transformPoint12(tf12, 0, 0, scaleMm) : [0, 0, scaleMm], '#3b82f6', 2.2);
  if (label) {
    const po = projectPoint(cam, origin);
    if (po) { ctx.fillStyle = '#94a3b8'; ctx.font = '11px Consolas, sans-serif'; ctx.fillText(label, po[0] + 6, po[1] - 4); }
  }
}
)JS2";

    ofs << R"JS3(
function render() {
  if (!canvas.width || !canvas.height) return;
  const grad = ctx.createLinearGradient(0, 0, 0, canvas.height);
  grad.addColorStop(0, '#0f172a'); grad.addColorStop(1, '#1e293b');
  ctx.fillStyle = grad; ctx.fillRect(0, 0, canvas.width, canvas.height);

  const cam = buildCameraBasis();
  const gridStep = 200, gridHalf = 1400;
  const cxGrid = Math.round(sceneBounds.center[0] / gridStep) * gridStep;
  const cyGrid = Math.round(sceneBounds.center[1] / gridStep) * gridStep;
  for (let g = -gridHalf; g <= gridHalf; g += gridStep) {
    draw3DLine(cam, [cxGrid - gridHalf, cyGrid + g, 0], [cxGrid + gridHalf, cyGrid + g, 0], 'rgba(148,163,184,0.14)', 1.0);
    draw3DLine(cam, [cxGrid + g, cyGrid - gridHalf, 0], [cxGrid + g, cyGrid + gridHalf, 0], 'rgba(148,163,184,0.14)', 1.0);
  }
  drawCoordinateTriad(cam, null, 180, 'WORLD');
  drawCoordinateTriad(cam, workcellMeta.uframeTransform, 140, 'UFRAME');

  const drawList = [];
  const forceTrans = chkTransparent.checked;
  for (let i = 0; i < sceneParts.length; ++i) {
    const part = sceneParts[i];
    appendMeshTriangles(drawList, cam, part.vertices, part.indices, null, part.rgb, forceTrans ? Math.min(0.42, part.opacity) : part.opacity, !forceTrans);
  }

  const hasFrames = bakedFrames.length > 0;
  const f = hasFrames ? bakedFrames[curIdx] : null;
  if (f && f.linkWorldTransforms && f.linkWorldTransforms.length === 7) {
    for (let k = 0; k < 7; ++k) {
      appendMeshTriangles(drawList, cam, robotLinkMeshes[k].vertices, robotLinkMeshes[k].indices, f.linkWorldTransforms[k], robotLinkMeshes[k].rgb, 0.96, false);
    }
    appendMeshTriangles(drawList, cam, torchMesh.vertices, torchMesh.indices, f.linkWorldTransforms[6], torchMesh.rgb, 0.98, false);
  }

  drawList.sort((a, b) => b.z - a.z);
  ctx.strokeStyle = 'rgba(15,23,42,0.18)'; ctx.lineWidth = 0.5;
  for (let i = 0; i < drawList.length; ++i) {
    const tri = drawList[i];
    ctx.fillStyle = tri.fill;
    ctx.beginPath(); ctx.moveTo(tri.x0, tri.y0); ctx.lineTo(tri.x1, tri.y1); ctx.lineTo(tri.x2, tri.y2); ctx.closePath();
    ctx.fill(); ctx.stroke();
  }

  if (f && f.linkWorldTransforms && f.linkWorldTransforms.length === 7) {
    for (let k = 1; k < 7; ++k) {
      const pA = [f.linkWorldTransforms[k-1][3], f.linkWorldTransforms[k-1][7], f.linkWorldTransforms[k-1][11]];
      const pB = [f.linkWorldTransforms[k][3], f.linkWorldTransforms[k][7], f.linkWorldTransforms[k][11]];
      draw3DLine(cam, pA, pB, 'rgba(250,204,21,0.55)', 2.0);
    }
    const flangeOrigin = [f.linkWorldTransforms[6][3], f.linkWorldTransforms[6][7], f.linkWorldTransforms[6][11]];
    draw3DLine(cam, flangeOrigin, f.tcp, '#38bdf8', 2.5);
  }

  if (hasFrames) {
    for (let i = 1; i < bakedFrames.length; ++i) {
      const f0 = bakedFrames[i - 1], f1 = bakedFrames[i];
      if (i <= curIdx) draw3DLine(cam, f0.tcp, f1.tcp, f1.arc ? '#ef4444' : '#3b82f6', f1.arc ? 3.6 : 2.0);
      else draw3DLine(cam, f0.tcp, f1.tcp, f1.arc ? 'rgba(239,68,68,0.25)' : 'rgba(59,130,246,0.2)', 1.2);
    }
    const pTcp = projectPoint(cam, f.tcp);
    if (pTcp) {
      if (f.arc) {
        const glow = ctx.createRadialGradient(pTcp[0], pTcp[1], 2, pTcp[0], pTcp[1], 22);
        glow.addColorStop(0, 'rgba(254, 240, 138, 0.95)'); glow.addColorStop(0.45, 'rgba(239, 68, 68, 0.75)'); glow.addColorStop(1, 'rgba(239, 68, 68, 0.0)');
        ctx.fillStyle = glow; ctx.beginPath(); ctx.arc(pTcp[0], pTcp[1], 22, 0, Math.PI * 2); ctx.fill();
      }
      ctx.fillStyle = f.arc ? '#fef08a' : '#10b981';
      ctx.beginPath(); ctx.arc(pTcp[0], pTcp[1], f.arc ? 6.5 : 5.0, 0, Math.PI * 2); ctx.fill();
    }
  }

  if (chkWaypoints.checked) {
    ctx.font = '11px "Microsoft YaHei", Consolas, sans-serif';
    for (let i = 0; i < waypoints.length; ++i) {
      const wp = waypoints[i], pw = projectPoint(cam, wp.pos);
      if (!pw) continue;
      ctx.fillStyle = wp.isWeld ? '#f87171' : '#38bdf8';
      ctx.beginPath(); ctx.arc(pw[0], pw[1], 3.5, 0, Math.PI * 2); ctx.fill();
      const labelText = `P[${wp.idx}] ${wp.name}`, tw = ctx.measureText(labelText).width;
      ctx.fillStyle = 'rgba(15, 23, 42, 0.78)'; ctx.fillRect(pw[0] + 6, pw[1] - 11, tw + 8, 16);
      ctx.fillStyle = wp.isWeld ? '#fca5a5' : '#bae6fd'; ctx.fillText(labelText, pw[0] + 10, pw[1] + 1);
    }
  }

  if (hasFrames) {
    slider.value = curIdx;
    timeLabel.textContent = `${f.t.toFixed(2)} / ${workcellMeta.totalCycleTimeSec.toFixed(2)} s`;
    arcBadge.textContent = f.arc ? 'Arc ON (焊接中)' : 'Arc OFF (空走)';
    arcBadge.className = f.arc ? 'badge badge-arc-on' : 'badge badge-arc-off';
    frameInfo.textContent =
      `TCP: (${f.tcp[0].toFixed(1)}, ${f.tcp[1].toFixed(1)}, ${f.tcp[2].toFixed(1)}) mm | ` +
      `Arc: ${f.arc ? 'ON' : 'OFF'} | ` +
      `J1..J6: [${f.jDeg.map(v => v.toFixed(1)).join(', ')}] deg`;
  } else {
    frameInfo.textContent = '当前程序无烘焙轨迹帧 (0 frames)';
  }
}

function tick(nowMs) {
  const dtSec = Math.min(0.1, Math.max(0.0, (nowMs - lastWallMs) * 0.001));
  lastWallMs = nowMs;
  if (playing && bakedFrames.length > 1) {
    const totalDur = bakedFrames[bakedFrames.length - 1].t;
    simTimeSec += dtSec * playbackSpeed;
    if (simTimeSec > totalDur || totalDur <= 1e-6) { simTimeSec = bakedFrames[0].t; curIdx = 0; }
    else {
      while (curIdx + 1 < bakedFrames.length && bakedFrames[curIdx + 1].t <= simTimeSec) ++curIdx;
      if (bakedFrames[curIdx].t > simTimeSec) curIdx = 0;
    }
    render();
  }
  requestAnimationFrame(tick);
}
resize(); resetCamera(); requestAnimationFrame(tick);
</script>
</body>
</html>
)JS3";
    return ofs.good();
}

} // namespace olp::io
