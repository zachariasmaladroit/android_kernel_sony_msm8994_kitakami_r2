/*
 * Generic entry point for the idle threads
 */
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/tick.h>
#include <linux/mm.h>
#include <linux/stackprotector.h>
#include <linux/percpu.h>

#include "../sched/sched.h"

#include <asm/tlb.h>

#include <trace/events/power.h>

int __read_mostly cpu_idle_force_poll;

void cpu_idle_poll_ctrl(bool enable)
{
	if (enable) {
		cpu_idle_force_poll++;
	} else {
		cpu_idle_force_poll--;
		WARN_ON_ONCE(cpu_idle_force_poll < 0);
	}

	/* Make sure poll mode is entered on all CPUs after the flag is set */
	mb();
}

static DEFINE_PER_CPU(int, idle_force_poll);

void per_cpu_idle_poll_ctrl(int cpu, bool enable)
{
	if (enable) {
		per_cpu(idle_force_poll, cpu)++;
	} else {
		per_cpu(idle_force_poll, cpu)--;
		WARN_ON_ONCE(per_cpu(idle_force_poll, cpu) < 0);
	}

	/*
	 * Make sure poll mode is entered on the relevant CPU after the flag is
	 * set
	 */
	mb();
}

#ifdef CONFIG_GENERIC_IDLE_POLL_SETUP
static int __init cpu_idle_poll_setup(char *__unused)
{
	cpu_idle_force_poll = 1;
	return 1;
}
__setup("nohlt", cpu_idle_poll_setup);

static int __init cpu_idle_nopoll_setup(char *__unused)
{
	cpu_idle_force_poll = 0;
	return 1;
}
__setup("hlt", cpu_idle_nopoll_setup);
#endif

static inline int cpu_idle_poll(void)
{
	rcu_idle_enter();
	trace_cpu_idle_rcuidle(0, smp_processor_id());
	local_irq_enable();
	while (!tif_need_resched() && (cpu_idle_force_poll ||
		__get_cpu_var(idle_force_poll) ||
		tick_check_broadcast_expired()))
		cpu_relax();
	trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, smp_processor_id());
	rcu_idle_exit();
	return 1;
}

/* Weak implementations for optional arch specific functions */
void __weak arch_cpu_idle_prepare(void) { }
void __weak arch_cpu_idle_enter(void) { }
void __weak arch_cpu_idle_exit(void) { }
void __weak arch_cpu_idle_dead(void) { }
void __weak arch_cpu_idle(void)
{
	cpu_idle_force_poll = 1;
	local_irq_enable();
}

/*
 * Generic idle loop implementation
 *
 * Called with polling cleared.
 */
static void cpu_idle_loop(void)
{
	int cpu = smp_processor_id();

	while (1) {
	        /*
		 * If the arch has a polling bit, we maintain an invariant:
		 *
		 * Our polling bit is clear if we're not scheduled (i.e. if
		 * rq->curr != rq->idle).  This means that, if rq->idle has
		 * the polling bit set, then setting need_resched is
		 * guaranteed to cause the cpu to reschedule.
		 */

                __current_set_polling();
		tick_nohz_idle_enter();

		while (!need_resched()) {
			check_pgt_cache();
			rmb();

			local_irq_disable();
			arch_cpu_idle_enter();

			/*
			 * In poll mode we reenable interrupts and spin.
			 *
			 * Also if we detected in the wakeup from idle
			 * path that the tick broadcast device expired
			 * for us, we don't want to go deep idle as we
			 * know that the IPI is going to arrive right
			 * away
			 */
			if (cpu_idle_force_poll ||
			    tick_check_broadcast_expired() ||
			    __get_cpu_var(idle_force_poll)) {
				cpu_idle_poll();
			} else {
				if (!current_clr_polling_and_test()) {
					stop_critical_timings();
					rcu_idle_enter();
					if (!need_resched())
						arch_cpu_idle();
					else
						local_irq_enable();
					WARN_ON_ONCE(irqs_disabled());
					rcu_idle_exit();
					start_critical_timings();
				} else {
					local_irq_enable();
				}
				__current_set_polling();
			}
			arch_cpu_idle_exit();
		}
		tick_nohz_idle_exit();
	        __current_clr_polling();

		/*
		 * We promise to call sched_ttwu_pending and reschedule
		 * if need_resched is set while polling is set.  That
		 * means that clearing polling needs to be visible
		 * before doing these things.
                 */
                smp_mb__after_atomic();

                sched_ttwu_pending();
		schedule_preempt_disabled();
		if (cpu_is_offline(cpu))
			arch_cpu_idle_dead();

	}
}

void cpu_startup_entry(enum cpuhp_state state)
{
	/*
	 * This #ifdef needs to die, but it's too late in the cycle to
	 * make this generic (arm and sh have never invoked the canary
	 * init for the non boot cpus!). Will be fixed in 3.11
	 */
#ifdef CONFIG_X86
	/*
	 * If we're the non-boot CPU, nothing set the stack canary up
	 * for us. The boot CPU already has it initialized but no harm
	 * in doing it again. This is a good place for updating it, as
	 * we wont ever return from this function (so the invalid
	 * canaries already on the stack wont ever trigger).
	 */
	boot_init_stack_canary();
#endif
	arch_cpu_idle_prepare();
	per_cpu(idle_force_poll, smp_processor_id()) = 0;
	cpu_idle_loop();
}
