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

/*
 * worker.c - Worker core functionality
 *
 * Poll dispatcher CPU to get request to execute. The request is in the form
 * of ucontext_t. If interrupted, swap to main context and poll for next
 * request.
 */

#include <ucontext.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>

#include <sys/types.h>
#include <sys/resource.h>

#include <ix/rocksdb.h>
#include <ix/hijack.h>
#include <ix/cpu.h>
#include <ix/log.h>
#include <ix/mbuf.h>
#include <asm/cpu.h>
#include <ix/context.h>
#include <ix/dispatch.h>
#include <ix/transmit.h>
#include <ix/intersection.h>

#include <c.h>
#include <dune.h>

#include <net/ip.h>
#include <net/udp.h>
#include <net/ethernet.h>

#include <rte_byteorder.h>

#define TYPE_REQ 1
#define TYPE_RES 0
#define PREEMPT_VECTOR 0xf2

__thread ucontext_t uctx_main;
__thread ucontext_t * cont;
__thread int cpu_nr_;
__thread volatile uint8_t finished;

extern uint8_t flag;

DEFINE_PERCPU(struct mempool, response_pool __attribute__((aligned(64))));

extern int getcontext_fast(ucontext_t *ucp);
extern int swapcontext_fast(ucontext_t *ouctx, ucontext_t *uctx);
extern int swapcontext_very_fast(ucontext_t *ouctx, ucontext_t *uctx);

extern void dune_apic_eoi();
extern int dune_register_intr_handler(int vector, dune_intr_cb cb);

/**
 * response_init - allocates global response datastore
 */
int response_init(void)
{
        return mempool_create_datastore(&response_datastore, 128000,
                                        sizeof(struct message), 1,
                                        MEMPOOL_DEFAULT_CHUNKSIZE,
                                        "response");
}

/**
 * response_init_cpu - allocates per cpu response mempools
 */
int response_init_cpu(void)
{
        struct mempool *m = &percpu_get(response_pool);
        return mempool_create(m, &response_datastore, MEMPOOL_SANITY_PERCPU,
                              percpu_get(cpu_id));
}

static void test_handler(struct dune_tf *tf)
{
    asm volatile ("cli":::);
    dune_apic_eoi();
    swapcontext_fast_to_control(cont, &uctx_main);   
}

static void rocksdb_work(struct message * req) {
    rocksdb_readoptions_t * readoptions = rocksdb_readoptions_create();
    rocksdb_iterator_t * iter = rocksdb_create_iterator(db, readoptions);
    int counter = 0;

    /*
    * @parham: different tasks at worker based on runNs (set by client). 
    * Client sends runNs 500 for GET and 0 for SCAN functions.
    */
    if (req->runNs > 0) {
        //log_info("\nRUNS 0\n");
        size_t long_size;
        char long_key[8];
                snprintf(long_key, 8, "long_key");
        for (int i = 0; i < 60; i++) {
            char * long_val = rocksdb_get(db, readoptions, long_key, 8,
                                                      &long_size, NULL);
            //log_info("got string from DB %s \n", long_val);
            //log_info("str size %d \n", strlen(long_val));
            free(long_val);
        }
    } else {
        //log_info("\nRUNS 1\n");
        for (rocksdb_iter_seek_to_first(iter); rocksdb_iter_valid(iter); rocksdb_iter_next(iter)) {
            char * retr_key;
            size_t klen;
            retr_key = rocksdb_iter_key(iter, &klen);
            if (req->runNs > 0 && ++counter > 5000)
                break;
        }
    }
    rocksdb_iter_destroy(iter);
    rocksdb_readoptions_destroy(readoptions);
}

uint64_t * search_work(struct message * req) {
    uint64_t query_word_ids[MAX_QUERY_WORDS];
    uint64_t *intersection_res, *intermediate_res;
    uint64_t intersection_tmp[2][1 + MAX_INSERSECTION_DOCS];
    uint64_t query_word_cnt = req->runNs;
    
    for (unsigned i = 0; i < query_word_cnt; i++) {
        query_word_ids[i] = req->app_data[i];
    }
    uint32_t word_id_ofst = query_word_ids[0]-1;
    intersection_res = word_to_docids[word_id_ofst];

    for (unsigned intersection_opr_cnt = 1; intersection_opr_cnt < query_word_cnt; intersection_opr_cnt++) {
      word_id_ofst = query_word_ids[intersection_opr_cnt]-1;
      intermediate_res = intersection_tmp[intersection_opr_cnt % 2];

      compute_intersection(intersection_res, word_to_docids[word_id_ofst], intermediate_res);
      intersection_res = intermediate_res;

      if (intersection_res[0] == 0) // stop if the intersection is empty
        break;
    }
    return intersection_res;
}


