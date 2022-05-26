#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <time.h>
#include <endian.h>

#include <rte_cycles.h>
#include <rte_debug.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_per_lcore.h>
#include <rte_udp.h>

#include "util.h"
#include "set.h"

/*
 * SAQR: Defines the ID of spine scheduler (each scheduler has a unique static ID),
  should be the same as ID set in p4 code for spine.
*/
#define SPINE_SCHEDULER_ID 100
// SAQR: Defines ID of this client (each client has a unique ID), so that reply packet is forwarded correctly to the 
// client that initiated the request. MAC address and this ID are configured in controller of switches.
#define CLIENT_ID 110

#define NUM_PKTS 1
#define MAX_RESULT_SIZE 16777216UL  // 2^24

#define APP_BENCHMARK 0
#define APP_KEYVAL 1
#define APP_SEARCH 2

#define SEARCH_MIN_WORD 1
#define SEARCH_MAX_WORD 16
#define SEARCH_MIN_KEY 1
#define SEARCH_MAX_KEY 200
/*
 * custom types
 */

/********* Configs *********/
char ip_client[][32] = {
    "10.1.0.1", "10.1.0.2", "10.1.0.3", "10.1.0.4",  "10.1.0.5",  "10.1.0.6",
    "10.1.0.7", "10.1.0.8", "10.1.0.9", "10.1.0.10", "10.1.0.11", "10.1.0.12",
};

char ip_server[][32] = {
    "10.1.0.1", "10.1.0.2", "10.1.0.3", "10.1.0.4",  "10.1.0.5",  "10.1.0.6",
    "10.1.0.7", "10.1.0.8", "10.1.0.9", "10.1.0.10", "10.1.0.11", "10.1.0.12",
};

// SAQR: Example of reserved port used for identifying Saqr headers by the switches (dst_port)
uint16_t src_port = 1234;
uint16_t dst_port = 1234;

/********* Custom parameters *********/
int is_latency_client = 0;   // 0 means batch client, 1 means latency client
uint32_t client_index = 11;  // default client: netx12
uint32_t server_index = 0;   // default server: netx1
uint32_t qps = 20000;       // default qps: 100000
char *dist_type = "fixed";   // default dist: fixed
uint32_t scale_factor = 1;   // 1 means the mean is 1us; mean = 1 * scale_factor
uint32_t is_rocksdb = 0; // 1 means testing with rocksdb, 0 means testing with synthetic workload
char *run_name;
/********* Packet related *********/
uint32_t req_id = 0;
int max_req_id = 65536;
uint32_t req_id_core[8] = {0};

/********* Results *********/
uint64_t pkt_sent = 0;
uint64_t pkt_recv = 0;
LatencyResults latency_results = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0};

SearchResults search_results = {NULL, NULL, NULL, NULL, NULL};

/********* Debug the us *********/
uint64_t work_ns_sample[1048576] = {0};
uint32_t sample_idx = 0;

/*
 * functions called when program ends
 */

