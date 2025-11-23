#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1_cpu.h"
#include "aad_vault.h"

#define DETI_COIN_SIGNATURE 0xAAD20250u

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig)
{
  (void)sig;
  stop_requested = 1;
}

int main(int argc, char **argv)
{
  // O bloco SHA1 tem 64 bytes (16 palavras de 32 bits)
  // A mensagem tem 55 bytes
  union { 
    u08_t c[64]; 
    u32_t i[16]; 
  } coin;
  
  u32_t hash[5];
  unsigned long long nonce = 0ULL;
  
  // Tabela para conversão rápida Hexadecimal
  static const char hex_map[] = "0123456789abcdef";

  for(int i=0; i<16; i++) coin.i[i] = 0;

  (void)signal(SIGINT, handle_sigint);

  srand((unsigned int)time(NULL));
  
  // Inicializar nonce com valor aleatório para variar o espaço de busca
  nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();
  
  // Cabeçalho fixo da DETI coin
  const char *hdr = "DETI coin 2 ";
  for(int k = 0; k < 12; k++)
    coin.c[k ^ 3] = (u08_t)hdr[k];

  // Espaço estático aleatório
  for(int k = 12; k < 40; k++) {
      coin.c[k ^ 3] = (u08_t)(32 + (rand() % 95));
  }

  // Byte 54 e 55
  coin.c[54 ^ 3] = (u08_t)'\n';
  coin.c[55 ^ 3] = (u08_t)0x80;
  
  coin.i[15] = 440; 

  time_measurement();
  double total_elapsed_time = 0.0;
  unsigned long long iter = 0ULL;
  unsigned long long last_report_iter = 0ULL;
  unsigned long long coins_found = 0ULL;

  fprintf(stderr, "Iniciando procura CPU...\n");

  while(!stop_requested)
  {
    unsigned long long temp_n = nonce;
    
    // Preenche de trás para a frente (53 -> 40)
    for (int pos = 53; pos >= 40; pos--) {
        coin.c[pos ^ 3] = (u08_t)hex_map[temp_n & 0xF];
        
        // Shift de 4 bits (equivalente a divisão por 16)
        temp_n >>= 4; 
        
        // Se o número acabou, preenche o resto com espaços para limpar dígitos antigos
        if (temp_n == 0 && pos > 40) {
             while(pos > 40) { 
                 pos--; 
                 coin.c[pos ^ 3] = ' '; 
             }
             break;
        }
    }

    sha1(&coin.i[0], hash);

    // Verifica se começa com aad20250
    if(hash[0] == DETI_COIN_SIGNATURE)
    {
      printf("Found DETI coin: nonce=%llu\n", nonce);
      printf("Coin Content: \"");
      for(int b = 0; b < 55; b++) {
        unsigned char ch = ((unsigned char *)coin.c)[b ^ 3];
        putchar((ch >= 32 && ch <= 126) ? ch : '.');
      }
      printf("\"\n");

      save_coin(&coin.i[0]);
      coins_found++;
    }

    iter++;
    nonce++;

    // Reporta a cada ~16 milhões de tentativas
    if((iter & 0xFFFFFF) == 0) 
    {
      time_measurement();
      double delta = wall_time_delta();
      total_elapsed_time += delta;
      double fps = (double)(iter - last_report_iter) / delta;
      last_report_iter = iter;
      
      fprintf(stderr, "Speed: %.2f MH/s (%.2f M/min) | Nonce: %llx\n", 
              fps / 1000000.0, 
              (fps * 60.0) / 1000000.0, 
              nonce);
    }
  }

  // Guardar moedas pendentes antes de sair
  save_coin(NULL);

  // Report
  time_measurement();
  double final_time = wall_time_delta();
  total_elapsed_time += final_time;

  unsigned long long final_total_hashes = iter;
  double avg_hashes_per_sec = (total_elapsed_time > 0.0) ? (double)final_total_hashes / total_elapsed_time : 0.0;
  double avg_hashes_per_min = avg_hashes_per_sec * 60.0;
  double hashes_per_coin = (coins_found > 0ULL) ? (double)final_total_hashes / (double)coins_found : 0.0;

  printf("\n");
  printf("========================================\n");
  printf("Final Summary (CPU):\n");
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