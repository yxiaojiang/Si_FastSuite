# 应用命令与场景

1. **会改文档的操作都走 `CommandManager`。** 包括整场景导入替换机器人和三点用户坐标系。命令没改到文档就不入栈。合并只在显式合并会话里发生。撤销深度保持原设计。
2. **方案 A：整场景 STEP 进来后，按名字隐藏静态机器人。** 名字含 `Static_` 或 `FANUC_` 的实体隐藏并不再参与碰撞。底座用 `Static_Base` 的底面，否则用 `Pedestal_Base` 的顶面。可动 M-10iA 放在该点。整场景是十二个实体、藏起五个静态外形，底座偏差小于 1 mm。
3. **三点用户坐标系返回新建或更新的节点 id。** 调用方再用 id 查找，不再假设返回的是节点指针。退化的三点返回空字符串。
4. **焊缝生成操作路径与姿态平滑命令封装（M08）。** `GenerateOperationFromSeamCommand` 从指定焊缝生成或重算焊接操作，在构造阶段预先确立 `operationId`，更新 `seam.programmed = true`，并在撤销时恢复之前的操作快照与 `beforeDirty_` 状态；`SmoothTargetOrientationsCommand` 对选定关键焊接点之间的中间点执行 SLERP 插值，记录 `beforeTargets_` 快照，撤销时完整恢复被插值点状态，确保命令历史栈与文档脏状态完全对称。
