//
// CUDA Histogram Analysis
// Measures: kernel execution time distribution and coins found per run
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cuda_runtime.h>
#include <math.h>
#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1.h"

extern "C" void save_coin_wrapper(u32_t *coin);
extern "C" void save_coin_flush(void);

#define NUM_HISTOGRAM_RUNS 1000
#define COINS_PER_BATCH 1048576  // 1M coins per kernel run
#define MAX_COINS_PER_RUN 256
#define THREADS_PER_BLOCK 256

// CUDA kernel for DETI coin search with embedded name
__global__ void search_coins_histogram_kernel(
    unsigned long long base_nonce,
    unsigned long long num_coins,
    u32_t *found_coins,
    int *found_count,
    int max_found)
{
    unsigned long long idx = blockIdx.x * blockDim.x + threadIdx.x;
    if(idx >= num_coins) return;
    
    unsigned long long nonce = base_nonce + idx;
    
    u32_t coin_words[14];
    u08_t *coin_bytes = (u08_t *)coin_words;
    
    // Fixed header with embedded name: "DETI coin 2 luisbfsousa"
    const char *hdr = "DETI coin 2 luisbfsousa";
    int hdr_len = 23;
    for(int k = 0; k < hdr_len && k < 12; k++)
        coin_bytes[k ^ 3] = (u08_t)hdr[k];
    
    // Fill remaining variable bytes (12..53) using nonce
    unsigned long long temp_nonce = nonce;
    for(int j = hdr_len; j < 42; j++) {
        u08_t byte_val = (u08_t)(32 + (temp_nonce % 95));
        coin_bytes[(12 + j) ^ 3] = byte_val;
        temp_nonce /= 95;
    }
    
    coin_bytes[54 ^ 3] = (u08_t)'\n';
    coin_bytes[55 ^ 3] = (u08_t)0x80;
    
    // Compute SHA1
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
    if(hash[0] == 0xAAD20250u) {
        int pos = atomicAdd(found_count, 1);
        if(pos < max_found) {
            for(int i = 0; i < 14; i++)
                found_coins[pos * 14 + i] = coin_words[i];
        }
    }
}

typedef struct {
    float kernel_times[NUM_HISTOGRAM_RUNS];
    int coins_found[NUM_HISTOGRAM_RUNS];
    int num_runs;
    float min_time, max_time, mean_time, stddev_time;
    int min_coins, max_coins, mean_coins;
} histogram_data_t;

void compute_statistics(histogram_data_t *hist)
{
    // Time statistics
    hist->min_time = hist->kernel_times[0];
    hist->max_time = hist->kernel_times[0];
    double sum_time = 0.0;
    
    for(int i = 0; i < hist->num_runs; i++) {
        if(hist->kernel_times[i] < hist->min_time) 
            hist->min_time = hist->kernel_times[i];
        if(hist->kernel_times[i] > hist->max_time) 
            hist->max_time = hist->kernel_times[i];
        sum_time += hist->kernel_times[i];
    }
    
    hist->mean_time = sum_time / hist->num_runs;
    
    double variance = 0.0;
    for(int i = 0; i < hist->num_runs; i++) {
        double delta = hist->kernel_times[i] - hist->mean_time;
        variance += delta * delta;
    }
    hist->stddev_time = (float)sqrt(variance / hist->num_runs);
    
    // Coins statistics
    hist->min_coins = hist->coins_found[0];
    hist->max_coins = hist->coins_found[0];
    int sum_coins = 0;
    
    for(int i = 0; i < hist->num_runs; i++) {
        if(hist->coins_found[i] < hist->min_coins) 
            hist->min_coins = hist->coins_found[i];
        if(hist->coins_found[i] > hist->max_coins) 
            hist->max_coins = hist->coins_found[i];
        sum_coins += hist->coins_found[i];
    }
    
    hist->mean_coins = sum_coins / hist->num_runs;
}

void write_histogram_to_file(const char *filename, histogram_data_t *hist)
{
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "run,kernel_time_ms,coins_found\n");
    for(int i = 0; i < hist->num_runs; i++) {
        fprintf(fp, "%d,%.6f,%d\n", i, hist->kernel_times[i], hist->coins_found[i]);
    }
    fclose(fp);
}

