#pragma once

/* Virtio G
 *
 * Copyright (c) 2016 Google Inc.
 *
 * Author:
 *  Gan Shun Lim <ganshunn@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This is intended to be a new virtio transport layer optimized to avoid
 * VMExits. It is built with asynchronicity in mind, with the idea that a
 * guest can make writes and reads that are then serviced by a host thread
 * running perhaps on another core entirely.
 *
 * In this rudimentary version of Virtio G, we simply utilize virtio_mmio's
 * register layout, however we pipe all read and write commands from
 * virtio_mmio through virtio_g's command queue to avoid EPT fault exits.
 *
 */

/* Canonical size of every memory mapped opt register. We fix it at 4 bytes
 * for ease of implementation
 */
#define VIRTIO_G_REG_SIZE					4

/*
 * Virtio G Read Only Registers
 */

/* Magic value - Read Only */
#define VIRTIO_G_MAGIC_VALUE				0x000

/* Virtio G device version - Read Only */
#define VIRTIO_G_VERSION_FIELD				0x004

/* Maximum size of the command queue - Read Only */
#define VIRTIO_G_CMD_QUEUE_NUM_MAX			0x008

/* Host Status field - Read Only */
#define VIRTIO_G_STATUS_RESP				0x00c

/* Write Page Address - Read Only */
#define VIRTIO_G_WRITE_PAGE_LOW				0x010
#define VIRTIO_G_WRITE_PAGE_HIGH			0x014

/* Status field Address - Read Only */
#define VIRTIO_G_STATUS_FIELD_LOW			0x018
#define VIRTIO_G_STATUS_FIELD_HIGH			0x01c

/* End of VIRTIO_G READ_AREA */
#define VIRTIO_G_READ_END					(VIRTIO_G_STATUS_FIELD_HIGH + \
                                             VIRTIO_G_REG_SIZE)

/*
 * Virtio G Read Write Registers
 */

/* Queue size for the command queue - Read Write */
#define VIRTIO_G_CMD_QUEUE_NUM				0x000

/* Descriptor Table address - Read Write */
#define VIRTIO_G_CMD_QUEUE_DESC_LOW			0x008
#define VIRTIO_G_CMD_QUEUE_DESC_HIGH		0x00c

/* Available Ring address - Read Write */
#define VIRTIO_G_CMD_QUEUE_AVAIL_LOW		0x010
#define VIRTIO_G_CMD_QUEUE_AVAIL_HIGH		0x014

/* Used Ring address - Read Write */
#define VIRTIO_G_CMD_QUEUE_USED_LOW			0x018
#define VIRTIO_G_CMD_QUEUE_USED_HIGH		0x01c

/* End of VIRTIO_G WRITE AREA */
#define VIRTIO_G_WRITE_END					(VIRTIO_G_CMD_QUEUE_USED_HIGH + \
                                             VIRTIO_G_REG_SIZE)

/*
 * Virtio G Status Register
 */

/* Status field - Read Write */
#define VIRTIO_G_CMD_QUEUE_STATUS			0x000

/* Virtio G status bit mappings */
#define VIRTIO_G_QUEUE_READY				(1 << 0)
#define VIRTIO_G_QUEUE_NOTIFY				(1 << 30)
#define VIRTIO_G_REG_WRITE					(1 << 31)

#define VIRTIO_G_CMD_QUEUE					0

/* TODO(ganshun): handle actual dynamic data size. */
#define VIRTIO_G_CMD_DATA_LEN				4

#include <stdint.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/vmm.h>

/**/

struct virtio_g_dev {
	/* The read page base address of the virtio g device
	 * This really only points to about 0x20 of memory, and should ideally be
	 * a backed EPT page unlike Virtio MMIO.
	 * Read only.
	 */
	uint32_t *read_page;

	/* The write page base address of the virtio g device
	 * This really only points to about 0x20 of memory, and should ideally be
	 * a backed EPT page unlike Virtio MMIO.
	 * Read-Write.
	 */
	uint32_t *write_page;

	/* The status field address of the virtio g device
	 * This points to one 32 bit field, and should ideally be on a backed EPT
	 * page unlike Virtio MMIO. This field is unlike other pages as the status
	 * field is designed to be placed next to those of other devices.
	 * Read-Write.
	 */
	uint32_t *status_field;

	/* The host backup copy of virtio g device' read page
	 * Since the guest ideally can write to the command queue registers
	 * without vm exiting, we need to keep a host copy to check for register
	 * writes. WE MIGHT NOT NEED THIS IF WE BACK THE EPT PAGE READ ONLY
	 */
	uint32_t *host_read_page;

	/* The host backup copy of virtio g device' write page
	 * Since the guest ideally can write to the command queue registers
	 * without vm exiting, we need to keep a host copy to check for register
	 * writes.
	 */
	uint32_t *host_write_page;

	/* The host backup copy of the device's status field. */
	uint32_t host_status_field;

	/* This is the function called to notify the guest when command queue
	 * read and write requests are completed. This tells the guest to look
	 * at the command virtqueue's used ring and check for its completed
	 * request. Calls to poke_guest should be done after the used ring has been
	 * updated.
	 */
	void (*poke_guest)(uint8_t vec);

	/* The generic vq device contained by this optimized transport */
	struct virtio_vq_dev *vqdev;

	/* Virtio G Interrupt fields.
	 *
	 * If we use the same irq and vector for the command queue as we do for
	 * the rest of the device, when we inject an interrupt telling the guest
	 * about a command queue used ring update, it will attempt to read the isr.
	 * This places another request into the command queue, which will again
	 * interrupt upon completion. This leads to a deadlock when the second
	 * interrupt is sent because the interrupt will be blocked by the other
	 * interrupt in service.
	 */

