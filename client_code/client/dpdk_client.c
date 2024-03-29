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
#include <stdbool.h>
#include <unistd.h> 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

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

#include "ll.h"
#include "util.h"
#include "set.h"

#define NUM_PKTS 1
#define MAX_RESULT_SIZE 16777216UL  // 2^24

typedef enum  {
  AppBenchmark, AppRocksdb, AppTpcc, AppSearch
} AppType_t;

#define SEARCH_MIN_WORD 1
#define SEARCH_MAX_WORD 16
#define SEARCH_MIN_KEY 1
#define SEARCH_MAX_KEY 200
/*
 * custom types
 */
#define RETRANS_THRESHOLD 100000 // ns
#define RETRANS_CHECK_INTERVAL 500000 // ns

/********* Configs *********/
char ip_client[][32] = {
    "10.1.0.1", "10.1.0.2", "10.1.0.3", "10.1.0.4",  "10.1.0.5",  "10.1.0.6",
    "10.1.0.7", "10.1.0.8", "10.1.0.9", "10.1.0.10", "10.1.0.11", "10.1.0.12",
};

char ip_server[][32] = {
    "10.1.0.1", "10.1.0.2", "10.1.0.3", "10.1.0.4",  "10.1.0.5",  "10.1.0.6",
    "10.1.0.7", "10.1.0.8", "10.1.0.9", "10.1.0.10", "10.1.0.11", "10.1.0.12",
};

// Horus: Example of reserved port used for identifying Horus headers by the switches (dst_port)
uint16_t src_port = 1234;
uint16_t dst_port = 1234;

/********* Custom parameters *********/
int is_latency_client = 0;   // 0 means batch client, 1 means latency client
uint32_t client_index = 11;  // default client: netx12
uint32_t server_index = 0;   // default server: netx1
uint32_t qps = 20000;       // default qps: 100000
uint32_t additional_qps = 0;
char *dist_type = "fixed";   // default dist: fixed
uint32_t scale_factor = 4000;   // 1 means the mean is 1us; mean = 1 * scale_factor
uint32_t is_rocksdb = 0; // 1 means testing with rocksdb, 0 means testing with synthetic workload
uint32_t dynamic_step_num  = 0; // Number of steps to increase load dynamically, 0 means static load based on initial qps
uint32_t dynamic_step_list [100]; // Stores the qps at each step of dynamic load.
uint32_t dynamic_step_interval_ms = 2000; // Time interval between each increase step (ms)
char *run_name;
atomic_int core_count = 0;
/********* Packet related *********/
uint32_t req_id = 0;
int max_req_id = (1<<25)-1;
uint32_t req_id_core[8] = {0};
/********* Results *********/
uint64_t pkt_sent = 0;
uint64_t pkt_resent = 0;
uint64_t pkt_recv = 0;
LatencyResults latency_results = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0};

SearchResults search_results = {NULL, NULL, NULL, NULL, NULL};

/********* Debug the us *********/
uint64_t work_ns_sample[1048576] = {0};
uint32_t sample_idx = 0;

ll_t *list; // Shared list between sender, receiver and arbiter threads

/*
 * functions called when program ends
 */

