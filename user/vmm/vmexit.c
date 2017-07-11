/* Copyright (c) 2015-2016 Google Inc.
 * See LICENSE for details. */

#include <parlib/common.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>
#include <vmm/vmm.h>
#include <parlib/arch/trap.h>
#include <parlib/bitmask.h>
#include <stdio.h>

static bool pir_notif_is_set(struct vmm_gpcore_init *gpci)
{
	return GET_BITMASK_BIT(gpci->posted_irq_desc, VMX_POSTED_OUTSTANDING_NOTIF);
}

static bool rvi_is_set(struct guest_thread *gth)
{
	uint8_t rvi = gth_to_vmtf(gth)->tf_guest_intr_status & 0xff;

	return rvi != 0;
}

/* Blocks a guest pcore / thread until it has an IRQ pending.  Syncs with
 * vmm_interrupt_guest(). */
static void sleep_til_irq(struct guest_thread *gth)
{
	struct vmm_gpcore_init *gpci = gth_to_gpci(gth);

	/* The invariant is that if an IRQ is posted, but not delivered, we will not
	 * sleep.  Anyone who posts an IRQ must signal after setting it.
	 * vmm_interrupt_guest() does this.  If we use alternate sources of IRQ
	 * posting, we'll need to revist this.
	 *
	 * Although vmm_interrupt_guest() only writes OUTSTANDING_NOTIF, it's
	 * possible that the hardware attempted to post the interrupt.  In SDM
	 * parlance, the processor could have "recognized" the virtual IRQ, but not
	 * delivered it yet.  This could happen if the guest had executed "sti", but
	 * not "hlt" yet.  The IRQ was posted and recognized, but not delivered
	 * ("sti blocking").  Then the guest executes "hlt", and vmexits.
	 * OUTSTANDING_NOTIF will be clear in this case.  RVI should be set - at
	 * least to the vector we just sent, but possibly to a greater vector if
	 * multiple were sent.  RVI should only be cleared after virtual IRQs were
	 * actually delivered.  So checking OUTSTANDING_NOTIF and RVI should
	 * suffice.
	 *
	 * Generally, we should also check GUEST_INTERRUPTIBILITY_INFO to see if
	 * there's some reason to not deliver the interrupt and check things like
	 * the VPPR (priority register).  But since we're emulating a halt, mwait,
	 * or something else that needs to be woken by an IRQ, we can ignore that
	 * and just wake them up.  Note that we won't actually deliver the IRQ,
	 * we'll just restart the guest and the hardware will deliver the virtual
	 * IRQ at the appropriate time.  So in the event that something weird
	 * happens, the halt/mwait just returns spuriously.
	 *
	 * The more traditional race here is if the halt starts concurrently with
	 * the post; that's why we sync with the mutex to make sure there is an
	 * ordering between the actual halt (this function) and the posting. */
	uth_mutex_lock(gth->halt_mtx);
	while (!(pir_notif_is_set(gpci) || rvi_is_set(gth)))
		uth_cond_var_wait(gth->halt_cv, gth->halt_mtx);
	uth_mutex_unlock(gth->halt_mtx);
}

enum {
		CPUID_0B_LEVEL_SMT = 0,
		CPUID_0B_LEVEL_CORE
};

static bool handle_cpuid(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	struct virtual_machine *vm = gth_to_vm(gth);
	uint32_t level = vm_tf->tf_rcx & 0x0F;

	if (vm_tf->tf_rax != 0x0B)
		return FALSE;

	vm_tf->tf_rip += 2;
	vm_tf->tf_rax = 0;
	vm_tf->tf_rbx = 0;
	vm_tf->tf_rcx = level;
	vm_tf->tf_rdx = gth->gpc_id;
	if (level == CPUID_0B_LEVEL_SMT) {
		vm_tf->tf_rax = 0;
		vm_tf->tf_rbx = 1;
		vm_tf->tf_rcx |= ((level + 1) << 8);
	}
	if (level == CPUID_0B_LEVEL_CORE) {
		uint32_t shift = LOG2_UP(vm->nr_gpcs);

		if (shift > 0x1F)
			shift = 0x1F;
		vm_tf->tf_rax = shift;
		vm_tf->tf_rbx = vm->nr_gpcs;
		vm_tf->tf_rcx |= ((level + 1) << 8);
	}

	return TRUE;
}

