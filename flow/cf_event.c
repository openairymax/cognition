/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cf_event.c — 事件账本、追踪标识、计数与 L1 记忆追加
 *
 * 账本是一行一个 JSON 的 NDJSON：追加写、不回头改写。计数器单独原子
 * 落盘，避免读账本时再扫全量历史。追踪标识取 8 字节随机数，读不到
 * 随机源时退化为时钟与进程号的混合，仍保证同一状态的多次运行不重号。
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cf_api.h"

static unsigned long long cf_mix(unsigned long long value)
{
	value ^= value >> 33;
	value *= 0xff51afd7ed558ccdULL;
	value ^= value >> 33;
	value *= 0xc4ceb9fe1a85ec53ULL;
	return value ^ (value >> 33);
}

static unsigned long long cf_rand64(void)
{
	unsigned long long value = 0;
	int fd = open("/dev/urandom", O_RDONLY);
	ssize_t got = -1;

	if (fd >= 0) {
		got = read(fd, &value, sizeof(value));
		close(fd);
	}
	if (got != (ssize_t)sizeof(value)) {
		struct timespec ts;
		unsigned long long seed;

		clock_gettime(CLOCK_REALTIME, &ts);
		seed = (unsigned long long)ts.tv_sec * 1000000000ULL +
		       (unsigned long long)ts.tv_nsec;
		value = cf_mix(seed ^ (unsigned long long)getpid());
	}
	return value;
}

void cf_trace_gen(char *out, size_t len)
{
	if (len < CF_TRACE_LEN) {
		if (len)
			out[0] = '\0';
		return;
	}
	snprintf(out, len, "%016llx", cf_rand64());
}

static long long cf_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void cf_event_put(FILE *ev, struct cf_ctx *ctx,
		  const struct cf_event *event)
{
	int score = td_milli(ctx->dec.score);
	const char *phase = event->phase ? event->phase : "";
	const char *node = event->node ? event->node : "";
	const char *result = event->result ? event->result : "";

	ctx->seq++;
	fprintf(ev, "{\"v\":1,\"seq\":%llu,\"trace\":\"%s\",\"task\":\"%s\","
		"\"type\":\"%s\",\"mode\":\"%s\",\"phase\":\"%s\","
		"\"node\":\"%s\",\"result\":\"%s\",\"depth\":%d,\"count\":%d,"
		"\"attempt\":%d,\"score\":%d.%03d,\"budget_ms\":%d,"
		"\"ts_ms\":%lld}\n", ctx->seq, ctx->trace, ctx->task,
		event->type, td_mode_name(ctx->dec.mode), phase, node, result,
		event->depth, event->count, event->attempt, score / 1000,
		score % 1000, ctx->dec.latency_ms, cf_now_ms());
	fflush(ev);
}

int cf_counters_get(const char *state, struct cf_counters *cnt)
{
	char path[TD_PATH_MAX];
	char line[128];
	FILE *fp;

	memset(cnt, 0, sizeof(*cnt));
	cnt->last = -1;
	snprintf(path, sizeof(path), "%s/%s", state, CF_COUNT_NAME);
	fp = fopen(path, "r");
	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp)) {
		unsigned long value = 0;

		if (sscanf(line, "flows=%lu", &value) == 1)
			cnt->flows = value;
		else if (sscanf(line, "fast=%lu", &value) == 1)
			cnt->fast = value;
		else if (sscanf(line, "slow=%lu", &value) == 1)
			cnt->slow = value;
		else if (sscanf(line, "nodes=%lu", &value) == 1)
			cnt->nodes = value;
		else if (sscanf(line, "repairs=%lu", &value) == 1)
			cnt->repairs = value;
		else if (sscanf(line, "aborts=%lu", &value) == 1)
			cnt->aborts = value;
		else if (!strncmp(line, "last=ok", 7))
			cnt->last = 0;
		else if (!strncmp(line, "last=aborted", 12))
			cnt->last = 1;
	}
	fclose(fp);
	return 0;
}

int cf_counters_put(const char *state, const struct cf_counters *cnt)
{
	char path[TD_PATH_MAX];
	char body[256];
	const char *last = "none";

	if (cnt->last == 0)
		last = "ok";
	else if (cnt->last == 1)
		last = "aborted";
	snprintf(path, sizeof(path), "%s/%s", state, CF_COUNT_NAME);
	snprintf(body, sizeof(body),
		 "flows=%lu\nfast=%lu\nslow=%lu\nnodes=%lu\n"
		 "repairs=%lu\naborts=%lu\nlast=%s\n",
		 cnt->flows, cnt->fast, cnt->slow, cnt->nodes, cnt->repairs,
		 cnt->aborts, last);
	return td_write_atom(path, body);
}

int cf_memory_add(const char *state, const struct cf_ctx *ctx, int nodes)
{
	char path[TD_PATH_MAX];
	FILE *fp;
	int fd;
	int rc = 0;

	snprintf(path, sizeof(path), "%s/%s", state, CF_MEM_NAME);
	fp = fopen(path, "a");
	if (!fp)
		return -1;
	fprintf(fp, "{\"trace\":\"%s\",\"task\":\"%s\",\"layer\":\"l1\","
		"\"nodes\":%d,\"seq\":%llu}\n", ctx->trace, ctx->task, nodes,
		ctx->seq);
	fflush(fp);
	fd = fileno(fp);
	if (fd >= 0 && fsync(fd))
		rc = -1;
	if (fclose(fp))
		rc = -1;
	return rc;
}

int cf_status_show(const char *state)
{
	struct cf_counters cnt;
	const char *last = "none";

	if (cf_counters_get(state, &cnt))
		return -1;
	if (cnt.last == 0)
		last = "ok";
	else if (cnt.last == 1)
		last = "aborted";
	printf("flows=%lu fast=%lu slow=%lu nodes=%lu repairs=%lu "
	       "aborts=%lu last=%s\n", cnt.flows, cnt.fast, cnt.slow,
	       cnt.nodes, cnt.repairs, cnt.aborts, last);
	return 0;
}
