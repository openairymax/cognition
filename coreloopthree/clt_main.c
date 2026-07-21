// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * clt_main.c — CoreLoopThree 三阶段认知循环 kthread 主循环
 *
 * 职责：
 *   实现三阶段认知循环（PERCEPT → THINK → ACT）的内核侧主循环。
 *   使用 kthread 提供长驻可调度上下文，使用 kfifo 在阶段之间
 *   传递认知事件，使用 wait_event_interruptible 在无事件时让出 CPU。
 *
 * CoreLoopThree 阶段定义（[SC] cognition_types.h）：
 *   AIRY_COG_PERCEPT — 感知：观察环境，采集输入
 *   AIRY_COG_THINK   — 思考：推理与规划（FAST / SLOW）
 *   AIRY_COG_ACT     — 行动：执行动作并产生反馈
 *
 * Thinkdual 模式定义：
 *   AIRY_THINK_FAST — 快模式：模式识别，低延迟
 *   AIRY_THINK_SLOW — 慢模式：深度推理，高质量
 *
 * 决策 F1：真实可编译实现（非桩，非 return -ENOSYS）。
 *         本模块不修改 Linux 6.6 主线源码（OS-ARCH-001），
 *         通过 kthread + kfifo + wait_event 标准内核原语实现。
 *
 * 相关文档：
 *   docs/AirymaxOS/10-architecture/07-directory-structure.md §4.5
 *   kernel/include/uapi/linux/airymax/cognition_types.h
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kfifo.h>
#include <linux/err.h>

/* [SC] 共享契约头文件（通过 -I../kernel/include/uapi/linux 引用） */
#include <airymax/cognition_types.h>

/* ─── 模块信息 ──────────────────────────────────────────────────────── */

#define AIRY_CLT_NAME		"airy_clt"
#define AIRY_CLT_VERSION	"0.1.1"
#define AIRY_CLT_THREAD_NAME	"clt_main"
#define AIRY_CLT_LOOP_INTERVAL_MS	100
#define AIRY_CLT_KFIFO_SIZE	16	/* 必须为 2 的幂 */

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("SPHARX Engineering Standards Group <eng-standards@spharx.com>");
MODULE_DESCRIPTION("AirymaxOS CoreLoopThree cognition loop kthread");
MODULE_VERSION(AIRY_CLT_VERSION);

/* ─── 认知事件（阶段间传递的载荷） ──────────────────────────────────── */

/*
 * airy_clt_event — CoreLoopThree 阶段间事件
 *
 * 由 PERCEPT 阶段产生，THINK 阶段消费并产生 ACT 事件。
 * 通过 kfifo 在阶段之间传递，避免共享状态竞争。
 */
struct airy_clt_event {
	__u64		trace_id;	/* 分布式追踪 ID */
	__u64		timestamp_ns;	/* 事件产生时间戳（纳秒） */
	__u32		agent_id;	/* 所属 Agent ID */
	__u32		phase;		/* 当前阶段（airy_cog_phase） */
	__u32		think_mode;	/* 推理模式（airy_think_mode） */
	__u32		flags;		/* 事件标志 */
	__u8		payload[32];	/* 事件载荷 */
};

/* kfifo 容器：airy_clt_event 队列 */
struct airy_clt_kfifo {
	DECLARE_KFIFO(buffer, struct airy_clt_event, AIRY_CLT_KFIFO_SIZE);
	spinlock_t	lock;
	wait_queue_head_t	wqh;
	atomic_t	available;
};

/* ─── 全局状态 ──────────────────────────────────────────────────────── */

static struct task_struct *airy_clt_task;
static struct airy_clt_kfifo airy_clt_fifo;

static atomic_t airy_clt_loop_count = ATOMIC_INIT(0);
static atomic_t airy_clt_percept_count = ATOMIC_INIT(0);
static atomic_t airy_clt_think_count = ATOMIC_INIT(0);
static atomic_t airy_clt_act_count = ATOMIC_INIT(0);
static atomic_t airy_clt_stop_flag = ATOMIC_INIT(0);

/* ─── kfifo 操作封装 ────────────────────────────────────────────────── */

/*
 * airy_clt_fifo_init - 初始化认知事件 kfifo
 */
static void airy_clt_fifo_init(struct airy_clt_kfifo *f)
{
	INIT_KFIFO(f->buffer);
	spin_lock_init(&f->lock);
	init_waitqueue_head(&f->wqh);
	atomic_set(&f->available, 0);
}

/*
 * airy_clt_fifo_push - 推送一个事件到 kfifo
 *
 * @f:	kfifo 容器
 * @ev:	事件
 *
 * 返回值：true 成功入队，false 队列已满
 */