static void dump_stats_to_file() {
  size_t count = latency_results.count;
  char output_name_base[100];
  char output_name[200];
  
  sprintf(output_name_base, "./results/output_%s_%s_%u", run_name, dist_type, ((core_count/2) * qps) + additional_qps);
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

  memset(output_name,0, strlen(output_name));
  sprintf(output_name, "%s.gen_us", output_name_base);
  FILE *gen_us_ptr = fopen(output_name, "wb");

  for (int i = 0; i < latency_results.count; i++) {
    fprintf(queue_length_ptr, "%u\n",latency_results.queue_lengths[i][0]);
    fprintf(output_ptr, "%lu\n", latency_results.sjrn_times[i]);
    fprintf(output_reply_ns_ptr, "%lu\n", latency_results.reply_run_ns[i]);
    fprintf(ratio_ptr, "%lu\n", latency_results.work_ratios[i]);
    fprintf(gen_us_ptr, "%lu\n", latency_results.gen_us[i]);
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
  fclose(gen_us_ptr);
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
  double recv_ratio = (double)pkt_recv / (double)(pkt_sent + pkt_resent);
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
  rte_free(latency_results.gen_us);
  rte_free(latency_results.work_ratios);
  rte_free(latency_results.queue_lengths);
  rte_free(latency_results.sjrn_times_short);
  rte_free(latency_results.sjrn_times_long);
  rte_free(latency_results.work_ratios_short);
  rte_free(latency_results.work_ratios_long);
  rte_free(latency_results.reply_run_ns);

  rte_exit(EXIT_SUCCESS, "\nStopped DPDK client\n");
}

static void insert_to_rtm_list (Message *req, uint64_t tstamp, unsigned int tx_count) {
  rtm_object *rtm1 = malloc(sizeof *rtm1); 
  rtm1->last_sent_tstamp=tstamp;
  rtm1->sent_msg = *req;
  rtm1->tx_count = ++tx_count;
  ll_insert_last(list, rtm1);
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
                            AppType_t app_type,
                            uint64_t app_data[]) {
  
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

  req->pkt_type = PKT_TYPE_NEW_TASK; // Horus: Packet type for a task to be scheduled
  req->cluster_id = 0 << 8; // Horus: In our testbed experiments we assume workers are on cluster_id=0
  
  /* 
   * Horus: Arbitrary source (different clients use different codes)
  */
  req->src_id = 0; 
  req->dst_id = SPINE_SCHEDULER_ID << 8;
  
  req->qlen = 0;
  
  // Horus: Client ID is used for routing the reply packets to correct client machine (configured by switch controller)
  if (app_type == AppRocksdb) {
    req->client_id = (CLIENT_ID_ROCKSDB<<8);  
  } else if (app_type == AppTpcc) {
    req->client_id = (CLIENT_ID_TPCC<<8);  
  }
  
  
  req->req_id = (req->client_id << 25) + req_id_core[lcore_id];
  //printf("TXLOOP: GEN, request id: %u, gen_ns: %lu\n", req->req_id, gen_ns);
  req->seq_num = SWAP_UINT16((uint16_t)(req->req_id));
  req->pkts_length = pkts_length;
  req->gen_ns = gen_ns; // INFO: Req generation time (for calculations)
  req->run_ns = run_ns; // INFO: Defines the type of task (used in worker to identify the work to be done)
  if (app_type == AppSearch) {
    int i=0;
    for (i=0; i < gen_ns; i++) {
      req->app_data[i] = app_data[i];
    }
  }
  // Keep a copy of the original request for retransmission, will be freed when response is received
  //insert_to_rtm_list(req, gen_ns, 0);
  //printf("TXLOOP: inserted, request id: %u\n", req->req_id);
  // printf("request client_id: %u, req_id: %u\n",req->client_id,req->req_id);
  mbuf->data_len += sizeof(Message);
  mbuf->pkt_len += sizeof(Message);

  pkt_sent++;
}

static void make_resubmit_packet(struct rte_mbuf *mbuf, rtm_object *origin, uint64_t gen_ns) {
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

  eth->d_addr.addr_bytes[0] = 0xF8;
  eth->d_addr.addr_bytes[1] = 0xF2;
  eth->d_addr.addr_bytes[2] = 0x1E;
  eth->d_addr.addr_bytes[3] = 0x3A;
  eth->d_addr.addr_bytes[4] = 0x13;
  eth->d_addr.addr_bytes[5] = 0x00;
  inet_pton(AF_INET, ip_client[client_index], &(ip->src_addr));
  inet_pton(AF_INET, ip_server[server_index], &(ip->dst_addr));
  udp->src_port = htons(src_port);
  udp->dst_port = htons(dst_port);
  *req = origin->sent_msg; // Copy the origin request to the new req header

  insert_to_rtm_list(req, gen_ns, origin->tx_count);

  mbuf->data_len += sizeof(Message);
  mbuf->pkt_len += sizeof(Message);

  pkt_resent++;
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

  
  //ll_print(*list);
  uint64_t cur_ns = get_cur_ns();

  //printf("cur_ns %lu\n",cur_ns);
  uint16_t reply_port = ntohs(udp->src_port);
  // printf("reply_port:%u\n",reply_port);
  uint64_t reply_run_ns = rte_le_to_cpu_64(res->run_ns);
  assert(cur_ns > res->gen_ns);
  uint64_t sjrn = cur_ns - res->gen_ns;  // diff in time
  //printf("Rx, client_id:%u, req_id:%u, run_ns:%lu, sjrn_us:%lu, differnece %ld\n",res->client_id, res->req_id, res->run_ns/1000, sjrn/1000, (sjrn - res->run_ns )/1000);
  latency_results.sjrn_times[latency_results.count] = sjrn;
  latency_results.gen_us[latency_results.count] = res->gen_ns / 1000;
  //printf("\nresponse time: %u\n", sjrn);
  latency_results.reply_run_ns[latency_results.count] = reply_run_ns;
  //int ret = ll_remove_search(list, search_list_req_id, res->req_id);
  // if (ret == -1) {
  //   printf("Removing request %u failed \n", res->req_id);
  // }
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
  if (reply_run_ns == 0) {
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
  // uint32_t x = 0;
  // while (x<10000000000) {
  //   x++;
  // }
  // printf("x%d", x);
  printf("lcore %u start sending fixed packets in %" PRIu32 " qps\n", lcore_id,
         qps);

  signal(SIGINT, sigint_handler);
  while (1) {
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist);
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, AppBenchmark, NULL);
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

static void exp_tx_loop(uint32_t lcore_id, double work_mu, AppType_t app_type) {
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
    if (app_type == AppSearch){
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
      if (app_type == AppSearch) {
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
      generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, AppBenchmark, NULL);
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
      generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, AppRocksdb, NULL);
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
  ExpDist *dist_list[dynamic_step_num]; // first distribution is for initial qps + #dynamic_step_num distributions for increased rates
  double mu;
  double lambda;
  uint64_t start_offset_ns = 1e7; // 10 ms delay for the first gen_ns dist
  for (int i = 0; i < dynamic_step_num; i++) {
      printf("Initializing distributions step %d with rate %d RPS\n", i, dynamic_step_list[i]);
      lambda = dynamic_step_list[i] * 1e-9;
      mu = 1.0 / lambda;
      dist_list[i] = malloc(sizeof(ExpDist));
      init_exp_dist_future(dist_list[i], mu, start_offset_ns);
      start_offset_ns += dynamic_step_interval_ms*1e6; // offset in ns
  }
  ExpDist *dist = dist_list[0];
  BimodalDist *work_dist = malloc(sizeof(BimodalDist));
  
  uint64_t work_ns;
  init_bimodal_dist(work_dist, work_1_ns, work_2_ns, ratio);
  printf("lcore %u start sending port bimodal packets in %" PRIu32 " qps\n",
         lcore_id, dynamic_step_list[0]);

  signal(SIGINT, sigint_handler);

  uint64_t elapsed_ns = 0;
  uint32_t dist_index = 0;
  uint64_t start_ns = get_cur_ns();
  
  while (1) {
    work_ns = bimodal_dist_work_ns(work_dist);
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist);
    //printf("next_arrival_us: %f\n", (float)(gen_ns)/1000);
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      if (work_ns == work_1_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, AppRocksdb, NULL);
      } else if (work_ns == work_2_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, AppRocksdb, NULL);
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
    elapsed_ns = get_cur_ns() - start_ns;
    if (elapsed_ns > dynamic_step_interval_ms*1e6) {
        if (dist_index < dynamic_step_num - 1) {
          dist_index ++;
          printf("lcore %u changing task rate after taskID = %u, next rate = %d RPS\n", lcore_id, req_id_core[lcore_id], dynamic_step_list[dist_index]);
          dist = dist_list[dist_index];
        } else {
          if (dist_index == dynamic_step_num - 1){
            printf("lcore %u: no more changes to task rate\n",lcore_id);  
          } 
          dist_index ++;
        }
        start_ns = get_cur_ns();
    }
  }
  for (int i=0; i < dynamic_step_num; i++) {
    free_exp_dist(dist_list[i]);  
  }
  free_bimodal_dist(work_dist);
}

