/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cf_main.c — airy-cogflow 命令行入口
 *
 * 职责：
 *   run    提交一次任务：策略同步、复杂度判定、三相循环与事件落盘；
 *   status 打印累计流次数、模式分布、执行节点、补偿与中止计数。
 *
 * 退出码：
 *   0 成功；2 参数错误；3 策略无效；4 状态目录不可写；
 *   5 规划缺失或无效；6 守卫拒绝；7 规划过深；8 执行失败。
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cf_api.h"

static void cf_usage(void)
{
	printf("用法: airy-cogflow <run|status> [选项]\n"
	       "\n"
	       "选项:\n"
	       "  --policy PATH      策略文件（默认 %s）\n"
	       "  --state DIR        状态目录（默认 %s）\n"
	       "  --plan FILE        System 2 规划文件\n"
	       "  --replan FILE      失败后的备用规划\n"
	       "  --task NAME        任务标识（默认 task）\n"
	       "  --intent TEXT      意图文本，长度参与复杂度评估\n"
	       "  --similarity DEC   历史相似度 0..1（默认 1）\n"
	       "  --risk LEVEL       low|medium|high（默认 low）\n"
	       "  --tokens N         预估 token 数（默认 0）\n"
	       "\n"
	       "事件契约见 cognition/Documentation/cognition-flow.md。\n",
	       TD_POLICY_PATH, CF_STATE_DIR);
}

static int cf_task_ok(const char *task)
{
	size_t len = strlen(task);
	size_t i;

	if (len == 0 || len >= CF_NAME_MAX)
		return 0;
	for (i = 0; i < len; i++) {
		char c = task[i];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_' || c == '-' ||
		    c == '.')
			continue;
		return 0;
	}
	return 1;
}

static int cf_opt_tokens(const char *text, unsigned int *out)
{
	unsigned long value;
	char *end = NULL;

	if (*text < '0' || *text > '9')
		return -1;
	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || !end || *end != '\0' || value > 1000000UL)
		return -1;
	*out = (unsigned int)value;
	return 0;
}

static int cf_state_open(const char *state, FILE **ev)
{
	char path[TD_PATH_MAX];

	if (mkdir(state, 0755) && errno != EEXIST)
		return -1;
	snprintf(path, sizeof(path), "%s/%s", state, CF_EVENT_NAME);
	*ev = fopen(path, "a");
	return *ev ? 0 : -1;
}

static const char *cf_result(int rc)
{
	switch (rc) {
	case 0:
		return "ok";
	case CF_EXIT_DENY:
		return "permission";
	case CF_EXIT_DEPTH:
		return "depth";
	case CF_EXIT_STATE:
		return "state";
	default:
		return "retry-exhausted";
	}
}

int main(int argc, char **argv)
{
	const char *policy = TD_POLICY_PATH;
	const char *state = CF_STATE_DIR;
	const char *plan = NULL;
	const char *replan = NULL;
	const char *task = "task";
	const char *intent = "";
	const char *action;
	struct td_input in;
	struct cf_counters cnt;
	struct cf_ctx ctx;
	FILE *ev = NULL;
	unsigned long before;
	char events[TD_PATH_MAX];
	int similarity = AIRY_Q16_ONE;
	int risk = TD_RISK_LOW;
	unsigned int tokens = 0;
	int i;
	int rc;

	if (argc < 2) {
		cf_usage();
		return CF_EXIT_USE;
	}
	action = argv[1];
	if (!strcmp(action, "-h") || !strcmp(action, "--help")) {
		cf_usage();
		return 0;
	}
	if (strcmp(action, "run") && strcmp(action, "status")) {
		fprintf(stderr, "未知动作: %s\n", action);
		return CF_EXIT_USE;
	}
	for (i = 2; i < argc; i++) {
		const char *opt = argv[i];
		int bad = 0;

		if (!strcmp(opt, "-h") || !strcmp(opt, "--help")) {
			cf_usage();
			return 0;
		}
		if (i + 1 >= argc) {
			fprintf(stderr, "选项缺参数: %s\n", opt);
			return CF_EXIT_USE;
		}
		if (!strcmp(opt, "--policy"))
			policy = argv[++i];
		else if (!strcmp(opt, "--state"))
			state = argv[++i];
		else if (!strcmp(opt, "--plan"))
			plan = argv[++i];
		else if (!strcmp(opt, "--replan"))
			replan = argv[++i];
		else if (!strcmp(opt, "--task")) {
			task = argv[++i];
			bad = !cf_task_ok(task);
		} else if (!strcmp(opt, "--intent"))
			intent = argv[++i];
		else if (!strcmp(opt, "--similarity"))
			bad = td_parse_q16(argv[++i], &similarity) ||
			      similarity < 0 || similarity > AIRY_Q16_ONE;
		else if (!strcmp(opt, "--risk"))
			bad = td_risk_parse(argv[++i], &risk);
		else if (!strcmp(opt, "--tokens"))
			bad = cf_opt_tokens(argv[++i], &tokens);
		else
			bad = 1;
		if (bad) {
			fprintf(stderr, "参数无效: %s\n", opt);
			return CF_EXIT_USE;
		}
	}
	if (!strcmp(action, "status"))
		return cf_status_show(state) ? CF_EXIT_STATE : 0;

	memset(&ctx, 0, sizeof(ctx));
	rc = td_core_open(&ctx.core, policy, state);
	if (rc == -2) {
		fprintf(stderr, "策略文件无效: %s\n", policy);
		return CF_EXIT_CFG;
	}
	in.intent = intent;
	in.similarity = similarity;
	in.risk = risk;
	in.tokens = tokens;
	if (td_decide(&ctx.core.pol, &in, &ctx.dec) < 0)
		return CF_EXIT_USE;
	snprintf(ctx.task, sizeof(ctx.task), "%s", task);
	cf_trace_gen(ctx.trace, sizeof(ctx.trace));

	if (!plan && ctx.dec.mode == AIRY_THINK_SLOW) {
		fprintf(stderr, "System 2 需要 --plan\n");
		return CF_EXIT_PLAN;
	}
	if (plan) {
		rc = cf_plan_read(plan, &ctx.plan);
		if (rc) {
			fprintf(stderr, "规划无效: %s\n", plan);
			return CF_EXIT_PLAN;
		}
		ctx.have_plan = 1;
	}
	if (replan) {
		rc = cf_plan_read(replan, &ctx.replan);
		if (rc) {
			fprintf(stderr, "备用规划无效: %s\n", replan);
			return CF_EXIT_PLAN;
		}
		ctx.have_replan = 1;
	}
	if (cf_counters_get(state, &cnt))
		return CF_EXIT_STATE;
	if (cf_state_open(state, &ev))
		return CF_EXIT_STATE;
	before = cnt.nodes;
	rc = cf_flow_run(&ctx, ev, &cnt, state);
	fclose(ev);
	if (cf_counters_put(state, &cnt))
		return CF_EXIT_STATE;
	snprintf(events, sizeof(events), "%s/%s", state, CF_EVENT_NAME);
	printf("trace=%s mode=%s score=%d.%03d nodes=%lu result=%s "
	       "events=%s\n", ctx.trace, td_mode_name(ctx.dec.mode),
	       td_milli(ctx.dec.score) / 1000, td_milli(ctx.dec.score) % 1000,
	       cnt.nodes - before, cf_result(rc), events);
	return rc;
}