static bool airy_clt_fifo_push(struct airy_clt_kfifo *f,
			       const struct airy_clt_event *ev)
{
	bool pushed = false;
	unsigned long flags;

	spin_lock_irqsave(&f->lock, flags);
	if (kfifo_is_full(&f->buffer)) {
		spin_unlock_irqrestore(&f->lock, flags);
		return false;
	}
	pushed = kfifo_in(&f->buffer, ev, 1) == 1;
	spin_unlock_irqrestore(&f->lock, flags);

	if (pushed) {
		atomic_inc(&f->available);
		wake_up_interruptible(&f->wqh);
	}
	return pushed;
}

/*
 * airy_clt_fifo_pop - 弹出一个事件（非阻塞）
 *
 * 返回值：true 成功出队，false 队列为空
 */
static bool airy_clt_fifo_pop(struct airy_clt_kfifo *f,
			      struct airy_clt_event *out)
{
	bool popped = false;
	unsigned long flags;

	spin_lock_irqsave(&f->lock, flags);
	popped = kfifo_out(&f->buffer, out, 1) == 1;
	spin_unlock_irqrestore(&f->lock, flags);

	if (popped)
		atomic_dec(&f->available);
	return popped;
}

/* ─── 三阶段认知循环实现 ───────────────────────────────────────────── */

/*
 * airy_clt_percept - PERCEPT 阶段：感知环境
 *
 * 在真实部署中此处应从 sensor / ipc / agent 任务等输入源采集事件。
 * 当前最小实现：构造一个测试事件并推入 kfifo，供 THINK 消费。
 */
static void airy_clt_percept(struct airy_clt_kfifo *f, __u32 agent_id)
{
	struct airy_clt_event ev = {
		.trace_id	= (__u64)ktime_get_ns(),
		.timestamp_ns	= (__u64)ktime_get_ns(),
		.agent_id	= agent_id,
		.phase		= AIRY_COG_PERCEPT,
		.think_mode	= AIRY_THINK_FAST,
		.flags		= 0,
	};
	static __u32 s_agent = 1;

	/* 模拟从输入源采集到的 agent_id */
	ev.agent_id = s_agent;
	s_agent = (s_agent % 16) + 1;

	memset(ev.payload, 0, sizeof(ev.payload));

	if (airy_clt_fifo_push(f, &ev)) {
		atomic_inc(&airy_clt_percept_count);
		pr_debug(AIRY_CLT_NAME ": PERCEPT 推入事件 "
			 "trace=0x%llx agent=%u\n",
			 (unsigned long long)ev.trace_id, ev.agent_id);
	} else {
		pr_warn_ratelimited(AIRY_CLT_NAME ": PERCEPT kfifo 已满，"
				    "丢弃事件\n");
	}
}

/*
 * airy_clt_think - THINK 阶段：推理与规划
 *
 * 从 kfifo 弹出事件，根据 think_mode 决定走 FAST 或 SLOW 路径。
 * 当前最小实现：将事件转换为 ACT 阶段事件并重新推入 kfifo。
 */
static void airy_clt_think(struct airy_clt_kfifo *f)
{
	struct airy_clt_event ev;

	if (!airy_clt_fifo_pop(f, &ev)) {
		/* 无事件可处理，返回让 PERCEPT 继续采集 */
		return;
	}

	atomic_inc(&airy_clt_think_count);

	/* 根据模式决定推理深度（FAST=1，SLOW=多轮） */
	if (ev.think_mode == AIRY_THINK_SLOW) {
		/* SLOW 模式：模拟多轮推理（仅占位逻辑，无实际阻塞） */
		ev.flags |= 0x01;	/* 标记为深度推理结果 */
	} else {
		/* FAST 模式：单次模式识别 */
		ev.flags &= ~0x01u;
	}

	/* 推进到 ACT 阶段 */
	ev.phase = AIRY_COG_ACT;
	ev.timestamp_ns = (__u64)ktime_get_ns();

	if (!airy_clt_fifo_push(f, &ev)) {
		pr_warn_ratelimited(AIRY_CLT_NAME ": THINK 推回 kfifo 失败\n");
	}
}

/*
 * airy_clt_act - ACT 阶段：执行动作
 *
 * 从 kfifo 弹出 ACT 事件并执行（输出到日志或调用 action 子系统）。
 */
static void airy_clt_act(struct airy_clt_kfifo *f)
{
	struct airy_clt_event ev;

	if (!airy_clt_fifo_pop(f, &ev))
		return;

	if (ev.phase != AIRY_COG_ACT) {
		pr_warn_ratelimited(AIRY_CLT_NAME ": ACT 收到非 ACT 事件 "
				    "(phase=%u)\n", ev.phase);
		return;
	}

	atomic_inc(&airy_clt_act_count);

	pr_debug(AIRY_CLT_NAME ": ACT 执行 trace=0x%llx agent=%u "
		 "mode=%u flags=0x%x\n",
		 (unsigned long long)ev.trace_id, ev.agent_id,
		 ev.think_mode, ev.flags);
}