static void fivemodal_tx_loop(uint32_t lcore_id, 
  uint64_t work_1_ns, uint64_t work_2_ns, uint64_t work_3_ns, uint64_t work_4_ns, uint64_t work_5_ns,
  double ratio_1, double ratio_2, double ratio_3, double ratio_4) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering Five Modal TX loop on lcore %u\n",
         (long long)time(NULL), lcore_id);

  struct rte_mbuf *mbuf;
  ExpDist *dist_list[dynamic_step_num]; // first distribution is for initial qps + #dynamic_step_num distributions for increased rates
  double mu;
  double lambda;
  uint64_t start_offset_ns = 1e7; // 10 ms delay for the first gen_ns dist
  for (int i = 0; i < dynamic_step_num; i++) {
      printf("Initializing distributions step %d with rate %d RPS\n", i, dynamic_step_list[i]);
      lambda = dynamic_step_list[i] * 1e-9;
      mu = 1.0 / lambda;
      dist_list[i] = malloc(sizeof(ExpDist));
      init_exp_dist_future(dist_list[i], mu, start_offset_ns);
      start_offset_ns += dynamic_step_interval_ms*1e6; // offset in ns
  }
  ExpDist *dist = dist_list[0];
  FivemodalDist *work_dist = malloc(sizeof(FivemodalDist));
  
  uint64_t work_ns;
  init_fivemodal_dist(work_dist, work_1_ns, work_2_ns, work_3_ns, work_4_ns, work_5_ns, ratio_1, ratio_2, ratio_3, ratio_4);
  printf("lcore %u start sending fivemodal packets in %" PRIu32 " qps\n",
         lcore_id, dynamic_step_list[0]);

  if (lcore_id == 1)
    signal(SIGINT, sigint_handler);

  uint64_t elapsed_ns = 0;
  uint32_t dist_index = 0;
  uint64_t start_ns = get_cur_ns();
  
  while (1) {
    work_ns = fivemodal_dist_work_ns(work_dist);
    uint64_t gen_ns = exp_dist_next_arrival_ns(dist);
    // printf("work_ns: %u\n", work_ns);
    uint32_t pkts_length = NUM_PKTS * sizeof(Message);
    for (uint8_t i = 0; i < NUM_PKTS; i++) {
      mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
      generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, AppTpcc, NULL);
      
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
    elapsed_ns = get_cur_ns() - start_ns;
    if (elapsed_ns > dynamic_step_interval_ms*1e6) {
        if (dist_index < dynamic_step_num - 1) {
          dist_index ++;
          printf("lcore %u changing task rate after taskID = %u, next rate = %d RPS\n", lcore_id, req_id_core[lcore_id], dynamic_step_list[dist_index]);
          dist = dist_list[dist_index];
        } else {
          if (dist_index == dynamic_step_num - 1){
            printf("lcore %u: no more changes to task rate\n",lcore_id);  
          } 
          dist_index ++;
        }
        start_ns = get_cur_ns();
    }
  }
  for (int i=0; i < dynamic_step_num; i++) {
    free_exp_dist(dist_list[i]);  
  }
  free_fivemodal_dist(work_dist);
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
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 0, AppBenchmark, NULL);
      } else if (work_ns == work_2_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 1, AppBenchmark, NULL);
      } else if (work_ns == work_3_ns) {
        generate_packet(lcore_id, mbuf, i, pkts_length, gen_ns, work_ns, 2, AppBenchmark, NULL);
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
    exp_tx_loop(lcore_id, 1000 * scale_factor, AppSearch);  // mu = 1us
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
  } else if (strcmp(dist_type, "db_port_bimodal") == 0) {
    port_bimodal_tx_loop(lcore_id, 1000 * scale_factor, 0 * scale_factor, 0.5);
  } else if (strcmp(dist_type, "tpc") == 0) {
    fivemodal_tx_loop(lcore_id, 5.7 * scale_factor, 6 * scale_factor, 20*scale_factor, 88*scale_factor, 100*scale_factor, 0.44, 0.04, 0.44, 0.04);
  }
  else {
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
        if (lcore_id==0){ // Only one core process the latency others help reading from the queues to avoid backlogs
          process_packet(lcore_id, mbuf);
        }
        rte_pktmbuf_free(mbuf);
      }
    }
  }
}