static void dump_stats_to_file() {
  size_t count = latency_results.count;
  char output_name_base[100];
  char output_name[200];
  
  sprintf(output_name_base, "./results/output_%s_%s_%u", run_name, dist_type, qps);
  FILE *output_ptr = fopen(output_name_base, "wb");
  
  sprintf(output_name, "%s.reply_ns", output_name_base);
  FILE *output_reply_ns_ptr = fopen(output_name, "wb");

  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.short", output_name_base);
  FILE *output_ptr_short = fopen(output_name, "wb");

  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.long", output_name_base);
  FILE *output_ptr_long = fopen(output_name, "wb");
  
  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.ratios", output_name_base);
  FILE *ratio_ptr = fopen(output_name, "wb");
  
  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.ratios_short", output_name_base);
  FILE *ratio_ptr_short = fopen(output_name, "wb");
  
  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.ratios_long", output_name_base);
  FILE *ratio_ptr_long = fopen(output_name, "wb");
  
  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.queue_lengths", output_name_base);
  FILE *queue_length_ptr = fopen(output_name, "wb");

  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.search_doc_cnt", output_name_base);
  FILE *search_res_ptr = fopen(output_name, "wb");

  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.search_doc_id", output_name_base);
  FILE *search_id_ptr = fopen(output_name, "wb");

  for (int i = 0; i < latency_results.count; i++) {
    fprintf(queue_length_ptr, "%u\n",latency_results.queue_lengths[i][0]);
    fprintf(output_ptr, "%lu\n", latency_results.sjrn_times[i]);
    fprintf(output_reply_ns_ptr, "%lu\n", latency_results.reply_run_ns[i]);
    fprintf(ratio_ptr, "%lu\n", latency_results.work_ratios[i]);
    // Search results
    fprintf(search_res_ptr, "%lu\n", search_results.doc_cnt[i]);
    fprintf(search_id_ptr, "%lu\n", search_results.doc_id1[i]);
    fprintf(search_id_ptr, "%lu\n", search_results.doc_id2[i]);
    fprintf(search_id_ptr, "%lu\n", search_results.doc_id3[i]);
    fprintf(search_id_ptr, "%lu\n", search_results.doc_id4[i]);

    if (latency_results.count_short > i){
      fprintf(output_ptr_short, "%lu\n", latency_results.sjrn_times_short[i]);
      fprintf(ratio_ptr_short, "%lu\n", latency_results.work_ratios_short[i]);
    }
    
    if (latency_results.count_long > i){
      fprintf(output_ptr_long, "%lu\n", latency_results.sjrn_times_long[i]);
      fprintf(ratio_ptr_long, "%lu\n", latency_results.work_ratios_long[i]);
    }
  }

  fclose(search_res_ptr);
  fclose(search_id_ptr);
  fclose(queue_length_ptr);
  fclose(output_ptr_short);
  fclose(output_ptr_long);
  fclose(output_ptr);
  fclose(ratio_ptr);
  fclose(ratio_ptr_long);
  fclose(ratio_ptr_short);
}

static void sigint_handler(int sig) {
  double req_recv_ratio = (double)pkt_recv / (double)req_id;
  double recv_ratio = (double)pkt_recv / (double)pkt_sent;
  printf("\nRequests sent: %u\n", req_id);
  printf("Packets sent: %lu\n", pkt_sent);
  printf("Responses/Packets received: %lu\n", pkt_recv);
  printf("Ratio of responses recv/requests sent: %lf\n", req_recv_ratio);
  printf("Ratio of packets recv/packets sent: %lf\n", recv_ratio);
  
  fflush(stdout);
  if (is_latency_client > 0) {
    dump_stats_to_file();
  }
  rte_free(latency_results.sjrn_times);
  rte_free(latency_results.work_ratios);
  rte_free(latency_results.queue_lengths);
  rte_free(latency_results.sjrn_times_short);
  rte_free(latency_results.sjrn_times_long);
  rte_free(latency_results.work_ratios_short);
  rte_free(latency_results.work_ratios_long);
  rte_free(latency_results.reply_run_ns);

  rte_exit(EXIT_SUCCESS, "\nStopped DPDK client\n");
}

/*
 * functions
 */
