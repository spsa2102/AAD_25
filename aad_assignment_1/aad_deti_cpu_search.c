//
// CPU-only DETI coin search (no SIMD)
//
// Produces messages that match the DETI template prefix and varies bytes 12..53
// using a decimal nonce. For each candidate it computes the SHA1 (reference
// implementation) and calls save_coin() to store any valid coins.
//

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
  // if no argument is provided, run until Ctrl+C (infinite)
  unsigned long long n_trials = 0ULL; // 0 means infinite
  if(argc > 1)
    n_trials = strtoull(argv[1],NULL,10);

  union { u08_t c[14 * 4]; u32_t i[14]; } coin;
  u32_t hash[5];
  unsigned long long nonce = 0ULL;
  unsigned long long report_interval = 0ULL;

  // prepare fixed template prefix "DETI coin 2 " (indices 0..11)
  const char *hdr = "DETI coin 2 ";
  for(int k = 0; k < 12; k++)
    coin.c[k ^ 3] = (u08_t)hdr[k];

  // fixed newline at index 54 and SHA1 padding byte at index 55
  coin.c[54 ^ 3] = (u08_t)'\n';
  coin.c[55 ^ 3] = (u08_t)0x80;

  // setup signal handler for Ctrl+C to request a graceful stop
  (void)signal(SIGINT,handle_sigint);

  // compute report interval: for finite runs print ~100 times, for infinite use a sensible default
  if(n_trials == 0ULL)
    report_interval = 1000000ULL; // default for infinite runs
  else
    report_interval = n_trials / 100ULL;
  if(report_interval == 0ULL) report_interval = 1ULL;

  // search loop (iterate until n_trials reached, or forever if n_trials==0)
  for(unsigned long long iter = 0ULL; ; ++iter)
  {
    if(n_trials != 0ULL && iter >= n_trials) break;
    if(stop_requested) break;
    // generate a small printable representation of the nonce and fill bytes 12..53
    char buf[64];
    int len = snprintf(buf,sizeof(buf),"%llu",(unsigned long long)nonce);
    if(len <= 0) len = 1, buf[0] = '0';
    // fill the 42 variable bytes (indices 12..53 inclusive)
    for(int j = 0; j < 42; j++)
    {
      char ch = buf[j % len];
      if(ch == '\n') ch = '?';
      coin.c[(12 + j) ^ 3] = (u08_t)ch;
    }

    // compute SHA1 using the reference implementation
    sha1(&coin.i[0],hash);

    // test DETI coin signature (first word) --- same test used in save_coin()
    if(hash[0] == 0xAAD20250u)
    {
      // count leading zero bits in words hash[1..4]
      unsigned int zeros = 0u;
      for(zeros = 0u; zeros < 128u; zeros++)
        if(((hash[1u + zeros / 32u] >> (31u - zeros % 32u)) & 1u) != 0u)
          break;
      if(zeros > 99u) zeros = 99u;
      // print a human-friendly message
      printf("Found DETI coin: nonce=%llu zeros=%u\n", (unsigned long long)nonce, zeros);
      // print the 55-byte coin as ASCII (non-printable replaced by '?')
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
      // print the SHA1 hash in hex
      printf("sha1: ");
      for(int h = 0; h < 20; h++)
        printf("%02x", ((unsigned char *)hash)[h ^ 3]);
      printf("\n");
      // save the coin (save_coin will re-check the format and the SHA1)
      save_coin(&coin.i[0]);
      // immediately flush saved coins to disk
      save_coin(NULL);
    }

    // progress report at configured interval; show current nonce BEFORE incrementing it
    if((iter % report_interval) == 0ULL)
      fprintf(stderr,"iter=%llu nonce=%llu\n",(unsigned long long)iter,(unsigned long long)nonce);

    ++nonce;
  }

  // flush saved coins to vault file
  save_coin(NULL);
  return 0;
}
