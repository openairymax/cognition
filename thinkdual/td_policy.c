/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * td_policy.c — 策略解析、生效缓存与热更新同步
 *
 * 职责：
 *   读取 /etc/agentrt/cognition_policy.yaml 的文档化子集，校验取值范围，
 *   并把最近一次生效的策略连同文件指纹写进状态目录。策略文件变更后，
 *   只有 hot_reload 为真才重新生效；为假时继续使用缓存，正在运行的任务
 *   不受替换影响（FR-050）。
 *
 * 文件指纹：
 *   用 FNV-1a 64 位哈希而非 mtime，避免同一秒内的连续改写被漏判。
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "td_api.h"

#define TD_FILE_MAX	65536
#define TD_CACHE_NAME	"policy.cache"

static unsigned long long td_hash(const char *buf, size_t len)
{
	unsigned long long h = 1469598103934665603ULL;
	size_t i;

	for (i = 0; i < len; i++) {
		h ^= (unsigned char)buf[i];
		h *= 1099511628211ULL;
	}
	return h;
}

static char *td_trim(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t' || *s == '\r')
		s++;
	end = s + strlen(s);
	while (end > s) {
		char c = end[-1];

		if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			break;
		end--;
	}
	*end = '\0';
	return s;
}

static int td_parse_uint(const char *text, int *out)
{
	unsigned long v;
	char *end = NULL;

	if (!text || *text < '0' || *text > '9')
		return -1;
	errno = 0;
	v = strtoul(text, &end, 10);
	if (errno || !end || *end != '\0' || v > 1000000UL)
		return -1;
	*out = (int)v;
	return 0;
}

