/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cf_test.c — 规划解析、事件账本与三相循环的回归用例
 *
 * 覆盖：
 *   规划读取的合法与非法输入、守卫扫描；
 *   计数器的读写往返；
 *   FAST 一节点、SLOW 三节点、深度中止、权限中止、补偿后重试与无备用
 *   规划的执行中止；每次运行后校验事件账本与 L1 记忆的关键字段。
 *
 * 不覆盖 CLI 参数解析：那部分由门禁按命令行调用逐条断言，重复实现只会
 * 让两处判定在边界上分叉。
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cf_api.h"

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,	\
			#cond);						\
		return 1;						\
	}								\
} while (0)

static int fixture(const char *name, char *out, size_t cap)
{
	int n = snprintf(out, cap, "%s/%s", CF_FIXTURE_DIR, name);

	return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

static int make_dir(const char *root, const char *name, char *out,
		    size_t cap)
{
	int n = snprintf(out, cap, "%s/%s", root, name);

	if (n < 0 || (size_t)n >= cap)
		return -1;
	return mkdir(out, 0755) ? -1 : 0;
}

static char *slurp(const char *path)
{
	FILE *fp = fopen(path, "r");
	long size;
	char *buf;

	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) || (size = ftell(fp)) < 0 ||
	    fseek(fp, 0, SEEK_SET)) {
		fclose(fp);
		return NULL;
	}
	buf = malloc((size_t)size + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
		free(buf);
		fclose(fp);
		return NULL;
	}
	buf[size] = '\0';
	fclose(fp);
	return buf;
}

static int hit(const char *path, const char *needle)
{
	char *text = slurp(path);
	int found = text && strstr(text, needle);

	free(text);
	return found != 0;
}

static int lines(const char *path)
{
	FILE *fp = fopen(path, "r");
	int count = 0;
	int c;

	if (!fp)
		return -1;
	while ((c = fgetc(fp)) != EOF)
		if (c == '\n')
			count++;
	fclose(fp);
	return count;
}

static void ctx_init(struct cf_ctx *ctx, int mode)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->dec.mode = (enum airy_think_mode)mode;
	ctx->core.pol.dag_max_depth = 10;
	ctx->core.pol.feedback = 1;
	snprintf(ctx->task, sizeof(ctx->task), "%s", "demo");
	cf_trace_gen(ctx->trace, sizeof(ctx->trace));
}

static int t_plan(void)
{
	char path[512];
	struct cf_plan plan;
	int index = -1;

	CHECK(!fixture("plan.yaml", path, sizeof(path)));
	CHECK(!cf_plan_read(path, &plan));
	CHECK(plan.count == 3 && plan.depth == 2);
	CHECK(!strcmp(plan.node[1].name, "transform"));
	CHECK(plan.node[1].depth == 2 && !plan.node[1].guard);

	CHECK(!fixture("plan_denied.yaml", path, sizeof(path)));
	CHECK(!cf_plan_read(path, &plan));
	CHECK(cf_guard_scan(&plan, &index) == 1 && index == 1);

	CHECK(!fixture("plan_deep.yaml", path, sizeof(path)));
	CHECK(!cf_plan_read(path, &plan) && plan.depth == 11);

	CHECK(fixture("absent.yaml", path, sizeof(path)) == 0);
	CHECK(cf_plan_read(path, &plan) == -1);
	return 0;
}

static int t_count(const char *root)
{
	char dir[512];
	struct cf_counters cnt;
	struct cf_counters got;

	CHECK(!make_dir(root, "count", dir, sizeof(dir)));
	CHECK(!cf_counters_get(dir, &cnt));
	CHECK(cnt.flows == 0 && cnt.last == -1);
	cnt.flows = 7;
	cnt.fast = 5;
	cnt.slow = 2;
	cnt.nodes = 11;
	cnt.repairs = 1;
	cnt.aborts = 1;
	cnt.last = 0;
	CHECK(!cf_counters_put(dir, &cnt));
	CHECK(!cf_counters_get(dir, &got));
	CHECK(got.flows == 7 && got.fast == 5 && got.slow == 2);
	CHECK(got.nodes == 11 && got.repairs == 1 && got.aborts == 1);
	CHECK(got.last == 0);
	return 0;
}

