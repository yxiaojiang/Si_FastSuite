# Claude–Codex / 执行者–审核者 代码协作机制

本目录是执行者（Executor，如 Claude / Gemini）与审核者（Reviewer，如 Codex / Opus）之间唯一的长期交接区。注意：文档中的名称代表角色分工。双方开始工作前，都必须先完整读取本文件、`STATUS.md`、`REVIEW.md`、`IMPLEMENTATION.md` 和 `DEFERRED.md`。

## 角色分工

### 执行者（Executor）

1. 读取 `STATUS.md` 和 `REVIEW.md`。
2. 只处理 `REVIEW.md` 中状态为 `OPEN` 且属于当前阶段的问题。
3. 修改实际代码并进行适当验证（自动化测试、编译构建、端到端校验）。
4. 将本轮实际修改、未修改项及验证结果写入 `IMPLEMENTATION.md`。
5. 把 `STATUS.md` 更新为 `READY_FOR_REVIEW`。
6. 不得把“已修改”当作“已验证”；交接内容必须对应实际文件。
7. 不编辑 `REVIEW.md` 中审核者的审核结论；如有异议，写入 `IMPLEMENTATION.md` 的“异议与理由”。

### 审核者（Reviewer）

1. 读取 `STATUS.md` 和 `IMPLEMENTATION.md`。
2. 必须检查实际代码、数学几何边界、协议、diff 和必要的调用链，不以执行者的修改说明作为完成证据。
3. 默认只审核，不修改业务代码；只有用户明确要求审核者修改时才能改代码。
4. 将问题按严重度写入 `REVIEW.md`：
   - `P0` 阻断级（编译失败、核心几何算法崩溃、不可逆破坏、数据模型损坏）。
   - `P1` 当前阶段必须修复（精度不达标、命令对称性丢失、漏选/漏更新、交互断链）。
   - `P2` 可延期但必须登记（次要 UI 瑕疵、极端容错、未来特性演进）。
5. 已确认修复的问题标记为 `RESOLVED`，不要在后续轮次重复提出。
6. 若仍有 P0/P1，将 `STATUS.md` 更新为 `CHANGES_REQUESTED`；若当前阶段闭环，更新为 `APPROVED`。
7. 属于未来阶段的问题移入 `DEFERRED.md`，写明触发阶段和验收条件。

## 状态流转

```text
IDLE
  → IMPLEMENTING
  → READY_FOR_REVIEW
  → CHANGES_REQUESTED → IMPLEMENTING（下一轮）
  → APPROVED
```

其他状态：

- `BLOCKED`：缺少需求决定、硬件真机参数、三方依赖或外部信息，无法安全继续。
- `SUPERSEDED`：本轮任务被新需求替代。

## 文件所有权

| 文件 | 主要写入者 | 用途 |
|---|---|---|
| `README.md` | 用户或经用户授权的任一方 | 固定协作规则 |
| `STATUS.md` | 当前交棒方 | 当前任务、轮次和状态 |
| `IMPLEMENTATION.md` | 执行者（Executor） | 实现与验证交接 |
| `REVIEW.md` | 审核者（Reviewer） | 审核结论与问题清单 |
| `DEFERRED.md` | 审核者；执行者可补充说明 | 已明确延期的问题 |

## 每轮规则

1. 一个问题使用稳定 ID，例如 `M08-001`、`ALG-001`、`UI-001`，后续轮次不得改编号。
2. 结论只能使用 `OPEN`、`RESOLVED`、`DEFERRED`、`WONT_FIX`。
3. `WONT_FIX` 必须记录用户决定或明确的技术理由。
4. 工业物理/数学契约（如右手正交系 $\det(R)=1$、OPW 运动学耦合、弧长单调性）与代码不一致时，必须保证物理真实性与数学严谨性，不能为迁就实现而擅自降级标准。
5. 遵循“最小实现、落在现有结构上”原则：能多一个函数就不要新类，优先复用现有通道。
6. 不顺手重构，不修改与当前问题无关的代码。
7. 每轮最多聚焦当前 P0/P1；P2 统一进入 `DEFERRED.md`，避免反复循环。
8. `APPROVED` 仅代表 `STATUS.md` 中写明的当前范围通过，不代表整个项目已完成。

## 用户最简操作

对执行者：

> 读取 CRDocs，按 REVIEW.md 修改，完成后交接。

对审核者：

> 读取 CRDocs，审核本轮实际改动并更新审核状态。

用户不再需要复制双方的长文本。
