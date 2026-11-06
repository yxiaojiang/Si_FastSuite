# Plan.md — 当前活跃事项

> 最近刷新：2026-11-06 00:21 Asia/Singapore（UTC+8）。分支 `vision-edge` @ `94d860d` + **未提交** VE-D1~D7 工作区。

## Vision Edge 演示闭环（VE-DEMO-001）

| 阶段 | 状态 |
| :--- | :--- |
| VE-D1 设备 Mock | APPROVED |
| VE-D2 手眼数据模型 | APPROVED |
| VE-D3 简化感知 | APPROVED |
| VE-D4 拟合+瞬态描述符 | APPROVED |
| VE-D5 沿边轨迹（右手姿态） | APPROVED |
| VE-D6 校验+LS+Mock | APPROVED |
| VE-D7 一键编排+A/B | **APPROVED**（211/211；VE-D6-001 一键 CLOSED） |

**本期软件模拟实现：COMPLETE（Mock / headless only）。**

不等于：真机、现场视觉、FCL 板件 Cleared、完整 GUI、RISK-001 关闭。

## 正在做的事

1. **无进行中的 Vision Edge 代码阶段**（模拟闭环已收口）。
2. **[OPEN] `RISK-001`** — 真机/ROBOGUIDE 对拍前禁止现场安全声称。
3. **[QUEUED 实机后续]** 见 SessionBrief「必须实机验证」。
4. **[OPEN P2]** `VE-D1-002/003/004`、`VE-D3-001`、`VE-D5-001`；`VE-D6-001` 仅一键已关。

## 姿态锁定

+X=t，+Z=(0,0,-1)，+Y=Z×X(=n_out)，压板 −Y。

## 门禁命令

```bash
cmake -S . -B build-headless -DOLP_HEADLESS=ON -DOLP_BUILD_TESTS=ON -DOLP_ENABLE_FCL=OFF
cmake --build build-headless --target olp_runtime olp_runtime_tests -j$(nproc)
./build-headless/olp_runtime_tests
# expect: Summary: 211 passed, 0 failed.
```