/*
 * INFO: Generates a request (i.e., task) packet
*/
static void generate_packet(uint32_t lcore_id, struct rte_mbuf *mbuf,
                            uint16_t seq_num, uint32_t pkts_length,
                            uint64_t gen_ns, uint64_t run_ns, int port_offset,
                            int app_type,
                            uint64_t app_data[]) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  assert(mbuf != NULL);

  mbuf->next = NULL;
  mbuf->nb_segs = 1;
  mbuf->ol_flags = 0;
  mbuf->data_len = 0;
  mbuf->pkt_len = 0;

  // init packet header
  struct ether_hdr *eth = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
  struct ipv4_hdr *ip =
      (struct ipv4_hdr *)((uint8_t *)eth + sizeof(struct ether_hdr));
  struct udp_hdr *udp =
      (struct udp_hdr *)((uint8_t *)ip + sizeof(struct ipv4_hdr));
  rte_memcpy(eth, header_template, sizeof(header_template));
  mbuf->data_len += sizeof(header_template);
  mbuf->pkt_len += sizeof(header_template);

  // init req msg
  Message *req = (Message *)((uint8_t *)eth + sizeof(header_template));

  // eth->d_addr.addr_bytes[0] = 0xF8;
  // eth->d_addr.addr_bytes[1] = 0xF2;
  // eth->d_addr.addr_bytes[2] = 0x1E;
  // eth->d_addr.addr_bytes[3] = 0x13;
  // eth->d_addr.addr_bytes[4] = 0xCA;
  // eth->d_addr.addr_bytes[5] = 0xFC;

  // F8:F2:1E:3A:13:ED

  eth->d_addr.addr_bytes[0] = 0xF8;
  eth->d_addr.addr_bytes[1] = 0xF2;
  eth->d_addr.addr_bytes[2] = 0x1E;
  eth->d_addr.addr_bytes[3] = 0x3A;
  eth->d_addr.addr_bytes[4] = 0x13;
  eth->d_addr.addr_bytes[5] = 0x00;
  
  inet_pton(AF_INET, ip_client[client_index], &(ip->src_addr));
  inet_pton(AF_INET, ip_server[server_index], &(ip->dst_addr));
  udp->src_port = htons(src_port + port_offset);
  udp->dst_port = htons(dst_port + port_offset);

  req->pkt_type = PKT_TYPE_NEW_TASK; // SAQR: Packet type for a task to be scheduled
  req->cluster_id = 0 << 8; // SAQR: In our testbed experiments we assume workers are on cluster_id=0
  
  /* 
   * SAQR: Arbitrary source (different clients use different codes)
  */
  req->src_id = 7; 
  req->dst_id = SPINE_SCHEDULER_ID << 8;
  
  req->qlen = 0;
  
  
  // SAQR: Client ID is used for routing the reply packets to correct client machine (configured by switch controller)
  req->client_id = (CLIENT_ID<<8);
  
  req->req_id = (req->client_id << 25) + req_id_core[lcore_id];
  req->seq_num = SWAP_UINT16((uint16_t)(req->req_id));
  req->pkts_length = pkts_length;
  req->gen_ns = gen_ns; // INFO: Req generation time (for calculations)
  req->run_ns = run_ns; // INFO: Defines the type of task (used in worker to identify the work to be done)
  if (app_type == APP_SEARCH) {
    int i=0;
    for (i=0; i < gen_ns; i++) {
      req->app_data[i] = app_data[i];
    }
  }
  // printf("request client_id: %u, req_id: %u\n",req->client_id,req->req_id);
  mbuf->data_len += sizeof(Message);
  mbuf->pkt_len += sizeof(Message);

  pkt_sent++;
}

static void process_packet(uint32_t lcore_id, struct rte_mbuf *mbuf) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];

  // parse packet header
  struct ether_hdr *eth = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
  struct ipv4_hdr *ip =
      (struct ipv4_hdr *)((uint8_t *)eth + sizeof(struct ether_hdr));
  struct udp_hdr *udp =
      (struct udp_hdr *)((uint8_t *)ip + sizeof(struct ipv4_hdr));

  // parse header
  Message *res = (Message *)((uint8_t *)eth + sizeof(header_template));
  //printf("res->pkt_type: %u, res->qlen: %u", res->pkt_type, res->qlen);
  
  uint8_t server_idx = ntohl(ip->src_addr) - 167837696;
  // debug
  // printf("client_id:%u, req_id:%u\n",res->client_id, res->req_id);
  uint64_t cur_ns = get_cur_ns();
  uint16_t reply_port = ntohs(udp->src_port);
  // printf("reply_port:%u\n",reply_port);
  uint64_t reply_run_ns = rte_le_to_cpu_64(res->run_ns);
  assert(cur_ns > res->gen_ns);
  uint64_t sjrn = cur_ns - res->gen_ns;  // diff in time
  latency_results.sjrn_times[latency_results.count] = sjrn;
  //printf("\nresponse time: %u\n", sjrn);
  latency_results.reply_run_ns[latency_results.count] = reply_run_ns;
  
  if (is_rocksdb == 1) { // INFO: Used for calculating ratio of response time to actual task running time (note: scheduler is not aware of the actual running times)
      if (res->run_ns == 0)
        res->run_ns = 650000;
      else
        res->run_ns = 40000;
  }
  else {
      if (res->run_ns == 0)
        res->run_ns = 1;
  }
  latency_results.work_ratios[latency_results.count] = sjrn / res->run_ns;
  
  latency_results.queue_lengths[latency_results.count][0] =
      (res->qlen) >> 8;
  latency_results.count++;
  if (reply_run_ns >= 100000000) {  //@muh: changes for long and short out file differ
      // long request
      latency_results.sjrn_times_long[latency_results.count_long] = sjrn;
      latency_results.work_ratios_long[latency_results.count_long] = sjrn / res->run_ns;
      latency_results.count_long++;
  }
  else {
      // short request
      latency_results.sjrn_times_short[latency_results.count_short] = sjrn;
      latency_results.work_ratios_short[latency_results.count_short] = sjrn / res->run_ns;
      latency_results.count_short++;
  }
  pkt_recv++;

  if (unlikely(latency_results.count >= MAX_RESULT_SIZE)) {
    printf("Latency result limit reached. Automatically stopped.\n");
    sigint_handler(SIGINT);
  }
}