static bool handle_ept_fault(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	struct virtual_machine *vm = gth_to_vm(gth);
	uint64_t gpa, *regp;
	uint8_t regx;
	int store, size;
	int advance;

	int ret = decode(gth, &gpa, &regx, &regp, &store, &size, &advance);

	if (ret < 0)
		return FALSE;
	if (ret == VM_PAGE_FAULT) {
		/* We were unable to translate RIP due to an ept fault */
		vm_tf->tf_trap_inject = VM_TRAP_VALID
		                      | VM_TRAP_ERROR_CODE
		                      | VM_TRAP_HARDWARE
		                      | HW_TRAP_PAGE_FAULT;
		return TRUE;
	}

	assert(size >= 0);
	/* TODO use helpers for some of these addr checks.  the fee/fec ones might
	 * be wrong too. */
	for (int i = 0; i < VIRTIO_MMIO_MAX_NUM_DEV; i++) {
		if (vm->virtio_mmio_devices[i] == NULL)
			continue;
		if (PG_ADDR(gpa) != vm->virtio_mmio_devices[i]->addr)
			continue;
		/* TODO: can the guest cause us to spawn off infinite threads? */
		if (store)
			virtio_mmio_wr(vm, vm->virtio_mmio_devices[i], gpa, size,
			               (uint32_t *)regp);
		else
			*regp = virtio_mmio_rd(vm, vm->virtio_mmio_devices[i], gpa, size);
		vm_tf->tf_rip += advance;
		return TRUE;
	}
	if (PG_ADDR(gpa) == 0xfec00000) {
		do_ioapic(gth, gpa, regx, regp, store);
	} else if (PG_ADDR(gpa) == 0) {
		memmove(regp, &vm->low4k[gpa], size);
	} else {
		fprintf(stderr, "EPT violation: can't handle %p\n", gpa);
		fprintf(stderr, "RIP %p, exit reason 0x%x\n", vm_tf->tf_rip,
				vm_tf->tf_exit_reason);
		fprintf(stderr, "Returning 0xffffffff\n");
		showstatus(stderr, gth);
		/* Just fill the whole register for now. */
		*regp = (uint64_t) -1;
		return FALSE;
	}
	vm_tf->tf_rip += advance;
	return TRUE;
}

static bool handle_vmcall_printc(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	uint8_t byte;

	byte = vm_tf->tf_rdi;
	printf("%c", byte);
	if (byte == '\n')
		printf("%c", '%');
	fflush(stdout);
	return TRUE;
}

static bool handle_vmcall_smpboot(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	struct vm_trapframe *vm_tf_ap;
	struct virtual_machine *vm = gth_to_vm(gth);
	int cur_pcores = vm->up_gpcs;

	/* Check if we're guest pcore 0. Only the BSP is allowed to start APs. */
	if (vm_tf->tf_guest_pcoreid != 0) {
		fprintf(stderr,
		        "Only guest pcore 0 is allowed to start APs. core was %ld\n",
		        vm_tf->tf_guest_pcoreid);
		return FALSE;
	}

	/* Check if we've reached the maximum, if yes, blow out. */
	if (vm->nr_gpcs == cur_pcores) {
		fprintf(stderr,
		        "guest tried to start up too many cores. max was %ld, current up %ld\n",
		        vm->nr_gpcs, cur_pcores);
		return FALSE;
	}

	/* Start up secondary core. */
	vm_tf_ap = gth_to_vmtf(vm->gths[cur_pcores]);
	/* We use the BSP's CR3 for now. This should be fine because they
	 * change it later anyway. */
	vm_tf_ap->tf_cr3 = vm_tf->tf_cr3;

	/* Starting RIP is passed in via rdi. */
	vm_tf_ap->tf_rip = vm_tf->tf_rdi;

	/* Starting RSP is passed in via rsi. */
	vm_tf_ap->tf_rsp = vm_tf->tf_rsi;

	vm->up_gpcs++;

	start_guest_thread(vm->gths[cur_pcores]);

	return TRUE;
}

