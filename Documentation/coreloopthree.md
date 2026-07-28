# CoreLoopThree — 三阶段认知循环

> **子仓**：`cognition/`
> **组件**：`coreloopthree/`
> **可编译入口**：`coreloopthree/clt_main.c` → `airy_clt.ko`
> **[SC] 依赖**：`kernel/include/uapi/linux/airymax/cognition_types.h`
> **设计文档**：`docs/AirymaxOS/10-architecture/07-directory-structure.md` §4.5

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 设计目标

CoreLoopThree 是 AirymaxOS 认知子系统的核心控制循环。它将 Agent 的认知过程
划分为三个阶段，并在内核侧通过 kthread 提供长驻可调度上下文：

| 阶段 | 枚举 | 职责 |
|------|------|------|
| 感知 | `AIRY_COG_PERCEPT` | 观察环境，从 sensor / ipc / agent 任务采集输入事件 |
| 思考 | `AIRY_COG_THINK` | 推理与规划，根据模式（FAST / SLOW）选择推理深度 |
| 行动 | `AIRY_COG_ACT` | 执行动作，调用 action 子系统或输出反馈 |

阶段之间通过 **kfifo** 传递 `airy_clt_event` 事件，避免共享状态竞争；无事件时
通过 `wait_event_interruptible` 让出 CPU。

## 2. 内核原语选型

| 原语 | 用途 | 选型理由 |
|------|------|----------|
| `kthread_run` / `kthread_stop` | 长驻内核线程 | Linux 6.6 标准接口，无需修改主线源码（OS-ARCH-001） |
| `DECLARE_KFIFO` / `kfifo_in` / `kfifo_out` | 事件队列 | 无锁环形队列，SPSC 场景下 O(1) |
| `wait_queue_head_t` / `wake_up_interruptible` | 阻塞唤醒 | 与 kthread_should_stop 协同退出 |
| `msleep_interruptible` | 周期让出 CPU | 100ms 间隔可被信号打断，响应停止请求 |
| `spin_lock_irqsave` | kfifo 多核保护 | THINK/ACT 同 kthread 内串行，仍保留锁以备未来扩展 |

## 3. 模块参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `AIRY_CLT_LOOP_INTERVAL_MS` | 100 | 单轮循环间隔（毫秒） |
| `AIRY_CLT_KFIFO_SIZE` | 16 | kfifo 容量（必须为 2 的幂） |

## 4. 构建与加载

### 4.1 构建

```bash
cd cognition
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

产物：`coreloopthree/airy_clt.ko`

### 4.2 加载

```bash
sudo insmod coreloopthree/airy_clt.ko
dmesg | grep airy_clt
```

### 4.3 卸载

```bash
sudo rmmod airy_clt
dmesg | tail -n 20
```

加载后会输出类似：
```
airy_clt: 初始化 v0.1.1
airy_clt: kthread 'clt_main' 已启动 (interval=100ms kfifo_size=16)
airy_clt: kthread 启动 tid=1234 comm=clt_main
```

## 5. 统计接口

模块在 `/proc/airy/status`（由 system 子仓的 airymaxmon 读取）暴露以下计数：

| 计数器 | 含义 |
|--------|------|
| `airy_clt_loop_count` | 完成的循环数 |
| `airy_clt_percept_count` | PERCEPT 阶段产生的事件数 |
| `airy_clt_think_count` | THINK 阶段处理的事件数 |
| `airy_clt_act_count` | ACT 阶段执行的事件数 |

## 6. IRON-9 同源关系

| 层级 | 实体 | 说明 |
|------|------|------|
| `[IND]` | `airy_clt` 模块 | agentrt-linux 专属实现，无 agentrt 对应物 |

CoreLoopThree 是 AirymaxOS 在内核态实现认知循环的尝试，与 agentrt 用户态
认知框架通过 [SC] `cognition_types.h` 共享阶段与模式枚举，但模块本身
完全独立（[IND]）。

## 7. 与 Thinkdual 的协同

CoreLoopThree 在 THINK 阶段读取事件中的 `think_mode` 字段决定走 FAST 或
SLOW 路径。Thinkdual 子组件（`thinkdual/`）将负责实际的推理调度，
CoreLoopThree 仅负责事件分发与阶段推进。

## 8. 变更历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 0.1.1 | 2026-07-20 | 初始最小可编译实现：kthread + kfifo + 三阶段循环 |
