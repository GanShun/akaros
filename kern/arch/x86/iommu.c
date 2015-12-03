/* Copyright 2015 Google Inc.
 *
 * See LICENSE for details.
 */

#include <arch/iommu.h>
#include <page_alloc.h>
#include <kmalloc.h>
#include <pmap.h>

void init_iommu(void)
{
	uint64_t irtar, capability, ext_capability;
	uint64_t *irtarp;
	uint32_t version, status;
	int init = 0;
	int i;

	if (rootp == NULL)
		rootp = get_cont_pages(0, KMALLOC_WAIT);

	if (irtep == NULL) {
		irtep = get_cont_pages(8, KMALLOC_WAIT);
		init = 1;
	}
	if (irtep != NULL && init == 1 && rootp != NULL) {
		irtar = (uint64_t)PADDR(irtep) & ~0xfff;
		irtar |= 0x80f;
		printk("irtep : %p\n", irtep);
		printk("IOMMU IRTAR : 0x%llx\n", irtar);

		// Write it to the Interrupt Remapping Table Address Register

		capability = get_iommu_reg64(CAPABILITY_REG_OFFSET);
		printk("CAPABILITY: 0x%llx\n", capability);

		ext_capability = get_iommu_reg64(EXTENDED_CAPABILITY_REG_OFFSET);
		printk("EXTENDED CAPABILITY: 0x%llx\n", ext_capability);

		fault_offset = 16 * ((capability >> 24) & 0x3ff); // MOVE TO BITMASK
		printk("FAULT OFFSET: 0x%llx\n", fault_offset);
		num_fault_regs = ((capability >> 40) & 0xff) + 1;
		printk("NUM_FAULT_REGS: 0x%llx\n", num_fault_regs);

		// Read version.

		version = get_iommu_reg32(IOMMU_VERSION_REG_OFFSET);
		printk("IOMMU VERSION: %ld\n", version);

		// Set up Interrupt Remapping Pointer
		set_iommu_reg64(IRTE_BASE_REG_OFFSET, irtar);

		// Set command bit.
		set_gcr(SET_INTERRUPT_REMAP_TABLE_PTR);

		// Initialize Interrupt Remapping
		status = get_gsr();
		printk("STATUS BEFORE ENABLE: 0x%lx\n", status);

		set_gcr(INTERRUPT_REMAPPING_ENABLE);
		status = get_gsr();
		printk("STATUS AFTER ENABLE: 0x%lx\n", status);

		print_fault_regs();

	} else if (irtep == NULL) {
		panic("Failed to allocate interrupt remapping table pages!");
	} else {

	}
}

void init_irte(uint16_t irte_index, uint32_t dest_id, uint8_t vector,
               uint8_t delivery_mode) {
	if (irtep[irte_index*2] & PRESENT)
		panic("TRIED TO ALLOCATE ALREADY PRESENT IRTE");

	if (delivery_mode > DELIVERY_MODE_LAST || delivery_mode == 3 ||
	    delivery_mode == 6) {
		panic("INVALID DELIVERY MODE!");
	}

	irtep[irte_index*2+1] = 0;
	irtep[irte_index*2] = 0;

	irtep[irte_index*2] = PRESENT |
	                      //TRIGGER_MODE |
	                      //DESTINATION_MODE |
	                      //REDIRECTION_HINT |
	                      delivery_mode << 5 |
	                      (uint64_t)vector << 16 |
	                      (uint64_t)dest_id << 32;

	irtep[irte_index*2+1] = SOURCE_ID(0xf0ff) |
	                        SOURCE_ID_QUAL_ALL |
	                        SOURCE_VALIDATION_ON;
	//irtep[irte_index*2+1] = 0x4f0ff;

}

void print_fault_regs(void)
{
	int fault_status;
	int fault_index;
	uint64_t fault_reg_low, fault_reg_high;

	fault_status = get_iommu_reg32(FAULT_STATUS_REG_OFFSET);
	if (fault_status & 0x0002) {
		fault_index = (fault_status >> 8) & 0xFF;
		printk("Fault Status Register: 0x%lx\n", fault_status);

		fault_reg_low = get_iommu_reg64(fault_offset +
		                                (16 * fault_index));
		fault_reg_high = get_iommu_reg64(fault_offset + 8 +
		                                 (16 * fault_index));
		while (fault_reg_high >> 63) {
			printk("Fault Register at index 0x%lx Low: 0x%llx\n",
			       fault_index, fault_reg_low);
			printk("Fault Register at index 0x%lx High: 0x%llx\n",
			       fault_index, fault_reg_high);

			// Clear the fault by writing back the 1.
			set_iommu_reg64(fault_offset + 8 + (16 * fault_index),
			                fault_reg_high);

			fault_index++;
			fault_reg_low = get_iommu_reg64(fault_offset +
				                        (16 * fault_index));
			fault_reg_high = get_iommu_reg64(fault_offset + 8 +
				                         (16 * fault_index));

		}
	}
}

inline uint64_t get_iommu_reg64(uint64_t reg)
{
	return *((uint64_t *)(DMAR_REG_ADDR + reg));
}

inline void set_iommu_reg64(uint64_t reg, uint64_t value)
{
	*((uint64_t *)(DMAR_REG_ADDR + reg)) = value;
}

inline uint32_t get_iommu_reg32(uint64_t reg)
{
	return *((uint32_t *)(DMAR_REG_ADDR + reg));
}

inline void set_iommu_reg32(uint64_t reg, uint32_t value)
{
	*((uint32_t *)(DMAR_REG_ADDR + reg)) = value;
}

inline uint32_t get_gsr(void)
{
	return *((uint32_t *)(DMAR_REG_ADDR + GLOBAL_STATUS_REG_OFFSET));
}

void set_gcr(uint32_t value)
{
	uint32_t status;

	// Check reserved bits.
	if (value & 0x7FFFFF)
		panic("Setting reserved bits in Global Command Register");

	status = get_gsr() | value;
	*((uint32_t *)(DMAR_REG_ADDR + GLOBAL_COMMAND_REG_OFFSET)) = status;

	// Wait till the status bit reflects the change.
	do {
		status = get_gsr();
	} while (status != (status | value));
}

