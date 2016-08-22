/* Virtio G bindings
 *
 * Copyright (c) 2016 Google Inc.
 *
 * Author:
 *  Gan Shun Lim <ganshun@gmail.com>
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
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <vmm/virtio_config.h>
#include <vmm/virtio_g.h>
#include <vmm/vmm.h>

#define VIRTIO_G_MAGIC 0x6974706f /* 'opti' */

#define VIRTIO_G_VERSION 0x2

void virtio_g_status_poll(struct virtual_machine *vm)
{
	assert(vm);
	while (1) {
		/* Just keep checking all devices. */
		for (int i = 0; i < VIRTIO_MMIO_MAX_NUM_DEV; i++)
			virtio_g_status_update(vm, vm->virtio_g_devices[i]);
	}
}

/* This init function should only be called after setting up the fields
 * in virtio_g_dev. This function sets up the memory mapped areas for the guest
 * to access directly.
 */
void virtio_g_init(struct virtio_g_dev *g_dev)
{
	uint64_t write_page_addr = (uint64_t)g_dev->write_page;
	uint64_t status_field_addr = (uint64_t)g_dev->status_field;
	struct virtio_vq *cmd_queue = g_dev->vqdev[VIRTIO_G_CMD_QUEUE];

	/* Set up the read pages. */
	virtio_g_rdpg_wr(g_dev, VIRTIO_G_MAGIC_VALUE, VIRTIO_G_MAGIC);
	virtio_g_host_rdpg_wr(g_dev, VIRTIO_G_MAGIC_VALUE, VIRTIO_G_MAGIC);

	virtio_g_rdpg_wr(g_dev, VIRTIO_G_VERSION_FIELD, VIRTIO_G_VERSION);
	virtio_g_host_rdpg_wr(g_dev, VIRTIO_G_VERSION_FIELD, VIRTIO_G_VERSION);

	virtio_g_rdpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_NUM_MAX, cmd_queue.qnum_max);
	virtio_g_host_rdpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_NUM_MAX, cmd_queue.qnum_max);

	virtio_g_rdpg_wr(g_dev, VIRTIO_G_STATUS_RESP, 0);
	virtio_g_host_rdpg_wr(g_dev, VIRTIO_G_STATUS_RESP, 0);

	virtio_g_rdpg_wr(g_dev, VIRTIO_G_WRITE_PAGE_LOW,
	                 (uint32_t)(write_page_addr & 0x00000000ffffffff));
	virtio_g_host_rdpg_wr(g_dev, VIRTIO_G_WRITE_PAGE_LOW,
	                      (uint32_t)(write_page_addr & 0x00000000ffffffff));

	virtio_g_rdpg_wr(g_dev, VIRTIO_G_WRITE_PAGE_HIGH,
	                 (uint32_t)((write_page_addr &
	                             0xffffffff00000000) >> 32));
	virtio_g_host_rdpg_wr(g_dev, VIRTIO_G_WRITE_PAGE_HIGH,
	                      (uint32_t)((write_page_addr &
	                                  0xffffffff00000000) >> 32));

	virtio_g_rdpg_wr(g_dev, VIRTIO_G_STATUS_FIELD_LOW,
	                 (uint32_t)(status_field_addr & 0x00000000ffffffff));
	virtio_g_host_rdpg_wr(g_dev, VIRTIO_G_STATUS_FIELD_LOW,
	                      (uint32_t)(status_field_addr & 0x00000000ffffffff));

	virtio_g_rdpg_wr(g_dev, VIRTIO_G_STATUS_FIELD_HIGH,
	                 (uint32_t)((status_field_addr &
	                             0xffffffff00000000) >> 32));
	virtio_g_host_rdpg_wr(g_dev, VIRTIO_G_STATUS_FIELD_HIGH,
	                      (uint32_t)((status_field_addr &
	                                  0xffffffff00000000) >> 32));

	/* Set up the write pages. We simply initialize all these to 0 */
	virtio_g_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_NUM, 0);
	virtio_g_host_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_NUM, 0);

	virtio_g_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_DESC_LOW, 0);
	virtio_g_host_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_DESC_LOW, 0);

	virtio_g_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_DESC_HIGH, 0);
	virtio_g_host_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_DESC_HIGH, 0);

	virtio_g_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_AVAIL_LOW, 0);
	virtio_g_host_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_AVAIL_LOW, 0);

	virtio_g_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_AVAIL_HIGH, 0);
	virtio_g_host_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_AVAIL_HIGH, 0);

	virtio_g_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_USED_LOW, 0);
	virtio_g_host_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_USED_LOW, 0);

	virtio_g_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_USED_HIGH, 0);
	virtio_g_host_wrpg_wr(g_dev, VIRTIO_G_CMD_QUEUE_USED_HIGH, 0);

	/* Set up the status field. */
	*g_dev->status_field = 0;
	g_dev->host_status_field = 0;

}