	/* The irq number for the command queue. */
	uint64_t cmd_irq;

	/* Actual vector the device will inject for the command queue. */
	uint8_t cmd_vec;

	/* The virtualized MMIO transport layer that flows on the command queue. */
	struct virtio_mmio_dev *mmio_dev;

	/* The virtualized MMIO transport layer that flows on the command queue. */
	struct virtual_machine *vm;
};

enum {
	VIRTIO_G_CMD_READ,
	VIRTIO_G_CMD_WRITE
};
/* The header attached to a command from the guest. */
struct virtio_g_cmd_hdr {
	/* The offset within the encapsulated transport layer to write to.
	 * For a simple encapsulation of virtio_mmio, it can even be the full
	 * address, doesn't matter.
	 */
	uint64_t offset;

	/* Specifies whether this cmd packet is a read or a write to the
	 * encapsulated transport layer.
	 *     VIRTIO_G_CMD_READ - This is a read command.
	 *     VIRTIO_G_CMD_WRITE - This is a write command.
	 */
	uint8_t type;

	/* Number of bytes to read/write. Must be 1, 2 or 4 for now. */
	uint8_t size;
}

/* Virtio G helper functions. */

/* Polling function that keeps checking all devices in a vm */
void virtio_g_status_poll(struct virtual_machine *vm);

/* Helper status update check function that is called by virtio_g_status poll */
void virtio_g_status_update(struct virtual_machine *vm,
                            struct virtio_g_dev *g_dev);

/* Update function that is called when the host detects a
 * write to the command queue page.
 */
void virtio_g_wrpg_update(struct virtual_machine *vm,
                          struct virtio_g_dev *g_dev, uint64_t offset);

/* Virtio G helper functions. */

uint32_t virtio_g_rdpg_rd(struct virtio_g_dev *g_dev,
                          uint32_t field_offset)
{
	if (field_offset >= VIRTIO_G_READ_END) {
		VIRTIO_DEV_ERRX(g_dev->vqdev,
		    "Attempted to read past VIRTIO_G_READ_END. Field_offset was %lx.\n",
		    field_offset);
	}
	return g_dev->read_page[field_offset/VIRTIO_G_REG_SIZE];
}

void virtio_g_rdpg_wr(struct virtio_g_dev *g_dev,
                      uint32_t field_offset, uint32_t value)
{
	if (field_offset >= VIRTIO_G_READ_END) {
		VIRTIO_DEV_ERRX(g_dev->vqdev,
		    "Attempted to write past VIRTIO_G_READ_END. Field_offset was %lx.\n",
		    field_offset);
	}
	g_dev->read_page[field_offset/VIRTIO_G_REG_SIZE] = value;
}

uint32_t virtio_g_wrpg_rd(struct virtio_g_dev *g_dev,
                          uint32_t field_offset)
{
	if (field_offset >= VIRTIO_G_WRITE_END) {
		VIRTIO_DEV_ERRX(g_dev->vqdev,
		    "Attempted to read past VIRTIO_G_WRITE_END. Field_offset was %lx.\n",
		    field_offset);
	}
	return g_dev->write_page[field_offset/VIRTIO_G_REG_SIZE];
}

void virtio_g_wrpg_wr(struct virtio_g_dev *g_dev,
                      uint32_t field_offset, uint32_t value)
{
	if (field_offset >= VIRTIO_G_WRITE_END) {
		VIRTIO_DEV_ERRX(g_dev->vqdev,
		    "Attempted to write past VIRTIO_G_WRITE_END. Field_offset was %lx.\n",
		    field_offset);
	}
	g_dev->write_page[field_offset/VIRTIO_G_REG_SIZE] = value;
}

uint32_t virtio_g_host_rdpg_rd(struct virtio_g_dev *g_dev,
                               uint32_t field_offset)
{
	if (field_offset >= VIRTIO_G_READ_END) {
		VIRTIO_DEV_ERRX(g_dev->vqdev,
		    "Attempted to read past VIRTIO_G_READ_END. Field_offset was %lx.\n",
		    field_offset);
	}
	return g_dev->host_read_page[field_offset/VIRTIO_G_REG_SIZE];
}

void virtio_g_host_rdpg_wr(struct virtio_g_dev *g_dev,
                           uint32_t field_offset, uint32_t value)
{
	if (field_offset >= VIRTIO_G_READ_END) {
		VIRTIO_DEV_ERRX(g_dev->vqdev,
		    "Attempted to write past VIRTIO_G_READ_END. Field_offset was %lx.\n",
		    field_offset);
	}
	g_dev->host_read_page[field_offset/VIRTIO_G_REG_SIZE] = value;
}

uint32_t virtio_g_host_wrpg_rd(struct virtio_g_dev *g_dev,
                               uint32_t field_offset)
{
	if (field_offset >= VIRTIO_G_WRITE_END) {
		VIRTIO_DEV_ERRX(g_dev->vqdev,
		    "Attempted to read past VIRTIO_G_WRITE_END. Field_offset was %lx.\n",
		    field_offset);
	}
	return g_dev->host_write_page[field_offset/VIRTIO_G_REG_SIZE];
}

void virtio_g_host_wrpg_wr(struct virtio_g_dev *g_dev,
                           uint32_t field_offset, uint32_t value)
{
	if (field_offset >= VIRTIO_G_READ_END) {
		VIRTIO_DEV_ERRX(g_dev->vqdev,
		    "Attempted to write past VIRTIO_G_WRITE_END. Field_offset was %lx.\n",
		    field_offset);
	}
	g_dev->host_write_page[field_offset/VIRTIO_G_REG_SIZE] = value;
}
