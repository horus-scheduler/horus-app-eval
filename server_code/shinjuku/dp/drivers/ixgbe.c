/*
 * Copyright 2013-19 Board of Trustees of Stanford University
 * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* For memmove and size_t */
#include <string.h>

/* For struct sockaddr */
#include <sys/socket.h>

/* For pipe */
#include <unistd.h>

/* General DPDK includes */
#include <rte_config.h>
#include <rte_ethdev.h>

/* DPDK ixgbe includes */
#include <ixgbe_rxtx.h>
#include <ixgbe_ethdev.h>
#include <base/ixgbe_vf.h>

/* Defines from IX override DPDK */
#undef EAGAIN
#undef EBADMSG
#undef EDEADLK
#undef EMULTIHOP
#undef ENAMETOOLONG
#undef ENOLCK
#undef ENOLINK
#undef ENOSYS
#undef ENOTEMPTY
#undef EPROTO
#undef ETH_DCB_NONE
#undef ETH_DCB_RX
#undef ETH_DCB_TX
#undef ETH_LINK_FULL_DUPLEX
#undef ETH_LINK_HALF_DUPLEX
#undef ETH_LINK_SPEED_AUTONEG
#undef ETH_RSS
#undef ETH_RSS_IPV4
#undef ETH_RSS_IPV6
#undef ETH_RSS_IPV6_EX
#undef ETH_RSS_IPV6_TCP_EX
#undef ETH_RSS_IPV6_UDP_EX
#undef ETH_VMDQ_DCB_TX
#undef IXGBE_MIN_RING_DESC
#undef LIST_HEAD
#undef PKT_TX_IP_CKSUM
#undef PKT_TX_TCP_CKSUM
#undef VMDQ_DCB
#undef likely
#undef mb
#undef min
#undef prefetch
#undef rmb
#undef unlikely
#undef wmb

/* IX includes */
#include <ix/byteorder.h>
#include <ix/ethdev.h>
#include <ix/dpdk.h>
#include <ix/drivers.h>
#include <ix/cfg.h>

#define IXGBE_ALIGN		128
#define IXGBE_MIN_RING_DESC	64
#define IXGBE_MAX_RING_DESC	4096

#define IXGBE_RDT_THRESH	32

struct rx_entry {
	struct mbuf *mbuf;
};

struct rx_queue {
	struct eth_rx_queue	erxq;
	volatile union ixgbe_adv_rx_desc *ring;
	machaddr_t		ring_physaddr;
	struct rx_entry		*ring_entries;

	volatile uint32_t	*rdt_reg_addr;
	uint16_t		reg_idx;

	uint16_t		head;
	uint16_t		tail;
	uint16_t		len;
};

#define eth_rx_queue_to_drv(rxq) container_of(rxq, struct rx_queue, erxq)

struct tx_entry {
	struct mbuf *mbuf;
};

struct tx_queue {
	struct eth_tx_queue	etxq;
	volatile union ixgbe_adv_tx_desc *ring;
	machaddr_t		ring_physaddr;
	struct tx_entry		*ring_entries;

	volatile uint32_t	*tdt_reg_addr;
	uint16_t		reg_idx;
	uint16_t		queue_id;

	uint16_t		head;
	uint16_t		tail;
	uint16_t		len;

	uint16_t		ctx_curr;
	struct ixgbe_advctx_info ctx_cache[IXGBE_CTX_NUM];
};

#define eth_tx_queue_to_drv(txq) container_of(txq, struct tx_queue, etxq)

static int ixgbe_alloc_rx_mbufs(struct rx_queue *rxq);
static int ixgbe_rx_poll(struct eth_rx_queue *rx);
static int ixgbe_tx_reclaim(struct eth_tx_queue *tx);
static int ixgbe_tx_xmit(struct eth_tx_queue *tx, int nr, struct mbuf **mbufs);
static int ixgbe_tx_xmit_ctx(struct tx_queue *txq, int ol_flags, int ctx_idx);

extern int optind;

static DEFINE_SPINLOCK(ixgbe_dev_lock);

