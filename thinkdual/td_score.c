/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * td_score.c — 复杂度评估与 Q16.16 定点辅助
 *
 * 职责：
 *   把意图长度、历史相似度、风险等级、预估 token 四个维度归一化到
 *   Q16.16，再按 2:3:3:2 加权求和，得到 [0,1] 的复杂度分数。
 *
 * 标定：
 *   四个维度同时到达文档给出的 System 2 边界（50 字符、相似度 0.85、
 *   中风险、1000 token）时，分数为 0.315，刚好越过默认阈值 0.3。
 *   高风险单独即达 0.300，与设计表「中高风险走 System 2」一致。
 *
 * 约束：
 *   全程整数运算，不引入浮点，与内核侧 IRON-9 §2.1 同构。
 */

#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <string.h>

#include "td_api.h"

int td_parse_q16(const char *text, int *out)
{
	long long whole = 0;
	long long frac = 0;
	int digits = 0;
	int scale = 1;
	const char *p = text;

	if (!text || !out)
		return -1;
	while (*p >= '0' && *p <= '9') {
		whole = whole * 10 + (*p - '0');
		if (whole > 65535)
			return -1;
		p++;
		digits++;
	}
	if (*p == '.') {
		p++;
		while (*p >= '0' && *p <= '9') {
			if (digits > 12)
				return -1;
			if (scale < 100000) {
				frac = frac * 10 + (*p - '0');
				scale *= 10;
			}
			p++;
			digits++;
		}
	}
	if (digits == 0 || *p != '\0')
		return -1;
	*out = (int)(whole * AIRY_Q16_ONE + frac * AIRY_Q16_ONE / scale);
	return 0;
}

int td_milli(int q16)
{
	if (q16 <= 0)
		return 0;
	return (int)(((long long)q16 * 1000 + 32768) >> 16);
}

int td_risk_parse(const char *text, int *out)
{
	if (!text || !out)
		return -1;
	if (!strcmp(text, "low")) {
		*out = TD_RISK_LOW;
		return 0;
	}
	if (!strcmp(text, "medium")) {
		*out = TD_RISK_MED;
		return 0;
	}
	if (!strcmp(text, "high")) {
		*out = TD_RISK_HIGH;
		return 0;
	}
	return -1;
}

const char *td_risk_name(int risk)
{
	switch (risk) {
	case TD_RISK_LOW:
		return "low";
	case TD_RISK_MED:
		return "medium";
	case TD_RISK_HIGH:
		return "high";
	default:
		return "unknown";
	}
}

const char *td_mode_name(enum airy_think_mode mode)
{
	return mode == AIRY_THINK_SLOW ? "slow" : "fast";
}

static int td_norm_len(const char *intent)
{
	size_t len = intent ? strlen(intent) : 0;

	if (len > TD_LEN_FULL)
		len = TD_LEN_FULL;
	return (int)(len * AIRY_Q16_ONE / TD_LEN_FULL);
}

static int td_norm_tok(unsigned int tokens)
{
	if (tokens > TD_TOK_FULL)
		tokens = TD_TOK_FULL;
	return (int)(tokens * AIRY_Q16_ONE / TD_TOK_FULL);
}

static int td_norm_risk(int risk)
{
	switch (risk) {
	case TD_RISK_LOW:
		return 0;
	case TD_RISK_MED:
		return AIRY_Q16_HALF;
	case TD_RISK_HIGH:
		return AIRY_Q16_ONE;
	default:
		return -1;
	}
}

int td_score(const struct td_input *in, int *score)
{
	int sim;
	int risk;
	int total;

	if (!in || !score)
		return -1;
	risk = td_norm_risk(in->risk);
	if (risk < 0)
		return -1;
	sim = in->similarity;
	if (sim < 0)
		sim = 0;
	if (sim > AIRY_Q16_ONE)
		sim = AIRY_Q16_ONE;
	total = td_norm_len(in->intent) * 2;
	total += (AIRY_Q16_ONE - sim) * 3;
	total += risk * 3;
	total += td_norm_tok(in->tokens) * 2;
	*score = total / 10;
	return 0;
}

int td_decide(const struct td_policy *pol, const struct td_input *in,
	      struct td_decision *out)
{
	int score;

	if (!pol || !in || !out)
		return -1;
	if (td_score(in, &score) < 0)
		return -1;
	out->score = score;
	if (!pol->s1_enabled && !pol->s2_enabled) {
		out->mode = AIRY_THINK_FAST;
		out->reason = "degraded";
	} else if (!pol->s2_enabled) {
		out->mode = AIRY_THINK_FAST;
		out->reason = "system1-only";
	} else if (!pol->s1_enabled) {
		out->mode = AIRY_THINK_SLOW;
		out->reason = "system2-only";
	} else if (score >= pol->threshold) {
		out->mode = AIRY_THINK_SLOW;
		out->reason = "score";
	} else {
		out->mode = AIRY_THINK_FAST;
		out->reason = "score";
	}
	out->latency_ms = out->mode == AIRY_THINK_SLOW ?
			  pol->llm_timeout_ms : pol->max_latency_ms;
	return 0;
}
