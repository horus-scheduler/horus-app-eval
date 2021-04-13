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

/*
 * ethqueue.h - ethernet queue support
 */

#pragma once

#include <ix/cpu.h>
#include <ix/mbuf.h>
#include <ix/errno.h>
#include <ix/bitmap.h>
#include <ix/ethfg.h>

#define ETH_DEV_RX_QUEUE_SZ     512
#define ETH_DEV_TX_QUEUE_SZ     4096
#define ETH_RX_MAX_DEPTH	32768
#define ETH_RX_MAX_BATCH        6

DECLARE_PERCPU(int, eth_num_queues);

struct eth_rx_queue * eth_rxqs[NETHDEV];
struct mbuf * recv_mbufs[ETH_RX_MAX_BATCH];
int recv_type[ETH_RX_MAX_BATCH];

/*
 * Receive Queue API
 */

struct eth_rx_queue {
	void *perqueue_offset;

	struct mbuf *head; /* pointer to first received buffer */
	struct mbuf *tail; /* pointer to last received buffer */
	int len;	   /* the total number of buffers */
	int queue_idx;	   /* the queue index number */

	/* poll for new packets */
	int (*poll)(struct eth_rx_queue *rx);
	/* returns if new packets are available */
	bool (*ready)(struct eth_rx_queue *rx);

	/* a bitmap of flow groups directed to this queue */
	DEFINE_BITMAP(assigned_fgs, ETH_MAX_NUM_FG);

	struct ix_rte_eth_dev *dev;
};

/**
 * eth_rx_poll - receive pending packets on an RX queue
 * @rx: the RX queue
 *
 * Returns the number of packets received.
 */
static inline int eth_rx_poll(struct eth_rx_queue *rx)
{
	return rx->poll(rx);
}

/**
 * eth_process_poll - polls HW for new packets
 *
 * Returns the number of new packets received.
 */
static inline int eth_process_poll(void)
{
        int i, count = 0;
        struct eth_rx_queue *rxq;

        for (i = 0; i < percpu_get(eth_num_queues); i++) {
                rxq = eth_rxqs[i];
                count += eth_rx_poll(rxq);
        }

        return count;
}

static inline int eth_process_recv_queue(struct eth_rx_queue *rxq, struct mbuf ** pos_p)
{
        (*pos_p) = rxq->head;

        if (!(*pos_p))
                return -EAGAIN;

        /* NOTE: pos could get freed after eth_input(), so check here */
        rxq->head = (*pos_p)->next;
        rxq->len--;

        return eth_input(rxq, *pos_p);
}

/**
 * eth_process_recv - retrieves pending received packets
 *
 * Returns true if there are are no remaining packets.
 */
static inline int eth_process_recv(void)
{
        int i, type, count = 0;
        bool empty;
        struct mbuf * pos;

        /*
        * We round robin through each queue one packet at
        * a time for fairness, and stop when all queues are
        * empty or the batch limit is hit.
        */
        do {
                empty = true;
                for (i = 0; i < percpu_get(eth_num_queues); i++) {
                        struct eth_rx_queue *rxq = eth_rxqs[i];
                        type = eth_process_recv_queue(rxq, &pos);
                        if (type >= 0) {
                                recv_mbufs[count] = pos;
                                recv_type[count] = type;
                                count++;
                                empty = false;
                        }
                }
        } while (!empty && count < ETH_RX_MAX_BATCH);

        return count;
}

/**
 * eth_recv - enqueues a received packet
 * @rxq: the receive queue
 * @mbuf: the packet
 *
 * Typically called by the device driver's poll() routine.
 *
 * Returns 0 if successful, otherwise the packet should be dropped.
 */
static inline int eth_recv(struct eth_rx_queue *rxq, struct mbuf *mbuf)
{
	if (unlikely(rxq->len >= ETH_RX_MAX_DEPTH))
		return -EBUSY;

	mbuf->next = NULL;

	if (!rxq->head) {
		rxq->head = mbuf;
		rxq->tail = mbuf;
	} else {
		rxq->tail->next = mbuf;
		rxq->tail = mbuf;
	}

	rxq->len++;
	return 0;
}

extern bool eth_rx_idle_wait(uint64_t max_usecs);


/*
 * Transmit Queue API
 */

struct eth_tx_queue {
	int cap;	/* number of available buffers left */
	int len;	/* number of buffers used so far */
	struct mbuf *bufs[ETH_DEV_TX_QUEUE_SZ];

	int (*reclaim)(struct eth_tx_queue *tx);
	int (*xmit)(struct eth_tx_queue *tx, int nr, struct mbuf **mbufs);
};

/**
 * eth_tx_reclaim - scans the queue and reclaims finished buffers
 * @tx: the TX queue
 *
 * NOTE: scatter-gather mbuf's can span multiple descriptors, so
 * take that into account when interpreting the count provided by
 * this function.
 *
 * Returns an available descriptor count.
 */
static inline int eth_tx_reclaim(struct eth_tx_queue *tx)
{
	return tx->reclaim(tx);
}

/**
 * eth_tx_xmit - transmits packets on a TX queue
 * @tx: the TX queue
 * @nr: the number of mbufs to transmit
 * @mbufs: an array of mbufs to process
 *
 * Returns the number of mbuf's transmitted.
 */
static inline int eth_tx_xmit(struct eth_tx_queue *tx,
			      int nr, struct mbuf **mbufs)
{
	return tx->xmit(tx, nr, mbufs);
}

/* FIXME: convert to per-flowgroup */
//DECLARE_PERQUEUE(struct eth_tx_queue *, eth_txq);

/**
 * eth_send - enqueues a packet to be sent
 * @mbuf: the packet
 *
 * Returns 0 if successful, otherwise out of space.
 */
static inline int eth_send(struct eth_tx_queue *txq, struct mbuf *mbuf)
{
	int nr = 1 + mbuf->nr_iov;
	if (unlikely(nr > txq->cap))
		return -EBUSY;

	txq->bufs[txq->len++] = mbuf;
	txq->cap -= nr;

	return 0;
}

/**
 * eth_send_one - enqueues a packet without scatter-gather to be sent
 * @mbuf: the packet
 * @len: the length of the packet
 *
 * Returns 0 if successful, otherwise out of space.
 */
static inline int eth_send_one(struct eth_tx_queue *txq, struct mbuf *mbuf, size_t len)
{
	mbuf->len = len;
	mbuf->nr_iov = 0;

	return eth_send(txq, mbuf);
}

DECLARE_PERCPU(struct eth_tx_queue *, eth_txqs[]);

extern void eth_process_send(void);
extern void eth_process_reclaim(void);

