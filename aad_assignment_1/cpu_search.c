#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1_cpu.h"
#include "aad_vault.h"

// Assinatura obrigatória do hash: aad20250... [cite: 12]
#define DETI_COIN_SIGNATURE 0xAAD20250u

static volatile sig_atomic_t stop_requested = 0;

// Tratamento do sinal Ctrl+C
static void handle_sigint(int sig)
{
  (void)sig;
  stop_requested = 1;
}

int main(int argc, char **argv)
{
  // O bloco SHA1 tem 64 bytes (16 palavras de 32 bits).
  // A mensagem tem 55 bytes.
  union { 
    u08_t c[64]; 
    u32_t i[16]; 
  } coin;
  
  u32_t hash[5];
  unsigned long long nonce = 0ULL;
  
  // Tabela para conversão rápida Hexadecimal
  static const char hex_map[] = "0123456789abcdef";

  // 1. INICIALIZAÇÃO GERAL
  // Limpar a estrutura a zeros
  for(int i=0; i<16; i++) coin.i[i] = 0;

  // Setup do sinal de paragem
  (void)signal(SIGINT, handle_sigint);

  // Inicializar semente de aleatoriedade uma única vez
  srand((unsigned int)time(NULL));
  
  // Inicializar nonce com valor aleatório para variar o espaço de busca
  nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();

  // 2. PREPARAÇÃO DO TEMPLATE (FEITO UMA VEZ FORA DO CICLO)
  
  // Parte A: Cabeçalho fixo [cite: 13]
  const char *hdr = "DETI coin 2 ";
  for(int k = 0; k < 12; k++)
    coin.c[k ^ 3] = (u08_t)hdr[k];

  // Parte B: Preenchimento estático aleatório (Bytes 12 a 40)
  // Gera caracteres ASCII imprimíveis (32-126) [cite: 16]
  // Isto garante que a moeda é única por execução, mas não perde tempo no ciclo.
  for(int k = 12; k < 40; k++) {
      coin.c[k ^ 3] = (u08_t)(32 + (rand() % 95));
  }

  // Parte C: Sufixo obrigatório e Padding SHA1
  // Byte 54 deve ser '\n' [cite: 14]
  coin.c[54 ^ 3] = (u08_t)'\n';
  
  // SHA1 Padding: Bit 1 logo após a mensagem (0x80 no byte 55)
  coin.c[55 ^ 3] = (u08_t)0x80;
  
  // Tamanho da mensagem em bits (55 bytes * 8 = 440 bits) colocado no fim do bloco
  // Nota: Verifica se a macro sha1() espera isto configurado. 
  // Na implementação padrão SHA1, o tamanho fica nos últimos 64 bits do bloco.
  coin.i[15] = 440; 

  // 3. MEDIÇÃO E LOOP
  time_measurement();
  double total_elapsed_time = 0.0;
  unsigned long long iter = 0ULL;
  unsigned long long last_report_iter = 0ULL;

  fprintf(stderr, "Iniciando procura CPU Scalar (Hex Nonce)...\n");

  while(!stop_requested)
  {
    // --- OTIMIZAÇÃO: Geração do Nonce Hexadecimal ---
    // Apenas atualizamos os bytes 40 a 53
    unsigned long long temp_n = nonce;
    
    // Preenche de trás para a frente (53 -> 40)
    for (int pos = 53; pos >= 40; pos--) {
        // Obtém o nibble (4 bits) e converte usando a tabela
        coin.c[pos ^ 3] = (u08_t)hex_map[temp_n & 0xF];
        
        // Shift de 4 bits (equivalente a dividir por 16)
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

    // --- CÁLCULO DO HASH ---
    sha1(&coin.i[0], hash);

    // --- VERIFICAÇÃO ---
    // Verifica se começa com aad20250 [cite: 12]
    if(hash[0] == DETI_COIN_SIGNATURE)
    {
      // Contagem de zeros consecutivos (Valor da moeda) [cite: 18]
      unsigned int zeros = 0u;
      
      // Verifica hash[1] até hash[4]
      for(int w=1; w<5; w++) {
          u32_t val = hash[w];
          if (val == 0) {
              zeros += 32;
          } else {
              // Conta bits a zero a partir do MSB (Most Significant Bit)
              u32_t mask = 0x80000000;
              while((val & mask) == 0) {
                  zeros++;
                  mask >>= 1;
              }
              break; // Encontrou um '1', para a contagem
          }
      }

      // Capar o valor a 99 (opcional, mas comum para formatação)
      if(zeros > 99u) zeros = 99u;
      
      printf("Found DETI coin: nonce=%llu val=%u\n", nonce, zeros);
      
      // Imprime a moeda encontrada (para debug/verificação)
      printf("Coin Content: \"");
      for(int b = 0; b < 55; b++) {
        unsigned char ch = ((unsigned char *)coin.c)[b ^ 3];
        // Garante que não imprime lixo no terminal
        putchar((ch >= 32 && ch <= 126) ? ch : '.');
      }
      printf("\"\n");

      // Guarda a moeda [cite: 121]
      save_coin(&coin.i[0]);
    }

    iter++;
    nonce++;

    // --- REPORTE DE STATUS ---
    // Reporta a cada ~16 milhões de tentativas (ajustar conforme a velocidade do CPU)
    if((iter & 0xFFFFFF) == 0) 
    {
      double delta = wall_time_delta();
      total_elapsed_time += delta;
      double fps = (double)(iter - last_report_iter) / delta;
      last_report_iter = iter;
      
      // Output formatado para métrica "tentativas por minuto" [cite: 139]
      fprintf(stderr, "Speed: %.2f MH/s (%.2f M/min) | Nonce: %llx\n", 
              fps / 1000000.0, 
              (fps * 60.0) / 1000000.0, 
              nonce);
      
      time_measurement(); // Reset timer
    }
  }

  // Guardar moedas pendentes antes de sair
  save_coin(NULL);
  return 0;
}