/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cf_plan.c — 认知数据流的规划读取与守卫扫描
 *
 * 规划是一行一个节点的纯文本：名称、层级、守卫结论、预期结果。解析只
 * 接受白名单字符与固定取值，拒绝尾随字段，避免把任意文本带进事件账本。
 *
 * 行格式：<name> <depth> <allow|deny> <ok|fail>
 *   name   1..31 字节，限字母、数字、下划线、点与连字符
 *   depth  1..64
 */

#include <stdio.h>
#include <string.h>

#include "cf_api.h"

#define CF_LINE_MAX	256

static char *cf_trim(char *text)
{
	char *end;

	while (*text == ' ' || *text == '\t')
		text++;
	end = text + strlen(text);
	while (end > text && (end[-1] == '\n' || end[-1] == '\r' ||
			      end[-1] == ' ' || end[-1] == '\t'))
		end--;
	*end = '\0';
	return text;
}

static int cf_name_ok(const char *name)
{
	size_t len = strlen(name);
	size_t i;

	if (len == 0 || len >= CF_NAME_MAX)
		return 0;
	for (i = 0; i < len; i++) {
		char c = name[i];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_' || c == '-' ||
		    c == '.')
			continue;
		return 0;
	}
	return 1;
}

static int cf_line_parse(char *line, struct cf_node *node)
{
	char name[CF_NAME_MAX];
	char guard[16];
	char outcome[16];
	int depth = 0;
	int used = 0;

	if (sscanf(line, "%31s %d %15s %15s %n", name, &depth, guard,
		   outcome, &used) != 4)
		return -1;
	if (line[used] != '\0' || !cf_name_ok(name))
		return -1;
	if (depth < 1 || depth > CF_NODE_MAX)
		return -1;
	memset(node, 0, sizeof(*node));
	memcpy(node->name, name, strlen(name) + 1);
	node->depth = depth;
	if (!strcmp(guard, "allow"))
		node->guard = CF_GUARD_ALLOW;
	else if (!strcmp(guard, "deny"))
		node->guard = CF_GUARD_DENY;
	else
		return -1;
	if (!strcmp(outcome, "ok"))
		node->outcome = CF_EXEC_OK;
	else if (!strcmp(outcome, "fail"))
		node->outcome = CF_EXEC_FAIL;
	else
		return -1;
	return 0;
}

int cf_plan_read(const char *path, struct cf_plan *plan)
{
	char line[CF_LINE_MAX];
	FILE *fp;

	memset(plan, 0, sizeof(*plan));
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		char *text;
		struct cf_node node;

		if (!strchr(line, '\n') && !feof(fp)) {
			fclose(fp);
			return -2;
		}
		text = cf_trim(line);
		if (!*text || *text == '#')
			continue;
		if (plan->count >= CF_NODE_MAX ||
		    cf_line_parse(text, &node)) {
			fclose(fp);
			return -2;
		}
		plan->node[plan->count++] = node;
		if (node.depth > plan->depth)
			plan->depth = node.depth;
	}
	fclose(fp);
	return plan->count ? 0 : -2;
}

int cf_guard_scan(const struct cf_plan *plan, int *index)
{
	int i;

	for (i = 0; i < plan->count; i++) {
		if (plan->node[i].guard == CF_GUARD_DENY) {
			*index = i;
			return 1;
		}
	}
	return 0;
}