static int t_fast(const char *root)
{
	char dir[512];
	char events[600];
	struct cf_counters cnt;
	struct cf_ctx ctx;
	FILE *ev;

	CHECK(!make_dir(root, "fast", dir, sizeof(dir)));
	snprintf(events, sizeof(events), "%s/%s", dir, CF_EVENT_NAME);
	memset(&cnt, 0, sizeof(cnt));
	ctx_init(&ctx, AIRY_THINK_FAST);
	ev = fopen(events, "a");
	CHECK(ev != NULL);
	CHECK(cf_flow_run(&ctx, ev, &cnt, dir) == 0);
	fclose(ev);
	CHECK(cnt.fast == 1 && cnt.slow == 0 && cnt.nodes == 1);
	CHECK(cnt.aborts == 0 && cnt.last == 0);
	CHECK(hit(events, "\"type\":\"done\""));
	CHECK(hit(events, "\"node\":\"respond\""));
	CHECK(hit(events, "\"mode\":\"fast\""));
	return 0;
}

static int t_slow(const char *root)
{
	char path[512];
	char dir[512];
	char events[600];
	char memory[600];
	struct cf_counters cnt;
	struct cf_ctx ctx;
	FILE *ev;

	CHECK(!make_dir(root, "slow", dir, sizeof(dir)));
	snprintf(events, sizeof(events), "%s/%s", dir, CF_EVENT_NAME);
	snprintf(memory, sizeof(memory), "%s/%s", dir, CF_MEM_NAME);
	CHECK(!fixture("plan.yaml", path, sizeof(path)));
	memset(&cnt, 0, sizeof(cnt));
	ctx_init(&ctx, AIRY_THINK_SLOW);
	CHECK(!cf_plan_read(path, &ctx.plan));
	ctx.have_plan = 1;
	ev = fopen(events, "a");
	CHECK(ev != NULL);
	CHECK(cf_flow_run(&ctx, ev, &cnt, dir) == 0);
	fclose(ev);
	CHECK(cnt.slow == 1 && cnt.nodes == 3 && cnt.repairs == 0);
	CHECK(hit(events, "\"type\":\"plan\""));
	CHECK(hit(events, "\"node\":\"transform\""));
	CHECK(hit(events, "\"type\":\"feedback\""));
	CHECK(hit(events, "\"type\":\"memory\""));
	CHECK(hit(events, "\"mode\":\"slow\""));
	CHECK(lines(memory) == 1);
	return 0;
}

static int t_depth(const char *root)
{
	char path[512];
	char dir[512];
	char events[600];
	struct cf_counters cnt;
	struct cf_ctx ctx;
	FILE *ev;

	CHECK(!make_dir(root, "depth", dir, sizeof(dir)));
	snprintf(events, sizeof(events), "%s/%s", dir, CF_EVENT_NAME);
	CHECK(!fixture("plan_deep.yaml", path, sizeof(path)));
	memset(&cnt, 0, sizeof(cnt));
	ctx_init(&ctx, AIRY_THINK_SLOW);
	CHECK(!cf_plan_read(path, &ctx.plan));
	ctx.have_plan = 1;
	ev = fopen(events, "a");
	CHECK(ev != NULL);
	CHECK(cf_flow_run(&ctx, ev, &cnt, dir) == CF_EXIT_DEPTH);
	fclose(ev);
	CHECK(cnt.aborts == 1 && cnt.last == 1);
	CHECK(hit(events, "\"result\":\"depth\""));
	return 0;
}