void virtio_g_srv_fn(void *_vq)
{
	struct virtio_vq *vq = _vq;

	assert(vq != NULL);

	struct virtio_g_dev *dev = vq->vqdev->transport_dev;
	struct iovec *iov;
	uint32_t head;
	uint32_t olen, ilen;
	struct virtio_g_cmd_hdr *cmd_hdr;
	int64_t ret;
	uint64_t offset;
	size_t wlen;
	uint8_t *status;
	struct virtio_blk_config *cfg = vq->vqdev->cfg;

	if (vq->qready != 0x1)
		VIRTIO_DEV_ERRX(vq->vqdev,
		                "The service function for queue '%s' was launched before the driver set QueueReady to 0x1.",
		                 vq->name);

	if (!dev->poke_guest)
		VIRTIO_DEV_ERRX(vq->vqdev,
		                "The 'poke_guest' function pointer was not set.");

	iov = malloc(vq->qnum_max * sizeof(struct iovec));
	if (iov == NULL)
		VIRTIO_DEV_ERRX(vq->vqdev,
		                "malloc returned null trying to allocate iov.\n");

	for (;;) {
		head = virtio_next_avail_vq_desc(vq, iov, &olen, &ilen);
		/* There are always two iovecs.
		 * The first is the header.
		 * The second is the actual data.
		 */
		if (iov[0].iov_len != sizeof(struct virtio_g_cmd_hdr))
			VIRTIO_DEV_ERRX(vq->vqdev,
			                "cmd_hdr len is %d, should be %d\n",
			                iov[0].iov_len, sizeof(struct virtio_g_cmd_hdr));

		/* TODO(ganshun): handle actual dynamic data size. */
		if (iov[1].iov_len != sizeof(struct virtio_g_cmd_hdr))
			VIRTIO_DEV_ERRX(vq->vqdev,
			                "cmd data len is %d, should be %d\n",
			                iov[1].iov_len, VIRTIO_G_CMD_DATA_LEN);

		cmd_hdr = iov[0].iov_base;

		offset = cmd_hdr->offset;

		if (cmd_hdr->type == VIRTIO_G_CMD_WRITE) {
			virtio_mmio_wr(dev->vm, dev->mmio_dev,
			               cmd_hdr->offset, cmd_hdr->size,
			               (uint32_t *)(iov[1].iov_base));
			wlen = 0;
		} else {
			*((uint32_t *)(iov[1].iov_base)) =
			    virtio_mmio_rd(dev->vm, dev->mmio_dev,
			                   cmd_hdr->offset, cmd_hdr->size);
			wlen = VIRTIO_G_CMD_DATA_LEN;
		}

		virtio_add_used_desc(vq, head, wlen);
		/* We don't need this if we have the guest poll. */
		dev->poke_guest(dev->cmd_vec);
	}
}

void virtio_g_status_update(struct virtual_machine *vm,
                            struct virtio_g_dev *g_dev)
{
	uint32_t value = *g_dev->status_field;
	uint32_t status_change = value ^ g_dev->host_status_field;
	/* Check for a difference between the host copy and the guest copy. */
	if (status_change == 0)
		return;

	/* The status field consists of 3 bits:
	 * bit 0  - (QUEUE_READY)
	 *        - 1 indicates a request to turn on the Command Queue
	 *        - 0 indicates a request to turn off the Command Queue
	 *        - The guest should not assume request completion and should
	 *          read VIRTIO_G_STATUS_RESP for synchronization.
	 * bit 30 - (QUEUE_NOTIFY)
	 *        - Writing 1 notfies the Command Queue. The guest should wait
	 *          until it reads back a 0 in this bit before proceeding.
	 * bit 31 - (REG_WRITE)
	 *        - Writing 1 signals a change in the command queue registers.
	 *          The guest should wait until it reads back a 0 in this bit
	 *          before proceeding.
	 *
	 * As a general rule, the host should check REG_WRITE first, followed
	 * by QUEUE_READY then QUEUE_NOTIFY. It is conceivable that the guest
	 * writes all three one after another in the correct order, but the host
	 * did not check the status field until all three writes had completed.
	 *
	 * Correct guest operation in the future should wait for one write to
	 * complete before proceeding, so this should not occur.
	 * */

