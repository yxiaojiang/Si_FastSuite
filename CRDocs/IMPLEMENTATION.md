# M14-PROJECT-JSON-001 — Round 3：工位工程 JSON 序列化与反序列化持久化（DIR-001 第 6 步，M14）

> 执行者：执行方（Executor）
>
> 日期：2026-10-28
>
> 轮次：Round 3
>
> 状态：**READY_FOR_REVIEW**

根据 Round 2 审核意见（`CRDocs/REVIEW.md`），`M14-008`、`M14-010`、`M14-011` 已经审核关闭保持不动；执行方本轮专注于 **彻底修复 `M14-009`**。严格未动第 7 步。

---

## 一、 Round 3 核心修复（`M14-009`）

### 1. 彻底移除 `copy_file` 覆盖回退分支，原子替换失败坚决保证原文件毫发无损

- **问题根因**：
  Round 2 中，在 `MoveFileExW` 失败时设计了 `std::filesystem::copy_file(tempPath, targetPath, overwrite_existing)` 作为回退机制。然而：
  1. `copy_file` 覆盖原文件是非原子的，若拷贝中途断开，原文件会被破坏；
  2. 原代码随后调用 `remove(tempPath, ec)`，覆盖了 `copy_file` 的错误码 `ec`，导致若临时文件清理成功而拷贝实际失败时，函数误报返回 `true`，进而错误重置 `isDirty`。
- **修复方案** (`src/io/ProjectSerializer.cpp`)：
  - 彻底移除 `copy_file` 回退分支；
  - 在 Windows 下：
    ```cpp
    #ifdef _WIN32
        if (!MoveFileExW(tempPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD lastErr = GetLastError();
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            setError("替换目标工程文件失败 (Win32 Error " + std::to_string(lastErr) + "): " + filePath);
            return false;
        }
    #else
        std::error_code ec;
        std::filesystem::rename(tempPath, targetPath, ec);
        if (ec) {
            std::error_code rmEc;
            std::filesystem::remove(tempPath, rmEc);
            setError("替换目标工程文件失败: " + filePath + " (" + ec.message() + ")");
            return false;
        }
    #endif
    ```
  - 当原子替换失败时：
    1. 立即获取底层系统错误码并写入 `errorOut`；
    2. 安全删除 `${targetPath}.tmp` 临时文件，不留残余垃圾；
    3. 立即返回 `false`；
    4. 目标文件在原子替换成功前保持 100% 原始字节与状态，绝无任何写操作触及原文件。

### 2. 测试覆盖增强 (`tests/test_project_io.cpp`)

在原有已通过的原子覆盖测试基础之上，新增失败分支测试：
- 以无法被普通文件替换的目标路径（如已存在的同名目录）调用 `saveToFile`；
- 断言函数安全返回 `false`，并返回明确错误原因；
- 断言临时文件 `${targetPath}.tmp` 已被安全清理，磁盘无残留。

---

## 二、 自动化测试套件与验证结果

Test 22 全部测试通过：
- `M14-001`：数学矩阵与向量全精度往返还原（$< 10^{-9}$）；
- `M14-002`：FANUC WPR 万向锁（$P = +90^\circ$）往返测试；
- `M14-003`：单位与工艺条件表 1..32 往返测试；
- `M14-004`：焊缝拓扑签名与采样帧切向/面法向/角平分线/局部矩阵完整还原测试；
- `M14-005`：`userModified = true`、`userDeltaOffset`、工艺事件、构型锁定往返测试；
- `M14-006`：整工位（含带真实顶点面片的 `TriMesh` 零件、机器人、焊枪、焊缝、控制器）文件落盘与读取测试；
- `M14-007`：非法输入防御性测试（空路径、不存在文件、损坏 JSON 等）；
- `M14-008`：`{}` 拦截与调用方原文档不变测试、类型不匹配拦截测试；
- **`M14-009`**：同名文件原子覆盖写盘测试，以及替换失败时临时文件清理与安全拦截测试；
- `M14-010`：中文目录及文件名 UTF-8 往返持久化测试；
- `M14-011`：零件网格顶点/三角形/包围盒恢复比对测试。

---

## 三、 风险与约束遵守情况

1. **`RISK-001` 维持 OPEN**：J2/J3 耦合比（`+1.0`）及圈数计算保持不变，不依赖未安装的 ROBOGUIDE；
2. **`RISK-002`、`RISK-003` 保持 OPEN**；
3. **第 7 步严格未做**：严格未动 3D PDF 悬置收口与 T 型接头回归；
4. **编译严格带 `/p:VCToolsVersion=14.44.35207`**；
5. **单文件行数限制（1000 行）严格遵守**：
   - `include/olp/io/ProjectSerializer.h`: 41 行
   - `src/io/ProjectSerializer.cpp`: 898 行（$< 1000$）
   - `tests/test_project_io.cpp`: 582 行（$< 1000$）
   - `include/olp/ui/MainWindow.h`: 147 行
   - `src/ui/MainWindow.cpp`: 599 行（$< 1000$）
   - `include/olp/ui/ShellWindow.h`: 112 行
   - `src/ui/ShellWindow.cpp`: 863 行（$< 1000$）
   - `tests/test_support.h`: 33 行
   - `tests/test_main.cpp`: 66 行
   - `CMakeLists.txt`: 220 行

---

## 四、 验证证据

### 1. 编译命令与输出
```powershell
cmake --build build-full --config Release -- /p:VCToolsVersion=14.44.35207
```
**结果**：Exit code 0，全部 C++20 目标构建通过。

### 2. 自动化测试套件
```powershell
cmd.exe /c "set PATH=D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;%PATH% && build-full\Release\olp_tests.exe"
```
**结果**：**725 passed, 0 failed**，退出码 0。

### 3. 产品启动与验证命令
```powershell
cmd.exe /c "set PATH=D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;%PATH% && build-full\Release\SiFastSuiteOLP.exe --verify"
```
**结果**：Exit code 0。
