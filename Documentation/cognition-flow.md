# Cognition Flow — 三相循环与双系统分支

`airy-cogflow` 把认知循环从「模式判定」推进到「一次任务真的走完」：
感知、思考、行动三相按序推进，System 1 走单节点快路径，System 2
依次完成规划、深度与权限守卫、执行、补偿、至多重试一次、记忆与反馈。
模式与复杂度不自立阈值，直接复用 Thinkdual 的 `td_decide()`；本模块只
决定拿到模式之后做什么。

## 三相循环

| 相位 | 事件 | 职责 |
|------|------|------|
| `percept` | `submit` | 接收意图、分配 trace、记下任务名 |
| `think` | `plan` / `guard` / `replan` | 读取规划、校验深度、扫描守卫 |
| `act` | `exec` / `compensate` / `done` | 逐节点执行、失败回滚、收尾 |

每次运行都以 `submit` 开头、以 `done` 或 `abort` 结尾；两者之间的事件
顺序即事实，不另存状态机快照。

## System 1 快路径

复杂度低于阈值时只做一轮：`percept` 起止、`act` 起点、一个名为
`respond` 的执行节点、`act` 终点，随后追加一条 L1 记忆并记 `done`。
快路径固定一个节点，不读规划文件；`--plan` 在此模式下只被忽略。

## System 2 慢路径

1. `plan` 事件记下规划的最大深度与节点数；
2. 深度超过策略 `dag_max_depth` 直接中止（`depth`）；
3. 守卫扫描遇到 `deny` 节点先记一条 `guard deny`，再中止
   （`permission`），不执行任何节点；
4. 逐节点发 `guard allow` 与 `exec ok|fail`；失败的节点不记入已完成表；
5. 失败后逆序对已完成节点发 `compensate reverted`，每条计一次修复；
6. 有备用规划则发 `replan` 并以 `attempt=2` 再执行一次；备用规划也
   失败时先补偿再中止（`retry-exhausted`）；
7. 成功后按策略 `feedback_enabled` 记 `feedback`，追加 L1 记忆并记
   `memory`，最后 `done`。

SLOW 且既未给 `--plan` 也未给可解析的规划时退 5；FAST 不需要规划。

## 规划文件

纯文本，一行一个节点，四列以空白分隔：

```
<名称> <深度> <allow|deny> <ok|fail>
```

名称为 1–31 字节，限字母、数字与 `_`、`-`、`.`；深度 1–64；后两列是
守卫结论与预期执行结果。空行与 `#` 开头的注释行忽略。整份规划的深度
取所有节点的最大值。解析失败、行超长或没有任何节点都视为无效规划
（退出码 5），不做「跳过坏行继续跑」的宽容解析。

`deny` 与 `fail` 不是策略配置，而是规划自身携带的声明：守卫结论属于
规划产物，执行预期用于让补偿与重试分支可被确定性复算。

## 事件账本

每次运行向 `<state>/events.ndjson` 追加一行 JSON，字段固定：

| 字段 | 含义 |
|------|------|
| `v` | 账本版本，当前为 1 |
| `seq` | 本进程内自增序号，跨运行连续 |
| `trace` | 16 位十六进制追踪号 |
| `task` | 任务名（白名单字符） |
| `type` | `submit`/`phase_begin`/`phase_end`/`plan`/`guard`/`exec`/`compensate`/`replan`/`feedback`/`memory`/`done`/`abort` |
| `mode` | `fast` 或 `slow` |
| `phase` | `percept`/`think`/`act`，无相位事件为空串 |
| `node` | 节点名或 `respond` |
| `result` | `ok`/`fail`/`allow`/`deny`/`reverted`/`depth`/`permission`/`state`/`retry-exhausted` |
| `depth` | 规划深度或节点深度 |
| `count` | 规划节点数或已完成节点数 |
| `attempt` | 第几次尝试，重试路径为 2 |
| `score` | 复杂度，保留三位小数 |
| `budget_ms` | 本次模式的时延预算 |
| `ts_ms` | 墙钟毫秒 |

账本只落结构化的类型与计数：`--intent` 原文不写入事件，落地边界与
Thinkdual 的审计面一致。

## 记忆与计数

* `<state>/memory.ndjson` —— 追加 `trace`/`task`/`layer=l1`/`nodes`/`seq`，
  写后 `fsync`；L1 只追加，不提供修改入口。写入失败按状态错误中止。
* `<state>/flow-state` —— `flows`/`fast`/`slow`/`nodes`/`repairs`/`aborts`
  与 `last=ok|aborted|none`，经 Thinkdual 的原子替换写入。

`nodes` 累计实际执行过的节点数，`repairs` 累计补偿次数，`aborts` 累计
未成功收尾的运行数；`last` 只表达最近一次是否成功。

## 命令行

```
airy-cogflow run    [--policy P] [--state D] [--plan F] [--replan F]
                    [--task T] [--intent X] [--similarity D]
                    [--risk R] [--tokens N]
airy-cogflow status [--state D]
```

`run` 先经 Thinkdual 判定模式，再执行对应路径并打印一行结论：

```
trace=7e656ee699d28a32 mode=slow score=0.300 nodes=2 result=ok events=/run/airymax/cognition-flow/events.ndjson
```

`status` 打印累计计数；缺状态目录回零，不视为错误。

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 成功 |
| 2 | 参数错误 |
| 3 | 策略文件无效 |
| 4 | 状态目录或计数不可写 |
| 5 | 规划缺失或无效 |
| 6 | 守卫拒绝 |
| 7 | 规划深度超限 |
| 8 | 执行失败且无可用重试 |

分类归因让调用方无需解析账本就能决定重试、提权还是终止。

## 与 Thinkdual 的关系

`cf_api.h` 引入 `td_api.h` 取得策略、决策与计数器工具；SLOW/FAST 由
`td_decide()` 给定，`dag_max_depth`、`feedback_enabled`、`llm_timeout_ms`
也都从同一份策略读取。本模块不新增阈值、不解析第二份策略，避免模式
判定与执行编排在边界上漂移。构建时以 `add_subdirectory` 复用 `td`
静态库，契约头仍只经 `-idirafter <kernel>/include/uapi` 引入。

## 不做的事

* 不调用 LLM、不执行 Wasm：执行单元是账本里声明的节点，真实执行器
  在框架面接入，本模块保证编排、审计与补偿顺序成立。
* 不做 DAG 扩展：规划是输入，增量规划属于更上层的规划器。
* 不把 L1 记忆下沉为 L2–L4：卷载由 memory 面负责，这里只交付不可变
  的追加记录。