	if (status_change & VIRTIO_G_REG_WRITE) {
		/* Reg write is used for the guest to notify the host about
		 * changes in the vring addresses.
		 */

		for (int i = 0; i < VIRTIO_G_WRITE_END; i += VIRTIO_G_REG_SIZE)
			virtio_g_wrpg_update(i);

		/* Turn off the REG_WRITE bit so that the guest can proceed. */
		*g_dev->status_field &= ~(1 << VIRTIO_G_REG_WRITE);

	}
	/* The guest is toggling queue ready for the command queue */
	if (status_change & VIRTIO_G_QUEUE_READY) {
		if (g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready ==
		    0x0) {
			/* The driver is enabling the command queue. We validate
			 * the vring addresses, launch the service thread, and set
			 * VIRTIO_G_CMD_QUEUE_HOST_STATUS' bit 0 to 1.
			 */

			/* Check that there is a command queue service function. */
			if (!g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].srv_fn) {
				VIRTIO_DEV_ERRX(g_dev->vqdev,
				    "The host must provide a service function for the command queue before the driver writes 0x1 to QueueReady. No service function found.");
			}

			virtio_check_vring(&g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE]);

			g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].eventfd = eventfd(0, 0);
			g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready = 0x1;

			g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].srv_th =
			    vmm_run_task(vm,
			                 g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].srv_fn,
			                 &g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE]);
			if (!g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].srv_th) {
				VIRTIO_DEV_ERRX(g_dev->vqdev,
					"vm_run_task failed when trying to start service thread after driver wrote 0x1 to QueueReady.");
			}
			/* Update the memory mapped fields.
			 * We do the rdpg instead of host_rdpg because we map the page
			 * read only so we don't need the extra copy.
			 */
			int host_response = virtio_g_rdpg_rd(g_dev, VIRTIO_G_STATUS_RESP);

			/* Turn on the QUEUE_READY bit */
			host_response |= (1 << VIRTIO_G_QUEUE_READY);
			virtio_g_rdpg_wr(g_dev, VIRTIO_G_STATUS_RESP, host_response);
		} else if (g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready == 0x1) {
			// Driver is trying to revoke QueueReady while the queue is
			// currently enabled (QueueReady is 0x1).
			// TODO: For now we are going to just make this an error.
			//       In the future, when we actually decide how we want
			//       to clean up the threads, the sequence might look
			//       something like this:
			//       1. Ask the queue's service thread to exit and wait
			//          for it to finish and exit.
			//       2. Once it has exited, close the queue's eventfd
			//          and set both the eventfd and srv_th fields to 0.
			//       3. Finally, write 0x0 to QueueReady.
			VIRTIO_DEV_ERRX(g_dev->vqdev,
				"Our (Akaros) G device does not currently allow the driver to revoke QueueReady (i.e. change QueueReady from 0x1 to 0x0). The driver tried to revoke it, so whatever you are doing might require this ability.");
		}

	}
	if (status_change & VIRTIO_G_QUEUE_NOTIFY) {
		/* TODO(ganshun): REPLACE WITH NEW STYLE, ASK BARRET */
		struct virtio_vq *notified_queue;

		notified_queue = &g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE];

		// kick the queue's service thread
		if (notified_queue->eventfd > 0)
			eventfd_write(notified_queue->eventfd, 1);
		else
			VIRTIO_DEV_ERRX(mmio_dev->vqdev,
			    "You need to provide a valid eventfd on your virtio_vq so that it can be kicked when the driver writes to QueueNotify.");

		/* Turn off the QUEUE_NOTIFY bit so that the guest can proceed. */
		*g_dev->status_field &= ~(1 << VIRTIO_G_QUEUE_NOTIFY);

	}

			break;
}

/* virtio_g_guest_update is called when there is a write to VIRTIO_G_REG_WRITE
 * This is a method for the guest to update the host when it's completed a
 * write to any writeable register other than VIRTIO_G_CMD_QUEUE_STATUS
 */