static void fixed_tx_loop(uint32_t lcore_id, double work_ns) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering fixed TX loop on lcore %u\n", (long long)time(NULL),
         lcore_id);

  struct rte_mbuf *mbuf;

  ExpDist *dist = malloc(sizeof(ExpDist));
  double lambda = qps * 1e-9;
  double mu = 1.0 / lambda;
  init_exp_dist(dist, mu);

  printf("lcore %u start sending fixed packets in %" PRIu32 " qps\n", lcore_id,
         qps);

  signal(SIGINT, sigint_handler);
  while (1) {
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist);
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, APP_BENCHMARK, NULL);
      enqueue_pkt(lcore_id, mbuf);
    }
    req_id++;
    if (req_id_core[lcore_id] >= max_req_id) {
      req_id_core[lcore_id] = 0;
    } else {
      req_id_core[lcore_id]++;
    }
    while (get_cur_ns() < gen_ns)
      ;
    send_pkt_burst(lcore_id);
  }
  free_exp_dist(dist);
}

static void exp_tx_loop(uint32_t lcore_id, double work_mu, int app_type) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering exp TX loop on lcore %u\n", (long long)time(NULL),
         lcore_id);

  struct rte_mbuf *mbuf;

  ExpDist *dist = malloc(sizeof(ExpDist));
  ExpDist *work_dist = malloc(sizeof(ExpDist));
  double lambda = qps * 1e-9;
  double mu = 1.0 / lambda;
  uint64_t work_ns;
  uint64_t search_word_cnt;
  init_exp_dist(dist, mu);
  init_exp_dist(work_dist, work_mu);

  printf("lcore %u start sending exp packets in %" PRIu32 " qps\n", lcore_id,
         qps);

  signal(SIGINT, sigint_handler);
  while (1) {
    // while (work_ns == 0) {
    //     work_ns = exp_dist_work_ns(work_dist);
    // }
    SimpleSet set;
    set_init(&set);
    uint64_t app_data[16]; 
    if (app_type == APP_SEARCH){
      search_word_cnt = rand() % (SEARCH_MAX_WORD + 1 - SEARCH_MIN_WORD) + SEARCH_MIN_WORD;
      int i=0;
      while (set_length(&set)!= search_word_cnt) {
        uint64_t word_id;
        word_id = rand() % (SEARCH_MAX_KEY + 1 - SEARCH_MIN_KEY) + SEARCH_MIN_KEY;
        if (set_add(&set, word_id) == SET_TRUE) { // Added to set (new key)
          app_data[i++] = htobe64(word_id);
        }
      }
      work_ns = htobe64(search_word_cnt);

    } else {
      work_ns = exp_dist_work_ns(work_dist);
    }
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist);
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      if (app_type == APP_SEARCH) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, app_type, app_data);
      } else {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, app_type, NULL);
      }
      
      enqueue_pkt(lcore_id, mbuf);
    }
    set_destroy(&set);
    req_id++;
    if (req_id_core[lcore_id] >= max_req_id) {
      req_id_core[lcore_id] = 0;
    } else {
      req_id_core[lcore_id]++;
    }
    while (get_cur_ns() < gen_ns)
      ;
    send_pkt_burst(lcore_id);
  }

  
  free_exp_dist(dist);
  free_exp_dist(work_dist);
}

static void lognormal_tx_loop(uint32_t lcore_id, double work_mu, double sigma) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering lognormal TX loop on lcore %u\n", (long long)time(NULL),
         lcore_id);

  struct rte_mbuf *mbuf;

  ExpDist *dist = malloc(sizeof(ExpDist));
  LognormalDist *work_dist = malloc(sizeof(LognormalDist));
  double lambda = qps * 1e-9;
  double mu = 1.0 / lambda;
  uint64_t work_ns;
  init_exp_dist(dist, mu);
  init_lognormal_dist(work_dist, work_mu, sigma);

  printf("lcore %u start sending lognormal packets in %" PRIu32 " qps\n",
         lcore_id, qps);

  signal(SIGINT, sigint_handler);
  while (1) {
    work_ns = lognormal_dist_work_ns(work_dist);
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist);
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, APP_KEYVAL, NULL);
      enqueue_pkt(lcore_id, mbuf);
    }
    req_id++;
    if (req_id_core[lcore_id] >= max_req_id) {
      req_id_core[lcore_id] = 0;
    } else {
      req_id_core[lcore_id]++;
    }
    while (get_cur_ns() < gen_ns)
      ;
    send_pkt_burst(lcore_id);
  }
  free_exp_dist(dist);
  free_lognormal_dist(work_dist);
}

