#include <stdio.h>
#include <ros/syscall.h>
#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <parlib/uthread.h>

void valmon_vcore_entry(void) {
	uint32_t vcoreid = vcore_id();
	if (current_uthread) {
		assert(vcoreid == 0);
		run_current_uthread();
	}
	printf("vcore_entry: %d\n", vcoreid);
	while(1);
}

struct schedule_ops valmon_sched_ops = {
	.sched_entry = valmon_vcore_entry,
};

int main(int argc, char **argv)
{
	printf("Bonjour monde!\n");
	struct uthread dummy = {0};
	sched_ops = &valmon_sched_ops;
	uthread_2ls_init(&dummy, &valmon_sched_ops);

	/* Get core 3 when we call mcp_init() */
	sys_provision(getpid(), RES_CORES, 3);
	uthread_mcp_init();

	/* Provision for 5. */
	sys_provision(getpid(), RES_CORES, 5);
	udelay(10*1000*1000);

	/* Get core 5 and some other core. */
	vcore_request(2);
	printf("Done!\n");
	while(1);
}
