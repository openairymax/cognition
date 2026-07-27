**Language:** English | [简体中文](README_zh.md)

# cognition — agentrt-linux (AirymaxOS) Cognition

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/cognition)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> Cognition subsystem of [agentrt-linux (AirymaxOS)](https://atomgit.com/openairymax/agentrt-linux) — the AI Agent Operating System.
> One of the 8 leaf repositories aggregated by the [agentrt-linux](https://atomgit.com/openairymax/agentrt-linux) management repo.
> Reuses and extends the Airymax `coreloopthree` (and `frameworks`) module for OS-level cognition.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

---

## Positioning

The **cognition** leaf repository is the cognition engine subsystem of
agentrt-linux (AirymaxOS). It runs CoreLoopThree as a kernel thread (kthread)
implementing the three-in-one cognition–action–memory loop, embeds a Wasm 3.0
runtime for agent logic, schedules LLM inference with token-aware energy
efficiency (Thinkdual), and isolates hyper-node agent execution inside Cupolas
sandboxes.

## Core Responsibilities

- **CoreLoopThree kthread** — three-in-one cognition–action–memory loop running as a kernel thread.
- **Thinkdual** — dual-track thinking mode for fast vs. deep reasoning.
- **Wasm 3.0** runtime for portable, sandboxed, near-native agent logic.
- **LLM scheduling** with token-aware batching and priority policies.
- **Token energy efficiency** optimizing tokens-per-joule across heterogeneous accelerators.
- **Hyper-node sandbox** delegating agent isolation to the `security` subsystem.
- **`[SC]` contribution** — owns the `cognition_types.h` shared-contract header
  (single physical source under `kernel/include/uapi/linux/airymax/`).

## Relationship with Airymax `coreloopthree` + `frameworks`

The cognition leaf repo reuses and extends the `coreloopthree` and `frameworks`
modules from the Airymax runtime platform. The three-loop cognition model, agent
framework abstractions and Wasm runtime are shared between the user-space runtime
(`agentrt`) and the OS-level cognition engine, ensuring architectural homology
with no adaptation layer.

## Document & File List

```
cognition/
├── README.md           # This file (English)
├── README_zh.md        # Chinese translation
├── LICENSE             # AGPL-3.0 + Apache-2.0 dual license
├── NOTICE              # Copyright, trademark and third-party notices
├── .gitignore
└── .github/
    └── README.md       # GitHub automation for this leaf repo
```

Design documents and reference distribution specifications are maintained in the
`docs/AirymaxOS/` directory of the umbrella documentation repository.

## CI Status

Cognition changes are governed by management-repository workflows (each ≤ 2 jobs):

| Workflow | Jobs | Applies to cognition via |
|----------|------|----------------------------|
| `mgmt-orchestrator.yml` | `file-integrity` + `orchestrate-leaf-ci` | Verifies the `cognition/` submodule dir; aggregates this repo's CI status |
| `sc-dual-ci.yml` | `sc-validate` + `sc-trigger-and-await` | Guards `cognition_types.h` in the `[SC]` 10 core headers; triggers agentrt mirror PR on changes |
| `nightly.yml` | `nightly-test-suite` (72h soak exercises the cognition loop) + `nightly-revert-or-budget` | Nightly cron |
| `release.yml` | `build-and-sign` (SBOM scan of `cognition/`) + `publish-release` | Release tag |

Language-level CI (C kthread, Rust/Wasm runtime) is delegated to this leaf
repository's own `.github/workflows/`.

## Development Guide

- **Branch**: `feature/official-hubs-01` (the management repo stays on `main`).
- **DCO**: every commit must be `Signed-off-by` (`git commit -s`).
- **Commit prefix**: `cognition:`.
- **Code style**: C — tab-8, 80 cols (`.clang-format`); Rust — `cargo fmt`
  (4-space); run `make format-check`.
- **`[SC]` changes**: edits to `cognition_types.h` require dual CI (agentrt-linux
  `sc-dual-ci.yml` + agentrt mirror PR) and L1+L3 approval per OS-IRON-014.
- **Function prefix**: `airy_*` (not legacy `airymaxos_*`).

## Upstream & Downstream

- **Upstream** — `kernel` (kthread, sched_tac, io_uring); `memory` (tiered CXL/PMEM for LLM serving and KV-cache); Airymax `coreloopthree` + `frameworks`.
- **Downstream** — `services` (exposes cognition capabilities to user space); agent applications built on the cognition engine.

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`).
See [LICENSE](LICENSE) for the full text.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