static void bimodal_tx_loop(uint32_t lcore_id, uint64_t work_1_ns,
                            uint64_t work_2_ns, double ratio) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering bimodal TX loop on lcore %u\n", (long long)time(NULL),
         lcore_id);

  struct rte_mbuf *mbuf;

  ExpDist *dist = malloc(sizeof(ExpDist));
  BimodalDist *work_dist = malloc(sizeof(BimodalDist));
  double lambda = qps * 1e-9;     // Number of Queries per nSec
  double mu = 1.0 / lambda;   // Time in nSec for 1 Query
  uint64_t work_ns;
  init_exp_dist(dist, mu);
  init_bimodal_dist(work_dist, work_1_ns, work_2_ns, ratio);

  printf("lcore %u start sending bimodal packets in %" PRIu32 " qps\n",
         lcore_id, qps);

  signal(SIGINT, sigint_handler);
  while (1) {
    work_ns = bimodal_dist_work_ns(work_dist);    //Ret: either work_1_ns or work_2_ns (in a normal dist. manner)
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist); //Ret: get_cur_ns() + gsl_ran_exponential(dist-> r, dist->mu)   [Cur time in nSec + rand Exp. dist. around mean(time in nSec for 1 query) ] 
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, APP_KEYVAL, NULL);
      enqueue_pkt(lcore_id, mbuf);
    }
    req_id++;
    if (req_id_core[lcore_id] >= max_req_id) {
      req_id_core[lcore_id] = 0;
    } else {
      req_id_core[lcore_id]++;
    }
    // @muh wait until get_cur_ns() >= gen_ns   @: Why wait for req generation time ? Ans: For assert cond when processing worker response at process_packet(). ie: resp. arrived after query generated time
    while (get_cur_ns() < gen_ns)
      ;
    send_pkt_burst(lcore_id);
  }
  free_exp_dist(dist);
  free_bimodal_dist(work_dist);
}

static void port_bimodal_tx_loop(uint32_t lcore_id, uint64_t work_1_ns,
                                 uint64_t work_2_ns, double ratio) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering port bimodal TX loop on lcore %u\n",
         (long long)time(NULL), lcore_id);

  struct rte_mbuf *mbuf;

  ExpDist *dist = malloc(sizeof(ExpDist));
  BimodalDist *work_dist = malloc(sizeof(BimodalDist));
  double lambda = qps * 1e-9;
  double mu = 1.0 / lambda;
  uint64_t work_ns;
  init_exp_dist(dist, mu);
  init_bimodal_dist(work_dist, work_1_ns, work_2_ns, ratio);

  printf("lcore %u start sending port bimodal packets in %" PRIu32 " qps\n",
         lcore_id, qps);

  signal(SIGINT, sigint_handler);
  while (1) {
    work_ns = bimodal_dist_work_ns(work_dist);
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist);
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      if (work_ns == work_1_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, APP_KEYVAL, NULL);
      } else if (work_ns == work_2_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, APP_KEYVAL, NULL);
      }
      enqueue_pkt(lcore_id, mbuf);
    }
    req_id++;
    if (req_id_core[lcore_id] >= max_req_id) {
      req_id_core[lcore_id] = 0;
    } else {
      req_id_core[lcore_id]++;
    }
    while (get_cur_ns() < gen_ns)
      ;
    send_pkt_burst(lcore_id);
  }
  free_exp_dist(dist);
  free_bimodal_dist(work_dist);
}

