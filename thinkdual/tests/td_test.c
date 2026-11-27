/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * td_test.c — Thinkdual 决策核心的单元测试
 *
 * 覆盖：Q16 定点解析与格式化、四维加权标定、模式判定、策略解析与取值
 * 校验、热更新与冷更新两条同步路径、计数与账本落盘。
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "td_api.h"

static int fails;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#cond); \
		fails++; \
	} \
} while (0)

static int put(const char *path, const char *text)
{
	FILE *fp = fopen(path, "w");
	int bad;

	if (!fp)
		return -1;
	bad = fputs(text, fp) < 0;
	if (fclose(fp))
		bad = 1;
	return bad ? -1 : 0;
}

static int lines(const char *path)
{
	char buf[512];
	FILE *fp = fopen(path, "r");
	int n = 0;

	if (!fp)
		return -1;
	while (fgets(buf, sizeof(buf), fp))
		n++;
	fclose(fp);
	return n;
}

static void test_q16(void)
{
	int v = 0;

	CHECK(td_parse_q16("0.3", &v) == 0 && v == 19660);
	CHECK(td_parse_q16("1", &v) == 0 && v == 65536);
	CHECK(td_parse_q16("0.92", &v) == 0 && v == 60293);
	CHECK(td_parse_q16("", &v) == -1);
	CHECK(td_parse_q16("abc", &v) == -1);
	CHECK(td_parse_q16("0.3x", &v) == -1);
	CHECK(td_milli(19660) == 300);
	CHECK(td_milli(20643) == 315);
	CHECK(strcmp(td_risk_name(TD_RISK_MED), "medium") == 0);
}

static void test_score(void)
{
	char intent[64];
	struct td_input in;
	int score = 0;

	memset(intent, 'a', 50);
	intent[50] = '\0';
	memset(&in, 0, sizeof(in));
	in.intent = intent;
	in.similarity = 55705;
	in.risk = TD_RISK_MED;
	in.tokens = 1000;
	CHECK(td_score(&in, &score) == 0 && score == 20643);

	in.intent = "";
	in.similarity = AIRY_Q16_ONE;
	in.risk = TD_RISK_HIGH;
	in.tokens = 0;
	CHECK(td_score(&in, &score) == 0 && score == 19660);

	in.risk = TD_RISK_LOW;
	CHECK(td_score(&in, &score) == 0 && score == 0);

	in.intent = intent;
	in.tokens = 100000;
	CHECK(td_score(&in, &score) == 0 && score == 14417);

	in.risk = TD_RISK_MAX;
	CHECK(td_score(&in, &score) == -1);
}

static void test_decide(void)
{
	struct td_policy pol;
	struct td_input in;
	struct td_decision dec;
	char intent[64];

	memset(intent, 'a', 50);
	intent[50] = '\0';
	memset(&in, 0, sizeof(in));
	in.intent = intent;
	in.similarity = 55705;
	in.risk = TD_RISK_MED;
	in.tokens = 1000;
	CHECK(td_pol_default(&pol) == 0);
	CHECK(td_decide(&pol, &in, &dec) == 0);
	CHECK(dec.mode == AIRY_THINK_SLOW && dec.score == 20643);
	CHECK(dec.latency_ms == 200 && !strcmp(dec.reason, "score"));

	pol.s1_enabled = 0;
	CHECK(td_decide(&pol, &in, &dec) == 0);
	CHECK(dec.mode == AIRY_THINK_SLOW);
	CHECK(!strcmp(dec.reason, "system2-only"));

	pol.s1_enabled = 1;
	pol.s2_enabled = 0;
	CHECK(td_decide(&pol, &in, &dec) == 0);
	CHECK(dec.mode == AIRY_THINK_FAST && dec.latency_ms == 10);
	CHECK(!strcmp(dec.reason, "system1-only"));

	pol.s1_enabled = 0;
	CHECK(td_decide(&pol, &in, &dec) == 0);
	CHECK(dec.mode == AIRY_THINK_FAST);
	CHECK(!strcmp(dec.reason, "degraded"));
}