static int dev_start(struct ix_rte_eth_dev *dev)
{
	int i;
	int ret;
	struct ixgbe_hw *hw;

	hw = IXGBE_DEV_PRIVATE_TO_HW(rte_eth_devices[dev->port].data->dev_private);

	ret = rte_eth_dev_start(dev->port);
	if (ret < 0)
		return ret;

	/* NOTE: This is a hack. We hijack DPDK mbufs with ours. */
	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		struct rx_queue *rxq = eth_rx_queue_to_drv(dev->data->rx_queues[i]);

		ret = ixgbe_alloc_rx_mbufs(rxq);
		if (ret)
			return ret;

		/* Configure descriptor ring base and length */
		IXGBE_WRITE_REG(hw, IXGBE_RDBAL(rxq->reg_idx), (uint32_t)(rxq->ring_physaddr & 0x00000000ffffffffULL));
		IXGBE_WRITE_REG(hw, IXGBE_RDBAH(rxq->reg_idx), (uint32_t)(rxq->ring_physaddr >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_RDLEN(rxq->reg_idx), rxq->len * sizeof(union ixgbe_adv_rx_desc));
	}

	for (i = 0; i < dev->data->nb_tx_queues; i++) {
		struct tx_queue *txq = eth_tx_queue_to_drv(dev->data->tx_queues[i]);

		IXGBE_WRITE_REG(hw, IXGBE_TDBAL(txq->reg_idx), (uint32_t)(txq->ring_physaddr & 0x00000000ffffffffULL));
		IXGBE_WRITE_REG(hw, IXGBE_TDBAH(txq->reg_idx), (uint32_t)(txq->ring_physaddr >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_TDLEN(txq->reg_idx), txq->len * sizeof(union ixgbe_adv_tx_desc));

		/* setup context descriptor 0 for IP/TCP checksums */
		ixgbe_tx_xmit_ctx(txq, PKT_TX_IP_CKSUM | PKT_TX_TCP_CKSUM, 0);
	}

	return 0;
}

static int dev_start_vf(struct ix_rte_eth_dev *dev)
{
	int i;
	int ret;
	struct ixgbe_hw *hw;

	hw = IXGBE_DEV_PRIVATE_TO_HW(rte_eth_devices[dev->port].data->dev_private);

	ret = rte_eth_dev_start(dev->port);
	if (ret < 0)
		return ret;

	/* NOTE: This is a hack. We hijack DPDK mbufs with ours. */
	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		struct rx_queue *rxq = eth_rx_queue_to_drv(dev->data->rx_queues[i]);

		ret = ixgbe_alloc_rx_mbufs(rxq);
		if (ret)
			return ret;

		/* Configure descriptor ring base and length */
		IXGBE_WRITE_REG(hw, IXGBE_VFRDBAL(i), (uint32_t)(rxq->ring_physaddr & 0x00000000ffffffffULL));
		IXGBE_WRITE_REG(hw, IXGBE_VFRDBAH(i), (uint32_t)(rxq->ring_physaddr >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_VFRDLEN(i), rxq->len * sizeof(union ixgbe_adv_rx_desc));
	}

	for (i = 0; i < dev->data->nb_tx_queues; i++) {
		struct tx_queue *txq = eth_tx_queue_to_drv(dev->data->tx_queues[i]);

		IXGBE_WRITE_REG(hw, IXGBE_VFTDBAL(i), (uint32_t)(txq->ring_physaddr & 0x00000000ffffffffULL));
		IXGBE_WRITE_REG(hw, IXGBE_VFTDBAH(i), (uint32_t)(txq->ring_physaddr >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_VFTDLEN(i), txq->len * sizeof(union ixgbe_adv_tx_desc));

		/* setup context descriptor 0 for IP/TCP checksums */
		ixgbe_tx_xmit_ctx(txq, PKT_TX_IP_CKSUM | PKT_TX_TCP_CKSUM, 0);
	}

	return 0;
}

static int reta_update(struct ix_rte_eth_dev *dev, struct rte_eth_rss_reta *reta_conf)
{
	uint8_t i, j, mask;
	uint32_t reta;
	struct ixgbe_hw *hw =
		IXGBE_DEV_PRIVATE_TO_HW(rte_eth_devices[dev->port].data->dev_private);

	spin_lock(&ixgbe_dev_lock);

	/*
	* Update Redirection Table RETA[n],n=0...31,The redirection table has
	* 128-entries in 32 registers
	 */
	for (i = 0; i < dev->data->nb_rx_fgs; i += 4) {
		mask = (uint8_t)((reta_conf->mask[BITMAP_POS_IDX(i)] >> BITMAP_POS_SHIFT(i)) & 0xF);
		if (mask != 0) {
			reta = 0;
			if (mask != 0xF)
				reta = IXGBE_READ_REG(hw, IXGBE_RETA(i >> 2));

			for (j = 0; j < 4; j++) {
				if (mask & (0x1 << j)) {
					if (mask != 0xF)
						reta &= ~(0xFF << 8 * j);
					reta |= reta_conf->reta[i + j] << 8 * j;
				}
			}
			IXGBE_WRITE_REG(hw, IXGBE_RETA(i >> 2), reta);
		}
	}

	spin_unlock(&ixgbe_dev_lock);

	return 0;
}

static int ixgbe_alloc_rx_mbufs(struct rx_queue *rxq)
{
	int i;

	for (i = 0; i < rxq->len; i++) {
		machaddr_t maddr;
		struct mbuf *b = mbuf_alloc_local();
		if (!b)
			goto fail;

		maddr = mbuf_get_data_machaddr(b);
		rxq->ring_entries[i].mbuf = b;
		rxq->ring[i].read.hdr_addr = cpu_to_le32(maddr);
		rxq->ring[i].read.pkt_addr = cpu_to_le32(maddr);
	}

	return 0;

fail:
	for (i--; i >= 0; i--)
		mbuf_free(rxq->ring_entries[i].mbuf);
	return -ENOMEM;
}

static int ixgbe_rx_poll(struct eth_rx_queue *rx)
{
	struct rx_queue *rxq = eth_rx_queue_to_drv(rx);
	volatile union ixgbe_adv_rx_desc *rxdp;
	union ixgbe_adv_rx_desc rxd;
	struct mbuf *b, *new_b;
	struct rx_entry *rxqe;
	machaddr_t maddr;
	uint32_t status;
	int nb_descs = 0;
	bool valid_checksum;
	int local_fg_id;
	long timestamp;

	timestamp = rdtsc();
	while (1) {
		rxdp = &rxq->ring[rxq->head & (rxq->len - 1)];
		status = le32_to_cpu(rxdp->wb.upper.status_error);
		valid_checksum = true;

		if (!(status & IXGBE_RXDADV_STAT_DD))
			break;

		rxd = *rxdp;
		rxqe = &rxq->ring_entries[rxq->head & (rxq->len - 1)];

		/* Check IP checksum calculated by hardware (if applicable) */
		if (unlikely((rxdp->wb.upper.status_error & IXGBE_RXD_STAT_IPCS) &&
			     (rxdp->wb.upper.status_error & IXGBE_RXDADV_ERR_IPE))) {
			log_err("ixgbe: IP RX checksum error, dropping pkt\n");
			valid_checksum = false;
		}

		/* Check TCP checksum calculated by hardware (if applicable) */
		if (unlikely((rxdp->wb.upper.status_error & IXGBE_RXD_STAT_L4CS) &&
			     (rxdp->wb.upper.status_error & IXGBE_RXDADV_ERR_TCPE))) {
			log_err("ixgbe: TCP RX checksum error, dropping pkt\n");
			valid_checksum = false;
		}

		b = rxqe->mbuf;
		b->len = le32_to_cpu(rxd.wb.upper.length);

		if (status & IXGBE_RXDADV_STAT_FLM) {
			b->fg_id = MBUF_INVALID_FG_ID;
		} else {
			local_fg_id = (le32_to_cpu(rxd.wb.lower.hi_dword.rss) &
				       (rx->dev->data->nb_rx_fgs - 1));
			b->fg_id = rx->dev->data->rx_fgs[local_fg_id].fg_id;
		}
		b->timestamp = timestamp;

		new_b = mbuf_alloc_local();
		if (unlikely(!new_b)) {
			log_err("ixgbe: unable to allocate RX mbuf\n");
			goto out;
		}

		maddr = mbuf_get_data_machaddr(new_b);
		rxqe->mbuf = new_b;
		rxdp->read.hdr_addr = cpu_to_le32(maddr);
		rxdp->read.pkt_addr = cpu_to_le32(maddr);

		if (unlikely(!valid_checksum || eth_recv(rx, b))) {
			log_debug("ixgbe: dropping packet\n");
			mbuf_free(b);
		}

		rxq->head++;
		nb_descs++;
	}

out:

	/*
	 * We threshold updates to the RX tail register because when it
	 * is updated too frequently (e.g. when written to on multiple
	 * cores even through separate queues) PCI performance
	 * bottlnecks have been observed.
	 */
	if ((uint16_t)(rxq->len - (rxq->tail + 1 - rxq->head)) >=
	    IXGBE_RDT_THRESH) {
		rxq->tail = rxq->head + rxq->len - 1;

		/* inform HW that more descriptors have become available */
		IXGBE_PCI_REG_WRITE(rxq->rdt_reg_addr,
				    (rxq->tail & (rxq->len - 1)));
	}

	return nb_descs;
}

static bool ixgbe_rx_ready(struct eth_rx_queue *rx)
{
	volatile union ixgbe_adv_rx_desc *desc;
	struct rx_queue *rxq;

	rxq = eth_rx_queue_to_drv(rx);
	desc = &rxq->ring[rxq->head & (rxq->len - 1)];
	return desc->wb.upper.status_error & cpu_to_le32(IXGBE_RXDADV_STAT_DD);
}

/**
 * rx_queue_setup - prepares an RX queue
 * @dev: the ethernet device
 * @queue_idx: the queue number
 * @numa_node: the desired NUMA affinity, or -1 for no preference
 * @nb_desc: the number of descriptors to create for the ring
 *
 * Returns 0 if successful, otherwise failure.
 */
static int rx_queue_setup(struct ix_rte_eth_dev *dev, int queue_idx,
			  int numa_node, uint16_t nb_desc)
{
	void *page;
	machaddr_t page_phys;
	int ret;
	struct rx_queue *rxq;
	struct ixgbe_rx_queue *drxq;

	/*
	 * The number of receive descriptors must not exceed hardware
	 * maximum and must be a multiple of IXGBE_ALIGN.
	 */
	if (((nb_desc * sizeof(union ixgbe_adv_rx_desc)) % IXGBE_ALIGN) != 0 ||
	    nb_desc > IXGBE_MAX_RING_DESC || nb_desc < IXGBE_MIN_RING_DESC)
		return -EINVAL;

	BUILD_ASSERT(align_up(sizeof(struct rx_queue), IXGBE_ALIGN) +
		     (sizeof(union ixgbe_adv_rx_desc) + sizeof(struct rx_entry))
		     * IXGBE_MAX_RING_DESC < PGSIZE_2MB);

	/*
	 * Additionally, for purely software performance optimization reasons,
	 * we require the number of descriptors to be a power of 2.
	 */
	if (nb_desc & (nb_desc - 1))
		return -EINVAL;

	/* NOTE: This is a hack, but it's the only way to support late/lazy
	 * queue setup in DPDK; a feature that IX depends on. */
	rte_eth_devices[dev->port].data->nb_rx_queues = queue_idx + 1;

	ret = rte_eth_rx_queue_setup(dev->port, queue_idx, nb_desc, numa_node, &rx_conf, dpdk_pool);
	if (ret < 0)
		return ret;

	if (numa_node == -1) {
		page = mem_alloc_page_local(PGSIZE_2MB);
		if (page == MAP_FAILED)
			return -ENOMEM;
	} else {
		page = mem_alloc_page(PGSIZE_2MB, numa_node, MPOL_BIND);
		if (page == MAP_FAILED)
			return -ENOMEM;
	}

	memset(page, 0, PGSIZE_2MB);
	rxq = (struct rx_queue *) page;
	drxq = (struct ixgbe_rx_queue *)rte_eth_devices[dev->port].data->rx_queues[queue_idx];

	rxq->ring = (union ixgbe_adv_rx_desc *)((uintptr_t) page +
						align_up(sizeof(struct rx_queue), IXGBE_ALIGN));
	rxq->ring_entries = (struct rx_entry *)((uintptr_t) rxq->ring +
						sizeof(union ixgbe_adv_rx_desc) * nb_desc);
	rxq->len = nb_desc;
	rxq->head = 0;
	rxq->tail = rxq->len - 1;

	ret = mem_lookup_page_machine_addr(page, PGSIZE_2MB, &page_phys);
	if (ret)
		goto err;
	rxq->ring_physaddr = page_phys +
			     align_up(sizeof(struct rx_queue), IXGBE_ALIGN);

	rxq->reg_idx = drxq->reg_idx;

	rxq->rdt_reg_addr = drxq->rdt_reg_addr;
	rxq->erxq.poll = ixgbe_rx_poll;
	rxq->erxq.ready = ixgbe_rx_ready;
	dev->data->rx_queues[queue_idx] = &rxq->erxq;
	return 0;

err:
	mem_free_page(page, PGSIZE_2MB);
	return ret;
}

static int ixgbe_tx_reclaim(struct eth_tx_queue *tx)
{
	struct tx_queue *txq = eth_tx_queue_to_drv(tx);
	struct tx_entry *txe;
	volatile union ixgbe_adv_tx_desc *txdp;
	int idx = 0, nb_desc = 0;

	while ((uint16_t)(txq->head + idx) != txq->tail) {
		txe = &txq->ring_entries[(txq->head + idx) & (txq->len - 1)];

		if (!txe->mbuf) {
			idx++;
			continue;
		}

		txdp = &txq->ring[(txq->head + idx) & (txq->len - 1)];
		if (!(le32_to_cpu(txdp->wb.status) & IXGBE_TXD_STAT_DD))
			break;

		mbuf_xmit_done(txe->mbuf);
		txe->mbuf = NULL;
		idx++;
		nb_desc = idx;
	}

	txq->head += nb_desc;
	return (uint16_t)(txq->len + txq->head - txq->tail);
}

#define IP_HDR_LEN	20
/* ixgbe_tx_xmit_ctx - "transmit" context descriptor
 * 			tells NIC to load a new ctx into its memory
 * Currently assuming no no LSO.
 */
static int ixgbe_tx_xmit_ctx(struct tx_queue *txq, int ol_flags, int ctx_idx)
{
	volatile struct ixgbe_adv_tx_context_desc *txctxd;
	uint32_t type_tucmd_mlhl, mss_l4len_idx, vlan_macip_lens;

	/* Make sure enough space is available in the descriptor ring */
	if (unlikely((uint16_t)(txq->tail + 1 - txq->head) >= txq->len)) {
		ixgbe_tx_reclaim(&txq->etxq);
		if ((uint16_t)(txq->tail + 1 - txq->head) >= txq->len)
			return -EAGAIN;
	}

	/* Mark desc type as advanced context descriptor */
	type_tucmd_mlhl = IXGBE_ADVTXD_DTYP_CTXT | IXGBE_ADVTXD_DCMD_DEXT;

	/* Checksums */
	if (ol_flags & PKT_TX_IP_CKSUM) {
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;
	}

	if (ol_flags & PKT_TX_TCP_CKSUM) {
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
	}

	/* Set context idx. MSS and L4LEN ignored if no LSO */
	mss_l4len_idx = ctx_idx << IXGBE_ADVTXD_IDX_SHIFT;

	vlan_macip_lens = (ETH_HDR_LEN << IXGBE_ADVTXD_MACLEN_SHIFT) | IP_HDR_LEN;

	/* Put context desc on the desc ring */
	txctxd = (struct ixgbe_adv_tx_context_desc *) &txq->ring[txq->tail & (txq->len - 1)];

	txctxd->type_tucmd_mlhl = cpu_to_le32(type_tucmd_mlhl);
	txctxd->mss_l4len_idx   = cpu_to_le32(mss_l4len_idx);
	txctxd->seqnum_seed     = 0;
	txctxd->vlan_macip_lens = cpu_to_le32(vlan_macip_lens);

	/* Mark corresponding mbuf as NULL to allow desc reclaim */
	txq->ring_entries[txq->tail & (txq->len - 1)].mbuf = NULL;

	/* Used up a descriptor, advance tail */
	txq->tail++;
	IXGBE_PCI_REG_WRITE(txq->tdt_reg_addr,
			    (txq->tail & (txq->len - 1)));

	/* Update flag info in software ctx_cache */
	txq->ctx_cache[ctx_idx].flags = ol_flags;

	return 0;
}

static int ixgbe_tx_xmit_one(struct tx_queue *txq, struct mbuf *mbuf)
{
	volatile union ixgbe_adv_tx_desc *txdp;
	machaddr_t maddr;
	int i, nr_iov = mbuf->nr_iov;
	uint32_t type_len, pay_len = mbuf->len;
	uint32_t  olinfo_status = 0;

	/*
	 * Make sure enough space is available in the descriptor ring
	 * NOTE: This should work correctly even with overflow...
	 */
	if (unlikely((uint16_t)(txq->tail + nr_iov + 1 - txq->head) >= txq->len)) {
		ixgbe_tx_reclaim(&txq->etxq);
		if ((uint16_t)(txq->tail + nr_iov + 1 - txq->head) >= txq->len)
			return -EAGAIN;
	}

	/*
	 * Check mbuf's offload flags
	 * If flags match context 0 on NIC (IP and TCP chksum), use context
	 * Otherwise, no context
	 */
	if ((mbuf->ol_flags & PKT_TX_IP_CKSUM) &&
	    (mbuf->ol_flags & PKT_TX_TCP_CKSUM)) {
		olinfo_status |= IXGBE_ADVTXD_POPTS_IXSM;
		olinfo_status |= IXGBE_ADVTXD_POPTS_TXSM;
		olinfo_status |= IXGBE_ADVTXD_CC;
	}

	for (i = 0; i < nr_iov; i++) {
		struct mbuf_iov iov = mbuf->iovs[i];
		txdp = &txq->ring[(txq->tail + i + 1) & (txq->len - 1)];

		txdp->read.buffer_addr = cpu_to_le64((uintptr_t) iov.maddr);
		type_len = (IXGBE_ADVTXD_DTYP_DATA |
			    IXGBE_ADVTXD_DCMD_IFCS |
			    IXGBE_ADVTXD_DCMD_DEXT);
		type_len |= iov.len;
		if (i == nr_iov - 1) {
			type_len |= (IXGBE_ADVTXD_DCMD_EOP |
				     IXGBE_ADVTXD_DCMD_RS);
		}

		txdp->read.cmd_type_len = cpu_to_le32(type_len);
		txdp->read.olinfo_status = cpu_to_le32(olinfo_status);
		pay_len += iov.len;
	}

	txq->ring_entries[(txq->tail + nr_iov) & (txq->len - 1)].mbuf = mbuf;

	txdp = &txq->ring[txq->tail & (txq->len - 1)];
	maddr = mbuf_get_data_machaddr(mbuf);
	txdp->read.buffer_addr = cpu_to_le64(maddr);

	type_len = (IXGBE_ADVTXD_DTYP_DATA |
		    IXGBE_ADVTXD_DCMD_IFCS |
		    IXGBE_ADVTXD_DCMD_DEXT);
	type_len |= mbuf->len;
	if (!nr_iov) {
		type_len |= (IXGBE_ADVTXD_DCMD_EOP |
			     IXGBE_ADVTXD_DCMD_RS);
	}

	txdp->read.cmd_type_len = cpu_to_le32(type_len);
	txdp->read.olinfo_status = cpu_to_le32(pay_len << IXGBE_ADVTXD_PAYLEN_SHIFT) |
				   cpu_to_le32(olinfo_status);

	txq->tail += nr_iov + 1;

	return 0;
}

static int ixgbe_tx_xmit(struct eth_tx_queue *tx, int nr, struct mbuf **mbufs)
{
	struct tx_queue *txq = eth_tx_queue_to_drv(tx);
	int nb_pkts = 0;

	while (nb_pkts < nr) {
		if (ixgbe_tx_xmit_one(txq, mbufs[nb_pkts]))
			break;

		nb_pkts++;
	}

	if (nb_pkts)
		IXGBE_PCI_REG_WRITE(txq->tdt_reg_addr,
				    (txq->tail & (txq->len - 1)));

	return nb_pkts;
}

static void ixgbe_reset_tx_queue(struct tx_queue *txq)
{
	int i;

	for (i = 0; i < txq->len; i++) {
		txq->ring_entries[i].mbuf = NULL;
	}

	txq->head = 0;
	txq->tail = 0;

}

static int tx_queue_setup(struct ix_rte_eth_dev *dev, int queue_idx,
			  int numa_node, uint16_t nb_desc)
{
	void *page;
	machaddr_t page_phys;
	int ret;
	struct tx_queue *txq;
	struct ixgbe_tx_queue *dtxq;

	/*
	 * The number of receive descriptors must not exceed hardware
	 * maximum and must be a multiple of IXGBE_ALIGN.
	 */
	if (((nb_desc * sizeof(union ixgbe_adv_tx_desc)) % IXGBE_ALIGN) != 0 ||
	    nb_desc > IXGBE_MAX_RING_DESC || nb_desc < IXGBE_MIN_RING_DESC)
		return -EINVAL;

	BUILD_ASSERT(align_up(sizeof(struct tx_queue), IXGBE_ALIGN) +
		     (sizeof(union ixgbe_adv_tx_desc) + sizeof(struct tx_entry))
		     * IXGBE_MAX_RING_DESC < PGSIZE_2MB);

	/*
	 * Additionally, for purely software performance optimization reasons,
	 * we require the number of descriptors to be a power of 2.
	 */
	if (nb_desc & (nb_desc - 1))
		return -EINVAL;

	/* NOTE: This is a hack, but it's the only way to support late/lazy
	 * queue setup in DPDK; a feature that IX depends on. */
	rte_eth_devices[dev->port].data->nb_tx_queues = queue_idx + 1;

	ret = rte_eth_tx_queue_setup(dev->port, queue_idx, nb_desc, numa_node, &tx_conf);
	if (ret < 0)
		return ret;

	if (numa_node == -1) {
		page = mem_alloc_page_local(PGSIZE_2MB);
		if (page == MAP_FAILED)
			return -ENOMEM;
	} else {
		page = mem_alloc_page(PGSIZE_2MB, numa_node, MPOL_BIND);
		if (page == MAP_FAILED)
			return -ENOMEM;
	}

	memset(page, 0, PGSIZE_2MB);
	txq = (struct tx_queue *) page;
	dtxq = (struct ixgbe_tx_queue *)rte_eth_devices[dev->port].data->tx_queues[queue_idx];

	txq->ring = (union ixgbe_adv_tx_desc *)((uintptr_t) page +
						align_up(sizeof(struct tx_queue), IXGBE_ALIGN));
	txq->ring_entries = (struct tx_entry *)((uintptr_t) txq->ring +
						sizeof(union ixgbe_adv_tx_desc) * nb_desc);
	txq->len = nb_desc;

	ret = mem_lookup_page_machine_addr(page, PGSIZE_2MB, &page_phys);
	if (ret)
		goto err;
	txq->ring_physaddr = page_phys +
			     align_up(sizeof(struct tx_queue), IXGBE_ALIGN);

	txq->reg_idx = dtxq->reg_idx;

	txq->tdt_reg_addr = dtxq->tdt_reg_addr;
	txq->etxq.reclaim = ixgbe_tx_reclaim;
	txq->etxq.xmit = ixgbe_tx_xmit;
	ixgbe_reset_tx_queue(txq);
	dev->data->tx_queues[queue_idx] = &txq->etxq;
	return 0;

err:
	mem_free_page(page, PGSIZE_2MB);
	return ret;
}

static struct ix_eth_dev_ops eth_dev_ops = {
	.allmulticast_enable = generic_allmulticast_enable,
	.dev_infos_get = generic_dev_infos_get,
	.dev_start = dev_start,
	.link_update = generic_link_update,
	.promiscuous_disable = generic_promiscuous_disable,
	.reta_update = reta_update,
	.rx_queue_setup = rx_queue_setup,
	.tx_queue_setup = tx_queue_setup,
	.fdir_add_perfect_filter = generic_fdir_add_perfect_filter,
	.fdir_remove_perfect_filter = generic_fdir_remove_perfect_filter,
	.rss_hash_conf_get = generic_rss_hash_conf_get,
	.mac_addr_add = generic_mac_addr_add,
};

static struct ix_eth_dev_ops vf_eth_dev_ops = {
	.allmulticast_enable = generic_allmulticast_enable,
	.dev_infos_get = generic_dev_infos_get,
	.dev_start = dev_start_vf,
	.link_update = generic_link_update,
	.promiscuous_disable = generic_promiscuous_disable,
	.reta_update = reta_update,
	.rx_queue_setup = rx_queue_setup,
	.tx_queue_setup = tx_queue_setup,
	.fdir_add_perfect_filter = generic_fdir_add_perfect_filter,
	.fdir_remove_perfect_filter = generic_fdir_remove_perfect_filter,
	.rss_hash_conf_get = generic_rss_hash_conf_get,
	.mac_addr_add = generic_mac_addr_add,
};

int ixgbe_init(struct ix_rte_eth_dev *dev, const char *driver_name)
{
	if (!strcmp(driver_name, "rte_ixgbe_pmd")) {
		dev->dev_ops = &eth_dev_ops;
	} else if (!strcmp(driver_name, "rte_ixgbevf_pmd")) {
		dev->dev_ops = &vf_eth_dev_ops;
		/* check that the config is right */
		if (CFG.num_cpus > 1) {
			log_err("using more than 1 core on a VF is currently \
					not supported.\n");
			return -1;
		}
	} else {
		assert(0);
	}

	return 0;
}