static void port_trimodal_tx_loop(uint32_t lcore_id, uint64_t work_1_ns,
                                  uint64_t work_2_ns, uint64_t work_3_ns,
                                  double ratio1, double ratio2) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering port trimodal TX loop on lcore %u\n",
         (long long)time(NULL), lcore_id);

  struct rte_mbuf *mbuf;

  ExpDist *dist = malloc(sizeof(ExpDist));
  TrimodalDist *work_dist = malloc(sizeof(TrimodalDist));
  double lambda = qps * 1e-9;
  double mu = 1.0 / lambda;
  uint64_t work_ns;
  init_exp_dist(dist, mu);
  init_trimodal_dist(work_dist, work_1_ns, work_2_ns, work_3_ns, ratio1,
                     ratio2);

  printf("lcore %u start sending port trimodal packets in %" PRIu32 " qps\n",
         lcore_id, qps);

  signal(SIGINT, sigint_handler);
  while (1) {
    work_ns = trimodal_dist_work_ns(work_dist);
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist);
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      if (work_ns == work_1_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, APP_BENCHMARK, NULL);
      } else if (work_ns == work_2_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 1, APP_BENCHMARK, NULL);
      } else if (work_ns == work_3_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 2, APP_BENCHMARK, NULL);
      }
      enqueue_pkt(lcore_id, mbuf);
    }
    req_id++;
    if (req_id_core[lcore_id] >= max_req_id) {
      req_id_core[lcore_id] = 0;
    } else {
      req_id_core[lcore_id]++;
    }
    while (get_cur_ns() < gen_ns)
      ;
    send_pkt_burst(lcore_id);
  }
  free_exp_dist(dist);
  free_trimodal_dist(work_dist);
}

static void tx_loop(uint32_t lcore_id) {
  if (strcmp(dist_type, "fixed") == 0) {
    fixed_tx_loop(lcore_id, 1000 * scale_factor);  // 1us
  } else if (strcmp(dist_type, "exp") == 0) {
    exp_tx_loop(lcore_id, 1000 * scale_factor, APP_SEARCH);  // mu = 1us
  } else if (strcmp(dist_type, "lognormal") == 0) {
    lognormal_tx_loop(lcore_id, 1000 * scale_factor,
                      10000);  // mu = 1us, sigma = 10us
  } else if (strcmp(dist_type, "bimodal") == 0) {
    bimodal_tx_loop(lcore_id, 1000 * scale_factor, 10000 * scale_factor,
                    0.9);  // 99.5%: 10us, 0.5%: 1000us
  } else if (strcmp(dist_type, "port_bimodal") == 0) {
    port_bimodal_tx_loop(lcore_id, 1000 * scale_factor, 10000 * scale_factor,
                         0.5);  // 50%: 1us, 50%: 100us
  } else if (strcmp(dist_type, "port_trimodal") == 0) {
    port_trimodal_tx_loop(lcore_id, 1000 * scale_factor, 10000 * scale_factor,
                          100000 * scale_factor, 0.333,
                          0.333);  // 33.3%: 1us, 33.3%: 10us, 33.3%: 100us
  } else if (strcmp(dist_type, "db_bimodal") == 0) {
    // run_ns=0, SCAN;
    // 90%: GET, 10%: SCAN
    bimodal_tx_loop(lcore_id, 500 * scale_factor, 0 * scale_factor, 0.9);
  } else if (strcmp(dist_type, "sim") == 0){  //@muh: added for short and long duration queries

    // run_ns=0, SCAN;
    // 90%: short duration, 10%: long duration, for 50 us and 500 ms
    //bimodal_tx_loop(lcore_id, 50000* scale_factor, 500000000 * scale_factor, 0.9);
    // 50%: short duration, 50%: long duration, for 50 us and 500 ms
    bimodal_tx_loop(lcore_id, 50000* scale_factor, 500000000 * scale_factor, 0.5);


  } else if (strcmp(dist_type, "db_port_bimodal") == 0) {
    port_bimodal_tx_loop(lcore_id, 1000 * scale_factor, 0 * scale_factor, 0.5);
  } else {
    rte_exit(EXIT_FAILURE, "Error: No matching distribution type found\n");
  }
}

static void rx_loop(uint32_t lcore_id) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering RX loop (master loop) on lcore %u\n",
         (long long)time(NULL), lcore_id);

  struct rte_mbuf *mbuf;
  struct rte_mbuf *mbuf_burst[MAX_BURST_SIZE];
  uint32_t i, j, nb_rx;

  while (1) {
    //printf("%u\n", lconf->n_rx_queue);
    for (i = 0; i < lconf->n_rx_queue; i++) {
      nb_rx = rte_eth_rx_burst(lconf->port, lconf->rx_queue_list[i], mbuf_burst,
                               MAX_BURST_SIZE);
      //printf("nb_rx: %u\n", nb_rx);
      for (j = 0; j < nb_rx; j++) {
        mbuf = mbuf_burst[j];
        rte_prefetch0(rte_pktmbuf_mtod(mbuf, void *));
        process_packet(lcore_id, mbuf);
        rte_pktmbuf_free(mbuf);
      }
    }
  }
}

