/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cf_loop.c — CoreLoopThree 三相循环与 System 1/2 分支
 *
 * 流程：
 *   FAST 感知定界后直接执行一个响应单元，不建规划；
 *   SLOW 感知后规划、查深度、过守卫，再逐节点执行；节点失败时按已完成
 *   节点的逆序补偿，有备用规划则重试一次，仍失败即中止并留痕。
 *   成功路径追加 L1 记忆，反馈事件由策略开关决定。
 *
 * 相位取值来自 [SC] 契约头的 enum airy_cog_phase，本文件不另立枚举。
 */

#include <string.h>

#include "cf_api.h"

static void cf_put(struct cf_ctx *ctx, FILE *ev, const char *type,
		   enum airy_cog_phase phase, const char *node,
		   const char *result, int depth, int count, int attempt)
{
	static const char *name[] = { "percept", "think", "act" };
	struct cf_event event;

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.phase = name[phase];
	event.node = node;
	event.result = result;
	event.depth = depth;
	event.count = count;
	event.attempt = attempt;
	cf_event_put(ev, ctx, &event);
}

static void cf_bare(struct cf_ctx *ctx, FILE *ev, const char *type,
		    const char *result, int count, int attempt)
{
	struct cf_event event;

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.phase = "";
	event.node = "";
	event.result = result;
	event.count = count;
	event.attempt = attempt;
	cf_event_put(ev, ctx, &event);
}

static int cf_failed(struct cf_ctx *ctx, FILE *ev, const char *reason)
{
	cf_put(ctx, ev, "abort", AIRY_COG_ACT, "", reason, 0, 0, 0);
	if (!strcmp(reason, "permission"))
		return CF_EXIT_DENY;
	if (!strcmp(reason, "depth"))
		return CF_EXIT_DEPTH;
	if (!strcmp(reason, "state"))
		return CF_EXIT_STATE;
	return CF_EXIT_EXEC;
}

static int cf_attempt(struct cf_ctx *ctx, FILE *ev,
		      const struct cf_plan *plan, int attempt,
		      int *done, int *count, int *executed)
{
	int i;

	*executed = 0;
	for (i = 0; i < plan->count; i++) {
		int fail = plan->node[i].outcome == CF_EXEC_FAIL;

		cf_put(ctx, ev, "guard", AIRY_COG_ACT, plan->node[i].name,
		       "allow", plan->node[i].depth, 0, attempt);
		cf_put(ctx, ev, "exec", AIRY_COG_ACT, plan->node[i].name,
		       fail ? "fail" : "ok", plan->node[i].depth, 0,
		       attempt);
		(*executed)++;
		if (fail)
			return 1;
		done[(*count)++] = i;
	}
	return 0;
}

static void cf_undo(struct cf_ctx *ctx, FILE *ev,
		    const struct cf_plan *plan, const int *done, int count,
		    int attempt, int *repairs)
{
	int i;

	for (i = count - 1; i >= 0; i--) {
		cf_put(ctx, ev, "compensate", AIRY_COG_ACT,
		       plan->node[done[i]].name, "reverted",
		       plan->node[done[i]].depth, 0, attempt);
		(*repairs)++;
	}
}

static int cf_plan_check(struct cf_ctx *ctx, FILE *ev,
			 const struct cf_plan *plan)
{
	int index = 0;

	if (plan->depth > ctx->core.pol.dag_max_depth)
		return cf_failed(ctx, ev, "depth");
	if (cf_guard_scan(plan, &index)) {
		cf_put(ctx, ev, "guard", AIRY_COG_THINK,
		       plan->node[index].name, "deny",
		       plan->node[index].depth, 0, 1);
		return cf_failed(ctx, ev, "permission");
	}
	return 0;
}

static int cf_retry(struct cf_ctx *ctx, FILE *ev, int *executed,
		    int *repairs)
{
	int done[CF_NODE_MAX];
	int count = 0;
	int rc;

	cf_put(ctx, ev, "replan", AIRY_COG_THINK, "", "ok",
	       ctx->replan.depth, ctx->replan.count, 2);
	rc = cf_plan_check(ctx, ev, &ctx->replan);
	if (rc)
		return rc;
	if (cf_attempt(ctx, ev, &ctx->replan, 2, done, &count, executed)) {
		cf_undo(ctx, ev, &ctx->replan, done, count, 2, repairs);
		return cf_failed(ctx, ev, "retry-exhausted");
	}
	return 0;
}

