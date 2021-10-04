/*
 * Copyright 2018-19 Board of Trustees of Stanford University
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

#include <limits.h>
#include <stdint.h>
#include <ucontext.h>

#include <ix/cfg.h>
#include <ix/mempool.h>
#include <ix/ethqueue.h>
#include <ix/log.h>
#include <net/ip.h>
#include <net/udp.h>

#define MAX_WORKERS   18

#define WAITING     0x00
#define ACTIVE      0x01

#define RUNNING     0x00
#define FINISHED    0x01
#define PREEMPTED   0x02
#define PROCESSED   0x03

#define NOCONTENT   0x00
#define PACKET      0x01
#define CONTEXT     0x02

#define MAX_UINT64  0xFFFFFFFFFFFFFFFF

#define PKT_TYPE_NEW_TASK 0
#define PKT_TYPE_NEW_TASK_RANDOM 1
#define PKT_TYPE_TASK_DONE 2
#define PKT_TYPE_TASK_DONE_IDLE 3
#define PKT_TYPE_QUEUE_REMOVE 4
#define PKT_TYPE_SCAN_QUEUE_SIGNAL 5
#define PKT_TYPE_IDLE_SIGNAL 6
#define PKT_TYPE_QUEUE_SIGNAL 7
#define PKT_TYPE_PROBE_IDLE_QUEUE 8
#define PKT_TYPE_PROBE_IDLE_RESPONSE 9
#define PKT_TYPE_IDLE_REMOVE 10

#define WORKER_STATE_IDLE 1
#define WORKER_STATE_BUSY 0
#define SWAP_UINT16(x) (((x) >> 8) | ((x) << 8))

struct mempool_datastore task_datastore;
struct mempool task_mempool __attribute((aligned(64)));
struct mempool_datastore fini_request_cell_datastore;
struct mempool fini_request_cell_mempool __attribute((aligned(64)));
struct mempool_datastore request_datastore;
struct mempool request_mempool __attribute((aligned(64)));
struct mempool_datastore rq_datastore;
struct mempool rq_mempool __attribute((aligned(64)));

uint32_t got_idles;
uint32_t sent_idles;
uint32_t max_queue_wait;
volatile uint32_t queue_length[CFG_MAX_PORTS];

/*
 * SAQR: def
 * Saqr packet header strcture defined here as well as the application specific fields (e.g client_id, runNs, etc.)
 * runNs: Defines the type of task to be executed (e.g GET, SCAN) this behaviour is
   implemented in generic work function in worker.c
 * genNs: Timestamp of the generated task by the client (only used for measuring response time)
*/
struct message {
    uint8_t pkt_type;
    uint16_t cluster_id;
    uint16_t src_id;
    uint16_t dst_id;
    uint16_t qlen;
    uint16_t seq_num; // For multi-packet requests
    // Switch scheduler does not care about the fields below (needed by server scheduler)
    uint16_t client_id;
    uint32_t req_id;
    uint32_t pkts_length;
    uint64_t runNs;
    uint64_t genNs;
} __attribute__((__packed__));

struct request
{
    uint32_t pkts_length;
    uint16_t type;
    void * mbufs[8];
} __attribute__((packed, aligned(64)));

struct request_cell
{
    uint8_t pkts_remaining;
    uint16_t client_id;
    uint32_t req_id;
    struct request * req;
    struct request_cell * next;
    struct request_cell * prev;
} __attribute__((packed, aligned(64)));

struct request_queue {
        struct request_cell * head;
};

struct request_queue rqueue;

struct worker_response
{
        uint64_t flag;
        void * rnbl;
        struct request * req;
        uint64_t timestamp;
        uint8_t type;
        uint8_t category;
        char make_it_64_bytes[30];
} __attribute__((packed, aligned(64)));

struct dispatcher_request
{
        uint64_t flag;
        void * rnbl;
        struct request * req;
        uint8_t type;
        uint8_t category;
        uint64_t timestamp;
        char make_it_64_bytes[30];
} __attribute__((packed, aligned(64)));

struct networker_pointers_t
{
        uint8_t cnt;
        uint8_t free_cnt;
        uint8_t types[ETH_RX_MAX_BATCH];
        struct request * reqs[ETH_RX_MAX_BATCH];
        char make_it_64_bytes[64 - ETH_RX_MAX_BATCH*9 - 2];
} __attribute__((packed, aligned(64)));

struct fini_request_cell {
        struct request * req;
        struct fini_request_cell * next;
};

struct fini_request_queue {
        struct fini_request_cell * head;
};

struct fini_request_queue frqueue;

/* 
 * SAQR: In worker_state we maintain the idleness view about workers in leaf worker recived a pkt with qlen==1, it means
   that state in switch is now busy (just removed from idle list). So next time it becomes idle it sends TASK_DONE_IDLE 
   so leaf adds the worker to the idle list.
 */
