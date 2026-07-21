# 贡献指南 - cognition 子仓（认知子系统）

> **治理依据**：[`docs/AirymaxOS/50-engineering-standards/07-maintainers-and-governance.md`](https://github.com/openairymax/docs/blob/main/AirymaxOS/50-engineering-standards/07-maintainers-and-governance.md)
> **开发流程**：[`docs/AirymaxOS/50-engineering-standards/05-development-process.md`](https://github.com/openairymax/docs/blob/main/AirymaxOS/50-engineering-standards/05-development-process.md)

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 子仓职责

cognition 子仓负责 AirymaxOS 的全部认知功能，包括：

- **CoreLoopThree**（`coreloopthree/`）：三阶段认知循环（PERCEPTION→THINKING→ACTION）
- **Thinkdual**（`thinkdual/`）：双模式推理（SYSTEM1_FAST / SYSTEM2_SLOW）
- **LLM 推理调度**（`llm/`）：PREFILL/DECODE/SPECULATIVE
- **计算加速**（`compute-accel/`）：GPU（drm_sched）/ NPU 调度
- **Wasm 沙箱**（`wasm-sandbox/`）：Wasm 运行时 + 隔离
- **kthread 通信**（`kthread/`）：kfifo + wait_event_interruptible

### IRON-9 主层

- `[IND]`：全部组件均为 agentrt-linux 专属实现

## 2. 开发环境要求

- **C 标准**：GNU C11（内核模块 kthread）
- **编译器**：gcc ≥ 11 或 clang ≥ 14
- **内核**：Linux 6.6+（kthread 内核模块构建）
- **内核头**：通过 `-I../kernel/include` 引用 [SC] 头文件

### [SC] 头文件引用约束（OS-IRON-014）

- [SC] 头文件**唯一物理宿主**：`../kernel/include/uapi/linux/airymax/`
- 本子仓通过 `-I` 引用，**禁止物理副本**
- Kbuild 配置：
  ```makefile
  ccflags-y += -I$(src)/../kernel/include
  ccflags-y += -I$(src)/../kernel/include/uapi/linux
  ```

## 3. 构建命令

```bash
# 从子仓根目录构建（需要内核源码树或 build 目录）
cd cognition
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# 构建产物
#   coreloopthree/airy_clt.ko
```

## 4. 代码规范

### 4.1 C 代码风格

- **OS-STD-FMT-001**：Tab-8 缩进（由 `.clang-format` 强制）
- **OS-STD-FMT-002**：80 列硬限制
- **GPL-2.0-only**：所有 `.c` / `.h` 文件必须包含 `SPDX-License-Identifier: GPL-2.0-only`
- **版权头**：`Copyright (c) 2025-2026 SPHARX Ltd.`

### 4.2 认知阶段常量（[SC] cognition_types.h）

| 阶段 | 枚举 | 说明 |
|------|------|------|
| 感知 | `AIRY_COG_PERCEPT` | 观察环境 |
| 思考 | `AIRY_COG_THINK` | 推理与规划 |
| 行动 | `AIRY_COG_ACT` | 执行动作 |

| 模式 | 枚举 | 说明 |
|------|------|------|
| 快模式 | `AIRY_THINK_FAST` | 模式识别，低延迟 |
| 慢模式 | `AIRY_THINK_SLOW` | 深度推理，高质量 |

## 5. 提交规范

### 5.1 DCO 签名

```bash
git commit -s
```

### 5.2 提交信息格式

```
cognition: 简短描述（≤72 字符）

详细说明 what 和 why，72 字符换行。

Signed-off-by: Your Name <your.email@example.com>
```

### 5.3 子系统前缀

| 前缀 | 范围 |
|------|------|
| `cognition:` | cognition/ 子仓整体 |
| `cognition: clt:` | CoreLoopThree |
| `cognition: td:` | Thinkdual |
| `cognition: llm:` | LLM 推理调度 |
| `cognition: wasm:` | Wasm 沙箱 |

## 6. 分支策略

- **开发分支**：`feature/official-hubs-01`
- PR 目标分支：`feature/official-hubs-01`

## 7. 测试要求

- **OS-STD-TEST-***：所有变更必须包含或更新测试
- 单元测试：随代码放在各组件目录
- 集成测试：在 `tests-linux/` 子仓
- 认知类型测试：见 `tests-linux/unit/test_cognition_types.c`

## 8. 审查流程

1. 向 `feature/official-hubs-01` 提交 PR
2. CI 运行：SSoT 校验 + Kbuild 构建 + sparse 检查 + 测试
3. 至少一名维护者审批
4. Squash-merge

## 9. 报告问题

- **Bug**：在 [cognition issues](https://github.com/openairymax/cognition/issues) 提交
- **设计讨论**：使用管理仓的 GitHub Discussions
