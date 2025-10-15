//
// DETI Coin Search Client (CUDA version)
//
// Connects to server, requests work, performs GPU search, reports results
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1_cpu.h"
#include "aad_cuda_utilities.h"
#include "aad_distributed.h"

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_sigint(int sig)
{
  (void)sig;
  g_stop_requested = 1;
}

//
// Send/Receive functions (same as client)
//
static int send_message(int sock, message_type_t type, const void *payload, uint32_t payload_len)
{
  message_header_t hdr;
  init_message_header(&hdr, type, payload_len);
  
  if(payload && payload_len > 0)
    hdr.checksum = simple_checksum(payload, payload_len);
  
  if(send(sock, &hdr, sizeof(hdr), 0) != sizeof(hdr))
    return -1;
  
  if(payload && payload_len > 0)
  {
    if(send(sock, payload, payload_len, 0) != (ssize_t)payload_len)
      return -1;
  }
  
  return 0;
}

static int recv_message(int sock, message_header_t *hdr, void *payload, uint32_t max_payload)
{
  ssize_t n = recv(sock, hdr, sizeof(*hdr), MSG_WAITALL);
  if(n != sizeof(*hdr))
    return -1;
  
  if(hdr->magic != PROTOCOL_MAGIC || hdr->version != DETI_PROTOCOL_VERSION)
    return -1;
  
  if(hdr->length > 0)
  {
    if(hdr->length > max_payload)
      return -1;
    
    n = recv(sock, payload, hdr->length, MSG_WAITALL);
    if(n != (ssize_t)hdr->length)
      return -1;
    
    uint32_t check = simple_checksum(payload, hdr->length);
    if(check != hdr->checksum)
      return -1;
  }
  
  return 0;
}

//
// Process work using CUDA
//
static void process_work_cuda(int sock, const work_assignment_t *work, cuda_data_t *cd)
{
  printf("Processing work %u: nonces %lu to %lu (CUDA)\n",
         work->work_id, (unsigned long)work->start_nonce, (unsigned long)work->end_nonce);
  
  u32_t *interleaved32_data = (u32_t *)cd->host_data[0];
  u32_t *interleaved32_hash = (u32_t *)cd->host_data[1];
  const char *hdr = "DETI coin 2 ";
  
  uint64_t total_nonces = work->end_nonce - work->start_nonce;
  uint64_t nonces_per_batch = cd->data_size[0] / (14 * sizeof(u32_t));
  uint32_t coins_found = 0;
  struct timespec ts_start;
  clock_gettime(CLOCK_MONOTONIC, &ts_start);
  double start_time = ts_start.tv_sec + ts_start.tv_nsec * 1e-9;
  
  for(uint64_t base = work->start_nonce; base < work->end_nonce && !g_stop_requested; base += nonces_per_batch)
  {
    uint64_t batch_end = base + nonces_per_batch;
    if(batch_end > work->end_nonce)
      batch_end = work->end_nonce;
    
    uint64_t batch_size = batch_end - base;
    u32_t n_warps = batch_size / 32u;
    
    // Prepare messages
    for(u32_t warp = 0u; warp < n_warps; warp++)
    {
      for(u32_t lane = 0u; lane < 32u; lane++)
      {
        uint64_t nonce = base + warp * 32u + lane;
        union { u08_t c[14 * 4]; u32_t i[14]; } coin;
        
        for(int k = 0; k < 12; k++)
          coin.c[k ^ 3] = (u08_t)hdr[k];
        coin.c[54 ^ 3] = (u08_t)'\n';
        coin.c[55 ^ 3] = (u08_t)0x80;
        
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%lu", (unsigned long)nonce);
        if(len <= 0) { len = 1; buf[0] = '0'; }
        for(int j = 0; j < 42; j++)
        {
          char ch = buf[j % len];
          if(ch == '\n') ch = '?';
          coin.c[(12 + j) ^ 3] = (u08_t)ch;
        }
        
        for(u32_t idx = 0u; idx < 14u; idx++)
          interleaved32_data[32u * 14u * warp + 32u * idx + lane] = coin.i[idx];
      }
    }
    
    // Execute on GPU
    host_to_device_copy(cd, 0);
    lauch_kernel(cd);
    device_to_host_copy(cd, 1);
    
    // Check for coins
    for(u32_t n = 0u; n < batch_size; n++)
    {
      u32_t warp_number = n / 32u;
      u32_t lane = n % 32u;
      
      // Read hash from interleaved array
      u32_t hash[5];
      for(u32_t idx = 0u; idx < 5u; idx++)
        hash[idx] = interleaved32_hash[32u * 5u * warp_number + 32u * idx + lane];
      
      // Check if it's a valid DETI coin
      if(hash[0] == 0xAAD20250u)
      {
        u32_t data[14];
        for(u32_t idx = 0u; idx < 14u; idx++)
          data[idx] = interleaved32_data[32u * 14u * warp_number + 32u * idx + lane];
        
        unsigned int zeros = 0u;
        for(zeros = 0u; zeros < 128u; zeros++)
          if(((hash[1u + zeros / 32u] >> (31u - zeros % 32u)) & 1u) != 0u)
            break;
        if(zeros > 99u) zeros = 99u;
        
        uint64_t found_nonce = base + n;
        
        coin_report_t report;
        report.nonce = found_nonce;
        report.zeros = zeros;
        report.work_id = work->work_id;
        memcpy(report.coin_data, data, sizeof(report.coin_data));
        memcpy(report.hash, hash, sizeof(report.hash));
        
        printf("FOUND COIN: nonce=%lu zeros=%u\n", (unsigned long)found_nonce, zeros);
        send_message(sock, MSG_REPORT_COIN, &report, sizeof(report));
        coins_found++;
      }
    }
  }
  
  struct timespec ts_end;
  clock_gettime(CLOCK_MONOTONIC, &ts_end);
  double end_time = ts_end.tv_sec + ts_end.tv_nsec * 1e-9;
  double elapsed = end_time - start_time;
  
  work_completion_t completion;
  completion.work_id = work->work_id;
  completion.nonces_tested = total_nonces;
  completion.coins_found = coins_found;
  completion.elapsed_time = elapsed;
  
  send_message(sock, MSG_WORK_COMPLETE, &completion, sizeof(completion));
  
  printf("Work %u complete: %.0f nonces/sec, %u coins\n",
         work->work_id, total_nonces / elapsed, coins_found);
}

