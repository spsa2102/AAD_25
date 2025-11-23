#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <omp.h>
#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1_cpu.h"
#include "aad_vault.h"

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig)
{
  (void)sig;
  stop_requested = 1;
}

#if defined(__AVX512F__)
# define N_LANES 16
# define USE_AVX512 1
#elif defined(__AVX2__)
# define N_LANES 8
# define USE_AVX2 1
#elif defined(__AVX__)
# define N_LANES 4
# define USE_AVX 1
#else
# define N_LANES 1
# define USE_SCALAR 1
#endif

static inline void to_base95_10(unsigned long long value, u08_t digits[10])
{
  for(int i = 0; i < 10; ++i)
  {
    digits[i] = (u08_t)(value % 95ULL);
    value /= 95ULL;
  }
}

static inline void base95_add(u08_t digits[10], unsigned long long delta)
{
  unsigned long long carry = delta;
  for(int i = 0; i < 10 && carry != 0u; ++i)
  {
    unsigned long long sum = (unsigned long long)digits[i] + carry;
    digits[i] = (u08_t)(sum % 95ULL);
    carry = sum / 95ULL;
  }
}

int main(int argc, char **argv)
{
  unsigned long long n_batches = 0ULL;
  if(argc > 1)
    n_batches = strtoull(argv[1], NULL, 10);

  (void)signal(SIGINT, handle_sigint);

  const char *hdr = "DETI coin 2 ";

  time_measurement();
  double total_elapsed_time = 0.0;
  unsigned long long total_iterations = 0ULL;
  unsigned long long last_report_iter = 0ULL;
  unsigned long long global_batches = 0ULL;
  unsigned long long coins_found = 0ULL;

  // Parallel region
  #pragma omp parallel
  {
    const int tid = omp_get_thread_num();
    const int nth = omp_get_num_threads();

    // Per-thread buffers
    union { u08_t c[14 * 4]; u32_t i[14]; } data[N_LANES];
    u32_t interleaved_data[14][N_LANES] __attribute__((aligned(64)));
    u32_t interleaved_hash[5][N_LANES] __attribute__((aligned(64)));

    // Per-thread LUT to map random bytes to printable ASCII [32..126]
    u08_t ascii95_lut[256];
    for(int i = 0; i < 256; ++i)
      ascii95_lut[i] = (u08_t)((i % 95) + 32);

    // Pre-generate random printable tails (32 bytes per lane)
    u08_t static_tail[N_LANES][32];
    for(int lane = 0; lane < N_LANES; ++lane)
      for(int j = 0; j < 32; ++j)
        static_tail[lane][j] = ascii95_lut[random_byte()];

    // Initialize a different base nonce per thread
  unsigned long long thread_seed = (unsigned long long)time(NULL) ^ (0x9E3779B97F4A7C15ULL * (unsigned long long)(tid + 1));
  unsigned long long base_nonce = ((thread_seed & 0xFFFFFFFFULL) << 32) | ((thread_seed >> 32) & 0xFFFFFFFFULL);
  u08_t base_digits[10];
  to_base95_10(base_nonce, base_digits);

    unsigned long long batches_done = 0ULL;

    while(!stop_requested && (n_batches == 0ULL || batches_done < n_batches))
    {
      // Prepare N_LANES messages for this batch
      u08_t lane_digits[N_LANES][10];
      for(int d = 0; d < 10; ++d)
        lane_digits[0][d] = base_digits[d];
      for(int lane = 1; lane < N_LANES; ++lane)
      {
        for(int d = 0; d < 10; ++d)
          lane_digits[lane][d] = lane_digits[lane - 1][d];
        base95_add(lane_digits[lane], 1ULL);
      }

      // Prepare data for each lane
      for(int lane = 0; lane < N_LANES; ++lane)
      {
        for(int k = 0; k < 12; ++k)
          data[lane].c[k ^ 3] = (u08_t)hdr[k];
        data[lane].c[54 ^ 3] = (u08_t)'\n';
        data[lane].c[55 ^ 3] = (u08_t)0x80;

        // first 10 bytes from odometer digits (base-95 printable ASCII)
        for(int j = 0; j < 10; ++j)
          data[lane].c[(12 + j) ^ 3] = (u08_t)(lane_digits[lane][j] + 32u);
        // remaining 32 bytes use static pre-generated printable characters
        for(int j = 10; j < 42; ++j)
        {
          data[lane].c[(12 + j) ^ 3] = static_tail[lane][j - 10];
        }
      }

      // Interleave (transpose) data into interleaved_data
      for(int idx = 0; idx < 14; ++idx)
        for(int lane = 0; lane < N_LANES; ++lane)
          interleaved_data[idx][lane] = data[lane].i[idx];

      // Compute SHA1
      #if defined(USE_AVX512)
        sha1_avx512f((v16si *)&interleaved_data[0], (v16si *)&interleaved_hash[0]);
      #elif defined(USE_AVX2)
        sha1_avx2((v8si *)&interleaved_data[0], (v8si *)&interleaved_hash[0]);
      #elif defined(USE_AVX)
        sha1_avx((v4si *)&interleaved_data[0], (v4si *)&interleaved_hash[0]);
      #else
        // scalar fallback, one lane at a time
        for(int lane = 0; lane < N_LANES; ++lane)
        {
          u32_t htmp[5];
          sha1((u32_t *)data[lane].i, htmp);
          for(int t = 0; t < 5; ++t)
            interleaved_hash[t][lane] = htmp[t];
        }
      #endif

      // Check results per lane
      for(int lane = 0; lane < N_LANES; ++lane)
      {
        u32_t h0 = interleaved_hash[0][lane];
        if(h0 == 0xAAD20250u)
        {
          // reconstruct hash array for zero count
          u32_t hash[5];
          for(int t = 0; t < 5; ++t)
            hash[t] = interleaved_hash[t][lane];
          unsigned int zeros = 0u;
          for(zeros = 0u; zeros < 128u; ++zeros)
            if(((hash[1u + zeros / 32u] >> (31u - (zeros % 32u))) & 1u) != 0u)
              break;
          if(zeros > 99u) zeros = 99u;

          unsigned long long found_nonce = base_nonce + (unsigned long long)lane;

          // Save coin to vault (guarded)
          #pragma omp critical(aad_vault)
          {
            save_coin((u32_t *)data[lane].i);
            coins_found++;
          }

          // Console message: allow any thread to print, serialize output
          #pragma omp critical(console)
          printf("Found DETI coin (SIMD+OMP): tid=%d nonce=%llu zeros=%u\n", tid, found_nonce, zeros);
        }
      }

      // Advance to next batch for this thread
  unsigned long long stride = (unsigned long long)N_LANES * (unsigned long long)nth;
  base_nonce += stride; // stride by number of threads
      ++batches_done;
  base95_add(base_digits, stride);
      
      // Update global counter atomically
      #pragma omp atomic
      global_batches++;

      // Progress report every ~16 million iterations (master thread only)
      #pragma omp master
      {
  unsigned long long batches_snapshot;
  #pragma omp atomic read
  batches_snapshot = global_batches;
  unsigned long long current_total = batches_snapshot * (unsigned long long)N_LANES;
        if((current_total & 0xFFFFFF) == 0ULL && current_total != total_iterations)
        {
          total_iterations = current_total;
          time_measurement();
          double delta = wall_time_delta();
          total_elapsed_time += delta;
          double fps = (delta > 0.0) ? (double)(total_iterations - last_report_iter) / delta : 0.0;
          last_report_iter = total_iterations;
          
          fprintf(stderr, "Speed: %.2f MH/s (%.2f M/min) | Nonce: %llx\n", 
                  fps / 1000000.0, 
                  (fps * 60.0) / 1000000.0, 
                  base_nonce);
        }
      }
    }
  } // end parallel

  // Flush vault buffer once after workers finish
  save_coin(NULL);
  
  // Final summary report
  time_measurement();
  double final_time = wall_time_delta();
  total_elapsed_time += final_time;
  
  unsigned long long final_total_hashes = global_batches * (unsigned long long)N_LANES;
  double avg_hashes_per_sec = (total_elapsed_time > 0.0) ? (double)final_total_hashes / total_elapsed_time : 0.0;
  double avg_hashes_per_min = avg_hashes_per_sec * 60.0;
  double hashes_per_coin = (coins_found > 0ULL) ? (double)final_total_hashes / (double)coins_found : 0.0;
  
  printf("\n");
  printf("========================================\n");
  printf("Final Summary:\n");
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
  
  return 0;
}