static void test_policy(void)
{
	unsigned long long stamp = 0;
	struct td_policy pol;

	CHECK(td_pol_read(TD_FIXTURE_DIR "/policy.yaml", &pol, &stamp) == 0);
	CHECK(pol.threshold == 19660 && pol.max_latency_ms == 10);
	CHECK(pol.dag_max_depth == 10 && pol.llm_timeout_ms == 200);
	CHECK(pol.cache_ttl_s == 300 && pol.hot_reload == 1 && pol.audit == 1);
	CHECK(td_pol_read(TD_FIXTURE_DIR "/policy_bad_key.yaml", &pol,
			  &stamp) == -2);
	CHECK(td_pol_read(TD_FIXTURE_DIR "/policy_bad_range.yaml", &pol,
			  &stamp) == -2);
	CHECK(td_pol_read(TD_FIXTURE_DIR "/policy_absent.yaml", &pol,
			  &stamp) == -1);
}

static const char cold_high[] =
	"system1:\n  complexity_threshold: 0.5\n"
	"switching:\n  hot_reload: false\n";
static const char cold_low[] =
	"system1:\n  complexity_threshold: 0.1\n"
	"switching:\n  hot_reload: false\n";
static const char warm_high[] =
	"system1:\n  complexity_threshold: 0.5\n"
	"switching:\n  hot_reload: true\n";
static const char warm_low[] =
	"system1:\n  complexity_threshold: 0.1\n"
	"switching:\n  hot_reload: true\n";

static void test_reload(void)
{
	char dir[] = "/tmp/tdreloadXXXXXX";
	char dir2[] = "/tmp/tdwarmXXXXXX";
	char path[TD_PATH_MAX];
	struct td_core core;
	struct td_core again;

	CHECK(mkdtemp(dir) != NULL);
	CHECK(mkdtemp(dir2) != NULL);
	snprintf(path, sizeof(path), "%s/policy.yaml", dir);
	CHECK(put(path, cold_high) == 0);
	CHECK(td_core_open(&core, path, dir) == 0);
	CHECK(core.pol.threshold == 32768 && core.source == TD_SRC_FILE);
	CHECK(put(path, cold_low) == 0);
	CHECK(td_core_sync(&core, path, dir) == 0);
	CHECK(core.pol.threshold == 32768 && core.reloaded == 0);
	CHECK(core.source == TD_SRC_CACHE);

	snprintf(path, sizeof(path), "%s/warm.yaml", dir2);
	CHECK(put(path, warm_high) == 0);
	CHECK(td_core_open(&core, path, dir2) == 0);
	CHECK(core.pol.threshold == 32768 && core.source == TD_SRC_FILE);
	CHECK(put(path, warm_low) == 0);
	CHECK(td_core_sync(&core, path, dir2) == 0);
	CHECK(core.pol.threshold == 6553 && core.reloaded == 1);
	CHECK(core.source == TD_SRC_FILE);
	CHECK(td_core_open(&again, path, dir2) == 0);
	CHECK(again.pol.threshold == 6553 && again.source == TD_SRC_CACHE);
}

static void test_state(void)
{
	char dir[] = "/tmp/tdstateXXXXXX";
	char path[TD_PATH_MAX];
	struct td_counters cnt;
	struct td_counters back;
	struct td_policy pol;
	struct td_input in;
	struct td_decision dec;

	CHECK(mkdtemp(dir) != NULL);
	CHECK(td_state_load(dir, &cnt) == 0);
	CHECK(cnt.decisions == 0 && cnt.last == -1);
	cnt.decisions = 2;
	cnt.slow = 1;
	cnt.fast = 1;
	cnt.last = AIRY_THINK_SLOW;
	CHECK(td_state_save(dir, &cnt) == 0);
	CHECK(td_state_load(dir, &back) == 0);
	CHECK(back.decisions == 2 && back.slow == 1);
	CHECK(back.last == AIRY_THINK_SLOW);

	memset(&in, 0, sizeof(in));
	in.intent = "abc";
	in.similarity = AIRY_Q16_ONE;
	in.risk = TD_RISK_LOW;
	in.tokens = 100;
	CHECK(td_pol_default(&pol) == 0);
	CHECK(td_decide(&pol, &in, &dec) == 0);
	CHECK(dec.mode == AIRY_THINK_FAST);
	CHECK(td_journal_add(dir, &in, &dec) == 0);
	CHECK(td_journal_add(dir, &in, &dec) == 0);
	snprintf(path, sizeof(path), "%s/journal.ndjson", dir);
	CHECK(lines(path) == 2);
}

int main(void)
{
	test_q16();
	test_score();
	test_decide();
	test_policy();
	test_reload();
	test_state();
	if (fails) {
		fprintf(stderr, "td_test: %d 项失败\n", fails);
		return 1;
	}
	printf("td_test: 全部通过\n");
	return 0;
}