//
// Main
//
int main(int argc, char **argv)
{
  const char *server_host = "localhost";
  int server_port = DETI_DEFAULT_PORT;
  u32_t n_tests = 128u * 65536u;
  
  if(argc > 1)
    server_host = argv[1];
  if(argc > 2)
    server_port = atoi(argv[2]);
  if(argc > 3)
    n_tests = (u32_t)strtoul(argv[3], NULL, 10);
  
  if(n_tests % RECOMENDED_CUDA_BLOCK_SIZE != 0u)
  {
    fprintf(stderr, "n_tests must be multiple of %d\n", RECOMENDED_CUDA_BLOCK_SIZE);
    return 1;
  }
  
  printf("DETI Coin Search Client (CUDA)\n");
  printf("===============================\n");
  printf("Server: %s:%d\n", server_host, server_port);
  printf("Batch size: %u\n", n_tests);
  printf("\n");
  
  signal(SIGINT, handle_sigint);
  signal(SIGPIPE, SIG_IGN);
  
  // Initialize CUDA
  cuda_data_t cd;
  cd.device_number = 0;
  cd.cubin_file_name = "sha1_cuda_kernel.cubin";
  cd.kernel_name = "sha1_cuda_kernel";
  cd.data_size[0] = n_tests * 14 * sizeof(u32_t);
  cd.data_size[1] = n_tests * 5 * sizeof(u32_t);
  
  initialize_cuda(&cd);
  
  cd.grid_dim_x = n_tests / RECOMENDED_CUDA_BLOCK_SIZE;
  cd.block_dim_x = RECOMENDED_CUDA_BLOCK_SIZE;
  cd.n_kernel_arguments = 2;
  cd.arg[0] = &cd.device_data[0];
  cd.arg[1] = &cd.device_data[1];
  
  // Connect to server
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock < 0)
  {
    perror("socket");
    return 1;
  }
  
  struct hostent *he = gethostbyname(server_host);
  if(!he)
  {
    fprintf(stderr, "Unknown host: %s\n", server_host);
    return 1;
  }
  
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server_port);
  memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
  
  printf("Connecting to %s:%d...\n", server_host, server_port);
  if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    perror("connect");
    close(sock);
    return 1;
  }
  
  printf("Connected!\n\n");
  
  // Send CLIENT_HELLO
  client_info_t client_info;
  memset(&client_info, 0, sizeof(client_info));
  gethostname(client_info.hostname, sizeof(client_info.hostname));
  strncpy(client_info.client_type, "CUDA", sizeof(client_info.client_type) - 1);
  client_info.capabilities = n_tests;
  client_info.version = DETI_PROTOCOL_VERSION;
  
  if(send_message(sock, MSG_CLIENT_HELLO, &client_info, sizeof(client_info)) < 0)
  {
    fprintf(stderr, "Failed to send CLIENT_HELLO\n");
    close(sock);
    return 1;
  }
  
  // Wait for SERVER_HELLO
  message_header_t hdr;
  char buffer[4096];
  if(recv_message(sock, &hdr, buffer, sizeof(buffer)) < 0 || hdr.type != MSG_SERVER_HELLO)
  {
    fprintf(stderr, "Failed to receive SERVER_HELLO\n");
    close(sock);
    return 1;
  }
  
  printf("Handshake complete, requesting work...\n\n");
  
  // Main work loop
  while(!g_stop_requested)
  {
    if(send_message(sock, MSG_REQUEST_WORK, NULL, 0) < 0)
    {
      fprintf(stderr, "Failed to request work\n");
      break;
    }
    
    if(recv_message(sock, &hdr, buffer, sizeof(buffer)) < 0)
    {
      fprintf(stderr, "Connection lost\n");
      break;
    }
    
    if(hdr.type == MSG_SHUTDOWN || hdr.type == MSG_NO_WORK)
    {
      printf("Server shutting down or no work available\n");
      break;
    }
    
    if(hdr.type != MSG_WORK_ASSIGNMENT)
    {
      fprintf(stderr, "Unexpected message type: %u\n", hdr.type);
      break;
    }
    
    work_assignment_t *work = (work_assignment_t *)buffer;
    process_work_cuda(sock, work, &cd);
  }
  
  printf("\nDisconnecting...\n");
  close(sock);
  terminate_cuda(&cd);
  
  return 0;
}