volatile uint32_t worker_state[CFG_MAX_PORTS];



/*
 * @parham: Frees the cell for finished request
*/
static inline struct request * request_dequeue(struct fini_request_queue * frq)
{
        struct fini_request_cell * tmp;
        struct request * req;

        if (!frq->head)
                return NULL;

        req = frq->head->req;
        tmp = frq->head;
        mempool_free(&fini_request_cell_mempool, tmp);
        frq->head = frq->head->next;

        return req;
}

/*
 * @parham: This function is used only for handling finished requests
*/
static inline void request_enqueue(struct fini_request_queue * frq, struct request * req)
{
        if (unlikely(!req))
                return;
        struct fini_request_cell * frcell = mempool_alloc(&fini_request_cell_mempool);
        frcell->req = req;
        frcell->next = frq->head;
        frq->head = frcell;
}

struct task {
        void * runnable;
        struct request * req;
        uint8_t type;
        uint8_t category;
        uint64_t timestamp;
        struct task * next;
};

struct task_queue
{
        struct task * head;
        struct task * tail;
};

struct task_queue tskq[CFG_MAX_PORTS];

static inline void tskq_enqueue_head(struct task_queue * tq, void * rnbl,
                                     struct request * req, uint8_t type,
                                     uint8_t category, uint64_t timestamp)
{
        struct task * tsk = mempool_alloc(&task_mempool);
        tsk->runnable = rnbl;
        tsk->req = req;
        tsk->type = type;
        tsk->category = category;
        tsk->timestamp = timestamp;
        if (tq->head != NULL) {
            struct task * tmp = tq->head;
            tq->head = tsk;
            tsk->next = tmp;
        } else {
            tq->head = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        }
}

static inline void tskq_enqueue_tail(struct task_queue * tq, void * rnbl,
                                     struct request * req, uint8_t type,
                                     uint8_t category, uint64_t timestamp)
{
        struct task * tsk = mempool_alloc(&task_mempool);
        if (!tsk)
                return;
        tsk->runnable = rnbl;
        tsk->req = req;
        tsk->type = type;
        tsk->category = category;
        tsk->timestamp = timestamp;
        if (tq->head != NULL) {
            tq->tail->next = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        } else {
            tq->head = tsk;
            tq->tail = tsk;
            tsk->next = NULL;
        }
}

static inline int tskq_dequeue(struct task_queue * tq, void ** rnbl_ptr,
                                struct request ** req, uint8_t *type, uint8_t *category,
                                uint64_t *timestamp)
{
        if (tq->head == NULL)
            return -1;
        (*rnbl_ptr) = tq->head->runnable;
        (*req) = tq->head->req;
        (*type) = tq->head->type;
        (*category) = tq->head->category;
        (*timestamp) = tq->head->timestamp;
        struct task * tsk = tq->head;
        tq->head = tq->head->next;
        mempool_free(&task_mempool, tsk);
        if (tq->head == NULL)
                tq->tail = NULL;
        return 0;
}

static inline uint64_t get_queue_timestamp(struct task_queue * tq, uint64_t * timestamp)
{
        if (tq->head == NULL)
            return -1;
        (*timestamp) = tq->head->timestamp;
        return 0;
}

static inline int naive_tskq_dequeue(struct task_queue * tq, void ** rnbl_ptr,
                                     struct request ** req, uint8_t *type,
                                     uint8_t *category, uint64_t *timestamp, uint8_t core_id)
{
        /* 
         * SAQR
         * "type" param previously used by Shinjuku to put different task types in dedicated queues 
         * We use the same idea for isolating task queues of different workers
         * dequeue from the worker queue (type param here indicates the queue number)
        */
        
        if(tskq_dequeue(&tq[core_id], rnbl_ptr, req, type, category,
                        timestamp) == 0)
                return 0;
        
        return -1;
}

/*
 @parham: smart_tskq_dequeue reads 
*/
static inline int smart_tskq_dequeue(struct task_queue * tq, void ** rnbl_ptr,
                                     struct request ** req, uint8_t *type,
                                     uint8_t *category, uint64_t *timestamp,
                                     uint64_t cur_time)
{
        int i, ret;
        uint64_t queue_stamp;
        int index = -1;
        double max = 0;
        
        for (i = 0; i < CFG.num_ports; i++) {
                ret = get_queue_timestamp(&tq[i], &queue_stamp);
                if (ret)
                        continue;

                int64_t diff = cur_time - queue_stamp;
                double current = diff / CFG.slos[i];
                if (current > max) {
                        max = current;
                        index = i;
                }
        }

        if (index != -1) {
                return tskq_dequeue(&tq[index], rnbl_ptr, req, type, category,
                                    timestamp);
        }
        return -1;
}

