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

#if defined(__AVX2__)
# define N_LANES 8
# define USE_AVX2 1
#else
# error "No AVX2 support detected"
#endif

// funtion to increment an base-95 number
static inline void base95_add(u08_t digits[11], unsigned int k)
{
  unsigned int carry = k;
  for(int i = 0; i < 11 && carry != 0u; ++i)
  {
    unsigned int sum = (unsigned int)digits[i] + carry;
    digits[i] = (u08_t)(sum % 95u);
    carry = sum / 95u;
  }
}

// convert a 64-bit value into 11 base-95 digits
static inline void to_base95_11(u64_t x, u08_t out_digits[11])
{
  for(int i = 0; i < 11; ++i)
  {
    out_digits[i] = (u08_t)(x % 95u);
    x /= 95u;
  }
}

int main(int argc,char **argv)
{
  unsigned long long n_batches = 0ULL;
  if(argc > 1)
    n_batches = strtoull(argv[1],NULL,10);

  (void)signal(SIGINT,handle_sigint);

  // aligned data structures
  u32_t interleaved_data[14][N_LANES] __attribute__((aligned(64)));
  u32_t interleaved_hash[5][N_LANES] __attribute__((aligned(64)));
  
  unsigned long long base_nonce = 0ULL;
  unsigned long long batches_done = 0ULL;
  unsigned long long report_interval = 100000000ULL;
  double total_elapsed_time = 0.0;

  srand((unsigned int)time(NULL));
  base_nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();
  time_measurement();

  // pre-inicialize all constants
  const u32_t fixed_header[3] = {
    0x44455449u, // first 4 bits: "DETI"
    0x20636f69u, // next 4 bits: " coi"
    0x6e203220u  // last 4 bits: "n 2 "
  };
  
  // initialize all lanes with fixed data
  for(int lane = 0; lane < N_LANES; ++lane)
  {
    interleaved_data[0][lane] = fixed_header[0];
    interleaved_data[1][lane] = fixed_header[1];
    interleaved_data[2][lane] = fixed_header[2];
    interleaved_data[13][lane] = 0x00000A80u; // 
  }

  // pre-generate static padding pattern (words 6-12)
  // add a small random offset so padding differs across program runs
  u32_t static_padding[7];
  u08_t rnd_offset = (u08_t)(rand() % 95u);
  for(int i = 0; i < 7; ++i)
  {
    u32_t word = 0;
    for(int b = 0; b < 4; ++b)
    {
      u08_t byte_val = (u08_t)(32 + ((i * 4 + b + rnd_offset) % 95));
      word |= ((u32_t)byte_val) << (8 * b);
    }
    static_padding[i] = word;
  }

  // fill static padding for all lanes once (words 6..12 never change)
  for(int lane = 0; lane < N_LANES; ++lane)
  {
    interleaved_data[6][lane]  = static_padding[0];
    interleaved_data[7][lane]  = static_padding[1];
    interleaved_data[8][lane]  = static_padding[2];
    interleaved_data[9][lane]  = static_padding[3];
    interleaved_data[10][lane] = static_padding[4];
    interleaved_data[11][lane] = static_padding[5];
    interleaved_data[12][lane] = static_padding[6];
  }

  // maintain a base-95 odometer for the nonce to avoid expensive div/mod each iteration
  u08_t base_digits[11];
  to_base95_11(base_nonce, base_digits);

  while((n_batches == 0ULL || batches_done < n_batches) && !stop_requested)
  {
    u08_t lane_digits[N_LANES][11];
    for(int d = 0; d < 11; ++d) lane_digits[0][d] = base_digits[d];
    for(int lane = 1; lane < N_LANES; ++lane)
    {
      for(int d = 0; d < 11; ++d) lane_digits[lane][d] = lane_digits[lane-1][d];
      base95_add(lane_digits[lane], 1u);
    }

    for(int lane = 0; lane < N_LANES; ++lane)
    {
      // map digits to bytes in [32,126]
      const u32_t b0 = (u32_t)(lane_digits[lane][0] + 32u);
      const u32_t b1 = (u32_t)(lane_digits[lane][1] + 32u);
      const u32_t b2 = (u32_t)(lane_digits[lane][2] + 32u);
      const u32_t b3 = (u32_t)(lane_digits[lane][3] + 32u);
      const u32_t b4 = (u32_t)(lane_digits[lane][4] + 32u);
      const u32_t b5 = (u32_t)(lane_digits[lane][5] + 32u);
      const u32_t b6 = (u32_t)(lane_digits[lane][6] + 32u);
      const u32_t b7 = (u32_t)(lane_digits[lane][7] + 32u);
      const u32_t b8 = (u32_t)(lane_digits[lane][8] + 32u);
      const u32_t b9 = (u32_t)(lane_digits[lane][9] + 32u);

      // pack into words
      u32_t w3 = (b0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
      u32_t w4 = (b4) | (b5 << 8) | (b6 << 16) | (b7 << 24);
      u32_t w5 = (b8) | (b9 << 8) | (b8 << 16) | (b9 << 24);

      interleaved_data[3][lane] = w3;
      interleaved_data[4][lane] = w4;
      interleaved_data[5][lane] = w5;

      interleaved_data[13][lane] = 0x00000A80u | (b9 << 16) | (b8 << 24);
    }

    // advance to next batch
    base95_add(base_digits, (unsigned int)N_LANES);

#if defined(USE_AVX2)
    sha1_avx2((v8si *)&interleaved_data[0],(v8si *)&interleaved_hash[0]);
#endif

    // check results
    for(int lane = 0; lane < N_LANES; ++lane)
    {
      if(interleaved_hash[0][lane] == 0xAAD20250u)
      {
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
        
        // reconstruct coin for printing
        u32_t coin_data[14];
        for(int i = 0; i < 14; ++i)
          coin_data[i] = interleaved_data[i][lane];
        
        printf("coin: \"");
        u08_t *coin_bytes = (u08_t *)coin_data;
        for(int b = 0; b < 55; ++b)
        {
          unsigned char ch = coin_bytes[b ^ 3];
          if(ch >= 32 && ch <= 126) putchar((int)ch); else putchar('?');
        }
        printf("\"\n");
        
        printf("sha1: ");
        for(int h = 0; h < 20; ++h) printf("%02x", ((unsigned char *)hash)[h ^ 3]);
        printf("\n");
        
        save_coin(coin_data);
        save_coin(NULL);
      }
    }

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
