# cognition 子仓文档索引

> **子仓定位**：AirymaxOS 认知子系统 —— CoreLoopThree 三阶段 + Thinkdual 双模式 + LLM 调度 + Wasm 沙箱 + GPU/NPU 调度。

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 文档清单

### 已有文档

| 文档 | 说明 |
|------|------|
| [coreloopthree.md](coreloopthree.md) | CoreLoopThree 三阶段设计 |
| [thinkdual.md](thinkdual.md) | Thinkdual 双模式设计 |

### 待编写文档（0.1.1 → 1.0.1）

| 文档 | 说明 | 计划版本 |
|------|------|---------|
| `gpu-npu-sched.md` | GPU/NPU 调度设计 | 0.1.1 |
| `llm-sched.md` | LLM 推理调度（PREFILL/DECODE/SPECULATIVE） | 0.1.1 |
| `wasm-sandbox.md` | Wasm 沙箱设计 | 1.0.1 |
| `kthread-comm.md` | kthread 通信基础设施 | 1.0.1 |
| `q16-fixedpoint.md` | Q16.16 定点数运算 | 1.0.1 |

## 相关文档

- [07-directory-structure.md](../../../docs/AirymaxOS/10-architecture/07-directory-structure.md) §4.5 — cognition 子仓完整目录结构
- [cognition_types.h](../../kernel/include/uapi/linux/airymax/cognition_types.h) — [SC] 认知类型契约
- 顶层 [CONTRIBUTING.md](../CONTRIBUTING.md) — cognition 子仓贡献指南
