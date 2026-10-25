#include "olp/io/HtmlAnimationExporter.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace olp::io {

bool HtmlAnimationExporter::exportSingleFileHtml(
    const std::string& outputFilePath,
    const core::Program& program,
    const std::vector<core::SceneNode>& sceneNodes,
    const algo::SimulationResult& simResult) {
    std::ofstream ofs(outputFilePath, std::ios::binary);
    if (!ofs.is_open()) {
        return false;
    }

    ofs << std::fixed << std::setprecision(2);
    ofs << R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8" />
<title>SiFastSuite OLP - 离线 3D 焊接仿真预览 (Program: )" << program.name << R"()</title>
<style>
  body { margin: 0; background: #111827; color: #f3f4f6; font-family: "Microsoft YaHei", sans-serif; overflow: hidden; }
  #hud { position: absolute; top: 16px; left: 16px; background: rgba(17,24,39,0.88); padding: 14px 18px; border-radius: 8px; border: 1px solid #374151; }
  #controls { position: absolute; bottom: 20px; left: 50%; transform: translateX(-50%); background: rgba(17,24,39,0.92); padding: 12px 20px; border-radius: 8px; border: 1px solid #374151; display: flex; gap: 12px; align-items: center; width: 70%; }
  button { background: #2563eb; color: #fff; border: none; padding: 6px 14px; border-radius: 4px; cursor: pointer; }
  input[type=range] { flex: 1; }
</style>
</head>
<body>
<div id="hud">
  <h3 style="margin:0 0 8px 0;">SiFastSuite OLP 离线仿真包 — )" << program.name << R"(</h3>
  <div>总节拍估算: <b>)" << simResult.totalCycleTimeSec << R"( s</b> (焊接: )" << simResult.arcWeldTimeSec
        << R"( s / 空走: )" << simResult.airCutTimeSec << R"( s)</div>
  <div id="frameInfo" style="margin-top:6px; font-family:monospace;">Frame: 0</div>
</div>
<div id="controls">
  <button id="btnPlay">播放 / 暂停</button>
  <input id="slider" type="range" min="0" max=")" << (simResult.frames.empty() ? 0 : simResult.frames.size() - 1) << R"(" value="0" />
  <span id="timeLabel">0.00 s</span>
</div>
<canvas id="viewport"></canvas>
<script>
const sceneParts = [
)";
    for (const auto& node : sceneNodes) {
        if (!node.visible || node.type != core::SceneNodeType::Part) continue;
        ofs << "  { name: \"" << node.name << "\", min: ["
            << node.renderMesh.bboxMin.x << "," << node.renderMesh.bboxMin.y << "," << node.renderMesh.bboxMin.z
            << "], max: ["
            << node.renderMesh.bboxMax.x << "," << node.renderMesh.bboxMax.y << "," << node.renderMesh.bboxMax.z
            << "] },\n";
    }
    ofs << "];\nconst bakedFrames = [\n";
    for (const auto& f : simResult.frames) {
        ofs << "  { t: " << f.timeSec
            << ", tcp: [" << f.tcpWorldPos.x << "," << f.tcpWorldPos.y << "," << f.tcpWorldPos.z << "]"
            << ", arc: " << (f.arcActive ? "true" : "false")
            << ", jDeg: ["
            << core::rad2deg(f.jointsRad[0]) << "," << core::rad2deg(f.jointsRad[1]) << ","
            << core::rad2deg(f.jointsRad[2]) << "," << core::rad2deg(f.jointsRad[3]) << ","
            << core::rad2deg(f.jointsRad[4]) << "," << core::rad2deg(f.jointsRad[5]) << "] },\n";
    }
    ofs << R"(];
const canvas = document.getElementById('viewport');
const ctx = canvas.getContext('2d');
const slider = document.getElementById('slider');
const timeLabel = document.getElementById('timeLabel');
const frameInfo = document.getElementById('frameInfo');
let curIdx = 0, playing = true;
function resize() { canvas.width = window.innerWidth; canvas.height = window.innerHeight; }
window.addEventListener('resize', resize); resize();
document.getElementById('btnPlay').onclick = () => { playing = !playing; };
slider.oninput = (e) => { curIdx = parseInt(e.target.value); playing = false; render(); };
function project(pt) {
  const s = 0.45;
  const cx = canvas.width * 0.5, cy = canvas.height * 0.65;
  return [cx + (pt[0] - pt[1]) * 0.7 * s, cy - (pt[2] + (pt[0] + pt[1]) * 0.35) * s];
}
function render() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (bakedFrames.length === 0) return;
  const f = bakedFrames[curIdx];
  slider.value = curIdx;
  timeLabel.textContent = f.t.toFixed(2) + ' s';
  frameInfo.textContent = `TCP: (${f.tcp[0].toFixed(1)}, ${f.tcp[1].toFixed(1)}, ${f.tcp[2].toFixed(1)}) mm | Arc: ${f.arc ? 'ON' : 'OFF'} | J1..J6: [${f.jDeg.map(v=>v.toFixed(1)).join(', ')}] deg`;
  ctx.lineWidth = 2;
  for (let i = 1; i <= curIdx; ++i) {
    const p0 = project(bakedFrames[i-1].tcp), p1 = project(bakedFrames[i].tcp);
    ctx.strokeStyle = bakedFrames[i].arc ? '#ef4444' : '#3b82f6';
    ctx.beginPath(); ctx.moveTo(p0[0], p0[1]); ctx.lineTo(p1[0], p1[1]); ctx.stroke();
  }
  const tcp2d = project(f.tcp);
  ctx.fillStyle = f.arc ? '#f59e0b' : '#10b981';
  ctx.beginPath(); ctx.arc(tcp2d[0], tcp2d[1], 6, 0, Math.PI * 2); ctx.fill();
}
setInterval(() => {
  if (playing && bakedFrames.length > 0) {
    curIdx = (curIdx + 1) % bakedFrames.length;
    render();
  }
}, 50);
render();
</script>
</body>
</html>
)";
    return ofs.good();
}

} // namespace olp::io