void virtio_g_wrpg_update(struct virtio_g_dev *g_dev, uint64_t offset)
{
	uint32_t value = virtio_g_wrpg_rd(g_dev, offset);

	const char *err; // returned err strings

	if (!g_dev->vqdev || !g_dev->mmio_dev) {
		// If there is no vqdev or attached mmio_device,
		// we just make all registers write-ignored.
		return;
	}

	/* Check for a difference between the host copy and the guest copy. */
	if (value == virtio_g_host_wrpg_rd(g_dev, offset))
		return;

	switch (offset) {

		// Device (host) features word selection.
		case VIRTIO_G_CMD_QUEUE_NUM:
			/* TODO(ganshun): convert to checking from struct vq */
			if (value > virtio_g_rdpg_rd(g_dev, VIRTIO_G_CMD_QUEUE_NUM_MAX))
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "The driver must not set VIRTIO_G_CMD_QUEUE_NUM larger than VIRTIO_G_CMD_QUEUE_NUM_MAX\n");

			if ((value != 0) && (value & ((value) - 1)))
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "The driver may only write powers of 2 to VIRTIO_G_CMD_QUEUE_NUM.\n"
				    "  See virtio-v1.0-cs04 s2.4 Virtqueues");
			break;

		/* Queue's Descriptor Table 64 bit long physical address, low 32 */
		case VIRTIO_G_CMD_QUEUE_DESC_LOW:
			if (g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready != 0)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Attempt to access QueueDescLow on the command queue, which has nonzero QueueReady.\n");

			/* virtio-v1.0-cs04 s2.4 Virtqueues */
			if (value & 0x0f)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Physical address of guest's descriptor table (%p) is misaligned. Address should be a multiple of 16.\n"
				    "  See virtio-v1.0-cs04 s2.4 Virtqueues");

			/* replace low 32 bits */
			uint64_t *desc =
			    &(g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].vring.desc);

			*desc = (*desc & 0xffffffff00000000ULL) | value;
			break;

		/* Queue's Descriptor Table 64 bit long physical address, high 32 */
		case VIRTIO_G_CMD_QUEUE_DESC_HIGH:
			if (g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready != 0)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Attempt to access QueueDescHigh on the command queue, which has nonzero QueueReady.\n");

			/* replace high 32 bits */
			uint64_t *desc =
			    &(g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].vring.desc);

			*desc = (*desc & 0x00000000ffffffffULL) | (((uint64_t)value) << 32);
			break;

		case VIRTIO_G_CMD_QUEUE_AVAIL_LOW:
			if (g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready != 0)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Attempt to access QueueAvailLow on the command queue, which has nonzero QueueReady.\n");

			/* virtio-v1.0-cs04 s2.4 Virtqueues */
			if (value & 0x01)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Physical address of guest's available ring (%p) is misaligned. Address should be a multiple of 2.\n"
				    "  See virtio-v1.0-cs04 s2.4 Virtqueues");

			/* replace low 32 bits */
			uint64_t *avail =
			    &(g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].vring.avail);

			*avail = (*avail & 0xffffffff00000000ULL) | value;
			break;

		/* Queue's Descriptor Table 64 bit long physical address, high 32 */
		case VIRTIO_G_CMD_QUEUE_AVAIL_HIGH:
			if (g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready != 0)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Attempt to access QueueAvailHigh on the command queue, which has nonzero QueueReady.\n");

			/* replace high 32 bits */
			uint64_t *avail =
			    &(g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].vring.avail);

			*avail = (*avail & 0x00000000ffffffffULL) | (((uint64_t)value) << 32);
			break;

		case VIRTIO_G_CMD_QUEUE_USED_LOW:
			if (g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready != 0)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Attempt to access QueueUsedLow on the command queue, which has nonzero QueueReady.\n");

			/* virtio-v1.0-cs04 s2.4 Virtqueues */
			if (value & 0x03)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Physical address of guest's used ring (%p) is misaligned. Address should be a multiple of 4.\n"
				    "  See virtio-v1.0-cs04 s2.4 Virtqueues");

			/* replace low 32 bits */
			uint64_t *used =
			    &(g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].vring.used);

			*used = (*used & 0xffffffff00000000ULL) | value;
			break;

		/* Queue's Descriptor Table 64 bit long physical address, high 32 */
		case VIRTIO_G_CMD_QUEUE_USED_HIGH:
			if (g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].qready != 0)
				VIRTIO_DRI_ERRX(g_dev->vqdev,
				    "Attempt to access QueueUsedHigh on the command queue, which has nonzero QueueReady.\n");

			/* replace high 32 bits */
			uint64_t *used =
			    &(g_dev->vqdev->vqs[VIRTIO_G_CMD_QUEUE].vring.used);

			*used = (*used & 0x00000000ffffffffULL) | (((uint64_t)value) << 32);
			break;

		default:
			// Bad register offset
			VIRTIO_DRI_ERRX(g_dev->vqdev,
				"Attempted to update invalid device register offset 0x%x.",
				offset);
			break;
	}

	/* Update the host page since writes didn't blow out with an error */
	virtio_g_host_wrpg_wr(g_dev, offset, value);
}
