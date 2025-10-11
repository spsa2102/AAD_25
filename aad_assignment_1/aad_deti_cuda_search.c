//
// CUDA DETI coin search host program
//
// Prepares interleaved input for the CUDA SHA1 kernel, launches it and checks results.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1_cpu.h"
#include "aad_cuda_utilities.h"
#include "aad_vault.h"

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig)
{
  (void)sig;
  stop_requested = 1;
}

int main(int argc,char **argv)
{
  u32_t n_tests = 128u * 65536u; // default number of threads/messages per kernel launch
  unsigned long long n_batches = 0ULL; // 0 -> infinite
  if(argc > 1)
    n_tests = (u32_t)strtoul(argv[1],NULL,10);
  if(argc > 2)
    n_batches = strtoull(argv[2],NULL,10);

  if(n_tests == 0u || n_tests % RECOMENDED_CUDA_BLOCK_SIZE != 0u)
  {
    fprintf(stderr,"aad_deti_cuda_search: n_tests must be multiple of %d\n",RECOMENDED_CUDA_BLOCK_SIZE);
    return 1;
  }

  (void)signal(SIGINT,handle_sigint);

  cuda_data_t cd;
  cd.device_number = 0;
  cd.cubin_file_name = "sha1_cuda_kernel.cubin";
  cd.kernel_name = "sha1_cuda_kernel";
  cd.data_size[0] = (u32_t)n_tests * (u32_t)14 * (u32_t)sizeof(u32_t);
  cd.data_size[1] = (u32_t)n_tests * (u32_t) 5 * (u32_t)sizeof(u32_t);

  initialize_cuda(&cd);

  u32_t *interleaved32_data = (u32_t *)cd.host_data[0];
  u32_t *interleaved32_hash = (u32_t *)cd.host_data[1];

  unsigned long long base_nonce = 0ULL;
  unsigned long long batches_done = 0ULL;

  const char *hdr = "DETI coin 2 ";

  while((n_batches == 0ULL || batches_done < n_batches) && !stop_requested)
  {
    // prepare n_tests messages in interleaved layout expected by the kernel
    u32_t n_warps = n_tests / 32u;
    for(u32_t warp = 0u; warp < n_warps; ++warp)
    {
      for(u32_t lane = 0u; lane < 32u; ++lane)
      {
        unsigned long long nonce = base_nonce + (unsigned long long)(warp * 32u + lane);
        union { u08_t c[14 * 4]; u32_t i[14]; } coin;
        // fixed header
        for(int k = 0; k < 12; ++k)
          coin.c[k ^ 3] = (u08_t)hdr[k];
        coin.c[54 ^ 3] = (u08_t)'\n';
        coin.c[55 ^ 3] = (u08_t)0x80;
        // fill variable bytes 12..53 with decimal ascii of nonce
        char buf[64];
        int len = snprintf(buf,sizeof(buf),"%llu",(unsigned long long)nonce);
        if(len <= 0) { len = 1; buf[0] = '0'; }
        for(int j = 0; j < 42; ++j)
        {
          char ch = buf[j % len];
          if(ch == '\n') ch = '?';
          coin.c[(12 + j) ^ 3] = (u08_t)ch;
        }
        // copy into interleaved array
        for(u32_t idx = 0u; idx < 14u; ++idx)
        {
          // address: 32*14*warp + 32*idx + lane
          interleaved32_data[32u * 14u * warp + 32u * idx + lane] = coin.i[idx];
        }
      }
    }

    // copy to device
    host_to_device_copy(&cd,0);

    // set kernel launch parameters
    cd.grid_dim_x = n_tests / (u32_t)RECOMENDED_CUDA_BLOCK_SIZE;
    cd.block_dim_x = (u32_t)RECOMENDED_CUDA_BLOCK_SIZE;
    cd.n_kernel_arguments = 2;
    cd.arg[0] = &cd.device_data[0];
    cd.arg[1] = &cd.device_data[1];

    lauch_kernel(&cd);

    // copy hashes back
    device_to_host_copy(&cd,1);

    // check results per thread
    for(u32_t n = 0u; n < n_tests; ++n)
    {
      u32_t warp_number = n / 32u;
      u32_t lane = n % 32u;
      u32_t hash[5];
      for(u32_t idx = 0u; idx < 5u; ++idx)
        hash[idx] = interleaved32_hash[32u * 5u * warp_number + 32u * idx + lane];

      if(hash[0] == 0xAAD20250u)
      {
        unsigned int zeros = 0u;
        for(zeros = 0u; zeros < 128u; ++zeros)
          if(((hash[1u + zeros / 32u] >> (31u - zeros % 32u)) & 1u) != 0u)
            break;
        if(zeros > 99u) zeros = 99u;
        unsigned long long found_nonce = base_nonce + (unsigned long long)n;
        printf("Found DETI coin (CUDA): nonce=%llu zeros=%u\n",found_nonce,zeros);
        // reconstruct coin message from interleaved data
        u32_t data[14];
        for(u32_t idx = 0u; idx < 14u; ++idx)
          data[idx] = interleaved32_data[32u * 14u * warp_number + 32u * idx + lane];
        // print ASCII-safe coin
        printf("coin: \"");
        for(int b = 0; b < 55; ++b)
        {
          unsigned char ch = ((unsigned char *)data)[b ^ 3];
          if(ch >= 32 && ch <= 126) putchar((int)ch); else putchar('?');
        }
        printf("\"\n");
        printf("sha1: ");
        for(int h = 0; h < 20; ++h) printf("%02x", ((unsigned char *)hash)[h ^ 3]);
        printf("\n");
        // save and flush
        save_coin(data);
        save_coin(NULL);
      }
    }

    base_nonce += (unsigned long long)n_tests;
    ++batches_done;
    if((batches_done & 0xFFu) == 0u) // occasional progress
      fprintf(stderr,"batches=%llu base_nonce=%llu\n",batches_done,base_nonce);
  }

  // final flush and cleanup
  save_coin(NULL);
  terminate_cuda(&cd);
  return 0;
}
