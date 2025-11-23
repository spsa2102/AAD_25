#include <stdio.h>
#include <time.h>
#include "aad_data_types.h"
#include "aad_sha1_cpu.h"

int main() {
    // Test 1: With interleaving overhead (like benchmark_all.c)
    const int N_LANES = 8;
    union { u08_t c[14 * 4]; u32_t i[14]; } data[N_LANES];
    u32_t interleaved_data[14][N_LANES] __attribute__((aligned(64)));
    u32_t interleaved_hash[5][N_LANES] __attribute__((aligned(64)));
    
    const char *hdr = "DETI coin 2 ";
    for(int lane = 0; lane < N_LANES; lane++) {
        for(int k = 0; k < 12; k++)
            data[lane].c[k ^ 3] = (u08_t)hdr[k];
        for(int j = 0; j < 42; j++)
            data[lane].c[(12 + j) ^ 3] = (u08_t)(32 + ((lane + j) % 94));
        data[lane].c[54 ^ 3] = (u08_t)'\n';
        data[lane].c[55 ^ 3] = (u08_t)0x80;
    }
    
    unsigned long long count = 0;
    unsigned long long base_nonce = 0;
    time_t start = time(NULL);
    
    while(difftime(time(NULL), start) < 10) {
        // Update nonces
        for(int lane = 0; lane < N_LANES; lane++) {
            unsigned long long nonce = base_nonce + lane;
            data[lane].c[12 ^ 3] = (u08_t)(32 + (nonce & 0x3F));
            data[lane].c[13 ^ 3] = (u08_t)(32 + ((nonce >> 6) & 0x3F));
        }
        
        // Interleave - THIS IS THE OVERHEAD
        for(int idx = 0; idx < 14; idx++)
            for(int lane = 0; lane < N_LANES; lane++)
                interleaved_data[idx][lane] = data[lane].i[idx];
        
        sha1_avx2((v8si *)&interleaved_data[0], (v8si *)&interleaved_hash[0]);
        base_nonce += N_LANES;
        count += N_LANES;
    }
    printf("AVX2 WITH interleaving: %.2e hashes/sec\n", (double)count / 10.0);
    
    // Test 2: Without interleaving (direct)
    u32_t direct_data[14][8] __attribute__((aligned(64)));
    u32_t direct_hash[5][8] __attribute__((aligned(64)));
    for(int i = 0; i < 14; i++)
        for(int j = 0; j < 8; j++)
            direct_data[i][j] = i * 8 + j;
    
    count = 0;
    start = time(NULL);
    while(difftime(time(NULL), start) < 10) {
        sha1_avx2((v8si *)direct_data, (v8si *)direct_hash);
        direct_data[0][0]++;
        count += 8;
    }
    printf("AVX2 WITHOUT interleaving: %.2e hashes/sec\n", (double)count / 10.0);
    
    return 0;
}
