//
// CUDA DETI coin search
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <cuda_runtime.h>
#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1.h"

// Vault wrappers are implemented in a separate C file to keep C99 initializers out of CUDA C++
extern "C" void save_coin_wrapper(u32_t *coin);
extern "C" void save_coin_flush(void);

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig)
{
  (void)sig;
  stop_requested = 1;
}

// CUDA kernel for DETI coin search
__global__ void search_coins_kernel(
    unsigned long long base_nonce,
    unsigned long long num_coins,
    u32_t *found_coins,
    int *found_count,
    int max_found)
{
  unsigned long long idx = blockIdx.x * blockDim.x + threadIdx.x;
  if(idx >= num_coins) return;
  
  unsigned long long nonce = base_nonce + idx;
  
  // Prepare coin data as 14 x 32-bit words (aligned) and a byte view
  u32_t coin_words[14];
  u08_t *coin_bytes = (u08_t *)coin_words;
  
  // Fixed header "DETI coin 2 "
  const char *hdr = "DETI coin 2 ";
  for(int k = 0; k < 12; k++)
    coin_bytes[k ^ 3] = (u08_t)hdr[k];
  
  // Fill variable bytes 12..53:
  // - first 10 bytes from the sequential nonce (base-95 printable ASCII)
  // - remaining 32 bytes using a simple per-thread LCG for printable ASCII
  unsigned long long temp_nonce = nonce;
  // first 10 bytes from nonce
  for(int j = 0; j < 10; j++)
  {
    u08_t byte_val = (u08_t)(32 + (temp_nonce % 95));
    coin_bytes[(12 + j) ^ 3] = byte_val;
    temp_nonce /= 95;
  }
  // simple device-side LCG seeded from nonce for printable bytes [32..126]
  unsigned int x = (unsigned int)(nonce ^ (nonce >> 32));
  for(int j = 10; j < 42; j++)
  {
    x = 3134521u * x + 1u;
    coin_bytes[(12 + j) ^ 3] = (u08_t)((x % 95u) + 32u);
  }
  
  // Newline and padding
  coin_bytes[54 ^ 3] = (u08_t)'\n';
  coin_bytes[55 ^ 3] = (u08_t)0x80;
  
  // Compute SHA1 hash for this coin using the CUSTOM_SHA1_CODE macro (device-side)
  u32_t hash[5];
# define T            u32_t
# define C(c)         (c)
# define ROTATE(x,n)  (((x) << (n)) | ((x) >> (32 - (n))))
# define DATA(i)      coin_words[(i)]
# define HASH(i)      hash[(i)]
  CUSTOM_SHA1_CODE();
# undef T
# undef C
# undef ROTATE
# undef DATA
# undef HASH
  
  // Check if valid DETI coin
  if(hash[0] == 0xAAD20250u)
  {
    // Atomically add to found list
    int pos = atomicAdd(found_count, 1);
    if(pos < max_found)
    {
      // Copy coin data to results
      for(int i = 0; i < 14; i++)
        found_coins[pos * 14 + i] = coin_words[i];
    }
  }
}

int main(int argc, char **argv)
{
  unsigned long long total_coins = 0ULL;
  if(argc > 1)
    total_coins = strtoull(argv[1], NULL, 10);
  
  (void)signal(SIGINT, handle_sigint);
  
  const int threads_per_block = 256;
  const unsigned long long coins_per_batch = 1024 * 1024;
  const int max_found_per_batch = 1024;
  
  unsigned long long base_nonce = 0ULL;
  unsigned long long batches_done = 0ULL;
  unsigned long long report_interval = 500ULL;
  double total_elapsed_time = 0.0;
  
  // Seed random number generator
  srand((unsigned int)time(NULL));
  
  // Initialize nonce with random value
  base_nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();
  
  time_measurement();
  
  // Allocate device memory
  u32_t *d_found_coins;
  int *d_found_count;
  cudaMalloc(&d_found_coins, max_found_per_batch * 14 * sizeof(u32_t));
  cudaMalloc(&d_found_count, sizeof(int));
  
  // Host memory for results
  u32_t *h_found_coins = (u32_t*)malloc(max_found_per_batch * 14 * sizeof(u32_t));
  int h_found_count = 0;
  
  printf("Starting CUDA DETI coin search...\n");
  printf("Threads per block: %d\n", threads_per_block);
  printf("Coins per batch: %llu\n", coins_per_batch);
  
  // Search loop
  while((total_coins == 0ULL || base_nonce < total_coins) && !stop_requested)
  {
    // Reset found counter
    h_found_count = 0;
    cudaMemcpy(d_found_count, &h_found_count, sizeof(int), cudaMemcpyHostToDevice);
    
    // Launch kernel
    int num_blocks = (coins_per_batch + threads_per_block - 1) / threads_per_block;
    search_coins_kernel<<<num_blocks, threads_per_block>>>(
        base_nonce, coins_per_batch, d_found_coins, d_found_count, max_found_per_batch);
    
    // Wait for kernel to complete
    cudaDeviceSynchronize();
    
    // Check for errors
    cudaError_t err = cudaGetLastError();
    if(err != cudaSuccess)
    {
      fprintf(stderr, "CUDA error: %s\n", cudaGetErrorString(err));
      break;
    }
    
    // Copy results back
    cudaMemcpy(&h_found_count, d_found_count, sizeof(int), cudaMemcpyDeviceToHost);
    
    if(h_found_count > 0)
    {
      cudaMemcpy(h_found_coins, d_found_coins, 
                 h_found_count * 14 * sizeof(u32_t), cudaMemcpyDeviceToHost);
      
      // Process found coins
      for(int i = 0; i < h_found_count && i < max_found_per_batch; i++)
      {
        printf("Found DETI coin on GPU!\n");
        
  // Save coin to vault (host-side wrapper)
  save_coin_wrapper(&h_found_coins[i * 14]);
      }
    }
    
    // Advance
    base_nonce += coins_per_batch;
    batches_done++;
    
    // Progress report
    if((batches_done % report_interval) == 0ULL)
    {
      time_measurement();
      double delta_time = wall_time_delta();
      total_elapsed_time += delta_time;
      
      double batches_per_second = (double)report_interval / delta_time;
      double iter_per_second = (double)(report_interval * coins_per_batch) / delta_time;
      
      if(batches_done == 0)
      {
        batches_per_second = 0.0;
        iter_per_second = 0.0;
      }
      
      fprintf(stderr, "batches=%llu nonce=%llu time=%.1f batch_per_sec=%.0f coins_per_sec=%.0f\n",
              batches_done, base_nonce, total_elapsed_time, batches_per_second, iter_per_second);
    }
  }
  
  // Cleanup
  cudaFree(d_found_coins);
  cudaFree(d_found_count);
  free(h_found_coins);
  
  // Flush vault buffer to disk
  save_coin_flush();

  printf("CUDA search completed.\n");
  return 0;
}
