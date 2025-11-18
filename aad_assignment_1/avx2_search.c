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

  // Aligned data structures
  u32_t interleaved_data[14][N_LANES] __attribute__((aligned(64)));
  u32_t interleaved_hash[5][N_LANES] __attribute__((aligned(64)));
  
  unsigned long long base_nonce = 0ULL;
  unsigned long long batches_done = 0ULL;
  unsigned long long report_interval = 10000000ULL;
  double total_elapsed_time = 0.0;

  srand((unsigned int)time(NULL));
  base_nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();
  time_measurement();

  // Pre-initialize ALL constant data in interleaved format
  // This way we only update the nonce bytes each iteration
  const u32_t fixed_header[3] = {
    0x49544544u, // "DETI" (reversed for little-endian)
    0x696f6320u, // " coi"
    0x2032206eu  // "n 2 "
  };
  
  // Initialize all lanes with fixed data
  for(int lane = 0; lane < N_LANES; ++lane)
  {
    interleaved_data[0][lane] = fixed_header[0];
    interleaved_data[1][lane] = fixed_header[1];
    interleaved_data[2][lane] = fixed_header[2];
    // Words 3-12 will be updated with nonce+padding each iteration
    // Word 13 contains newline + 0x80 padding
    interleaved_data[13][lane] = 0x80000a00u; // 0x00 0x0a 0x80 0x?? (last byte varies)
  }

  // Pre-generate static padding pattern (words 6-12)
  u32_t static_padding[7];
  for(int i = 0; i < 7; ++i)
  {
    u32_t word = 0;
    for(int b = 0; b < 4; ++b)
    {
      u08_t byte_val = (u08_t)(32 + ((i * 4 + b) % 95));
      word |= ((u32_t)byte_val) << (8 * b);
    }
    static_padding[i] = word;
  }

  while((n_batches == 0ULL || batches_done < n_batches) && !stop_requested)
  {
    // ULTRA-FAST DATA PREP: Only update the variable nonce words
    for(int lane = 0; lane < N_LANES; ++lane)
    {
      unsigned long long nonce = base_nonce + (unsigned long long)lane;
      
      // Encode 10 bytes of nonce into words 3, 4, 5 (and part of 13)
      // This is the ONLY per-iteration work needed
      unsigned long long temp = nonce;
      
      u32_t w3 = 0, w4 = 0, w5 = 0;
      u08_t last_byte = 0;
      
      // Unrolled base-95 encoding (10 bytes)
      w3 |= (u32_t)(32 + (temp % 95)); temp /= 95;
      w3 |= (u32_t)(32 + (temp % 95)) << 8; temp /= 95;
      w3 |= (u32_t)(32 + (temp % 95)) << 16; temp /= 95;
      w3 |= (u32_t)(32 + (temp % 95)) << 24; temp /= 95;
      
      w4 |= (u32_t)(32 + (temp % 95)); temp /= 95;
      w4 |= (u32_t)(32 + (temp % 95)) << 8; temp /= 95;
      w4 |= (u32_t)(32 + (temp % 95)) << 16; temp /= 95;
      w4 |= (u32_t)(32 + (temp % 95)) << 24; temp /= 95;
      
      w5 |= (u32_t)(32 + (temp % 95)); temp /= 95;
      w5 |= (u32_t)(32 + (temp % 95)) << 8;
      
      last_byte = (u08_t)(32 + (temp % 95));
      
      interleaved_data[3][lane] = w3;
      interleaved_data[4][lane] = w4;
      interleaved_data[5][lane] = w5;
      
      // Copy static padding (words 6-12) - these never change
      interleaved_data[6][lane] = static_padding[0];
      interleaved_data[7][lane] = static_padding[1];
      interleaved_data[8][lane] = static_padding[2];
      interleaved_data[9][lane] = static_padding[3];
      interleaved_data[10][lane] = static_padding[4];
      interleaved_data[11][lane] = static_padding[5];
      interleaved_data[12][lane] = static_padding[6];
      
      // Update last byte of word 13
      interleaved_data[13][lane] = 0x80000a00u | ((u32_t)last_byte << 24);
    }

#if defined(USE_AVX)
    sha1_avx((v4si *)&interleaved_data[0],(v4si *)&interleaved_hash[0]);
#endif

    // Check results
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
        
        // Reconstruct coin for printing
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