static void arbiter_loop(uint32_t lcore_id) {
  struct lcore_configuration *lconf = &lcore_conf[lcore_id];
  printf("%lld entering Arbiter loop (timout check) on lcore %u\n",
         (long long)time(NULL), lcore_id);
  uint64_t start_ns = get_cur_ns();
  uint64_t elapsed_ns = 0;
  struct rte_mbuf *mbuf;

  while (1) {
    if (elapsed_ns >= RETRANS_CHECK_INTERVAL) {
      //ll_print(*list);
      int need_retrans = 1;
      while (need_retrans) {
        rtm_object first; 
        rtm_object *first_ptr = ll_get_first(list, (void *)&first, sizeof(first));
        if (!first_ptr)
          break;
        if (get_cur_ns() < first.last_sent_tstamp) // Not sent yet
          break;
        if (get_cur_ns() - first.last_sent_tstamp >= RETRANS_THRESHOLD) { // Need to retransmit the packet
            mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
            uint64_t retrans_tstamp = get_cur_ns();
            
            //printf("Removing old %d request\n\n", first->sent_msg.req_id);
            int ret = ll_remove_search(list, search_list_req_id, first.sent_msg.req_id);
            if (ret != -1){
              //printf("Resending: %u, size of rxmt list %u, waited %lu\n", first.sent_msg.req_id, list->len, get_cur_ns() - first.last_sent_tstamp);
              make_resubmit_packet(mbuf, &first, retrans_tstamp);
              enqueue_pkt(lcore_id, mbuf);
              send_pkt_burst(lcore_id);
            }

            --need_retrans;
        } else { // No more timeouts for this round check at next interval
          need_retrans = 0;
        }
      }
      start_ns = get_cur_ns();
    }
    elapsed_ns = get_cur_ns() - start_ns;
  }
}

