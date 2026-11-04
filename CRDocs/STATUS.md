# 当前协作状态

- **Task ID**：`DIR-003-STEP3-INSTALLER-001`
- **Task**：DIR-003 第 3 步（P0-Installer，Inno Setup 单文件安装包与验证）
- **Round**：`1`
- **Status**：`APPROVED`
- **Last Updated By**：`Reviewer`
- **Blocking Issues**：`0`
- **Summary**：
  1. **范围**：本轮只关闭单文件安装包。`installer/SiFastSuite.iss` 76 行，`Tools/Package.bat` 176 行，两者都没有非 ASCII 字符。安装包长度为 22,816,160 字节。
  2. **安装内容**：`build/dist` 的 84 个文件原样进入安装目录，另有 `unins000.exe` 与 `unins000.dat`。开始菜单包含主程序、`Sample Scenes` 和卸载程序。桌面图标在选中 `desktopicon` 任务时创建。
  3. **独立验证**：当前用户静默安装到 `D:\UserTemp\SiFastSuiteOLP_review_install`，退出码 0。隔离 `--verify` 退出码 0，12 个实体，底座 `(0, 0, 500)` mm，1047 帧，导出在 `D:/UserTemp/SiFastSuiteOLP/`。`qwindows.dll` 从该安装目录的 `plugins\platforms` 加载。静默卸载后，该目录、当前用户快捷方式和 `HKCU` 卸载项都已消失。
  4. **未改动的既有安装**：`D:\Soft\SiFastSuite OLP` 及其 `HKLM` 卸载项、公共桌面和公共开始菜单保持原样。
  5. **延期**：`DIR-003-003`（自检用 `rmdir` 掩盖卸载残留）、`DIR-003-004`（桌面图标默认不勾选）已写入 `DEFERRED.md`。`DIR-003-002`、`DIR-003-001`、`DIR-002-003`、`DIR-002-004`、`DIR-002-005` 仍为 OPEN。
- **详细结论**：见 `CRDocs/REVIEW.md` 顶部 `DIR-003-STEP3-INSTALLER-001` Round 1。