static int t_deny(const char *root)
{
	char path[512];
	char dir[512];
	char events[600];
	struct cf_counters cnt;
	struct cf_ctx ctx;
	FILE *ev;

	CHECK(!make_dir(root, "deny", dir, sizeof(dir)));
	snprintf(events, sizeof(events), "%s/%s", dir, CF_EVENT_NAME);
	CHECK(!fixture("plan_denied.yaml", path, sizeof(path)));
	memset(&cnt, 0, sizeof(cnt));
	ctx_init(&ctx, AIRY_THINK_SLOW);
	CHECK(!cf_plan_read(path, &ctx.plan));
	ctx.have_plan = 1;
	ev = fopen(events, "a");
	CHECK(ev != NULL);
	CHECK(cf_flow_run(&ctx, ev, &cnt, dir) == CF_EXIT_DENY);
	fclose(ev);
	CHECK(cnt.aborts == 1 && cnt.nodes == 0);
	CHECK(hit(events, "\"node\":\"prune\""));
	CHECK(hit(events, "\"result\":\"deny\""));
	CHECK(hit(events, "\"result\":\"permission\""));
	return 0;
}

static int t_retry(const char *root)
{
	char path[512];
	char dir[512];
	char events[600];
	struct cf_counters cnt;
	struct cf_ctx ctx;
	FILE *ev;

	CHECK(!make_dir(root, "retry", dir, sizeof(dir)));
	snprintf(events, sizeof(events), "%s/%s", dir, CF_EVENT_NAME);
	CHECK(!fixture("plan_fail.yaml", path, sizeof(path)));
	memset(&cnt, 0, sizeof(cnt));
	ctx_init(&ctx, AIRY_THINK_SLOW);
	CHECK(!cf_plan_read(path, &ctx.plan));
	CHECK(!fixture("plan_replan.yaml", path, sizeof(path)));
	CHECK(!cf_plan_read(path, &ctx.replan));
	ctx.have_plan = 1;
	ctx.have_replan = 1;
	ev = fopen(events, "a");
	CHECK(ev != NULL);
	CHECK(cf_flow_run(&ctx, ev, &cnt, dir) == 0);
	fclose(ev);
	CHECK(cnt.slow == 1 && cnt.repairs == 1 && cnt.nodes == 2);
	CHECK(cnt.aborts == 0 && cnt.last == 0);
	CHECK(hit(events, "\"type\":\"compensate\""));
	CHECK(hit(events, "\"node\":\"fetch\""));
	CHECK(hit(events, "\"type\":\"replan\""));
	CHECK(hit(events, "\"attempt\":2"));
	return 0;
}

static int t_abort(const char *root)
{
	char path[512];
	char dir[512];
	char events[600];
	struct cf_counters cnt;
	struct cf_ctx ctx;
	FILE *ev;

	CHECK(!make_dir(root, "abort", dir, sizeof(dir)));
	snprintf(events, sizeof(events), "%s/%s", dir, CF_EVENT_NAME);
	CHECK(!fixture("plan_fail.yaml", path, sizeof(path)));
	memset(&cnt, 0, sizeof(cnt));
	ctx_init(&ctx, AIRY_THINK_SLOW);
	CHECK(!cf_plan_read(path, &ctx.plan));
	ctx.have_plan = 1;
	ev = fopen(events, "a");
	CHECK(ev != NULL);
	CHECK(cf_flow_run(&ctx, ev, &cnt, dir) == CF_EXIT_EXEC);
	fclose(ev);
	CHECK(cnt.aborts == 1 && cnt.repairs == 1 && cnt.nodes == 2);
	CHECK(hit(events, "\"result\":\"retry-exhausted\""));
	return 0;
}

int main(void)
{
	char root[] = "/tmp/cf_test.XXXXXX";
	int fails = 0;

	if (!mkdtemp(root)) {
		fprintf(stderr, "FAIL 无法创建临时目录\n");
		return 1;
	}
	fails += t_plan();
	fails += t_count(root);
	fails += t_fast(root);
	fails += t_slow(root);
	fails += t_depth(root);
	fails += t_deny(root);
	fails += t_retry(root);
	fails += t_abort(root);
	if (fails) {
		fprintf(stderr, "cogflow: %d 组用例失败\n", fails);
		return 1;
	}
	printf("cogflow: 全部用例通过\n");
	return 0;
}
