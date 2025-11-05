#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
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

// choose lanes and function at compile time
#if defined(__AVX__)
# define N_LANES 4
# define USE_AVX 1
#else
# error "No AVX support detected"
#endif

int main(int argc,char **argv)
{
  unsigned long long n_batches = 0ULL;
  if(argc > 1)
    n_batches = strtoull(argv[1],NULL,10);

  (void)signal(SIGINT,handle_sigint);

  union { u08_t c[14 * 4]; u32_t i[14]; } data[N_LANES];
  u32_t interleaved_data[14][N_LANES] __attribute__((aligned(64)));
  u32_t interleaved_hash[5][N_LANES] __attribute__((aligned(64)));
  unsigned long long base_nonce = 0ULL;
  unsigned long long batches_done = 0ULL;
  unsigned long long report_interval = 10000000ULL;
  double total_elapsed_time = 0.0;

  // seed random number generator
  srand((unsigned int)time(NULL));
  
  // initialize base_nonce with random value to explore different search space on each run
  base_nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();

  // initialize time measurement
  time_measurement();

  // prepare fixed bytes;
  const char *hdr = "DETI coin 2 ";
  // precompute mapping from random byte -> printable ASCII [32..126]
  u08_t ascii95_lut[256];
  for(int i = 0; i < 256; ++i)
    ascii95_lut[i] = (u08_t)((i % 95) + 32);

  while((n_batches == 0ULL || batches_done < n_batches) && !stop_requested)
  {
    // prepare N_LANES messages
    for(int lane = 0; lane < N_LANES; ++lane)
    {
      // fill random-ish data: fixed header, variable part from nonce
      for(int k = 0; k < 12; ++k)
        data[lane].c[k ^ 3] = (u08_t)hdr[k];
      data[lane].c[54 ^ 3] = (u08_t)'\n';
      data[lane].c[55 ^ 3] = (u08_t)0x80;

      // fill variable bytes 12..53:
      // - first 10 bytes from the sequential nonce (base-95 printable ASCII)
      // - remaining 32 bytes using random_byte() from aad_utilities
      unsigned long long nonce = base_nonce + (unsigned long long)lane;
      unsigned long long temp_nonce = nonce;
      // first 10 bytes from nonce
      for(int j = 0; j < 10; ++j)
      {
        u08_t byte_val = (u08_t)(32 + (temp_nonce % 95));
        data[lane].c[(12 + j) ^ 3] = byte_val;
        temp_nonce /= 95;
      }
      // remaining bytes from random_byte() using LUT (avoids per-iteration modulo)
      for(int j = 10; j < 42; ++j)
      {
        data[lane].c[(12 + j) ^ 3] = ascii95_lut[random_byte()];
      }
    }

    // interleave (transpose) data into interleaved_data
    for(int idx = 0; idx < 14; ++idx)
      for(int lane = 0; lane < N_LANES; ++lane)
        interleaved_data[idx][lane] = data[lane].i[idx];

#if defined(USE_AVX)
    sha1_avx((v4si *)&interleaved_data[0],(v4si *)&interleaved_hash[0]);
#endif

    // check results per lane
    for(int lane = 0; lane < N_LANES; ++lane)
    {
      u32_t h0 = interleaved_hash[0][lane];
      if(h0 == 0xAAD20250u)
      {
        // reconstruct coin and compute leading zeros
        u32_t hash[5];
        for(int t = 0; t < 5; ++t)
          hash[t] = interleaved_hash[t][lane];
        unsigned int zeros = 0u;
        for(zeros = 0u; zeros < 128u; ++zeros)
          if(((hash[1u + zeros / 32u] >> (31u - zeros % 32u)) & 1u) != 0u)
            break;
        if(zeros > 99u) zeros = 99u;
        unsigned long long found_nonce = base_nonce + (unsigned long long)lane;
        printf("Found DETI coin (SIMD): nonce=%llu zeros=%u\n",found_nonce,zeros);
        // print ASCII-safe coin
        printf("coin: \"");
        for(int b = 0; b < 55; ++b)
        {
          unsigned char ch = data[lane].c[b ^ 3];
          if(ch >= 32 && ch <= 126) putchar((int)ch); else putchar('?');
        }
        printf("\"\n");
        // print sha1
        printf("sha1: ");
        for(int h = 0; h < 20; ++h) printf("%02x", ((unsigned char *)hash)[h ^ 3]);
        printf("\n");
        // save and flush immediately
        save_coin((u32_t *)data[lane].i);
        save_coin(NULL);
      }
    }

    // advance
    base_nonce += (unsigned long long)N_LANES;
    ++batches_done;

    if((batches_done % report_interval) == 0ULL)
    {
      time_measurement();
      double delta_time = wall_time_delta();
      total_elapsed_time += delta_time;
      
      double batches_per_second = (double)report_interval / delta_time;
      double iterations_per_second = (double)(report_interval * N_LANES) / delta_time;
      
      if(batches_done == 0)
      {
        batches_per_second = 0.0;
        iterations_per_second = 0.0;
      }
      
      fprintf(stderr,"batches=%llu nonce=%llu time=%.1f batch_per_sec=%.0f iter_per_sec=%.0f\n",
              batches_done, base_nonce, total_elapsed_time, batches_per_second, iterations_per_second);
    }
  }

  save_coin(NULL);
  return 0;
}
