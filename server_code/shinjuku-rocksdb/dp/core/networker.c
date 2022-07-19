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
 * networker.c - networking core functionality
 *
 * A single core is responsible for receiving all network packets in the
 * system and forwading them to the dispatcher.
 */
#include <stdio.h>
#include <sys/time.h>
#include <ix/vm.h>
#include <ix/cfg.h>
#include <ix/log.h>
#include <ix/mbuf.h>
#include <ix/dispatch.h>
#include <ix/ethqueue.h>
#include <ix/transmit.h>

#include <asm/chksum.h>

#include <net/ip.h>
#include <net/udp.h>
#include <net/ethernet.h>
DEFINE_PERCPU(struct mempool, ka_response_pool __attribute__((aligned(64))));

/**
 * response_init - allocates global response datastore
 */
int ka_response_init(void)
{
        return mempool_create_datastore(&ka_response_datastore, 128000,
                                        sizeof(struct message), 1,
                                        MEMPOOL_DEFAULT_CHUNKSIZE,
                                        "keep_alive");
}

/**
 * response_init_cpu - allocates per cpu response mempools
 */
int ka_response_init_cpu(void)
{
        struct mempool *m = &percpu_get(ka_response_pool);
        return mempool_create(m, &ka_response_datastore, MEMPOOL_SANITY_PERCPU,
                              percpu_get(cpu_id));
}



/** - Checks if time elapsed since the given input time is longer than heart beat interval
 * 
 */
bool check_time(struct timeval start) {
	struct timeval end;
	gettimeofday(&end, NULL);
	uint64_t delta_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
	return delta_us >= CFG.keep_alive_interval_us; 
}

int send_keep_alive(uint64_t seq_num) {
	struct message resp;
	int ret;
	
	log_info("Sending Keepalive\n");
	resp.pkt_type = PKT_TYPE_KEEP_ALIVE;
	resp.src_id = CFG.ports[0];
	resp.dst_id = CFG.parent_leaf_id;
	resp.client_id = CFG.ports[0]; // Use first worker ID as client ID.
	resp.req_id = seq_num;
	resp.qlen = (uint16_t) CFG.num_ports;
	
	for (int i=0; i < CFG.num_ports; i++) {
		resp.app_data[i] = CFG.ports[i]; 
	}
	struct ip_tuple new_id = {
            .src_ip = CFG.host_addr.addr,
            .dst_ip = CFG.gateway_addr.addr,
            .src_port = CONTROLLER_PORT,
            .dst_port = CONTROLLER_PORT
    };
	ret = udp_send_one((void *)&resp, sizeof(struct message), &new_id);
	if (ret)
        log_warn("udp_send failed with error %d\n", ret);
	return ret;
}

int send_worker_id_ack () {
	struct message resp;
	int ret;
	log_info("Sending ACK for recently received worker IDs\n");
	resp.pkt_type = PKT_TYPE_WORKER_ID_ACK;
	for (int i=0; i < CFG.num_ports; i++) {
		resp.app_data[i] = CFG.ports[i]; 
	}
	struct ip_tuple new_id = {
            .src_ip = CFG.host_addr.addr,
            .dst_ip = CFG.gateway_addr.addr,
            .src_port = CONTROLLER_PORT,
            .dst_port = CONTROLLER_PORT
    };
	ret = udp_send_one((void *)&resp, sizeof(struct message), &new_id);
	if (ret)
        log_warn("udp_send failed with error %d\n", ret);
	return ret;
}


/**
 * do_networking - implements networking core's functionality
 * @parham: Receives packets from eth, and  fills the networker_pointer array, also removes the ones that are already done.
 * 
 */
void do_networking(void)
{
	ka_response_init();
	ka_response_init_cpu();
	int i, j, num_recv;
	uint8_t core_id;
	bool place_in_worker_queue;
	struct timeval last_heart_beat;
	uint64_t keep_alive_cnt = 0;
	gettimeofday(&last_heart_beat, NULL);
	rqueue.head = NULL;
	
	
	while (1)
	{	
		
		if (check_time(last_heart_beat)) { // Time elapsed is longer than HEARTBEAT_INTERVAL_US
			eth_process_reclaim();
        	eth_process_send();
			if (!send_keep_alive(keep_alive_cnt++)) // If succesfull, record the curr time as last heart beat
				gettimeofday(&last_heart_beat, NULL);
		}
		eth_process_poll();
		num_recv = eth_process_recv();
		if (num_recv == 0)
			continue;
		while (networker_pointers.cnt != 0)
			;
		for (i = 0; i < networker_pointers.free_cnt; i++)
		{
			struct request *req = networker_pointers.reqs[i];
			for (j = 0; j < req->pkts_length; j++)
			{
				mbuf_free(req->mbufs[j]);
			}
			mempool_free(&request_mempool, req);
		}
		networker_pointers.free_cnt = 0;
		j = 0;

		for (i = 0; i < num_recv; i++)
		{
			// @SAQR: pass core_id by ref so when parsing Saqr headers in rq_update, it will fill it based on dst_id
			struct request *req = rq_update(&rqueue, recv_mbufs[i], &core_id, &place_in_worker_queue);
			if (req)
			{
				networker_pointers.reqs[j] = req;
				networker_pointers.types[j] = core_id; // core_id makes task to be queued in its dedicated queue (each worker has its queue)
				//log_info("core_id: %u\n", (unsigned int) core_id);
				j++;
			} else if (!place_in_worker_queue) // Ctrl pkt for worker IDs
			{
				send_worker_id_ack();
			}
		}
		networker_pointers.cnt = j;
	}
}