static int td_parse_bool(const char *text, int *out)
{
	if (!strcmp(text, "true")) {
		*out = 1;
		return 0;
	}
	if (!strcmp(text, "false")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int td_set_sys1(struct td_policy *pol, const char *key,
		       const char *val)
{
	if (!strcmp(key, "enabled"))
		return td_parse_bool(val, &pol->s1_enabled);
	if (!strcmp(key, "complexity_threshold"))
		return td_parse_q16(val, &pol->threshold);
	if (!strcmp(key, "max_latency_ms"))
		return td_parse_uint(val, &pol->max_latency_ms);
	if (!strcmp(key, "cache_ttl_s"))
		return td_parse_uint(val, &pol->cache_ttl_s);
	return -1;
}

static int td_set_sys2(struct td_policy *pol, const char *key,
		       const char *val)
{
	if (!strcmp(key, "enabled"))
		return td_parse_bool(val, &pol->s2_enabled);
	if (!strcmp(key, "dag_max_depth"))
		return td_parse_uint(val, &pol->dag_max_depth);
	if (!strcmp(key, "llm_timeout_ms"))
		return td_parse_uint(val, &pol->llm_timeout_ms);
	if (!strcmp(key, "feedback_enabled"))
		return td_parse_bool(val, &pol->feedback);
	return -1;
}

static int td_set_switch(struct td_policy *pol, const char *key,
			 const char *val)
{
	char buf[32];
	size_t len;

	if (!strcmp(key, "hot_reload"))
		return td_parse_bool(val, &pol->hot_reload);
	if (!strcmp(key, "audit"))
		return td_parse_bool(val, &pol->audit);
	if (strcmp(key, "strategy"))
		return -1;
	len = strlen(val);
	if (len < 3 || len > sizeof(buf) - 1 || val[0] != '"' ||
	    val[len - 1] != '"')
		return -1;
	memcpy(buf, val + 1, len - 2);
	buf[len - 2] = '\0';
	return strcmp(buf, "score_based") ? -1 : 0;
}

static int td_pol_set(struct td_policy *pol, const char *sec,
		      const char *key, const char *val)
{
	if (!strcmp(sec, "system1"))
		return td_set_sys1(pol, key, val);
	if (!strcmp(sec, "system2"))
		return td_set_sys2(pol, key, val);
	if (!strcmp(sec, "switching"))
		return td_set_switch(pol, key, val);
	return -1;
}

static int td_pol_check(const struct td_policy *pol)
{
	if (pol->threshold < 0 || pol->threshold > AIRY_Q16_ONE)
		return -1;
	if (pol->max_latency_ms < 1 || pol->max_latency_ms > 1000)
		return -1;
	if (pol->cache_ttl_s < 0 || pol->cache_ttl_s > 86400)
		return -1;
	if (pol->dag_max_depth < 1 || pol->dag_max_depth > 64)
		return -1;
	if (pol->llm_timeout_ms < 1 || pol->llm_timeout_ms > 60000)
		return -1;
	return 0;
}

int td_pol_default(struct td_policy *pol)
{
	if (!pol)
		return -1;
	memset(pol, 0, sizeof(*pol));
	pol->threshold = AIRY_Q16_ONE * 3 / 10;
	pol->s1_enabled = 1;
	pol->s2_enabled = 1;
	pol->max_latency_ms = 10;
	pol->cache_ttl_s = 300;
	pol->dag_max_depth = 10;
	pol->llm_timeout_ms = 200;
	pol->feedback = 1;
	pol->hot_reload = 1;
	pol->audit = 1;
	return 0;
}

static int td_pol_body(struct td_policy *pol, char *buf)
{
	char section[16] = "";
	char *line = buf;

	while (line && *line) {
		char *next = strchr(line, '\n');
		char *hash;
		char *colon;
		char *val;

		if (next)
			*next++ = '\0';
		hash = strchr(line, '#');
		if (hash)
			*hash = '\0';
		line = td_trim(line);
		if (*line) {
			colon = strchr(line, ':');
			if (!colon)
				return -1;
			*colon = '\0';
			val = td_trim(colon + 1);
			line = td_trim(line);
			if (!*val) {
				if (strlen(line) >= sizeof(section))
					return -1;
				strcpy(section, line);
			} else if (td_pol_set(pol, section, line, val)) {
				return -1;
			}
		}
		line = next;
	}
	return td_pol_check(pol);
}

int td_pol_read(const char *path, struct td_policy *pol,
		unsigned long long *stamp)
{
	char buf[TD_FILE_MAX];
	struct td_policy tmp;
	FILE *fp;
	size_t len;
	int extra;

	if (!path || !pol || !stamp)
		return -2;
	if (td_pol_default(&tmp))
		return -2;
	fp = fopen(path, "rb");
	if (!fp)
		return errno == ENOENT ? -1 : -2;
	len = fread(buf, 1, sizeof(buf) - 1, fp);
	extra = ferror(fp) || fgetc(fp) != EOF;
	fclose(fp);
	if (extra)
		return -2;
	buf[len] = '\0';
	*stamp = td_hash(buf, len);
	if (td_pol_body(&tmp, buf))
		return -2;
	*pol = tmp;
	return 0;
}

static int td_cache_file(const char *dir, char *out, size_t cap)
{
	int n = snprintf(out, cap, "%s/%s", dir, TD_CACHE_NAME);

	return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

static int td_cache_line(struct td_core *core, const char *line)
{
	if (sscanf(line, "stamp=%llu", &core->stamp) == 1)
		return 1;
	if (sscanf(line, "threshold=%d", &core->pol.threshold) == 1)
		return 2;
	if (sscanf(line, "s1=%d", &core->pol.s1_enabled) == 1)
		return 2;
	if (sscanf(line, "s2=%d", &core->pol.s2_enabled) == 1)
		return 2;
	if (sscanf(line, "latency=%d", &core->pol.max_latency_ms) == 1)
		return 2;
	if (sscanf(line, "ttl=%d", &core->pol.cache_ttl_s) == 1)
		return 2;
	if (sscanf(line, "dag=%d", &core->pol.dag_max_depth) == 1)
		return 2;
	if (sscanf(line, "llm=%d", &core->pol.llm_timeout_ms) == 1)
		return 2;
	if (sscanf(line, "feedback=%d", &core->pol.feedback) == 1)
		return 2;
	if (sscanf(line, "hot=%d", &core->pol.hot_reload) == 1)
		return 2;
	if (sscanf(line, "audit=%d", &core->pol.audit) == 1)
		return 2;
	return 0;
}

int td_cache_load(const char *dir, struct td_core *core)
{
	char path[TD_PATH_MAX];
	char line[128];
	FILE *fp;
	int seen = 0;

	if (!dir || !core || td_cache_file(dir, path, sizeof(path)))
		return -1;
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp))
		if (td_cache_line(core, line) == 1)
			seen = 1;
	fclose(fp);
	if (!seen)
		return -1;
	core->have_cache = 1;
	return 0;
}

int td_write_atom(const char *path, const char *body)
{
	char tmp[TD_PATH_MAX];
	FILE *fp;
	size_t len = strlen(body);
	int n;
	int bad = 0;

	n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	if (n < 0 || (size_t)n >= sizeof(tmp))
		return -1;
	fp = fopen(tmp, "w");
	if (!fp)
		return -1;
	if (fwrite(body, 1, len, fp) != len)
		bad = 1;
	if (fflush(fp))
		bad = 1;
	if (fsync(fileno(fp)))
		bad = 1;
	if (fclose(fp))
		bad = 1;
	if (bad || rename(tmp, path)) {
		remove(tmp);
		return -1;
	}
	return 0;
}

int td_cache_save(const char *dir, const struct td_core *core)
{
	char path[TD_PATH_MAX];
	char body[512];
	int n;

	if (!dir || !core || td_cache_file(dir, path, sizeof(path)))
		return -1;
	if (mkdir(dir, 0755) && errno != EEXIST)
		return -1;
	n = snprintf(body, sizeof(body),
		     "stamp=%llu\nthreshold=%d\ns1=%d\ns2=%d\n"
		     "latency=%d\nttl=%d\ndag=%d\nllm=%d\n"
		     "feedback=%d\nhot=%d\naudit=%d\n",
		     core->stamp, core->pol.threshold, core->pol.s1_enabled,
		     core->pol.s2_enabled, core->pol.max_latency_ms,
		     core->pol.cache_ttl_s, core->pol.dag_max_depth,
		     core->pol.llm_timeout_ms, core->pol.feedback,
		     core->pol.hot_reload, core->pol.audit);
	if (n < 0 || (size_t)n >= sizeof(body))
		return -1;
	return td_write_atom(path, body);
}

int td_core_sync(struct td_core *core, const char *policy,
		 const char *state)
{
	struct td_policy tmp;
	unsigned long long stamp = 0;
	int rc;

	if (!core || !policy)
		return -1;
	rc = td_pol_read(policy, &tmp, &stamp);
	if (rc == -2)
		return -2;
	if (rc == -1) {
		core->source = core->have_cache ? TD_SRC_CACHE : TD_SRC_DEFAULT;
		core->reloaded = 0;
		return 0;
	}
	if (core->have_cache && core->stamp == stamp) {
		core->source = TD_SRC_CACHE;
		core->reloaded = 0;
		return 0;
	}
	if (core->have_cache && !core->pol.hot_reload) {
		core->source = TD_SRC_CACHE;
		core->reloaded = 0;
		return 0;
	}
	core->reloaded = core->have_cache;
	core->pol = tmp;
	core->stamp = stamp;
	core->have_cache = 1;
	core->source = TD_SRC_FILE;
	if (state)
		td_cache_save(state, core);
	return 0;
}

int td_core_open(struct td_core *core, const char *policy, const char *state)
{
	if (!core || !policy)
		return -1;
	memset(core, 0, sizeof(*core));
	td_pol_default(&core->pol);
	if (state)
		td_cache_load(state, core);
	return td_core_sync(core, policy, state);
}
