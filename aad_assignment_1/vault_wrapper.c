// Thin C wrapper to use the vault from CUDA C++ code
// Compiled as C to support C99 designated initializers in aad_vault.h

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "aad_data_types.h"
#include "aad_sha1_cpu.h"  // provides static sha1() used by the vault
#include "aad_vault.h"

void save_coin_wrapper(u32_t *coin)
{
  save_coin(coin);
}

void save_coin_flush(void)
{
  save_coin(NULL);
}