static bool handle_vmcall(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	bool retval = FALSE;

	if (gth->vmcall)
		return gth->vmcall(gth, vm_tf);

	switch (vm_tf->tf_rax) {
		case VMCALL_PRINTC:
			retval = handle_vmcall_printc(gth);
			break;
		case VMCALL_SMPBOOT:
			retval = handle_vmcall_smpboot(gth);
			break;
	}

	if (retval)
		vm_tf->tf_rip += 3;

	return retval;
}

static bool handle_io(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	int ret = io(gth);

	if (ret < 0)
		return FALSE;
	if (ret == VM_PAGE_FAULT) {
		/* We were unable to translate RIP due to an ept fault */
		vm_tf->tf_trap_inject = VM_TRAP_VALID
		                      | VM_TRAP_ERROR_CODE
		                      | VM_TRAP_HARDWARE
		                      | HW_TRAP_PAGE_FAULT;
	}
	return TRUE;
}

static bool handle_msr(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	if (msrio(gth, gth_to_gpci(gth), vm_tf->tf_exit_reason)) {
		/* Use event injection through vmctl to send a general protection fault
		 * vmctl.interrupt gets written to the VM-Entry Interruption-Information
		 * Field by vmx */
		vm_tf->tf_trap_inject = VM_TRAP_VALID
		                      | VM_TRAP_ERROR_CODE
		                      | VM_TRAP_HARDWARE
		                      | HW_TRAP_GP_FAULT;
	} else {
		vm_tf->tf_rip += 2;
	}
	return TRUE;
}

static bool handle_apic_access(struct guest_thread *gth)
{
	uint64_t gpa, *regp;
	uint8_t regx;
	int store, size;
	int advance;
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	if (decode(gth, &gpa, &regx, &regp, &store, &size, &advance))
		return FALSE;
	if (__apic_access(gth, gpa, regx, regp, store))
		return FALSE;
	vm_tf->tf_rip += advance;
	return TRUE;
}

static bool handle_halt(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	if (gth->halt_exit)
		return FALSE;
	/* It's possible the guest disabled IRQs and halted, perhaps waiting on an
	 * NMI or something.  If we need to support that, we can change this.  */
	sleep_til_irq(gth);
	vm_tf->tf_rip += 1;
	return TRUE;
}

/* Is this a vmm specific thing?  or generic?
 *
 * what do we do when we want to kill the vm?  what are our other options? */
bool handle_vmexit(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	switch (vm_tf->tf_exit_reason) {
	case EXIT_REASON_CPUID:
		return handle_cpuid(gth);
	case EXIT_REASON_EPT_VIOLATION:
		return handle_ept_fault(gth);
	case EXIT_REASON_VMCALL:
		return handle_vmcall(gth);
	case EXIT_REASON_IO_INSTRUCTION:
		return handle_io(gth);
	case EXIT_REASON_MSR_WRITE:
	case EXIT_REASON_MSR_READ:
		return handle_msr(gth);
	case EXIT_REASON_APIC_ACCESS:
		return handle_apic_access(gth);
	case EXIT_REASON_HLT:
		return handle_halt(gth);
	case EXIT_REASON_EXTERNAL_INTERRUPT:
	case EXIT_REASON_APIC_WRITE:
		/* TODO: just ignore these? */
		return TRUE;
	default:
		fprintf(stderr, "VMM library: don't know how to handle exit %d\n",
		        vm_tf->tf_exit_reason);
		fprintf(stderr, "RIP %p, shutdown 0x%x\n", vm_tf->tf_rip,
		        vm_tf->tf_exit_reason);
		return FALSE;
	}
}