int main(int argc, char **argv)
{
    printf("\n=== CUDA Histogram Analysis ===\n");
    printf("Running %d kernel invocations...\n\n", NUM_HISTOGRAM_RUNS);
    
    int num_blocks = (COINS_PER_BATCH + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK;
    
    histogram_data_t hist = {0};
    
    // Allocate device memory
    u32_t *d_found_coins;
    int *d_found_count;
    cudaMalloc(&d_found_coins, MAX_COINS_PER_RUN * 14 * sizeof(u32_t));
    cudaMalloc(&d_found_count, sizeof(int));
    
    // Host memory
    u32_t *h_found_coins = (u32_t*)malloc(MAX_COINS_PER_RUN * 14 * sizeof(u32_t));
    int h_found_count = 0;
    
    // Create CUDA events for timing
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    
    unsigned long long base_nonce = 0ULL;
    srand((unsigned int)time(NULL));
    base_nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();
    
    for(int run = 0; run < NUM_HISTOGRAM_RUNS; run++) {
        // Reset found counter
        h_found_count = 0;
        cudaMemcpy(d_found_count, &h_found_count, sizeof(int), cudaMemcpyHostToDevice);
        
        // Measure kernel time
        cudaEventRecord(start);
        search_coins_histogram_kernel<<<num_blocks, THREADS_PER_BLOCK>>>(
            base_nonce, COINS_PER_BATCH, d_found_coins, d_found_count, MAX_COINS_PER_RUN);
        cudaEventRecord(stop);
        cudaEventSynchronize(stop);
        
        float ms = 0;
        cudaEventElapsedTime(&ms, start, stop);
        hist.kernel_times[run] = ms;
        
        // Copy results back
        cudaMemcpy(&h_found_count, d_found_count, sizeof(int), cudaMemcpyDeviceToHost);
        
        if(h_found_count > 0) {
            cudaMemcpy(h_found_coins, d_found_coins, 
                     h_found_count * 14 * sizeof(u32_t), cudaMemcpyDeviceToHost);
            
            for(int i = 0; i < h_found_count; i++) {
                printf("Found coin #%d (run %d): ", hist.coins_found[run] + i + 1, run);
                // Print first 30 bytes
                for(int j = 0; j < 30 && j < 55; j++) {
                    unsigned char ch = ((unsigned char *)&h_found_coins[i * 14])[j ^ 3];
                    if(ch >= 32 && ch <= 126) putchar(ch);
                    else putchar('?');
                }
                printf("\n");
                save_coin_wrapper(&h_found_coins[i * 14]);
            }
        }
        
        hist.coins_found[run] = h_found_count;
        base_nonce += COINS_PER_BATCH;
        hist.num_runs++;
        
        if((run + 1) % 100 == 0)
            printf("Completed %d/%d runs...\n", run + 1, NUM_HISTOGRAM_RUNS);
    }
    
    save_coin_flush();
    
    // Compute statistics
    compute_statistics(&hist);
    
    // Write results
    write_histogram_to_file("cuda_histogram.csv", &hist);
    
    printf("\n=== Statistics ===\n");
    printf("Kernel Execution Time:\n");
    printf("  Min:    %.6f ms\n", hist.min_time);
    printf("  Max:    %.6f ms\n", hist.max_time);
    printf("  Mean:   %.6f ms\n", hist.mean_time);
    printf("  StdDev: %.6f ms\n", hist.stddev_time);
    printf("\nCoins Found Per Run:\n");
    printf("  Min:    %d\n", hist.min_coins);
    printf("  Max:    %d\n", hist.max_coins);
    printf("  Mean:   %d\n", hist.mean_coins);
    printf("  Total:  %d\n", hist.num_runs > 0 ? 
           (int)(hist.num_runs * hist.mean_coins) : 0);
    printf("\nData saved to cuda_histogram.csv\n");
    
    // Cleanup
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    cudaFree(d_found_coins);
    cudaFree(d_found_count);
    free(h_found_coins);
    
    return 0;
}