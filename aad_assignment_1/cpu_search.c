#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1_cpu.h"
#include "aad_vault.h"
#include <signal.h>

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig)
{
  (void)sig;
  stop_requested = 1;
}

int main(int argc,char **argv)
{
  unsigned long long n_trials = 0ULL;

  union { u08_t c[14 * 4]; u32_t i[14]; } coin;
  u32_t hash[5];
  unsigned long long nonce = 0ULL;
  unsigned long long report_interval = 0ULL;
  double total_elapsed_time = 0.0;

  // prepare fixed template (indices 0..11)
  const char *hdr = "DETI coin 2 ";
  for(int k = 0; k < 12; k++)
    coin.c[k ^ 3] = (u08_t)hdr[k];

  // (indices 54..55)
  coin.c[54 ^ 3] = (u08_t)'\n';
  coin.c[55 ^ 3] = (u08_t)0x80;

  // setup signal handler for Ctrl+C to request a graceful stop
  (void)signal(SIGINT,handle_sigint);

  // seed random number generator
  srand((unsigned int)time(NULL));
  
  // initialize nonce with random value to explore different search space on each run
  nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();

  // initialize time measurement
  time_measurement();

  // compute report interval
  if(n_trials == 0ULL)
    report_interval = 10000000ULL;
  else
    report_interval = n_trials / 100ULL;

  // search loop
  for(unsigned long long iter = 0ULL; ; ++iter)
  {
    if(n_trials != 0ULL && iter >= n_trials) break;
    if(stop_requested) break;
    // fill the 42 variable bytes using sequential nonce with printable ASCII
    unsigned long long temp_nonce = nonce;
    for(int j = 0; j < 42; j++)
    {
      // Use base-95 encoding (printable ASCII 32-126)
      u08_t byte_val = (u08_t)(32 + (temp_nonce % 95));
      coin.c[(12 + j) ^ 3] = byte_val;
      temp_nonce /= 95;
    }

    // compute SHA1 using the reference implementation
    sha1(&coin.i[0],hash);

    // test DETI coin
    if(hash[0] == 0xAAD20250u)
    {
      // count leading zero bits in words hash[1..4]
      unsigned int zeros = 0u;
      for(zeros = 0u; zeros < 128u; zeros++)
        if(((hash[1u + zeros / 32u] >> (31u - zeros % 32u)) & 1u) != 0u)
          break;
      if(zeros > 99u) zeros = 99u;
      printf("Found DETI coin: nonce=%llu zeros=%u\n", (unsigned long long)nonce, zeros);
      printf("coin: \"");
      for(int b = 0; b < 55; b++)
      {
        unsigned char ch = ((unsigned char *)coin.c)[b ^ 3];
        if(ch >= 32 && ch <= 126)
          putchar((int)ch);
        else
          putchar('?');
      }
      printf("\"\n");
      printf("sha1: ");
      for(int h = 0; h < 20; h++)
        printf("%02x", ((unsigned char *)hash)[h ^ 3]);
      printf("\n");

      save_coin(&coin.i[0]);
      save_coin(NULL);
    }

    if((iter % report_interval) == 0ULL)
    {
      time_measurement();
      double delta_time = wall_time_delta();
      total_elapsed_time += delta_time;

      double iterations_per_second = (double)report_interval / delta_time;
      if (iter == 0){
        iterations_per_second = 0.0;
      }      
      fprintf(stderr,"iter=%llu nonce=%llu time=%.1f iter_per_sec=%.0f\n",(unsigned long long)iter,(unsigned long long)nonce, total_elapsed_time, iterations_per_second);
    }

    ++nonce;
  }

  // flush saved coins to vault file
  save_coin(NULL);
  return 0;
}
