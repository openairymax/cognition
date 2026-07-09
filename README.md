**Language:** English | [简体中文](README_zh.md)

# agentrt-linux Cognition (AirymaxOS Cognition)

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/cognition)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> Cognition subsystem of [agentrt-linux（AirymaxOS）](https://atomgit.com/openairymax/agentrt-linux) — the AI Agent Operating System.
> One of the leaf repositories aggregated by the [agentrt-linux](https://atomgit.com/openairymax/agentrt-linux) management repo.
> Reuses and extends the Airymax `coreloopthree` and `frameworks` modules for OS-level cognition.

---

## Overview

The **agentrt-linux Cognition (AirymaxOS Cognition)** (`airymaxos-cognition`) is the cognition engine subsystem of agentrt-linux（AirymaxOS）, the AI Agent Operating System. It runs CoreLoopThree as a kernel thread (kthread), embeds a Wasm 3.0 runtime for agent logic, schedules LLM inference with token-aware energy efficiency, and isolates hyper-node agent execution inside Cupolas sandboxes.

In agentrt-linux 0.1.1, this repository is **documentation complete** (文档体系完成) containing design documents, reference distribution specifications, and architectural drafts. Actual kernel and OS development takes place in version 1.0.1.

### Core Technologies

- **CoreLoopThree kthread** — cognition–action–memory three-in-one loop running as a kernel thread
- **Wasm 3.0** runtime for portable, sandboxed, near-native agent logic
- **LLM scheduling** with token-aware batching and priority policies
- **Token energy efficiency** optimizing tokens-per-joule across heterogeneous accelerators
- **Hyper-node sandbox** delegating agent isolation to the agentrt-linux Security layer

### Relationship with Airymax coreloopthree + frameworks

The agentrt-linux Cognition (AirymaxOS Cognition) reuses and extends the `coreloopthree` and `frameworks` modules from the Airymax runtime platform. The three-loop cognition model, agent framework abstractions and Wasm runtime are shared between the user-space runtime (agentrt) and the OS-level cognition engine (agentrt-linux), ensuring architectural homology with no adaptation layer.

## Repository Structure (0.1.1 Documentation Complete)

```
cognition/
├── README.md           # This file (English)
├── README_zh.md        # Chinese translation
├── LICENSE             # AGPL-3.0 + Apache-2.0 dual license
├── NOTICE              # Copyright, trademark and third-party notices
└── .gitignore
```

Design documents and reference distribution specifications are maintained in the `docs/AirymaxAgentOS/` directory of the umbrella repository.

## Upstream & Downstream Dependencies

### Upstream

- **agentrt-linux Kernel (AirymaxOS Kernel)** — provides the kthread, sched_ext and io_uring primitives for the cognition loop
- **agentrt-linux Memory (AirymaxOS Memory)** — provides the tiered CXL/PMEM memory used for LLM serving and KV-cache
- **Airymax coreloopthree + frameworks** — provides the three-loop model and agent frameworks that are reused and extended

### Downstream

- **agentrt-linux Services (AirymaxOS Services)** — service layer that exposes cognition capabilities to user space
- **Agent applications** — end-user agents built on top of the cognition engine

## Branch Strategy

This leaf repository is developed on **`feature/official-hubs-01`**. The aggregating `agentrt-linux` management repo stays on `main`.

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`). See [LICENSE](LICENSE) for the full text.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
