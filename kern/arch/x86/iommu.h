/* Copyright 2015 Google Inc.
 *
 * See LICENSE for details.
 */

#pragma once

/*
 * This defines a Interrupt Remapping Table Entry for the IOMMU.
 * More details about the bit fields can be found in the intel
 * manual on Virtualization Technology for Directed I/O Section 9.10
 */

#define PRESENT				(1 << 0)
#define FAULT_PROCESSING_DISABLE	(1 << 1)
#define DESTINATION_MODE		(1 << 2)
#define REDIRECTION_HINT		(1 << 3)
#define TRIGGER_MODE			(1 << 4)

/* Delivery mode is 3 bits. */
#define DELIVERY_MODE_FIXED		0
#define DELIVERY_MODE_LOWEST_PRIORITY	1
#define DELIVERY_MODE_SMI		2
#define DELIVERY_MODE_NMI		4
#define DELIVERY_MODE_INIT		5
#define DELIVERY_MODE_EXT_INT		7
#define DELIVERY_MODE_LAST		DELIVERY_MODE_EXT_INT

/* Available area is 4 bits */
#define AVAILABLE			(0 << 8)

#define IRTE_MODE			(1 << 15)
#define IRTE_VECTOR(x)			((x) << 16)
#define IRTE_DESTINATION(x)		((x) << 32)

#define SOURCE_ID(x)			(x)

#define SOURCE_ID_QUAL_ALL		(0 << 16)
#define SOURCE_ID_QUAL_IGNORE_LAST	(1 << 16)
#define SOURCE_ID_QUAL_IGNORE_2_3_LAST	(2 << 16)
#define SOURCE_ID_QUAL_LAST_3		(3 << 16)

#define SOURCE_VALIDATION_NONE		(0 << 18)
#define SOURCE_VALIDATION_ON		(1 << 18)
#define SOURCE_VALIDATION_MSB_8		(2 << 18)

/* Temporary define for testing. We should dynamically get the right addr */

//#define DMAR_REG_PADDR			0xfbffe000
#define DMAR_REG_ADDR			0xffffffff80000000
//#define DMAR_REG_PADDR			0xdfffc000
#define DMAR_REG_PADDR			0xfbffc000
//#define DMAR_REG_PADDR			0xfbffd000

#define IOMMU_VERSION_REG_OFFSET	0x00
#define CAPABILITY_REG_OFFSET		0x08
#define EXTENDED_CAPABILITY_REG_OFFSET	0x10
#define GLOBAL_COMMAND_REG_OFFSET	0x18
#define GLOBAL_STATUS_REG_OFFSET	0x1C
#define ROOT_TABLE_ADDR_REG_OFFSET	0x20
#define FAULT_STATUS_REG_OFFSET		0x34
#define INVAL_EVENT_CONTROL_REG_OFFSET	0xA0
#define IRTE_BASE_REG_OFFSET		0xB8

/* Global Command Register bits */
#define COMPAT_FORMAT_INTERRUPT		(1 << 23)
#define SET_INTERRUPT_REMAP_TABLE_PTR	(1 << 24)
#define INTERRUPT_REMAPPING_ENABLE	(1 << 25)
#define QUEUED_INVALIDATION_ENABLE	(1 << 26)
#define WRITE_BUFFER_FLUSH		(1 << 27)
#define ENABLE_ADVANCED_FAULT_LOGGING	(1 << 28)
#define SET_FAULT_LOG			(1 << 29)
#define SET_ROOT_TABLE_PTR		(1 << 30)
#define TRANSLATION_ENABLE		(1 << 31)

/* Offset for MSI based interrupts */
#define IRTE_MSI_OFFSET			0x100

struct irte {
	uint64_t low;
	uint64_t high;
};

uint64_t fault_offset, num_fault_regs;

uint64_t *irtep;

uint64_t *rootp;

void init_iommu(void);

void print_fault_regs(void);

inline uint64_t get_iommu_reg64(uint64_t reg);

inline void set_iommu_reg64(uint64_t reg, uint64_t value);

inline uint32_t get_iommu_reg32(uint64_t reg);

inline void set_iommu_reg32(uint64_t reg, uint32_t value);

inline uint32_t get_gsr(void);

void set_gcr(uint32_t bits);

void init_irte(uint16_t irte_index, uint32_t dest_id, uint8_t vector,
               uint8_t delivery_mode);

