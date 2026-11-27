# Thinkdual — 双系统思考切换

Thinkdual 让认知循环在 System 1（快模式，模式识别）与 System 2（慢模式，
深度推理）之间按任务复杂度自动切换。本目录交付用户态决策核心与
`airy-thinkdual` 命令行；按 `40-dataflows/01-cognition-flow.md` §9.4，
agent-linux 上 Cognitio 的主路径是用户态进程，内核 kthread 只作可选加速，
因此切换逻辑不放进内核。

## 复杂度评估

四个维度统一归一化到 Q16.16 定点，再按 2:3:3:2 加权：

| 维度 | 归一化 | 权重 |
|------|--------|------|
| 意图长度 | `len / 500`，超过 500 记为 1 | 0.2 |
| 历史相似度缺口 | `1 - similarity` | 0.3 |
| 风险等级 | low=0、medium=0.5、high=1.0 | 0.3 |
| 预估 token | `tokens / 2000`，超过 2000 记为 1 | 0.2 |

```
score = (2*len + 3*(1-sim) + 3*risk + 2*tokens) / 10
```

全程整数运算（`td_score.c`），与内核侧 IRON-9 §2.1 的无浮点约束同构。

标定依据设计文档给出的 System 2 边界（50 字符、相似度 0.85、中风险、
1000 token）：四项同时抵达边界时 `score = 0.315`，刚好越过默认阈值
`0.3`；高风险单独贡献 `0.300`，即达到阈值。阈值可配置，因此边界值
本身不是硬编码判定。

## 模式判定

| 条件 | 模式 | `reason` |
|------|------|----------|
| 两者均启用，`score >= threshold` | SLOW | `score` |
| 两者均启用，`score < threshold` | FAST | `score` |
| 只启用 System 2 | SLOW | `system2-only` |
| 只启用 System 1 | FAST | `system1-only` |
| 两者均停用 | FAST | `degraded` |

两者均停用时退到 FAST，与 `[DSL]` 降级层「Thinkdual 塌缩到 FAST」一致。
时延预算取自策略：FAST 用 `max_latency_ms`，SLOW 用 `llm_timeout_ms`。

推理模式直接引用 `[SC]` 契约头
`kernel/include/uapi/linux/airymax/cognition_types.h` 的
`enum airy_think_mode`，本目录不复制契约取值（OS-IRON-014）。用户态
构建用 `-idirafter <kernel>/include/uapi`，使 `linux/types.h` 仍取自
系统 UAPI，只有 `linux/airymax` 目录取自已导出的内核头，避免内核
`types.h` 与 libc 的 `loff_t`/`blkcnt_t` 冲突。

## 策略文件与热更新

默认路径 `/etc/agentrt/cognition_policy.yaml`，只接受文档化的子集：

```yaml
system1:
  enabled: true
  complexity_threshold: 0.3
  max_latency_ms: 10
  cache_ttl_s: 300
system2:
  enabled: true
  dag_max_depth: 10
  llm_timeout_ms: 200
  feedback_enabled: true
switching:
  strategy: "score_based"
  hot_reload: true
  audit: true
```

未知键、未知节、越界取值、非 `score_based` 的策略一律拒绝（退出码 3），
不做「猜一个」的宽容解析。文件缺失时使用内置默认值，保证装机即可用。

每次切换前用 FNV-1a 64 位指纹比对策略文件：

* `hot_reload: true` —— 指纹变化即重新解析并生效，下一次切换用新阈值；
* `hot_reload: false` —— 指纹变化被忽略，继续用状态目录里的
  `policy.cache`，正在运行的任务不受替换影响（FR-050）；
* 指纹相同 —— 直接用缓存，不重复解析。

## 命令行

```
airy-thinkdual score  [--intent T] [--similarity D] [--risk R] [--tokens N]
airy-thinkdual decide [同上] [--policy P] [--state D]
airy-thinkdual policy [--policy P]
airy-thinkdual status [--state D]
```

`score` 只算分不落盘；`decide` 走完整切换并追加审计账本；`policy` 打印
生效策略与来源；`status` 打印累计计数与最近一次模式。

输出示例：

```
mode=slow score=0.315 threshold=0.300 latency_ms=200 reason=score
reloaded=no journal=/run/airymax/thinkdual/journal.ndjson
```

## 状态与审计

状态目录默认 `/run/airymax/thinkdual`：

* `state` —— `decisions`/`fast`/`slow`/`last` 计数，原子替换；
* `policy.cache` —— 最近一次生效策略与文件指纹，供冷更新路径回退；
* `journal.ndjson` —— `audit` 为真时逐次追加，记录模式、原因、分数、
  意图长度、相似度、风险与 token 数；不落意图原文。

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 成功 |
| 2 | 参数错误 |
| 3 | 策略文件无效 |
| 4 | 状态目录不可写 |

## 与早期设计的关系

早期设计占位提出 `airy_td_fast_classify()`、`airy_td_slow_deliberate()`
与 `airy_td_route()` 三个接口。0.0.5 把它们收敛到一个 `td_decide()`：
`score` 给出复杂度，`decision.mode` 即分类与路由的结果，慢模式的多轮
推理由 LLM 调度面按 `llm_timeout_ms` 执行。Thinkdual 只给模式与时延
预算，不代执行推理，因此分类、路由与执行三处不会各自演化出阈值。

## 不做的事

* 不实现推理本身：FAST/SLOW 只是模式判定与时延预算，具体推理由 LLM
  调度面承接。
* 不做跨任务的状态迁移：切换按调用生效，符合「切换不影响运行任务」。
* 不把策略写回内核：阈值与开关是用户态配置，内核侧 `[SC]` 枚举保持
  只读契约。
