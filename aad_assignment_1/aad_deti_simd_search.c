//
// SIMD DETI coin search using AVX2 / AVX / NEON implementations in aad_sha1_cpu.h
//

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
#if defined(__AVX2__)
# define N_LANES 8
# define USE_AVX2 1
#elif defined(__AVX__)
# define N_LANES 4
# define USE_AVX 1
#elif defined(__ARM_NEON)
# define N_LANES 4
# define USE_NEON 1
#else
# error "No AVX/AVX2/NEON support detected — compile with -mavx/-mavx2 or for ARM with NEON"
#endif

int main(int argc,char **argv)
{
  unsigned long long n_batches = 0ULL; // 0 -> infinite
  if(argc > 1)
    n_batches = strtoull(argv[1],NULL,10);

  (void)signal(SIGINT,handle_sigint);

  union { u08_t c[14 * 4]; u32_t i[14]; } data[N_LANES];
  u32_t interleaved_data[14][N_LANES] __attribute__((aligned(64)));
  u32_t interleaved_hash[5][N_LANES] __attribute__((aligned(64)));
  unsigned long long base_nonce = 0ULL;
  unsigned long long batches_done = 0ULL;
  unsigned long long report_interval = 10000000ULL;

  // prepare fixed header bytes (same for all lanes)
  const char *hdr = "DETI coin 2 ";

  while((n_batches == 0ULL || batches_done < n_batches) && !stop_requested)
  {
    // prepare N_LANES messages
    for(int lane = 0; lane < N_LANES; ++lane)
    {
      // fill random-ish data for the message: fixed header, variable part from nonce
      for(int k = 0; k < 12; ++k)
        data[lane].c[k ^ 3] = (u08_t)hdr[k];
      data[lane].c[54 ^ 3] = (u08_t)'\n';
      data[lane].c[55 ^ 3] = (u08_t)0x80;

      // fill variable bytes 12..53 with decimal ascii of (base_nonce + lane)
      char buf[64];
      unsigned long long nonce = base_nonce + (unsigned long long)lane;
      int len = snprintf(buf,sizeof(buf),"%llu",nonce);
      if(len <= 0) { len = 1; buf[0] = '0'; }
      for(int j = 0; j < 42; ++j)
      {
        char ch = buf[j % len];
        if(ch == '\n') ch = '?';
        data[lane].c[(12 + j) ^ 3] = (u08_t)ch;
      }
    }

    // interleave (transpose) data into interleaved_data
    for(int idx = 0; idx < 14; ++idx)
      for(int lane = 0; lane < N_LANES; ++lane)
        interleaved_data[idx][lane] = data[lane].i[idx];

    // compute SIMD SHA1
#if defined(USE_AVX2)
    sha1_avx2((v8si *)&interleaved_data[0],(v8si *)&interleaved_hash[0]);
#elif defined(USE_AVX)
    sha1_avx((v4si *)&interleaved_data[0],(v4si *)&interleaved_hash[0]);
#elif defined(USE_NEON)
    sha1_neon((uint32x4_t *)&interleaved_data[0],(uint32x4_t *)&interleaved_hash[0]);
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
      fprintf(stderr,"batches=%llu base_nonce=%llu\n",batches_done,base_nonce);
  }

  // final flush
  save_coin(NULL);
  return 0;
}