/*
 * @parham: Parses the packet headers: eth, ip, udp.
 * modified to work with Saqr headers and support core-granular scheduling (schedulers select a worker for task not server)
*/
static inline struct request * rq_update(struct request_queue * rq, struct mbuf * pkt, uint8_t *core_id)
{
    // Quickly parse packet without doing checks
    struct eth_hdr * ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
    struct ip_hdr *  iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
    int hdrlen = iphdr->header_len * sizeof(uint32_t);
    struct udp_hdr * udphdr = mbuf_nextd_off(iphdr, struct udp_hdr *,
                                                 hdrlen);
    // Get data and udp header
    void * data = mbuf_nextd(udphdr, void *);
    struct message * msg = (struct message *) data;
    
    /* 
     * SAQR: Line below contains decision type tag (0|1) which indicates that leaf selected this worker based on 
      Idle selection (1) or not (0).
    */
    uint16_t type = SWAP_UINT16(msg->qlen);
    uint16_t seq_num = msg->seq_num;
    uint16_t cluster_id = msg->cluster_id;
    uint16_t client_id = msg->client_id;
    uint32_t req_id = msg->req_id;
    uint32_t pkts_length = msg->pkts_length / sizeof(struct message);
    if (pkts_length == 0)
        pkts_length = 1; // Minimum 1 packet per task
    // SAQR: To make it consistent with shinjuku conf, worker IDs start from 1 (in switch we use 0 based index)
    uint16_t dst_id = SWAP_UINT16(msg->dst_id) + 1; 
    //log_info("\npkt_type: %u, cluster_id: %u, src_id: %u, dst_id: %u, seq_num: %u, client_id: %u, req_id: %u, pkts_length: %u, queue_len: %u\n", type, cluster_id, msg->src_id, dst_id, seq_num, client_id, req_id, pkts_length, msg->qlen);
    
    /* 
     * SAQR: Here we find out the physical core based on the shinjuku configuration and info in pkt:
     * The packet dst_id contains the ID of worker. 
     * In shinjuku.conf We use the *port=[]* to indicate worker IDs each of which maps to one of the CPUs 
     * Here we check and match the dst_id with the worker ID and use that index to set core_id. 
    */
    *core_id = 0;
    for (int i = 0; i < CFG.num_ports; i++) {
        if (dst_id == CFG.ports[i]) {
            *core_id = i;
        } 
    }
    
    if (pkts_length == 1) {
        struct request * req = mempool_alloc(&request_mempool);
        req->type = type;
        req->pkts_length = 1;
        req->mbufs[0] = pkt;
        return req;
    }

    if (!rq->head) {
            struct request_cell * rc = mempool_alloc(&rq_mempool);
            rc->pkts_remaining = pkts_length - 1;
            rc->client_id = client_id;
            rc->req_id = req_id;
            rc->req = mempool_alloc(&request_mempool);
            rc->req->mbufs[seq_num] = pkt;
            rc->req->pkts_length = pkts_length;
            rc->req->type = type;
            rc->next = NULL;
            rc->prev = NULL;
            rq->head = rc;
            log_info("!rq->head NULL\n");
            return NULL;
    }
    struct request_cell * cur = rq->head;
        while (cur != NULL) {
                if (cur->client_id == client_id && cur->req_id == req_id) {
                        cur->req->mbufs[seq_num] = pkt;
                        cur->pkts_remaining--;
            if (cur->pkts_remaining == 0) {
                struct request * req = cur->req;
                if (cur->prev == NULL) {
                    rq->head = cur->next;
                    if (rq->head != NULL)
                        rq->head->prev = NULL;
                } else {
                    cur->prev->next = cur->next;
                    if (cur->next != NULL)
                        cur->next->prev = cur->prev;
                }
                mempool_free(&rq_mempool, cur);
                return req;
            }
            log_info("in while ret NULL\n");
            return NULL;
        }
        cur = cur->next;
        }

        if (cur == NULL) {
                struct request_cell * rc = mempool_alloc(&rq_mempool);
                rc->pkts_remaining = pkts_length - 1;
                rc->client_id = client_id;
                rc->req_id = req_id;
                rc->req = mempool_alloc(&request_mempool);
                rc->req->mbufs[seq_num] = pkt;
                rc->req->pkts_length = pkts_length;
                rc->req->type = type;
                rc->next = rq->head;
        rc->next->prev = rc;
        rc->prev = NULL;
                rq->head = rc;
                log_info("cur==NULL\n");
                return NULL;
        }
        log_info("last null\n");
        return NULL;
}

uint64_t timestamps[MAX_WORKERS];
uint8_t preempt_check[MAX_WORKERS];

volatile struct networker_pointers_t networker_pointers;
volatile struct worker_response worker_responses[MAX_WORKERS];
volatile struct dispatcher_request dispatcher_requests[MAX_WORKERS];