/**
 * generic_work - generic function acting as placeholder for application-level
 *                work
 * @msw: the top 32-bits of the pointer containing the data
 * @lsw: the bottom 32 bits of the pointer containing the data
 */
static void generic_work(uint32_t msw, uint32_t lsw, uint32_t msw_id,
                         uint32_t lsw_id)
{
    asm volatile ("sti":::);

    struct ip_tuple * id = (struct ip_tuple *) ((uint64_t) msw_id << 32 | lsw_id);
    void * data = (void *)((uint64_t) msw << 32 | lsw);
    int ret;

    struct message * req = (struct message *) data;
    uint64_t *intersection_res;
    // log_info("Generic work being executed on %d\n", cpu_nr_);
    // log_info("queue_length %d: %d\n", cpu_nr_, queue_length[cpu_nr_]);
    // log_info("worker_state %d: %d\n", cpu_nr_, worker_state[cpu_nr_]);
    uint16_t client_id = SWAP_UINT16(req->client_id);
    if (client_id == ROCKSDB_CLIENT) {
        rocksdb_work(req);
    } else if (client_id == SEARCH_CLIENT) {
        intersection_res = search_work(req);
    } else {
        log_info("Unknown Client ID %d\n", client_id);
    }
    

	/* 
     * For synthetic workloads (generate different service times) use below:
        uint64_t i = 0;
        do {
                asm volatile ("nop");
                i++;
        } while ( i / 0.233 < req->runNs);
    */

    
    asm volatile ("cli":::);
    struct message resp;
    if (client_id == SEARCH_CLIENT) { // Write additional search result data to pkt
        uint64_t intersection_size = intersection_res[0];
        resp.runNs = intersection_size;
        for (unsigned i = 0; i < intersection_size; i++) {
            resp.app_data[i] = intersection_res[1+i];
        }
        
    } else {
        resp.runNs = req->runNs;    
    }
	resp.genNs = req->genNs;
	
    resp.cluster_id = req->cluster_id;
	resp.client_id = req->client_id;
	resp.req_id = req->req_id;
    uint16_t new_qlen;
    // HORUS: Set the latest worker qlen of the worker core in header field
    new_qlen = queue_length[cpu_nr_] - 1;
    resp.qlen = new_qlen;

    // HORUS: Sending reply back to the client:
    resp.src_id = (req->dst_id);
    resp.dst_id = (req->client_id);
    
    // HORUS: Leaf does not have this worker in its idle list and it became idle;
    // use PKT_TYPE_TASK_DONE_IDLE so that leaf add the worker to idle list. 
    if (new_qlen == 0 && (worker_state[cpu_nr_] > 0)) { 
        resp.pkt_type = PKT_TYPE_TASK_DONE_IDLE; 
        //log_info("worker IDLE %d: %d\n", cpu_nr_, worker_state[cpu_nr_]);
        // sent_idles += 1;
        // log_info("sent_idles: %u", sent_idles); 
    } else {
        resp.pkt_type = PKT_TYPE_TASK_DONE;
    }
    // if (resp.qlen > 0) {
    //     resp.pkt_type = PKT_TYPE_TASK_DONE;
    // } else if (resp.qlen == 0 && req->qlen==0){
    //     resp.pkt_type = PKT_TYPE_TASK_DONE_IDLE;
    // }

    struct ip_tuple new_id = {
            .src_ip = id->dst_ip,
            .dst_ip = id->src_ip,
            .src_port = id->dst_port,
            .dst_port = id->src_port
    };

    resp.qlen = SWAP_UINT16(resp.qlen); 
    ret = udp_send_one((void *)&resp, sizeof(struct message), &new_id); // HORUS: Send reply
    if (ret)
        log_warn("udp_send failed with error %d\n", ret);

    finished = true;
    swapcontext_very_fast(cont, &uctx_main);
}

