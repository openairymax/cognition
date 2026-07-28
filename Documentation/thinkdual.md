# Thinkdual — 双模式推理

> **子仓**：`cognition/`
> **组件**：`thinkdual/`（规划中，本文件为设计占位）
> **[SC] 依赖**：`kernel/include/uapi/linux/airymax/cognition_types.h`
> **协同组件**：`coreloopthree/`（CoreLoopThree 在 THINK 阶段调用 Thinkdual）
> **设计文档**：`docs/AirymaxOS/10-architecture/07-directory-structure.md` §4.5

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 设计目标

Thinkdual 是 AirymaxOS 认知子系统的双模式推理组件，受 Kahneman
《思考，快与慢》启发，将 Agent 推理划分为两种模式：

| 模式 | 枚举 | 特性 | 适用场景 |
|------|------|------|----------|
| 快模式 | `AIRY_THINK_FAST` | 模式识别，低延迟，单轮推理 | 已知模式匹配、常规决策 |
| 慢模式 | `AIRY_THINK_SLOW` | 深度推理，高质量，多轮迭代 | 复杂规划、罕见情况、需验证 |

两种模式由 [SC] `cognition_types.h` 中的 `enum airy_think_mode` 定义，
确保用户态（agentrt）与内核态（agentrt-linux）使用同一套语义。

## 2. 与 CoreLoopThree 的接口

CoreLoopThree 在 `airy_clt_think()` 中读取事件 `think_mode` 字段：

```c
if (ev.think_mode == AIRY_THINK_SLOW) {
    /* SLOW 模式：多轮推理 */
    ev.flags |= 0x01;
} else {
    /* FAST 模式：单次模式识别 */
    ev.flags &= ~0x01u;
}
```

Thinkdual 子组件（`thinkdual/`）将在后续版本提供：

- `airy_td_fast_classify()` — 快模式分类接口
- `airy_td_slow_deliberate()` — 慢模式多轮推理接口
- `airy_td_route()` — 模式路由（基于置信度阈值）

置信度阈值由 [SC] `struct airy_cog_config.confidence_threshold`（Q16.16
定点）配置，跨用户态/内核态共享。

## 3. Q16.16 定点表示

[SC] `cognition_types.h` 定义 `airy_q16_t` 为 Q16.16 定点类型：

| 宏 | 值 | 含义 |
|----|----|------|
| `AIRY_Q16_ONE` | `1 << 16` (65536) | 1.0 |
| `AIRY_Q16_HALF` | `1 << 15` (32768) | 0.5 |
| `AIRY_Q16_TO_FLOAT(x)` | — | Q16.16 → float |
| `AIRY_Q16_FROM_FLOAT(f)` | — | float → Q16.16 |

定点表示确保内核态（无浮点支持）与用户态（agentrt）使用相同算术语义。

## 4. 模式切换策略（设计）

| 输入特征 | 默认模式 | 切换条件 |
|----------|----------|----------|
| 高置信度（≥ threshold） | FAST | 不切换 |
| 低置信度（< threshold） | FAST → SLOW | 触发慢模式深推理 |
| SLOW 多轮后置信度仍低 | SLOW | 上报异常或降级 |
| 紧急中断（高优先级） | 任意 → FAST | 立即响应 |

## 5. IRON-9 同源关系

| 层级 | 实体 | 说明 |
|------|------|------|
| `[SC]` | `enum airy_think_mode` | 用户态/内核态共享枚举 |
| `[SC]` | `airy_q16_t` + Q16.16 宏 | 共享定点类型 |
| `[SC]` | `struct airy_cog_config` | 共享配置结构 |
| `[IND]` | Thinkdual 推理引擎实现 | agentrt-linux 专属，无 agentrt 对应物 |

## 6. 当前实现状态

0.1.1 版本中 Thinkdual 仅作为设计占位：

- ✅ [SC] 枚举与类型已完成
- ✅ CoreLoopThree 已预留 `think_mode` 字段
- ⏳ `thinkdual/` 子目录与 `airy_td_*` 接口尚未实现（后续版本）

当前 CoreLoopThree 的 `airy_clt_think()` 提供最小可编译的占位逻辑：
根据 `think_mode` 设置 `flags`，不调用实际推理引擎。

## 7. 变更历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 0.1.1 | 2026-07-20 | 初始设计文档；[SC] 枚举与 Q16.16 类型完成 |
