/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cf_api.h — 认知数据流的对外契约
 *
 * 职责：
 *   在用户态承载 CoreLoopThree 的三相循环与 System 1/2 分支：感知定界、
 *   规划与守卫、执行与补偿、记忆追加与反馈。相位直接引用 [SC] 契约头的
 *   enum airy_cog_phase，推理模式由 Thinkdual 决策给出，本模块不复制任何
 *   契约取值。
 *
 * 事件契约：
 *   所有状态变迁写 NDJSON 账本，一行一个事件，字段见 cf_event_put()。
 *   事件是审计与回放的事实来源，账本只追加、不改写。
 */

#ifndef AIRY_COGFLOW_API_H
#define AIRY_COGFLOW_API_H

#include <stddef.h>
#include <stdio.h>

#include <linux/airymax/cognition_types.h>

#include "td_api.h"

/* 单个任务的规划上限；超出即拒绝，不做静默截断。 */
#define CF_NODE_MAX	64
#define CF_NAME_MAX	32
#define CF_TRACE_LEN	17

#define CF_STATE_DIR	"/run/airymax/cognition-flow"
#define CF_EVENT_NAME	"events.ndjson"
#define CF_MEM_NAME	"memory.ndjson"
#define CF_COUNT_NAME	"flow-state"

/* 退出码：参数、策略、状态、规划、守卫、深度与执行各自可归因。 */
#define CF_EXIT_USE	2
#define CF_EXIT_CFG	3
#define CF_EXIT_STATE	4
#define CF_EXIT_PLAN	5
#define CF_EXIT_DENY	6
#define CF_EXIT_DEPTH	7
#define CF_EXIT_EXEC	8

enum cf_guard {
	CF_GUARD_ALLOW	= 0,
	CF_GUARD_DENY	= 1
};

enum cf_outcome {
	CF_EXEC_OK	= 0,
	CF_EXEC_FAIL	= 1
};

/* 规划中的一个节点：名称、层级、守卫结论与预期执行结果。 */
struct cf_node {
	char	name[CF_NAME_MAX];
	int	depth;
	int	guard;
	int	outcome;
};

/* 一次规划；depth 是节点层级的最大值，与策略的 dag_max_depth 比较。 */
struct cf_plan {
	struct cf_node	node[CF_NODE_MAX];
	int		count;
	int		depth;
};

/* 单条事件。seq、trace、task、mode、score 与 budget 取自运行态。 */
struct cf_event {
	const char	*type;
	const char	*phase;
	const char	*node;
	const char	*result;
	int		depth;
	int		count;
	int		attempt;
};

/* 数据面累计量，供 status 读取。last 为 0 成功、1 中止、-1 未运行。 */
struct cf_counters {
	unsigned long	flows;
	unsigned long	fast;
	unsigned long	slow;
	unsigned long	nodes;
	unsigned long	repairs;
	unsigned long	aborts;
	int		last;
};

/* 一次运行的上下文：决策、规划、事件序号与追踪标识。 */
struct cf_ctx {
	struct td_core		core;
	struct td_decision	dec;
	struct cf_plan		plan;
	struct cf_plan		replan;
	int			have_plan;
	int			have_replan;
	char			trace[CF_TRACE_LEN];
	char			task[CF_NAME_MAX];
	unsigned long long	seq;
};

int cf_plan_read(const char *path, struct cf_plan *plan);
int cf_guard_scan(const struct cf_plan *plan, int *index);
void cf_trace_gen(char *out, size_t len);
void cf_event_put(FILE *ev, struct cf_ctx *ctx,
		  const struct cf_event *event);
int cf_counters_get(const char *state, struct cf_counters *cnt);
int cf_counters_put(const char *state, const struct cf_counters *cnt);
int cf_memory_add(const char *state, const struct cf_ctx *ctx, int nodes);
int cf_flow_run(struct cf_ctx *ctx, FILE *ev, struct cf_counters *cnt,
		const char *state);
int cf_status_show(const char *state);

#endif /* AIRY_COGFLOW_API_H */
