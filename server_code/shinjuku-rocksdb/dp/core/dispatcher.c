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
 * dispatcher.c - dispatcher core functionality
 *
 * A single core is responsible for receiving network packets from the network
 * core and dispatching these packets or contexts to the worker cores.
 */

#include <ix/cfg.h>
#include <ix/context.h>
#include <ix/dispatch.h>

extern void dune_apic_send_posted_ipi(uint8_t vector, uint32_t dest_core);

#define PREEMPT_VECTOR 0xf2

static void timestamp_init(int num_workers)
{
        int i;
        for (i = 0; i < num_workers; i++)
                timestamps[i] = MAX_UINT64;
}

static void preempt_check_init(int num_workers)
{
        int i;
        for (i = 0; i < num_workers; i++)
                preempt_check[i] = false;
}

static inline void handle_finished(int i)
{
    uint8_t core_id;
    if (worker_responses[i].req == NULL)
            log_warn("No mbuf was returned from worker\n");
    context_free(worker_responses[i].rnbl);
    core_id = worker_responses[i].type;
    // SAQR: Task finished, decrement worker queue len
    --queue_length[core_id];
    /* 
     * SAQR: If the worker were previously removed from the idle list of leaf,
      when it became idle we sent an TASK_DONE_IDLE reply (in worker.c)
     * Here we change the state  
    */
    if (queue_length[core_id] == 0 && (worker_state[core_id] > 0)){
        worker_state[core_id] -= 1;
    }
    request_enqueue(&frqueue, (struct request *) worker_responses[i].req);
    preempt_check[i] = false;
    worker_responses[i].flag = PROCESSED;
}

static inline void handle_preempted(int i)
{
        void * rnbl;
	struct request * req;
        uint8_t type, category;
        uint64_t timestamp;

        rnbl = worker_responses[i].rnbl;
        req = worker_responses[i].req;
        category = worker_responses[i].category;
        type = worker_responses[i].type;
        timestamp = worker_responses[i].timestamp;
	if (CFG.queue_settings[type]) {
		tskq_enqueue_head(&tskq[type], rnbl, req, type, category, timestamp);
	} else {
		tskq_enqueue_tail(&tskq[type], rnbl, req, type, category, timestamp);
	}
        preempt_check[i] = false;
        worker_responses[i].flag = PROCESSED;
}

static inline void dispatch_request(int i, uint64_t cur_time)
{
    void * rnbl;
	struct request * req;
    uint8_t type, category;
    uint64_t timestamp;
    
    /* 
     * SAQR:
     * Pick a task from the queue "tskq"
     * Isolating worker queues is done here
     * Pass the i (cpu core id) to the dequeue function so it can dequeue from the queue that belong to a certian worker
     * We use naive_tskq_dequeue instead of smart_tskq_dequeue since we do not assume prior knowledge about task type and SLOs (smart_tskq_dequeue is SLO based). 
     * Can be modified in other cases if task types are known
    */
    if(naive_tskq_dequeue(tskq, &rnbl, &req, &type,
                          &category, &timestamp, (uint8_t)i))
            return;
    // NOTE: changes the worker stat to RUNNING (Busy) So the parent loop won't call this function no more (until flag changes)
    worker_responses[i].flag = RUNNING;
    // NOTE: Fill the dispatcher_request array for this worker, with regards to data that we took from taskq
    dispatcher_requests[i].rnbl = rnbl;
    dispatcher_requests[i].req = req;
    dispatcher_requests[i].type = type;
    dispatcher_requests[i].category = category;
    dispatcher_requests[i].timestamp = timestamp;
    timestamps[i] = cur_time;
    preempt_check[i] = true;
    dispatcher_requests[i].flag = ACTIVE;
}

static inline void preempt_worker(int i, uint64_t cur_time)
{
        if (preempt_check[i] && (((cur_time - timestamps[i]) / 2.5) > CFG.preemption_delay)) {
                // Avoid preempting more times.
                preempt_check[i] = false;
                dune_apic_send_posted_ipi(PREEMPT_VECTOR, CFG.cpu[i + 2]);
        }
}

static inline void handle_worker(int i, uint64_t cur_time)
{
        if (worker_responses[i].flag != RUNNING) { // NOTE: Worker is not executing anything...
                if (worker_responses[i].flag == FINISHED) {
                        handle_finished(i);
                } else if (worker_responses[i].flag == PREEMPTED) {
                        handle_preempted(i);
                }
                dispatch_request(i, cur_time);  // Dispatch for worker i (i is core number)
        } 
        //else
                //preempt_worker(i, cur_time);
}

/*
 * NOTE: The networker_pointers elements are added by networker.c do_networking().
 * Here this function takes those elements and enqueues task objects into the taskq[] 
 * Racksched has different tasqs for types of packets, we use these different queues for different workers
*/
static inline void handle_networker(uint64_t cur_time)
{
        int i, ret;
        uint8_t core_id;
        ucontext_t * cont;

        if (networker_pointers.cnt != 0) {
                for (i = 0; i < networker_pointers.cnt; i++) {
                        ret = context_alloc(&cont);
                        if (unlikely(ret)) {
                                //log_warn("Cannot allocate context\n");
                                request_enqueue(&frqueue, networker_pointers.reqs[i]);
                                continue;
                        }
                        core_id = networker_pointers.types[i];
			            // SAQR: increment worker queue len 
                        ++queue_length[core_id];
                        struct request *req = networker_pointers.reqs[i];
                        //log_info("WORKER %d REQTYPE %d", core_id, req->type);
                        if (req->type == WORKER_STATE_IDLE && worker_state[core_id] == 0) { 
                            // SAQR: WORKER_STATE_IDLE means leaf selected this worker based on idle selection.
                            // Therfore, leaf scheduler just poped this worker from its idle list. 
                            // We keep this state so worker will re-send an idle signal when idle (in worker.c)
                            worker_state[core_id] = 1;
                        }
                        tskq_enqueue_tail(&tskq[core_id], cont,
                                          networker_pointers.reqs[i],
                                          core_id, PACKET, cur_time);
                }

                for (i = 0; i < ETH_RX_MAX_BATCH; i++) {
                        struct request * req = request_dequeue(&frqueue);
                        if (!req)
                                break;
                        networker_pointers.reqs[i] = req;
                        networker_pointers.free_cnt++;
                }
                networker_pointers.cnt = 0;
        }
}

/**
 * do_dispatching - implements dispatcher core's main loop
 * NOTE: This is the main loop:
 *      Calls "handle_networker()" which enqueues the requests received (from networker.c which calls ethernet recv) to tasq array. 
 *      Calls "handle_worker()" for each worker core. The handle_worker() will try to dequeue from tasq and dispatch for that so it will run it (worker.c).
 */
void do_dispatching(int num_cpus)
{
        int i;
        uint64_t cur_time;

        preempt_check_init(num_cpus - 2);
        timestamp_init(num_cpus - 2);
        
        while(1) {
                cur_time = rdtsc();
                for (i = 0; i < num_cpus - 2; i++)
                        handle_worker(i, cur_time);
                handle_networker(cur_time);
        }
}

