#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <cuda_runtime.h>
#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1.h"

extern "C" void save_coin_wrapper(u32_t *coin);
extern "C" void save_coin_flush(void);

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig)
{
  (void)sig;
  stop_requested = 1;
}

// Kernel for coin search
__global__ void search_coins_kernel(
    unsigned long long base_nonce,
    unsigned long long num_coins,
    u32_t *found_coins,
    int *found_count,
    int max_found,
    u32_t *static_padding)
{
  unsigned long long idx = blockIdx.x * blockDim.x + threadIdx.x;
  if(idx >= num_coins) return;
  
  unsigned long long nonce = base_nonce + idx;
  
  // Prepare coin data
  u32_t coin_words[16];
  u08_t *coin_bytes = (u08_t *)coin_words;
  
  // Initialize all to zero
  for(int i = 0; i < 16; i++)
    coin_words[i] = 0;
  
  // Fixed header "DETI coin 2 "
  const char *hdr = "DETI coin 2 ";
  for(int k = 0; k < 12; k++)
    coin_bytes[k ^ 3] = (u08_t)hdr[k];
  
  // Fill bytes 12 to 53 from static padding template
  for(int i = 3; i < 14; i++)
    coin_words[i] = static_padding[i];
  
  // Fill nonce bytes 40 to 49 (10 bytes) so high bytes remain random
  unsigned long long temp_nonce = nonce;
  for(int j = 0; j < 10; j++)
  {
    u08_t byte_val = (u08_t)(32 + (temp_nonce % 95));
    coin_bytes[(40 + j) ^ 3] = byte_val;
    temp_nonce /= 95;
  }
  
  // Newline and padding (bytes 54-55)
  coin_bytes[54 ^ 3] = (u08_t)'\n';
  coin_bytes[55 ^ 3] = (u08_t)0x80;
  
  coin_words[15] = 440;
  
  // Compute SHA1 hash for this coin
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
    // Add to found list
    int pos = atomicAdd(found_count, 1);
    if(pos < max_found)
    {
      // Copy coin data to results
      for(int i = 0; i < 16; i++)
        found_coins[pos * 16 + i] = coin_words[i];
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
  unsigned long long total_iterations = 0ULL;
  unsigned long long last_report_iter = 0ULL;
  double total_elapsed_time = 0.0;
  unsigned long long coins_found = 0ULL;

  srand((unsigned int)time(NULL));
  
  // Initialize nonce with random value
  base_nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();
  
  time_measurement();
  
  // Generate random padding for bytes 12-39
  u32_t h_static_padding[16];
  for(int i = 0; i < 16; i++)
    h_static_padding[i] = 0;
  
  u08_t *padding_bytes = (u08_t *)h_static_padding;
  for(int k = 12; k < 54; k++)
  {
    padding_bytes[k ^ 3] = (u08_t)(32 + (rand() % 95));
  }
  
  h_static_padding[15] = 440;
  
  // Allocate device memory
  u32_t *d_found_coins;
  int *d_found_count;
  u32_t *d_static_padding;
  cudaMalloc(&d_found_coins, max_found_per_batch * 16 * sizeof(u32_t));
  cudaMalloc(&d_found_count, sizeof(int));
  cudaMalloc(&d_static_padding, 16 * sizeof(u32_t));
  
  // Copy static padding to device
  cudaMemcpy(d_static_padding, h_static_padding, 16 * sizeof(u32_t), cudaMemcpyHostToDevice);
  
  // Host memory for results
  u32_t *h_found_coins = (u32_t*)malloc(max_found_per_batch * 16 * sizeof(u32_t));
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
        base_nonce, coins_per_batch, d_found_coins, d_found_count, max_found_per_batch, d_static_padding);
    
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
      int coins_to_process = (h_found_count > max_found_per_batch) ? max_found_per_batch : h_found_count;
      cudaMemcpy(h_found_coins, d_found_coins, 
                 coins_to_process * 16 * sizeof(u32_t), cudaMemcpyDeviceToHost);
      
      // Process found coins
      for(int i = 0; i < coins_to_process; i++)
      {
        printf("Found DETI coin! \n");
        // Save coin to vault
        save_coin_wrapper(&h_found_coins[i * 16]);
      }

      coins_found += (unsigned long long)coins_to_process;
    }
    
    base_nonce += coins_per_batch;
    batches_done++;
    total_iterations += coins_per_batch;
    
    // Progress report every ~16 million iterations
    if((total_iterations & 0xFFFFFF00000000ULL) != ((total_iterations - coins_per_batch) & 0xFFFFFF00000000ULL))
    {
      time_measurement();
      double delta = wall_time_delta();
      total_elapsed_time += delta;
      double fps = (double)(total_iterations - last_report_iter) / delta;
      last_report_iter = total_iterations;
      
      fprintf(stderr, "Speed: %.2f MH/s (%.2f M/min) | Nonce: %llx\n", 
              fps / 1000000.0, 
              (fps * 60.0) / 1000000.0, 
              base_nonce);
    }
  }
  
  // Cleanup
  cudaFree(d_found_coins);
  cudaFree(d_found_count);
  cudaFree(d_static_padding);
  free(h_found_coins);
  
  // Flush vault buffer to disk
  save_coin_flush();

  time_measurement();
  double final_time = wall_time_delta();
  total_elapsed_time += final_time;

  unsigned long long final_total_hashes = total_iterations;
  double avg_hashes_per_sec = (total_elapsed_time > 0.0) ? (double)final_total_hashes / total_elapsed_time : 0.0;
  double avg_hashes_per_min = avg_hashes_per_sec * 60.0;
  double hashes_per_coin = (coins_found > 0ULL) ? (double)final_total_hashes / (double)coins_found : 0.0;

  printf("\n");
  printf("========================================\n");
  printf("Final Summary (CUDA):\n");
  printf("========================================\n");
  printf("Total coins found:    %llu\n", coins_found);
  printf("Total hashes:         %llu\n", final_total_hashes);
  printf("Total time:           %.2f seconds\n", total_elapsed_time);
  printf("Average speed:        %.2f MH/s\n", avg_hashes_per_sec / 1000000.0);
  printf("Average speed:        %.2f M/min\n", avg_hashes_per_min / 1000000.0);
  if(coins_found > 0ULL)
    printf("Hashes per coin:      %.2f\n", hashes_per_coin);
  else
    printf("Hashes per coin:      N/A (no coins found)\n");
  printf("========================================\n");

  printf("CUDA search completed.\n");
  return 0;
}