// main processing loop for client
static int32_t client_loop(__attribute__((unused)) void *arg) {
  uint32_t lcore_id = rte_lcore_id();

  if (is_latency_client > 0) {
    if (lcore_id == 0) {
      rx_loop(lcore_id);
    } else {
      tx_loop(lcore_id);
    }
  } else {
    tx_loop(lcore_id);
  }

  return 0;
}

// initialization
static void custom_init(void) {
  if (is_latency_client > 0) {
    // init huge arrays to store results
    latency_results.sjrn_times =
        (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
    latency_results.sjrn_times_long =
        (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
    latency_results.sjrn_times_short =
        (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
    latency_results.work_ratios =
        (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
    latency_results.work_ratios_short =
        (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
    latency_results.work_ratios_long =
        (uint64_t *)rte_malloc(NULL, sizeof(uint16_t) * MAX_RESULT_SIZE, 0);
    latency_results.queue_lengths = (uint64_t(*)[1])rte_malloc(
        NULL, sizeof(uint64_t[1]) * MAX_RESULT_SIZE, 0);
    latency_results.reply_run_ns =
        (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
  }
  // init huge arrays for responses (#documents and doc IDs) in search application
  search_results.doc_cnt = (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
  search_results.doc_id1 = (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
  search_results.doc_id2 = (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
  search_results.doc_id3 = (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
  search_results.doc_id4 = (uint64_t *)rte_malloc(NULL, sizeof(uint64_t) * MAX_RESULT_SIZE, 0);
  printf("\n=============== Finish initialization ===============\n\n");
}

/*
 * functions for parsing arguments
 */

static void parse_client_args_help(char *program_name) {
  fprintf(stderr,
          "Usage: %s -l <WORKING_CORES> -- -l <IS_LATENCY_CLIENT> -c "
          "<CLIENT_IDX> -s <SERVER_ID> -d "
          "<DIST_TYPE> -q "
          "<QPS_PER_CORE>\n",
          program_name);
}

static int parse_client_args(int argc, char **argv) {
  int opt, num;
  while ((opt = getopt(argc, argv, "l:c:s:d:q:x:r:n:")) != -1) {
    switch (opt) {
      case 'l':
        num = atoi(optarg);
        is_latency_client = num;
        break;
      case 'c':
        num = atoi(optarg);
        client_index = num - 1;
        break;
      case 's':
        num = atoi(optarg);
        server_index = num - 1;
        break;
      case 'd':
        dist_type = optarg;
        break;
      case 'q':
        num = atoi(optarg);
        qps = num;
        break;
      case 'x':
        num = atoi(optarg);
        scale_factor = num;
        break;
      case 'r':
        num = atoi(optarg);
        is_rocksdb = num;
      case 'n':
        run_name = optarg;

      break;
      default:
        parse_client_args_help(argv[0]);
        return -1;
    }
  }

  printf("\nType of client: %s\n",
         is_latency_client > 0 ? "Latency client" : "Batch client");
  printf("Client (src): %s\n", ip_client[client_index]);
  printf("Server (dst): %s\n", ip_server[server_index]);
  printf("Type of distribution: %s\n", dist_type);
  printf("QPS per core: %" PRIu32 "\n\n", qps);
  printf("Result save name: %s\n", run_name);
  return 0;
}

/*
 * main function
 */

int main(int argc, char **argv) {
  int ret;
  uint32_t lcore_id;

  // parse eal arguments
  ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "invalid EAL arguments\n");
  }
  argc -= ret;
  argv += ret;

  // parse client arguments
  ret = parse_client_args(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "invalid client arguments\n");
  }

  // init
  init();
  custom_init();

  // launch main loop in every lcore
  rte_eal_mp_remote_launch(client_loop, NULL, CALL_MASTER);
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    if (rte_eal_wait_lcore(lcore_id) < 0) {
      ret = -1;
      break;
    }
  }

  return 0;
}

