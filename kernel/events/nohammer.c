/*
 * Attempt to prevent rowhammer attack.
 *
 * On many new DRAM chips, repeated read access to nearby cells can cause
 * victim cell to flip bits. Unfortunately, that can be used to gain root
 * on affected machine, or to execute native code from javascript, escaping
 * the sandbox.
 *
 * Fortunately, a lot of memory accesses is needed between DRAM refresh
 * cycles. This is rather unusual workload, and we can detect it, and
 * prevent the DRAM accesses, before bit flips happen.
 *
 * Thanks to Peter Zijlstra <peterz@infradead.org>.
 * Thanks to presentation at blackhat.
 */

#include <linux/perf_event.h>
#include <linux/module.h>
#include <linux/delay.h>

static struct perf_event_attr rh_attr = {
	.type	= PERF_TYPE_HARDWARE,
	.config = PERF_COUNT_HW_CACHE_MISSES,
	.size	= sizeof(struct perf_event_attr),
	.pinned	= 1,
	.sample_period = 10000,
};

/*
 * How often is the DRAM refreshed. Setting it too high is safe.
 */
static int dram_refresh_msec = 64;

static DEFINE_PER_CPU(struct perf_event *, rh_event);
static DEFINE_PER_CPU(u64, rh_timestamp);

static void rh_overflow(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs)
{
	u64 *ts = this_cpu_ptr(&rh_timestamp); /* this is NMI context */
	u64 now = ktime_get_mono_fast_ns();
	s64 delta = now - *ts;

	*ts = now;

	if (delta < dram_refresh_msec * NSEC_PER_MSEC)
		mdelay(dram_refresh_msec);
}

static __init int rh_module_init(void)
{
	int cpu;

/*
 * DRAM refresh is every 64 msec. That is not enough to prevent rowhammer.
 * Some vendors doubled the refresh rate to 32 msec, that helps a lot, but
 * does not close the attack completely. 8 msec refresh would probably do
 * that on almost all chips.
 *
 * Thinkpad X60 can produce cca 12,200,000 cache misses a second, that's
 * 780,800 cache misses per 64 msec window.
 *
 * X60 is from generation that is not yet vulnerable from rowhammer, and
 * is pretty slow machine. That means that this limit is probably very
 * safe on newer machines.
 */
	int cache_misses_per_second = 12200000;

/*
 * Maximum permitted utilization of DRAM. Setting this to f will mean that
 * when more than 1/f of maximum cache-miss performance is used, delay will
 * be inserted, and will have similar effect on rowhammer as refreshing memory
 * f times more often.
 *
 * Setting this to 8 should prevent the rowhammer attack.
 */
	int dram_max_utilization_factor = 8;

	/*
	 * Hardware should be able to do approximately this many
	 * misses per refresh
	 */
	int cache_miss_per_refresh = (cache_misses_per_second * dram_refresh_msec)/1000;

	/*
	 * So we do not want more than this many accesses to DRAM per
	 * refresh.
	 */
	int cache_miss_limit = cache_miss_per_refresh / dram_max_utilization_factor;

/*
 * DRAM is shared between CPUs, but these performance counters are per-CPU.
 */
	int max_attacking_cpus = 2;

	/*
	 * We ignore counter overflows "too far away", but some of the
	 * events might have actually occurent recently. Thus additional
	 * factor of 2
	 */

	rh_attr.sample_period = cache_miss_limit / (2*max_attacking_cpus);

	printk("Rowhammer protection limit is set to %d cache misses per %d msec\n",
	       (int) rh_attr.sample_period, dram_refresh_msec);

	/* XXX borken vs hotplug */

	for_each_online_cpu(cpu) {
		struct perf_event *event;

		event = perf_event_create_kernel_counter(&rh_attr, cpu, NULL, rh_overflow, NULL);
		per_cpu(rh_event, cpu) = event;		
		if (!event) {
			pr_err("Not enough resources to initialize nohammer on cpu %d\n", cpu);
			continue;
		}
		pr_info("Nohammer initialized on cpu %d\n", cpu);
	}
	return 0;
}

static __exit void rh_module_exit(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct perf_event *event = per_cpu(rh_event, cpu);

		if (event)
			perf_event_release_kernel(event);
	}
	return;
}

module_init(rh_module_init);
module_exit(rh_module_exit);

MODULE_DESCRIPTION("Rowhammer protection");
//MODULE_LICENSE("GPL v2+");
MODULE_LICENSE("GPL");
