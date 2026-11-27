/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * td_state.c — 决策计数、状态落盘与审计账本
 *
 * 职责：
 *   维护 fast/slow 两条路径的决策计数与最近一次模式，供 status 动作读取；
 *   在 audit 为真时把每次切换追加成一行 NDJSON，便于事后复算切换依据。
 *
 * 约束：
 *   账本只记整数量与枚举名，不落意图原文，避免把用户输入带进审计面。
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "td_api.h"

#define TD_STATE_NAME	"state"
#define TD_JOURNAL_NAME	"journal.ndjson"

static int td_state_path(const char *dir, const char *name, char *out,
			 size_t cap)
{
	int n = snprintf(out, cap, "%s/%s", dir, name);

	return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

int td_state_load(const char *dir, struct td_counters *cnt)
{
	char path[TD_PATH_MAX];
	char line[128];
	FILE *fp;

	if (!dir || !cnt)
		return -1;
	memset(cnt, 0, sizeof(*cnt));
	cnt->last = -1;
	if (td_state_path(dir, TD_STATE_NAME, path, sizeof(path)))
		return -1;
	fp = fopen(path, "r");
	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, "decisions=%lu", &cnt->decisions) == 1)
			continue;
		if (sscanf(line, "fast=%lu", &cnt->fast) == 1)
			continue;
		if (sscanf(line, "slow=%lu", &cnt->slow) == 1)
			continue;
		if (!strncmp(line, "last=slow", 9))
			cnt->last = AIRY_THINK_SLOW;
		else if (!strncmp(line, "last=fast", 9))
			cnt->last = AIRY_THINK_FAST;
	}
	fclose(fp);
	return 0;
}

int td_state_save(const char *dir, const struct td_counters *cnt)
{
	char path[TD_PATH_MAX];
	char body[256];
	int n;

	if (!dir || !cnt || td_state_path(dir, TD_STATE_NAME, path,
					  sizeof(path)))
		return -1;
	if (mkdir(dir, 0755) && errno != EEXIST)
		return -1;
	n = snprintf(body, sizeof(body), "decisions=%lu\nfast=%lu\nslow=%lu\n"
		     "last=%s\n", cnt->decisions, cnt->fast, cnt->slow,
		     cnt->last < 0 ? "none" : td_mode_name(cnt->last));
	if (n < 0 || (size_t)n >= sizeof(body))
		return -1;
	return td_write_atom(path, body);
}

int td_journal_add(const char *dir, const struct td_input *in,
		   const struct td_decision *dec)
{
	char path[TD_PATH_MAX];
	char line[256];
	FILE *fp;
	int n;

	if (!dir || !in || !dec)
		return -1;
	if (mkdir(dir, 0755) && errno != EEXIST)
		return -1;
	if (td_state_path(dir, TD_JOURNAL_NAME, path, sizeof(path)))
		return -1;
	n = snprintf(line, sizeof(line),
		     "{\"time\":%lld,\"mode\":\"%s\",\"reason\":\"%s\","
		     "\"score\":%d,\"intent_len\":%zu,\"similarity\":%d,"
		     "\"risk\":\"%s\",\"tokens\":%u}\n",
		     (long long)time(NULL), td_mode_name(dec->mode),
		     dec->reason, dec->score,
		     in->intent ? strlen(in->intent) : 0,
		     in->similarity, td_risk_name(in->risk), in->tokens);
	if (n < 0 || (size_t)n >= sizeof(line))
		return -1;
	fp = fopen(path, "a");
	if (!fp)
		return -1;
	if (fwrite(line, 1, (size_t)n, fp) != (size_t)n)
		n = -1;
	else if (fflush(fp) || fsync(fileno(fp)))
		n = -1;
	else
		n = 0;
	if (fclose(fp))
		n = -1;
	return n;
}

int td_status_print(const char *dir)
{
	struct td_counters cnt;

	if (td_state_load(dir, &cnt))
		return -1;
	printf("decisions=%lu fast=%lu slow=%lu last=%s\n", cnt.decisions,
	       cnt.fast, cnt.slow,
	       cnt.last < 0 ? "none" : td_mode_name(cnt.last));
	return 0;
}
