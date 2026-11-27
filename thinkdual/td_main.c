/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * td_main.c — airy-thinkdual 命令行入口
 *
 * 职责：
 *   score  只算复杂度与模式，不落盘；
 *   decide 走完整切换：策略同步、复杂度判定、审计账本与计数落盘；
 *   policy 打印当前生效策略与来源；
 *   status 打印累计决策计数与最近一次模式。
 *
 * 退出码：
 *   0 成功；2 参数错误；3 策略无效；4 状态目录不可写。
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "td_api.h"

static void td_usage(void)
{
	printf("用法: airy-thinkdual <score|decide|policy|status> [选项]\n"
	       "\n"
	       "选项:\n"
	       "  --policy PATH      策略文件（默认 %s）\n"
	       "  --state DIR        状态目录（默认 %s）\n"
	       "  --intent TEXT      意图文本，长度参与复杂度评估\n"
	       "  --similarity DEC   历史相似度 0..1（默认 1）\n"
	       "  --risk LEVEL       low|medium|high（默认 low）\n"
	       "  --tokens N         预估 token 数（默认 0）\n"
	       "\n"
	       "算法约定见 cognition/Documentation/thinkdual.md。\n",
	       TD_POLICY_PATH, TD_STATE_DIR);
}

static const char *td_src_name(int src)
{
	switch (src) {
	case TD_SRC_FILE:
		return "file";
	case TD_SRC_CACHE:
		return "cache";
	default:
		return "defaults";
	}
}

static const char *td_bool(int value)
{
	return value ? "true" : "false";
}

static void td_show(const struct td_policy *pol,
		    const struct td_decision *dec)
{
	int score = td_milli(dec->score);
	int threshold = td_milli(pol->threshold);

	printf("mode=%s score=%d.%03d threshold=%d.%03d latency_ms=%d "
	       "reason=%s\n", td_mode_name(dec->mode), score / 1000,
	       score % 1000, threshold / 1000, threshold % 1000,
	       dec->latency_ms, dec->reason);
}

static int td_opt_tokens(const char *text, unsigned int *out)
{
	unsigned long v;
	char *end = NULL;

	if (*text < '0' || *text > '9')
		return -1;
	errno = 0;
	v = strtoul(text, &end, 10);
	if (errno || !end || *end != '\0' || v > 1000000UL)
		return -1;
	*out = (unsigned int)v;
	return 0;
}

static int td_act_policy(const struct td_core *core)
{
	printf("source=%s system1=%s system2=%s threshold=%d.%03d "
	       "max_latency_ms=%d cache_ttl_s=%d dag_max_depth=%d "
	       "llm_timeout_ms=%d feedback=%s hot_reload=%s audit=%s "
	       "strategy=score_based\n", td_src_name(core->source),
	       td_bool(core->pol.s1_enabled), td_bool(core->pol.s2_enabled),
	       td_milli(core->pol.threshold) / 1000,
	       td_milli(core->pol.threshold) % 1000,
	       core->pol.max_latency_ms, core->pol.cache_ttl_s,
	       core->pol.dag_max_depth, core->pol.llm_timeout_ms,
	       td_bool(core->pol.feedback), td_bool(core->pol.hot_reload),
	       td_bool(core->pol.audit));
	return 0;
}

static int td_act_decide(const struct td_core *core,
			 const struct td_input *in, const char *state)
{
	struct td_counters cnt;
	char journal[TD_PATH_MAX];
	int has_audit = core->pol.audit;
	struct td_decision dec;

	if (td_decide(&core->pol, in, &dec) < 0)
		return TD_EXIT_USE;
	if (has_audit && td_journal_add(state, in, &dec))
		return TD_EXIT_STATE;
	if (td_state_load(state, &cnt))
		return TD_EXIT_STATE;
	cnt.decisions++;
	if (dec.mode == AIRY_THINK_SLOW)
		cnt.slow++;
	else
		cnt.fast++;
	cnt.last = dec.mode;
	if (td_state_save(state, &cnt))
		return TD_EXIT_STATE;
	td_show(&core->pol, &dec);
	if (has_audit) {
		snprintf(journal, sizeof(journal), "%s/journal.ndjson", state);
		printf("reloaded=%s journal=%s\n",
		       core->reloaded ? "yes" : "no", journal);
	} else {
		printf("reloaded=%s journal=off\n",
		       core->reloaded ? "yes" : "no");
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *policy = TD_POLICY_PATH;
	const char *state = TD_STATE_DIR;
	const char *intent = "";
	const char *action;
	struct td_input in;
	struct td_core core;
	int similarity = AIRY_Q16_ONE;
	int risk = TD_RISK_LOW;
	unsigned int tokens = 0;
	int i;
	int rc;

	if (argc < 2) {
		td_usage();
		return TD_EXIT_USE;
	}
	action = argv[1];
	if (!strcmp(action, "-h") || !strcmp(action, "--help")) {
		td_usage();
		return 0;
	}
	if (strcmp(action, "score") && strcmp(action, "decide") &&
	    strcmp(action, "policy") && strcmp(action, "status")) {
		fprintf(stderr, "未知动作: %s\n", action);
		return TD_EXIT_USE;
	}
	for (i = 2; i < argc; i++) {
		const char *opt = argv[i];
		int bad = 0;

		if (!strcmp(opt, "-h") || !strcmp(opt, "--help")) {
			td_usage();
			return 0;
		}
		if (i + 1 >= argc) {
			fprintf(stderr, "选项缺参数: %s\n", opt);
			return TD_EXIT_USE;
		}
		if (!strcmp(opt, "--policy"))
			policy = argv[++i];
		else if (!strcmp(opt, "--state"))
			state = argv[++i];
		else if (!strcmp(opt, "--intent"))
			intent = argv[++i];
		else if (!strcmp(opt, "--similarity"))
			bad = td_parse_q16(argv[++i], &similarity) ||
			      similarity < 0 || similarity > AIRY_Q16_ONE;
		else if (!strcmp(opt, "--risk"))
			bad = td_risk_parse(argv[++i], &risk);
		else if (!strcmp(opt, "--tokens"))
			bad = td_opt_tokens(argv[++i], &tokens);
		else
			bad = 1;
		if (bad) {
			fprintf(stderr, "参数无效: %s\n", opt);
			return TD_EXIT_USE;
		}
	}
	if (!strcmp(action, "status")) {
		if (td_status_print(state))
			return TD_EXIT_STATE;
		return 0;
	}
	rc = td_core_open(&core, policy,
			  strcmp(action, "decide") ? NULL : state);
	if (rc == -2) {
		fprintf(stderr, "策略文件无效: %s\n", policy);
		return TD_EXIT_CFG;
	}
	if (!strcmp(action, "policy"))
		return td_act_policy(&core);
	in.intent = intent;
	in.similarity = similarity;
	in.risk = risk;
	in.tokens = tokens;
	if (!strcmp(action, "score")) {
		struct td_decision dec;

		if (td_decide(&core.pol, &in, &dec) < 0)
			return TD_EXIT_USE;
		td_show(&core.pol, &dec);
		return 0;
	}
	return td_act_decide(&core, &in, state);
}
