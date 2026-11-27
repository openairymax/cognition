/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * td_api.h — Thinkdual 双系统思考的对外契约
 *
 * 职责：
 *   定义复杂度评估的输入、策略、决策与运行态结构，供 CLI 与常驻认知
 *   进程共用。推理模式直接引用 [SC] 契约头的 enum airy_think_mode，
 *   本文件不复制任何契约取值（OS-IRON-014）。
 *
 * 数值约定：
 *   一切比例用 Q16.16 定点（airy_q16_t），决策路径不做浮点运算，与
 *   内核侧 IRON-9 §2.1 的无浮点约束保持同构。
 *
 * 构建：
 *   需以 -idirafter <kernel>/include/uapi 引入契约头，使 linux/types.h
 *   仍取自系统 UAPI，只有 linux/airymax 目录取自已导出的内核头。
 */

#ifndef AIRY_THINKDUAL_API_H
#define AIRY_THINKDUAL_API_H

#include <linux/airymax/cognition_types.h>

/* 复杂度归一化的饱和点：超过即记满分（见 Documentation/thinkdual.md）。 */
#define TD_LEN_FULL		500
#define TD_TOK_FULL		2000

#define TD_PATH_MAX		256

/* 风险等级：低风险走 System 1，中高风险的加权分推向 System 2。 */
enum td_risk {
	TD_RISK_LOW	= 0,
	TD_RISK_MED	= 1,
	TD_RISK_HIGH	= 2,
	TD_RISK_MAX
};

/* 策略来源：缺省值、策略文件、上次生效的缓存。 */
enum td_source {
	TD_SRC_DEFAULT	= 0,
	TD_SRC_FILE	= 1,
	TD_SRC_CACHE	= 2
};

/* 退出码：参数、配置、状态三类失败分别可归因。 */
#define TD_EXIT_USE		2
#define TD_EXIT_CFG		3
#define TD_EXIT_STATE		4

#define TD_POLICY_PATH		"/etc/agentrt/cognition_policy.yaml"
#define TD_STATE_DIR		"/run/airymax/thinkdual"

/* 单次任务的复杂度评估输入。 */
struct td_input {
	const char	*intent;
	int		similarity;	/* Q16.16，历史相似度 */
	int		risk;		/* enum td_risk */
	unsigned int	tokens;		/* 预估 token 数 */
};

/* 生效策略，字段语义与策略文件一一对应。 */
struct td_policy {
	int	threshold;		/* Q16.16 复杂度阈值 */
	int	s1_enabled;
	int	s2_enabled;
	int	max_latency_ms;		/* System 1 时延上限 */
	int	cache_ttl_s;
	int	dag_max_depth;		/* System 2 规划深度 */
	int	llm_timeout_ms;		/* System 2 推理超时 */
	int	feedback;
	int	hot_reload;
	int	audit;
};

/* 一次切换的结果。 */
struct td_decision {
	enum airy_think_mode	mode;
	int			score;		/* Q16.16 */
	int			latency_ms;
	const char		*reason;
};

/* 运行态：策略、策略文件指纹与最近一次同步结论。 */
struct td_core {
	struct td_policy	pol;
	unsigned long long	stamp;
	int			have_cache;
	int			source;
	int			reloaded;
};

/* 决策计数与最近一次模式，供 status 动作读取。 */
struct td_counters {
	unsigned long	decisions;
	unsigned long	fast;
	unsigned long	slow;
	int		last;		/* enum airy_think_mode 或 -1 */
};

int td_parse_q16(const char *text, int *out);
int td_milli(int q16);
int td_risk_parse(const char *text, int *out);
const char *td_risk_name(int risk);
const char *td_mode_name(enum airy_think_mode mode);
int td_write_atom(const char *path, const char *body);

int td_score(const struct td_input *in, int *score);
int td_decide(const struct td_policy *pol, const struct td_input *in,
	      struct td_decision *out);

int td_pol_default(struct td_policy *pol);
int td_pol_read(const char *path, struct td_policy *pol,
		unsigned long long *stamp);

int td_cache_load(const char *dir, struct td_core *core);
int td_cache_save(const char *dir, const struct td_core *core);
int td_core_open(struct td_core *core, const char *policy, const char *state);
int td_core_sync(struct td_core *core, const char *policy,
		 const char *state);

int td_state_load(const char *dir, struct td_counters *cnt);
int td_state_save(const char *dir, const struct td_counters *cnt);
int td_journal_add(const char *dir, const struct td_input *in,
		   const struct td_decision *dec);
int td_status_print(const char *dir);

#endif /* AIRY_THINKDUAL_API_H */
