/* Copyright 2015 Google Inc.
 *
 * See LICENSE for details.
 */

#include <arch/iommu.h>
#include <acpi.h>
#include <page_alloc.h>
#include <kmalloc.h>
#include <pmap.h>

void init_iommu(void)
{
	uint64_t irtar, capability, ext_capability;
	uint32_t version, status;
	struct Atable **drhds;
	int i, drhd_num;

	// Find all the different IOMMUs
	if (!dmar) {
		printk("IOMMU Not Found");
		return;
	}
	drhds = dmar->children;
	struct Drhd *tempdrhd;
	for (i = 0, drhd_num = 0; i < dmar->nchildren; i++, drhds++) {
		if ((*drhds)->type != 0)
			continue;
		tempdrhd = (*drhds)->tbl;
		printk("IOMMU RBA VALUE FOR i: %d, 0x%llx\n", i, tempdrhd->rba);
		iommu_regs[drhd_num] = (uintptr_t)KADDR(tempdrhd->rba);
		if(!iommu_regs[drhd_num])
			panic("Unable to map IOMMU");
		drhd_num++;
	}
	iommu_count = drhd_num;


	/* I don't technically need this for now. But we will probably want
	* something for the DMA remapping.
	*/
	//rootp = get_cont_pages(0, KMALLOC_WAIT);

	/* Allocate the full 1MB of pages for possible IRTE entries. */
	irtep = get_cont_pages(8, KMALLOC_WAIT);


	if (irtep != NULL) {

		/* The IRTAR must be 4KB aligned. The low 12 bits of the
		* address are used to determine if X2APIC is enabled, and
		* the number of entries in the table.
		* Extended Interrupt Mode states that the X2APIC is being
		* used, which the 0x0f states that there are 2^(X+1), X = 15
		* entries in this IRT.
		*/
		irtar = (uint64_t)PADDR(irtep) & ~0xfff;
		irtar |= EXTENDED_INTERRUPT_MODE | 0xf;
		printk("irtep : %p\n", irtep);
		printk("IOMMU IRTAR : 0x%llx\n", irtar);

		// Write it to the Interrupt Remapping Table Address Register
		for (i = 0; i < iommu_count; i++) {
			capability = get_iommu_reg64(i, CAPABILITY_REG_OFFSET);
			printk("CAPABILITY: 0x%llx\n", capability);

			ext_capability =
			    get_iommu_reg64(i, EXTENDED_CAPABILITY_REG_OFFSET);
			printk("EXTENDED CAPABILITY: 0x%llx\n",
			       ext_capability);

			fault_offset = 16 * ((capability >> 24) & 0x3ff);
			printk("FAULT OFFSET: 0x%llx\n", fault_offset);
			num_fault_regs = ((capability >> 40) & 0xff) + 1;
			printk("NUM_FAULT_REGS: 0x%llx\n", num_fault_regs);

			// Read version.

			version = get_iommu_reg32(i, IOMMU_VERSION_REG_OFFSET);
			printk("IOMMU VERSION: %ld\n", version);

			// Set up Interrupt Remapping Pointer
			set_iommu_reg64(i, IRTE_BASE_REG_OFFSET, irtar);

			// Set command bit.
			set_gcr(i, SET_INTERRUPT_REMAP_TABLE_PTR);

			// Initialize Interrupt Remapping
			status = get_gsr(i);
			printk("STATUS BEFORE ENABLE: 0x%lx\n", status);

			set_gcr(i, INTERRUPT_REMAPPING_ENABLE);
			status = get_gsr(i);
			printk("STATUS AFTER ENABLE: 0x%lx\n", status);

			print_fault_regs(i);
		}
		// Register that there is an iommu that is working
		iommu_active = 1;

	} else {
		printk("Failed to allocate interrupt remapping table pages!\n");
		printk("IOMMU not initialized");
	}
}

void init_irte(uint16_t irte_index, uint32_t dest_id, uint8_t vector,
               uint8_t delivery_mode, uint16_t bdf) {
	struct irte * curirte;

	curirte = &(((struct irte *)irtep)[irte_index]);

	if (curirte->low & IRTE_PRESENT)
		panic("TRIED TO ALLOCATE ALREADY PRESENT IRTE");

	if (delivery_mode > DELIVERY_MODE_LAST || delivery_mode == 3 ||
	    delivery_mode == 6) {
		panic("INVALID DELIVERY MODE!");
	}

	curirte->low = 0;
	curirte->high = 0;

	curirte->low = IRTE_PRESENT |
	               IRTE_TRIGGER_MODE |
	               //IRTE_DESTINATION_MODE |
	               //IRTE_REDIRECTION_HINT |
	               delivery_mode << 5 |
	               (uint64_t)vector << 16 |
	               (uint64_t)dest_id << 32;

	curirte->high = IRTE_SOURCE_ID(bdf) |
	                IRTE_SOURCE_ID_QUAL_ALL |
	                IRTE_SOURCE_VALIDATION_ON;

	printk("IRTE LOW: 0x%llx\n", curirte->low);
	printk("IRTE HIGH: 0x%llx\n", curirte->high);

}

void print_fault_regs_all(void)
{
	for (int i = 0; i < iommu_count; i++) {
		printk("IOMMU %d\n", i);
		print_fault_regs(i);
	}
}

void print_fault_regs(int index)
{
	int fault_status;
	int fault_index;
	uint64_t fault_reg_low, fault_reg_high;

	fault_status = get_iommu_reg32(index, FAULT_STATUS_REG_OFFSET);
	if (fault_status & 0x0002) {
		fault_index = (fault_status >> 8) & 0xFF;
		printk("Fault Status Register: 0x%lx\n", fault_status);

		fault_reg_low = get_iommu_reg64(index, fault_offset +
		                                (16 * fault_index));
		fault_reg_high = get_iommu_reg64(index, fault_offset + 8 +
		                                 (16 * fault_index));
		while (fault_reg_high >> 63) {
			printk("Fault Register at index 0x%lx Low: 0x%llx\n",
			       fault_index, fault_reg_low);
			printk("Fault Register at index 0x%lx High: 0x%llx\n",
			       fault_index, fault_reg_high);

			// Clear the fault by writing back the 1.
			set_iommu_reg64(index, fault_offset + 8 +
			                (16 * fault_index), fault_reg_high);

			fault_index++;
			fault_reg_low = get_iommu_reg64(index, fault_offset +
				                        (16 * fault_index));
			fault_reg_high = get_iommu_reg64(index, fault_offset + 8 +
				                         (16 * fault_index));

		}
	}
}

void set_gcr(int index, uint32_t value)
{
	uint32_t status;

	// Check reserved bits.
	if (value & 0x7FFFFF)
		panic("Setting reserved bits in Global Command Register");

	status = get_gsr(index) | value;
	write_mmreg32(iommu_regs[index] + GLOBAL_COMMAND_REG_OFFSET, status);

	// Wait till the status bit reflects the change.
	do {
		status = get_gsr(index);
	} while (status != (status | value));
}

