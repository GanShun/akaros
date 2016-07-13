#define _LARGEFILE64_SOURCE /* See feature_test_macros(7) */
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <vmm/virtio.h>
#include <vmm/virtio_blk.h>
#include <vmm/virtio_mmio.h>

int debug_virtio_blk = 0;

#define DPRINTF(fmt, ...) \
	if (debug_virtio_blk) { fprintf(stderr, "virtio_blk: " fmt, ## __VA_ARGS__); }

//TODO(kmilka): multiple disks
static int diskfd;

void blk_request(void *_vq)
{
	struct virtio_vq *vq = _vq;
	struct virtio_mmio_dev *dev = vq->vqdev->transport_dev;
	struct iovec *iov;
	uint32_t head;
	uint32_t olen, ilen;
	struct virtio_blk_outhdr *out;
	uint64_t offset;
	int ret;
	unsigned int wlen;
	uint8_t *in;
	struct virtio_blk_config *cfg = vq->vqdev->cfg;
	uint64_t total_read = 0;

	DPRINTF("blk_request entered\n");

	if (!vq)
		VIRTIO_DEV_ERRX(vq->vqdev,
			"\n  %s:%d\n"
			"  Virtio device: (not sure which one): Error, device behavior.\n"
			"  The device must provide a valid virtio_vq as an argument to %s."
			, __FILE__, __LINE__, __func__);

	if (vq->qready == 0x0)
		VIRTIO_DEV_ERRX(vq->vqdev,
			"The service function for queue '%s' was launched before the driver set QueueReady to 0x1.",
			vq->name);

	iov = malloc(vq->qnum_max * sizeof(struct iovec));
	assert(iov != NULL);

	if (!dev->poke_guest) {
		free(iov);
		VIRTIO_DEV_ERRX(vq->vqdev,
			"The 'poke_guest' function pointer was not set.");
	}

	for (;;) {
		head = virtio_next_avail_vq_desc(vq, iov, &olen, &ilen);
		out = iov[0].iov_base;

		in = NULL;
		for (int i = olen + ilen - 1; i >= olen; i--) {
			if (iov[i].iov_len > 0) {
				in = iov[i].iov_base + iov[i].iov_len - 1;
				iov[i].iov_len--;
				break;
			}
		}
		if (!in)
			VIRTIO_DEV_ERRX(vq->vqdev, "no room for status\n");

		offset = out->sector * 512;
		DPRINTF("offset is: %llu      sector is: %lld\n", offset, out->sector);
		if (lseek64(diskfd, offset, SEEK_SET) != offset)
			VIRTIO_DEV_ERRX(vq->vqdev, "Bad seek at sector %llu\n",
			                out->sector);

		if (out->type & VIRTIO_BLK_T_OUT) {
			DPRINTF("blk device write\n");
			int wrote_len = offset;
			for (int i = 1; i < olen; i++) {
				int write_len = iov[i].iov_len;

				if (wrote_len + write_len > cfg->capacity * 512)
					VIRTIO_DEV_ERRX(vq->vqdev, "write past end of file!\n");

				ret = write(diskfd, iov[i].iov_base, write_len);
				assert(ret == write_len);
			}

			wlen = sizeof(*in);
			*in = (ret >= 0 ? VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR);
		}
		else if (out->type & VIRTIO_BLK_T_FLUSH) {
			VIRTIO_DEV_ERRX(vq->vqdev, "Flush not supported!\n");
		}
		else {
			DPRINTF("blk device read\n");
			DPRINTF("olen is: %d     ilen is: %d\n", olen, ilen);
			DPRINTF("iov[1] is: %d\n", iov[1].iov_len);
			ret = readv(diskfd, iov + olen, ilen);
			//if (offset == 0) {
				/*for (int i = 0; i < iov[olen].iov_len; i += 2) {
					fprintf(stderr, "%02x", *((uint8_t *)(iov[olen].iov_base + i + 1)));
					fprintf(stderr, "%02x", *((uint8_t *)(iov[olen].iov_base + i)));
					fprintf(stderr, " ");
					if (i % 16 == 14)
						fprintf(stderr, "\n");
				}*/
			//}
			total_read += ret;
			DPRINTF("read %d bytes,  total_read is:%d\n", ret, total_read);
			DPRINTF("blk device read done\n");
			if (ret >= 0) {
				wlen = sizeof(*in) + ret;
				*in = VIRTIO_BLK_S_OK;
				DPRINTF("in is set\n");
			}
			else {
				wlen = sizeof(*in);
				*in = VIRTIO_BLK_S_IOERR;
			}
		}

		virtio_add_used_desc(vq, head, wlen);
		virtio_mmio_set_vring_irq(dev);
		dev->poke_guest(dev->vec);
		DPRINTF("done with everything\n");
	}
}

void blk_init_fn(struct virtio_vq_dev *vqdev, const char *filename)
{
	struct virtio_blk_config *cfg = vqdev->cfg;
	struct virtio_blk_config *cfg_d = vqdev->cfg_d;
	uint64_t len;

	diskfd = open(filename, O_RDWR);
	if (diskfd < 0)
		VIRTIO_DEV_ERRX(vqdev, "Could not open disk image file %s", filename);

	len = lseek64(diskfd, 0, SEEK_END) / 512;
	cfg->capacity = len;
	cfg_d->capacity = len;
}