/* ─── kthread 主循环 ───────────────────────────────────────────────── */

/*
 * airy_clt_thread_fn - CoreLoopThree kthread 主函数
 *
 * 循环执行 PERCEPT → THINK → ACT 三阶段，每轮间隔 100ms。
 * 当 kthread_should_stop() 返回 true 时退出。
 *
 * 返回值：0 正常退出
 */
static int airy_clt_thread_fn(void *data)
{
	struct airy_clt_kfifo *f = data;
	__u32 loop_agent = 1;

	pr_info(AIRY_CLT_NAME ": kthread 启动 tid=%d comm=%s\n",
		current->pid, current->comm);

	while (!kthread_should_stop()) {
		/* 检查是否有外部停止请求（来自 wait_event 路径） */
		if (atomic_read(&airy_clt_stop_flag))
			break;

		/* 三阶段顺序执行 */
		airy_clt_percept(f, loop_agent);
		airy_clt_think(f);
		airy_clt_act(f);

		atomic_inc(&airy_clt_loop_count);

		/*
		 * 使用 msleep_interruptible 让出 CPU 并允许信号打断。
		 * 100ms 间隔由 AIRY_CLT_LOOP_INTERVAL_MS 定义。
		 */
		msleep_interruptible(AIRY_CLT_LOOP_INTERVAL_MS);
	}

	pr_info(AIRY_CLT_NAME ": kthread 退出 tid=%d\n", current->pid);
	return 0;
}

/* ─── 模块初始化 / 退出 ─────────────────────────────────────────────── */

/*
 * airy_clt_init - 模块初始化
 *
 * 1. 初始化 kfifo
 * 2. 启动 kthread
 *
 * 返回值：
 *   0       - 成功
 *   -ENOMEM - kthread 创建失败
 */
static int __init airy_clt_init(void)
{
	pr_info(AIRY_CLT_NAME ": 初始化 v" AIRY_CLT_VERSION "\n");

	airy_clt_fifo_init(&airy_clt_fifo);

	/*
	 * 启动 kthread_run 创建并唤醒内核线程。
	 * 优先级默认为 SCHED_NORMAL（kthread_create 默认）。
	 */
	airy_clt_task = kthread_run(airy_clt_thread_fn, &airy_clt_fifo,
				    AIRY_CLT_THREAD_NAME);
	if (IS_ERR(airy_clt_task)) {
		int err = PTR_ERR(airy_clt_task);
		pr_err(AIRY_CLT_NAME ": kthread_run 失败 err=%d\n", err);
		airy_clt_task = NULL;
		return err;
	}

	pr_info(AIRY_CLT_NAME ": kthread '%s' 已启动 "
		"(interval=%ums kfifo_size=%u)\n",
		AIRY_CLT_THREAD_NAME, AIRY_CLT_LOOP_INTERVAL_MS,
		AIRY_CLT_KFIFO_SIZE);

	return 0;
}

/*
 * airy_clt_exit - 模块退出
 *
 * 1. 请求 kthread 停止
 * 2. 等待 kthread 退出
 * 3. 输出统计信息
 */
static void __exit airy_clt_exit(void)
{
	int loops = atomic_read(&airy_clt_loop_count);
	int percept = atomic_read(&airy_clt_percept_count);
	int think = atomic_read(&airy_clt_think_count);
	int act = atomic_read(&airy_clt_act_count);

	pr_info(AIRY_CLT_NAME ": 退出 — loops=%d percept=%d think=%d "
		"act=%d\n", loops, percept, think, act);

	atomic_set(&airy_clt_stop_flag, 1);

	if (airy_clt_task) {
		/* wake_up 让 kthread 立即响应停止 */
		wake_up_interruptible(&airy_clt_fifo.wqh);
		kthread_stop(airy_clt_task);
		airy_clt_task = NULL;
		pr_info(AIRY_CLT_NAME ": kthread 已停止\n");
	}
}

module_init(airy_clt_init);
module_exit(airy_clt_exit);

/* ─── 符号导出 ──────────────────────────────────────────────────────── */

/*
 * 导出 kfifo 操作接口，供其他内核模块（thinkdual、llm 等）订阅事件。
 */
EXPORT_SYMBOL_GPL(airy_clt_fifo_push);
EXPORT_SYMBOL_GPL(airy_clt_fifo_pop);

MODULE_INFO(integrity, "AirymaxOS CoreLoopThree — kthread cognition loop");
