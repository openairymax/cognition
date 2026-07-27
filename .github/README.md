# `.github/` — cognition Leaf Repository Automation

> GitHub automation directory for the **cognition** leaf repository of
> [agentrt-linux (AirymaxOS)](https://atomgit.com/openairymax/agentrt-linux).

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

---

## Positioning

This directory hosts cognition-specific GitHub automation. Management-level
orchestration (governance integrity, `[SC]` dual-CI for `cognition_types.h`,
nightly soak of the cognition loop, release SBOM, SSoT validation) runs in the
management repository; this leaf repository hosts language-level CI for the C
kthread and the Rust / Wasm 3.0 runtime.

## Directory Contents

```
cognition/.github/
└── README.md    # This file
```

## Applicable Management-Repository Workflows

| Workflow | Jobs | Relevance to cognition |
|----------|------|-------------------------|
| `mgmt-orchestrator.yml` | `file-integrity` + `orchestrate-leaf-ci` | Verifies the `cognition/` submodule dir exists; aggregates this repo's CI status |
| `sc-dual-ci.yml` | `sc-validate` + `sc-trigger-and-await` | Guards `cognition_types.h` (`[SC]` 10 core headers); creates agentrt mirror PR on changes |
| `nightly.yml` | `nightly-test-suite` + `nightly-revert-or-budget` | 72h soak exercises the CoreLoopThree kthread and LLM scheduling; chaos (CPU hotplug) |
| `release.yml` | `build-and-sign` + `publish-release` | `syft cognition/` SBOM; signed release artifacts |

## Development Guide

- Add leaf-local workflows for CoreLoopThree kthread tests, Thinkdual mode
  coverage, Wasm 3.0 runtime unit tests, and token-efficiency benchmarks; keep
  each workflow ≤ 2 jobs.
- `[SC]` header `cognition_types.h` lives at
  `kernel/include/uapi/linux/airymax/cognition_types.h` — single physical source, no
  duplicates (OS-IRON-014).
- Cognition APIs use the `airy_*` prefix; Rust modules follow `cargo fmt`.

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`).
See the repository root [LICENSE](../LICENSE) and [NOTICE](../NOTICE).

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