static int cf_fast(struct cf_ctx *ctx, FILE *ev, const char *state)
{
	cf_put(ctx, ev, "phase_begin", AIRY_COG_PERCEPT, "", "", 0, 0, 1);
	cf_put(ctx, ev, "phase_end", AIRY_COG_PERCEPT, "", "", 0, 0, 1);
	cf_put(ctx, ev, "phase_begin", AIRY_COG_ACT, "", "", 0, 0, 1);
	cf_put(ctx, ev, "exec", AIRY_COG_ACT, "respond", "ok", 1, 0, 1);
	cf_put(ctx, ev, "phase_end", AIRY_COG_ACT, "", "", 0, 0, 1);
	if (cf_memory_add(state, ctx, 1))
		return cf_failed(ctx, ev, "state");
	cf_bare(ctx, ev, "memory", "l1", 1, 1);
	cf_put(ctx, ev, "done", AIRY_COG_ACT, "", "ok", 0, 1, 1);
	return 0;
}

static int cf_slow(struct cf_ctx *ctx, FILE *ev, const char *state,
		   struct cf_counters *cnt)
{
	int done[CF_NODE_MAX];
	int count = 0;
	int executed = 0;
	int repairs = 0;
	int attempt = 1;
	int rc;

	cf_put(ctx, ev, "phase_begin", AIRY_COG_PERCEPT, "", "", 0, 0, 1);
	cf_put(ctx, ev, "phase_end", AIRY_COG_PERCEPT, "", "", 0, 0, 1);
	cf_put(ctx, ev, "phase_begin", AIRY_COG_THINK, "", "", 0, 0, 1);
	cf_put(ctx, ev, "plan", AIRY_COG_THINK, "", "ok", ctx->plan.depth,
	       ctx->plan.count, 1);
	rc = cf_plan_check(ctx, ev, &ctx->plan);
	if (rc)
		goto out;
	cf_put(ctx, ev, "phase_end", AIRY_COG_THINK, "", "", 0, 0, 1);
	cf_put(ctx, ev, "phase_begin", AIRY_COG_ACT, "", "", 0, 0, 1);
	if (cf_attempt(ctx, ev, &ctx->plan, 1, done, &count, &executed)) {
		cf_undo(ctx, ev, &ctx->plan, done, count, 1, &repairs);
		if (!ctx->have_replan) {
			rc = cf_failed(ctx, ev, "retry-exhausted");
			goto out;
		}
		rc = cf_retry(ctx, ev, &executed, &repairs);
		if (rc)
			goto out;
		attempt = 2;
	}
	cf_put(ctx, ev, "phase_end", AIRY_COG_ACT, "", "", 0, 0, attempt);
	if (ctx->core.pol.feedback)
		cf_bare(ctx, ev, "feedback", "ok", executed, attempt);
	if (cf_memory_add(state, ctx, executed)) {
		rc = cf_failed(ctx, ev, "state");
		goto out;
	}
	cf_bare(ctx, ev, "memory", "l1", executed, attempt);
	cf_put(ctx, ev, "done", AIRY_COG_ACT, "", "ok", 0, executed, attempt);
	rc = 0;
out:
	cnt->nodes += executed;
	cnt->repairs += repairs;
	return rc;
}

int cf_flow_run(struct cf_ctx *ctx, FILE *ev, struct cf_counters *cnt,
		const char *state)
{
	int rc;

	cf_put(ctx, ev, "submit", AIRY_COG_PERCEPT, "", "ok",
	       ctx->have_plan ? ctx->plan.depth : 0,
	       ctx->have_plan ? ctx->plan.count : 0, 1);
	cnt->flows++;
	if (ctx->dec.mode == AIRY_THINK_SLOW) {
		cnt->slow++;
		rc = cf_slow(ctx, ev, state, cnt);
	} else {
		cnt->fast++;
		rc = cf_fast(ctx, ev, state);
		if (!rc)
			cnt->nodes++;
	}
	cnt->last = rc ? 1 : 0;
	if (rc)
		cnt->aborts++;
	return rc;
}
