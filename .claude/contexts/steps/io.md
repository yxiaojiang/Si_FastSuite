# 导出与工程持久化

1. **`.ls` 在逆解缓存无效时拒绝写出。** 配置字用缓存里解出的构型，不用目标上可能过期的配置字。圆弧运动返回错误，不写成直线。回安全点目前是一行注释加关节角度，不是已经核对过的发那科关节位置记录。
2. **HTML 是二维 TCP 折线预览。** 没有 Three.js，也没有 GLB。标题和说明按这个范围写，不把它说成三维场景。
3. **主程序先写文件，再按问题决定退出码。** 导出失败返回 1。可达性、碰撞、超限位或构型跳变返回 2。夹具起弧点修好之后，当前演示返回 0。
4. **工程 JSON 序列化与反序列化 (`ProjectSerializer`)**：
   - 纯 C++ Layer 5 `olp_io` 实现，文件格式标识 `SiFastSuite_Project`，Schema Version 1；
   - 包含场景节点、焊缝拓扑签名及采样帧、控制器与操作点位、焊接工艺条件表 1..32、单位设置；
   - 焊缝局部采样矩阵、示教微调标志 `userModified`、微调增量矩阵 `userDeltaOffset`、工艺事件（`ArcStart` / `WeaveStart` / `ArcEnd`）100% 往返无损还原（$< 10^{-9}$）；
   - 两阶段原子写盘：先写入同目录 `.tmp` 临时文件，`flush` 并检查 `fail()` 成功后，通过 Win32 `MoveFileExW`（`MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`）原子替换；替换失败清理临时文件并返回 false，原文件绝不损坏；
   - Windows 下使用 `MultiByteToWideChar(CP_UTF8, ...)` 构造宽字符 `std::filesystem::path`，完全兼容 ANSI 936（GBK）环境下的中文与非 ASCII 路径；
   - 解析先入局部对象，异常安全全局兜底，空对象 `{}` 或类型错配安全拒绝且原工程文档毫发无损。
