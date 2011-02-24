#include <arch/arch.h>
#include <stdbool.h>
#include <errno.h>
#include <vcore.h>
#include <mcs.h>
#include <sys/param.h>
#include <parlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <rstdio.h>
#include <glibc-tls.h>

/* starting with 1 since we alloc vcore0's stacks and TLS in vcore_init(). */
static size_t _max_vcores_ever_wanted = 1;
static mcs_lock_t _vcore_lock = MCS_LOCK_INIT;

/* Which operations we'll call for the 2LS.  Will change a bit with Lithe.  For
 * now, there are no defaults. */
struct schedule_ops default_2ls_ops = {0};
struct schedule_ops *sched_ops = &default_2ls_ops;

extern void** vcore_thread_control_blocks;

/* Get a TLS, returns 0 on failure.  Vcores have their own TLS, and any thread
 * created by a user-level scheduler needs to create a TLS as well. */
void *allocate_tls(void)
{
	extern void *_dl_allocate_tls(void *mem) internal_function;
	void *tcb = _dl_allocate_tls(NULL);
	if (!tcb)
		return 0;
	/* Make sure the TLS is set up properly - its tcb pointer points to itself.
	 * Keep this in sync with sysdeps/ros/XXX/tls.h.  For whatever reason,
	 * dynamically linked programs do not need this to be redone, but statics
	 * do. */
	tcbhead_t *head = (tcbhead_t*)tcb;
	head->tcb = tcb;
	head->self = tcb;
	return tcb;
}

/* TODO: probably don't want to dealloc.  Considering caching */
static void free_transition_tls(int id)
{
	extern void _dl_deallocate_tls (void *tcb, bool dealloc_tcb) internal_function;
	if(vcore_thread_control_blocks[id])
	{
		_dl_deallocate_tls(vcore_thread_control_blocks[id],true);
		vcore_thread_control_blocks[id] = NULL;
	}
}

static int allocate_transition_tls(int id)
{
	/* We want to free and then reallocate the tls rather than simply 
	 * reinitializing it because its size may have changed.  TODO: not sure if
	 * this is right.  0-ing is one thing, but freeing and reallocating can be
	 * expensive, esp if syscalls are involved.  Check out glibc's
	 * allocatestack.c for what might work. */
	free_transition_tls(id);

	void *tcb = allocate_tls();

	if ((vcore_thread_control_blocks[id] = tcb) == NULL) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

static void free_transition_stack(int id)
{
	// don't actually free stacks
}

static int allocate_transition_stack(int id)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[id];
	if (vcpd->transition_stack)
		return 0; // reuse old stack

	void* stackbot = mmap(0, TRANSITION_STACK_SIZE,
	                      PROT_READ|PROT_WRITE|PROT_EXEC,
	                      MAP_POPULATE|MAP_ANONYMOUS, -1, 0);

	if(stackbot == MAP_FAILED)
		return -1; // errno set by mmap

	vcpd->transition_stack = (uintptr_t)stackbot + TRANSITION_STACK_SIZE;

	return 0;
}

int vcore_init()
{
	static int initialized = 0;
	if(initialized)
		return 0;

	vcore_thread_control_blocks = (void**)calloc(max_vcores(),sizeof(void*));

	if(!vcore_thread_control_blocks)
		goto vcore_init_fail;

	/* Need to alloc vcore0's transition stuff here (technically, just the TLS)
	 * so that schedulers can use vcore0's transition TLS before it comes up in
	 * vcore_entry() */
	if(allocate_transition_stack(0) || allocate_transition_tls(0))
		goto vcore_init_tls_fail;

	initialized = 1;
	return 0;

vcore_init_tls_fail:
	free(vcore_thread_control_blocks);
vcore_init_fail:
	errno = ENOMEM;
	return -1;
}

int vcore_request(size_t k)
{
	int ret = -1;
	size_t i,j;

	if(vcore_init() < 0)
		return -1;

	// TODO: could do this function without a lock once we 
	// have atomic fetch and add in user space
	mcs_lock_lock(&_vcore_lock);

	size_t vcores_wanted = num_vcores() + k;
	if(k < 0 || vcores_wanted > max_vcores())
	{
		errno = EAGAIN;
		goto fail;
	}

	for(i = _max_vcores_ever_wanted; i < vcores_wanted; i++)
	{
		if(allocate_transition_stack(i) || allocate_transition_tls(i))
			goto fail; // errno set by the call that failed
		_max_vcores_ever_wanted++;
	}
	ret = sys_resource_req(RES_CORES, vcores_wanted, 1, 0);

fail:
	mcs_lock_unlock(&_vcore_lock);
	return ret;
}

void vcore_yield()
{
	sys_yield(0);
}

size_t max_vcores()
{
	return MIN(__procinfo.max_vcores, MAX_VCORES);
}

size_t num_vcores()
{
	return __procinfo.num_vcores;
}

int vcore_id()
{
	return __vcoreid;
}

/* Deals with a pending preemption (checks, responds).  If the 2LS registered a
 * function, it will get run.  Returns true if you got preempted.  Called
 * 'check' instead of 'handle', since this isn't an event handler.  It's the "Oh
 * shit a preempt is on its way ASAP". */
bool check_preempt_pending(uint32_t vcoreid)
{
	bool retval = FALSE;
	if (__procinfo.vcoremap[vcoreid].preempt_pending) {
		retval = TRUE;
		if (sched_ops->preempt_pending)
			sched_ops->preempt_pending();
		/* this tries to yield, but will pop back up if this was a spurious
		 * preempt_pending. */
		sys_yield(TRUE);
	}
	return retval;
}