// main processing loop for client
static int32_t client_loop(__attribute__((unused)) void *arg) {
  uint32_t lcore_id = rte_lcore_id();
  core_count ++;
  if (is_latency_client > 0) {
    if (lcore_id == 0) {
      rx_loop(lcore_id);
    } else if (lcore_id == 1) {
      tx_loop(lcore_id);
    } else if (lcore_id == 2){ 
      tx_loop(lcore_id);
    } else if (lcore_id ==3){
      rx_loop(lcore_id);
    } else if (lcore_id==4) {
      rx_loop(lcore_id);
    } else if (lcore_id==5){
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
    latency_results.gen_us =
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
          "<QPS_PER_CORE for dynamic rates enter comma seperated values>"
          " -n <Experiment Name>"
          " -i <Interval (ms) between steps>\n",
          program_name);
}

static int parse_client_args(int argc, char **argv) {
  int opt, num;
  char *pt;
  while ((opt = getopt(argc, argv, "l:c:s:d:q:x:r:n:p:i:a:")) != -1) {
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
        pt = strtok(optarg, ",");
        while (pt != NULL) {
          uint32_t rate = atoi(pt);
          dynamic_step_list[dynamic_step_num] = rate;
          printf("Rate step [%d] = %d\n", dynamic_step_num, rate);
          pt = strtok(NULL, ",");
          dynamic_step_num++;
          if (dynamic_step_num >= (sizeof(dynamic_step_list)/sizeof(dynamic_step_list[0]))) { // reached max #steps
            printf("Reached maximum number of dynamic rate steps!\n");
            parse_client_args_help(argv[0]);
            return -1;
          }
        }
        qps = dynamic_step_list[0];
        break;
      case 'x':
        num = atoi(optarg);
        scale_factor = num;
        break;
      case 'r':
        num = atoi(optarg);
        is_rocksdb = num;
        break;
      case 'n':
        run_name = optarg;
        break;
      case 'i':
        num = atoi(optarg);
        dynamic_step_interval_ms = num;
        break;
      case 'a':
        num = atoi(optarg);
        additional_qps = num;
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
  // printf("QPS per core: %" PRIu32 "\n\n", qps);
  printf("Result save name: %s\n", run_name);
  if (dynamic_step_num > 0) {
    printf("Dynamic #Steps: %d\n", dynamic_step_num);
    printf("Dynamic increase interval (ms): %d\n", dynamic_step_interval_ms);
  } else {
    printf("Using static QPS load\n");
  }
  printf("\n");
  return 0;
}

/*
 * main function
 */

int main(int argc, char **argv) {
  int ret;
  uint32_t lcore_id;

  list = ll_new(num_teardown);
  list->val_printer = rtm_node_printer;
  rtm_object *rtm1 = malloc(sizeof *rtm1); 
  rtm1->last_sent_tstamp=50;
  rtm1->sent_msg.req_id = 200;
  rtm_object *rtm2 = malloc(sizeof *rtm1); 
  rtm2->last_sent_tstamp=50;
  rtm2->sent_msg.req_id = 500;
  // ll_insert_first(list, rtm1);
  // ll_insert_last(list, rtm2);
  // ll_print(*list);
  // ll_remove_search(list, search_list_req_id, 500);
  // ll_print(*list);
  
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

  //launch main loop in every lcore
  rte_eal_mp_remote_launch(client_loop, NULL, CALL_MASTER);
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    if (rte_eal_wait_lcore(lcore_id) < 0) {
      ret = -1;
      break;
    }
  }

  return 0;
}