static inline void parse_packet(struct mbuf * pkt, void ** data_ptr,
                                struct ip_tuple ** id_ptr)
{
        // Quickly parse packet without doing checks
        struct eth_hdr * ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
        struct ip_hdr *  iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
        int hdrlen = iphdr->header_len * sizeof(uint32_t);
        struct udp_hdr * udphdr = mbuf_nextd_off(iphdr, struct udp_hdr *,
                                                 hdrlen);
        // Get data and udp header
        (*data_ptr) = mbuf_nextd(udphdr, void *);
        uint16_t len = ntoh16(udphdr->len);

        if (unlikely(!mbuf_enough_space(pkt, udphdr, len))) {
                log_warn("worker: not enough space in mbuf\n");
                (*data_ptr) = NULL;
                return;
        }

        (*id_ptr) = mbuf_mtod(pkt, struct ip_tuple *);
        (*id_ptr)->src_ip = ntoh32(iphdr->src_addr.addr);
        (*id_ptr)->dst_ip = ntoh32(iphdr->dst_addr.addr);
        (*id_ptr)->src_port = ntoh16(udphdr->src_port);
        (*id_ptr)->dst_port = ntoh16(udphdr->dst_port);
        pkt->done = (void *) 0xDEADBEEF;
}

static inline void init_worker(void)
{
        cpu_nr_ = percpu_get(cpu_nr) - 2;
        worker_responses[cpu_nr_].flag = PROCESSED;
        worker_state[cpu_nr_] = 0; // HORUS: Initial state of all workers are 0 (in idle list of leaf)
        if (cpu_nr_ == 0) {
            // Initialize search app requirements
            load_docs();
            log_info("Search App init: Loaded %d words.\n", word_cnt);
        }
        dune_register_intr_handler(PREEMPT_VECTOR, test_handler);
        eth_process_reclaim();
        asm volatile ("cli":::);
}

static inline void handle_new_packet(void)
{
        int ret;
        void * data;
        struct ip_tuple * id;
        struct mbuf * pkt = (struct mbuf *) dispatcher_requests[cpu_nr_].req->mbufs[0];
        
        parse_packet(pkt, &data, &id);
        
        if (data) {
                uint32_t msw = ((uint64_t) data & 0xFFFFFFFF00000000) >> 32;
                uint32_t lsw = (uint64_t) data & 0x00000000FFFFFFFF;
                uint32_t msw_id = ((uint64_t) id & 0xFFFFFFFF00000000) >> 32;
                uint32_t lsw_id = (uint64_t) id & 0x00000000FFFFFFFF;
                cont = dispatcher_requests[cpu_nr_].rnbl;
                getcontext_fast(cont);
                set_context_link(cont, &uctx_main);
                makecontext(cont, (void (*)(void)) generic_work, 4, msw, lsw,
                            msw_id, lsw_id);
                finished = false;
                ret = swapcontext_very_fast(&uctx_main, cont);
                if (ret) {
                        log_err("Failed to do swap into new context\n");
                        exit(-1);
                }
        } else {
                log_info("OOPS No Data\n");
                finished = true;
        }
}

static inline void handle_context(void)
{
        int ret;
        finished = false;
        cont = dispatcher_requests[cpu_nr_].rnbl;
        set_context_link(cont, &uctx_main);
        ret = swapcontext_fast(&uctx_main, cont);
        if (ret) {
                log_err("Failed to swap to existing context\n");
                exit(-1);
        }
}

static inline void handle_request(void)
{
        while (dispatcher_requests[cpu_nr_].flag == WAITING);
        dispatcher_requests[cpu_nr_].flag = WAITING;
        if (dispatcher_requests[cpu_nr_].category == PACKET){
                
                handle_new_packet();
        }
        else{
                handle_context();
                
            }
}

static inline void finish_request(void)
{
        worker_responses[cpu_nr_].timestamp = \
                        dispatcher_requests[cpu_nr_].timestamp;
        worker_responses[cpu_nr_].type = \
                        dispatcher_requests[cpu_nr_].type;
        worker_responses[cpu_nr_].req = \
                        dispatcher_requests[cpu_nr_].req;
        worker_responses[cpu_nr_].rnbl = cont;
        worker_responses[cpu_nr_].category = CONTEXT;
        if (finished) {
                worker_responses[cpu_nr_].flag = FINISHED;
        } else {
                worker_responses[cpu_nr_].flag = PREEMPTED;
        }
}

void do_work(void)
{
        init_worker();
        log_info("do_work: Waiting for dispatcher work\n");

        while (true) {
                eth_process_reclaim();
                eth_process_send();
                handle_request();
                finish_request();
        }
}

