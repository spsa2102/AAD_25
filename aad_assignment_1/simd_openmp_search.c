// SIMD + OpenMP DETI coin search
//
// Uses AVX/AVX2/AVX512 (if available) via aad_sha1_cpu.h vector implementations
// and parallelizes batches across threads with OpenMP. Vault writes are guarded.

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

// Choose lanes and function at compile time
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

int main(int argc, char **argv)
{
  unsigned long long n_batches = 0ULL;
  if(argc > 1)
    n_batches = strtoull(argv[1], NULL, 10);

  (void)signal(SIGINT, handle_sigint);

  const char *hdr = "DETI coin 2 ";

  time_measurement();
  unsigned long long report_interval = 10000000ULL;
  double total_elapsed_time = 0.0;

  // Parallel region
  #pragma omp parallel
  {
    const int tid = omp_get_thread_num();
    const int nth = omp_get_num_threads();

    // Per-thread buffers
    union { u08_t c[14 * 4]; u32_t i[14]; } data[N_LANES];
    u32_t interleaved_data[14][N_LANES] __attribute__((aligned(64)));
    u32_t interleaved_hash[5][N_LANES] __attribute__((aligned(64)));

    // Initialize a different base nonce per thread
    unsigned long long thread_seed = (unsigned long long)time(NULL) ^ (0x9E3779B97F4A7C15ULL * (unsigned long long)(tid + 1));
    unsigned long long base_nonce = ((thread_seed & 0xFFFFFFFFULL) << 32) | ((thread_seed >> 32) & 0xFFFFFFFFULL);

    unsigned long long batches_done = 0ULL;

    while(!stop_requested && (n_batches == 0ULL || batches_done < n_batches))
    {
      // Prepare N_LANES messages for this batch
      for(int lane = 0; lane < N_LANES; ++lane)
      {
        for(int k = 0; k < 12; ++k)
          data[lane].c[k ^ 3] = (u08_t)hdr[k];
        data[lane].c[54 ^ 3] = (u08_t)'\n';
        data[lane].c[55 ^ 3] = (u08_t)0x80;

        unsigned long long nonce = base_nonce + (unsigned long long)lane;
        unsigned long long tnonce = nonce;
        for(int j = 0; j < 42; ++j)
        {
          u08_t byte_val = (u08_t)(32 + (tnonce % 95ULL)); // printable ASCII
          data[lane].c[(12 + j) ^ 3] = byte_val;
          tnonce /= 95ULL;
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
          }

          // Console message: allow any thread to print, serialize output
          #pragma omp critical(console)
          printf("Found DETI coin (SIMD+OMP): tid=%d nonce=%llu zeros=%u\n", tid, found_nonce, zeros);
        }
      }

      // Advance to next batch for this thread
      base_nonce += (unsigned long long)N_LANES * (unsigned long long)nth; // stride by number of threads
      ++batches_done;

      // Occasional progress report
      if((batches_done % report_interval) == 0ULL)
      {
        #pragma omp master
        {
          time_measurement();
          double dt = wall_time_delta();
          total_elapsed_time += dt;
          double batches_per_sec = (double)report_interval / dt;
          double iters_per_sec = (double)(report_interval * N_LANES * (unsigned long long)nth) / dt;
          fprintf(stderr, "OMP threads=%d lanes=%d bps=%.0f ips=%.0f\n", nth, N_LANES, batches_per_sec, iters_per_sec);
        }
      }
    }
  } // end parallel

  // Flush vault buffer once after workers finish
  save_coin(NULL);
  return 0;
}
