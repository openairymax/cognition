**语言:** [English](README.md) | 简体中文

# agentrt-linux 认知（AirymaxOS Cognition）

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/cognition)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> [agentrt-linux（AirymaxOS）](https://atomgit.com/openairymax/agentrt-linux)（智能体操作系统）的认知子系统。
> 由 [agentrt-linux](https://atomgit.com/openairymax/agentrt-linux) 管理仓聚合的叶子仓之一。
> 复用并扩展 Airymax `coreloopthree` 与 `frameworks` 模块以提供 OS 级认知能力。

---

## 概述

**agentrt-linux 认知（AirymaxOS Cognition）**（`airymaxos-cognition`）是 agentrt-linux（AirymaxOS）（智能体操作系统）的认知引擎子系统。它将 CoreLoopThree 作为内核线程（kthread）运行，嵌入 Wasm 3.0 运行时承载智能体逻辑，以 token 感知的能效策略调度 LLM 推理，并在 Cupolas 沙箱内隔离超节点智能体执行。

在 agentrt-linux 0.1.1 中，本仓库为**文档体系完成**，包含设计文档、参考发行版规范及架构草案。实际的内核与 OS 开发在 1.0.1 版本进行。

### 核心技术

- **CoreLoopThree kthread** — 作为内核线程运行的认知-动作-记忆三位一体循环
- **Wasm 3.0** 运行时提供可移植、沙箱化、近原生的智能体逻辑
- **LLM 调度** 实现 token 感知的批处理与优先级策略
- **Token 能效** 在异构加速器间优化 tokens-per-joule
- **超节点沙箱** 将智能体隔离委托给 agentrt-linux 安全态层

### 与 Airymax coreloopthree + frameworks 的关系

agentrt-linux 认知（AirymaxOS Cognition）复用并扩展了 Airymax 运行时平台的 `coreloopthree` 与 `frameworks` 模块。三循环认知模型、智能体框架抽象与 Wasm 运行时在用户态运行时（agentrt）与 OS 级认知引擎（agentrt-linux）之间共享，确保架构同源、无适配层。

## 仓库结构（0.1.1（文档体系完成））

```
cognition/
├── README.md           # 本文件（英文）
├── README_zh.md        # 中文翻译
├── LICENSE             # AGPL-3.0 + Apache-2.0 双许可证
├── NOTICE              # 版权、商标与第三方声明
└── .gitignore
```

设计文档与 参考发行版规范维护在伞仓的 `docs/AirymaxAgentOS/` 目录。

## 上下游依赖

### 上游

- **agentrt-linux 内核（AirymaxOS Kernel）** — 提供认知循环所需的 kthread、sched_ext 与 io_uring 原语
- **agentrt-linux 内存管理（AirymaxOS Memory）** — 提供 LLM 服务与 KV-cache 所用的分层 CXL/PMEM 内存
- **Airymax coreloopthree + frameworks** — 提供被复用并扩展的三循环模型与智能体框架

### 下游

- **agentrt-linux 服务态（AirymaxOS Services）** — 向用户态暴露认知能力的服务层
- **Agent applications** — 构建于认知引擎之上的终端智能体应用

## 分支策略

本叶子仓在 **`feature/official-hubs-01`** 分支上开发。聚合管理仓 `agentrt-linux` 保持在 `main` 分支。

## 许可证

采用 **AGPL v3 + Apache 2.0** 双许可证（SPDX：`AGPL-3.0-or-later OR Apache-2.0`）。完整文本见 [LICENSE](LICENSE)。

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
