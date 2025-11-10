//
// OpenCL kernel for DETI coin search
// Arquiteturas de Alto Desempenho 2025/2026
//

// SHA-1 constants and functions
#define SHA1_F1(x,y,z)  ((x & y) | (~x & z))
#define SHA1_K1         0x5A827999u
#define SHA1_F2(x,y,z)  (x ^ y ^ z)
#define SHA1_K2         0x6ED9EBA1u
#define SHA1_F3(x,y,z)  ((x & y) | (x & z) | (y & z))
#define SHA1_K3         0x8F1BBCDCu
#define SHA1_F4(x,y,z)  (x ^ y ^ z)
#define SHA1_K4         0xCA62C1D6u

#define SHA1_D(t)                                                                            \
  do                                                                                         \
  {                                                                                          \
    unsigned int tmp = w[((t) - 3) & 15] ^ w[((t) - 8) & 15] ^ w[((t) - 14) & 15] ^ w[((t) - 16) & 15]; \
    w[(t) & 15] = rotate(tmp, 1u);                                                           \
  }                                                                                          \
  while(0)

#define SHA1_S(F,t,K)                                                                        \
  do                                                                                         \
  {                                                                                          \
    unsigned int tmp = rotate(a, 5u) + F(b,c,d) + e + w[(t) & 15] + (K);                    \
    e = d;                                                                                   \
    d = c;                                                                                   \
    c = rotate(b, 30u);                                                                      \
    b = a;                                                                                   \
    a = tmp;                                                                                 \
  }                                                                                          \
  while(0)

// Inline SHA-1 computation
inline void compute_sha1(unsigned int *coin_words, unsigned int *hash)
{
  unsigned int a, b, c, d, e, w[16];
  
  // Initial state
  a = 0x67452301u;
  b = 0xEFCDAB89u;
  c = 0x98BADCFEu;
  d = 0x10325476u;
  e = 0xC3D2E1F0u;
  
  // Copy data to internal buffer
  w[ 0] = coin_words[ 0];
  w[ 1] = coin_words[ 1];
  w[ 2] = coin_words[ 2];
  w[ 3] = coin_words[ 3];
  w[ 4] = coin_words[ 4];
  w[ 5] = coin_words[ 5];
  w[ 6] = coin_words[ 6];
  w[ 7] = coin_words[ 7];
  w[ 8] = coin_words[ 8];
  w[ 9] = coin_words[ 9];
  w[10] = coin_words[10];
  w[11] = coin_words[11];
  w[12] = coin_words[12];
  w[13] = coin_words[13];
  w[14] = 0;
  w[15] = 440; // 55*8 bits
  
  // First group (0-19)
              SHA1_S(SHA1_F1, 0,SHA1_K1);
              SHA1_S(SHA1_F1, 1,SHA1_K1);
              SHA1_S(SHA1_F1, 2,SHA1_K1);
              SHA1_S(SHA1_F1, 3,SHA1_K1);
              SHA1_S(SHA1_F1, 4,SHA1_K1);
              SHA1_S(SHA1_F1, 5,SHA1_K1);
              SHA1_S(SHA1_F1, 6,SHA1_K1);
              SHA1_S(SHA1_F1, 7,SHA1_K1);
              SHA1_S(SHA1_F1, 8,SHA1_K1);
              SHA1_S(SHA1_F1, 9,SHA1_K1);
              SHA1_S(SHA1_F1,10,SHA1_K1);
              SHA1_S(SHA1_F1,11,SHA1_K1);
              SHA1_S(SHA1_F1,12,SHA1_K1);
              SHA1_S(SHA1_F1,13,SHA1_K1);
              SHA1_S(SHA1_F1,14,SHA1_K1);
              SHA1_S(SHA1_F1,15,SHA1_K1);
  SHA1_D(16); SHA1_S(SHA1_F1,16,SHA1_K1);
  SHA1_D(17); SHA1_S(SHA1_F1,17,SHA1_K1);
  SHA1_D(18); SHA1_S(SHA1_F1,18,SHA1_K1);
  SHA1_D(19); SHA1_S(SHA1_F1,19,SHA1_K1);
  
  // Second group (20-39)
  SHA1_D(20); SHA1_S(SHA1_F2,20,SHA1_K2);
  SHA1_D(21); SHA1_S(SHA1_F2,21,SHA1_K2);
  SHA1_D(22); SHA1_S(SHA1_F2,22,SHA1_K2);
  SHA1_D(23); SHA1_S(SHA1_F2,23,SHA1_K2);
  SHA1_D(24); SHA1_S(SHA1_F2,24,SHA1_K2);
  SHA1_D(25); SHA1_S(SHA1_F2,25,SHA1_K2);
  SHA1_D(26); SHA1_S(SHA1_F2,26,SHA1_K2);
  SHA1_D(27); SHA1_S(SHA1_F2,27,SHA1_K2);
  SHA1_D(28); SHA1_S(SHA1_F2,28,SHA1_K2);
  SHA1_D(29); SHA1_S(SHA1_F2,29,SHA1_K2);
  SHA1_D(30); SHA1_S(SHA1_F2,30,SHA1_K2);
  SHA1_D(31); SHA1_S(SHA1_F2,31,SHA1_K2);
  SHA1_D(32); SHA1_S(SHA1_F2,32,SHA1_K2);
  SHA1_D(33); SHA1_S(SHA1_F2,33,SHA1_K2);
  SHA1_D(34); SHA1_S(SHA1_F2,34,SHA1_K2);
  SHA1_D(35); SHA1_S(SHA1_F2,35,SHA1_K2);
  SHA1_D(36); SHA1_S(SHA1_F2,36,SHA1_K2);
  SHA1_D(37); SHA1_S(SHA1_F2,37,SHA1_K2);
  SHA1_D(38); SHA1_S(SHA1_F2,38,SHA1_K2);
  SHA1_D(39); SHA1_S(SHA1_F2,39,SHA1_K2);
  
  // Third group (40-59)
  SHA1_D(40); SHA1_S(SHA1_F3,40,SHA1_K3);
  SHA1_D(41); SHA1_S(SHA1_F3,41,SHA1_K3);
  SHA1_D(42); SHA1_S(SHA1_F3,42,SHA1_K3);
  SHA1_D(43); SHA1_S(SHA1_F3,43,SHA1_K3);
  SHA1_D(44); SHA1_S(SHA1_F3,44,SHA1_K3);
  SHA1_D(45); SHA1_S(SHA1_F3,45,SHA1_K3);
  SHA1_D(46); SHA1_S(SHA1_F3,46,SHA1_K3);
  SHA1_D(47); SHA1_S(SHA1_F3,47,SHA1_K3);
  SHA1_D(48); SHA1_S(SHA1_F3,48,SHA1_K3);
  SHA1_D(49); SHA1_S(SHA1_F3,49,SHA1_K3);
  SHA1_D(50); SHA1_S(SHA1_F3,50,SHA1_K3);
  SHA1_D(51); SHA1_S(SHA1_F3,51,SHA1_K3);
  SHA1_D(52); SHA1_S(SHA1_F3,52,SHA1_K3);
  SHA1_D(53); SHA1_S(SHA1_F3,53,SHA1_K3);
  SHA1_D(54); SHA1_S(SHA1_F3,54,SHA1_K3);
  SHA1_D(55); SHA1_S(SHA1_F3,55,SHA1_K3);
  SHA1_D(56); SHA1_S(SHA1_F3,56,SHA1_K3);
  SHA1_D(57); SHA1_S(SHA1_F3,57,SHA1_K3);
  SHA1_D(58); SHA1_S(SHA1_F3,58,SHA1_K3);
  SHA1_D(59); SHA1_S(SHA1_F3,59,SHA1_K3);
  
  // Fourth group (60-79)
  SHA1_D(60); SHA1_S(SHA1_F4,60,SHA1_K4);
  SHA1_D(61); SHA1_S(SHA1_F4,61,SHA1_K4);
  SHA1_D(62); SHA1_S(SHA1_F4,62,SHA1_K4);
  SHA1_D(63); SHA1_S(SHA1_F4,63,SHA1_K4);
  SHA1_D(64); SHA1_S(SHA1_F4,64,SHA1_K4);
  SHA1_D(65); SHA1_S(SHA1_F4,65,SHA1_K4);
  SHA1_D(66); SHA1_S(SHA1_F4,66,SHA1_K4);
  SHA1_D(67); SHA1_S(SHA1_F4,67,SHA1_K4);
  SHA1_D(68); SHA1_S(SHA1_F4,68,SHA1_K4);
  SHA1_D(69); SHA1_S(SHA1_F4,69,SHA1_K4);
  SHA1_D(70); SHA1_S(SHA1_F4,70,SHA1_K4);
  SHA1_D(71); SHA1_S(SHA1_F4,71,SHA1_K4);
  SHA1_D(72); SHA1_S(SHA1_F4,72,SHA1_K4);
  SHA1_D(73); SHA1_S(SHA1_F4,73,SHA1_K4);
  SHA1_D(74); SHA1_S(SHA1_F4,74,SHA1_K4);
  SHA1_D(75); SHA1_S(SHA1_F4,75,SHA1_K4);
  SHA1_D(76); SHA1_S(SHA1_F4,76,SHA1_K4);
  SHA1_D(77); SHA1_S(SHA1_F4,77,SHA1_K4);
  SHA1_D(78); SHA1_S(SHA1_F4,78,SHA1_K4);
  SHA1_D(79); SHA1_S(SHA1_F4,79,SHA1_K4);
  
  // Store final hash (add to initial state)
  hash[0] = a + 0x67452301u;
  hash[1] = b + 0xEFCDAB89u;
  hash[2] = c + 0x98BADCFEu;
  hash[3] = d + 0x10325476u;
  hash[4] = e + 0xC3D2E1F0u;
}

__kernel void search_coins_kernel(
    unsigned long base_nonce,
    unsigned long num_coins,
    __global unsigned int *found_coins,
    __global int *found_count,
    int max_found)
{
  unsigned long idx = get_global_id(0);
  if(idx >= num_coins) return;
  
  unsigned long nonce = base_nonce + idx;
  
  // Prepare coin data (14 x 32-bit words, 56 bytes)
  unsigned int coin_words[14];
  unsigned char *coin_bytes = (unsigned char *)coin_words;
  
  // Fixed header "DETI coin 2 "
  const char hdr[12] = {'D','E','T','I',' ','c','o','i','n',' ','2',' '};
  for(int k = 0; k < 12; k++)
    coin_bytes[k ^ 3] = (unsigned char)hdr[k];
  
  // Fill variable bytes 12..53 using nonce with printable ASCII (base-95)
  unsigned long temp_nonce = nonce;
  for(int j = 0; j < 42; j++)
  {
    unsigned char byte_val = (unsigned char)(32 + (temp_nonce % 95));
    coin_bytes[(12 + j) ^ 3] = byte_val;
    temp_nonce /= 95;
  }
  
  // Newline and padding
  coin_bytes[54 ^ 3] = (unsigned char)'\n';
  coin_bytes[55 ^ 3] = (unsigned char)0x80;
  
  // Compute SHA-1 hash
  unsigned int hash[5];
  compute_sha1(coin_words, hash);
  
  // Check if valid DETI coin
  if(hash[0] == 0xAAD20250u)
  {
    // Atomically add to found list
    int pos = atomic_add(found_count, 1);
    if(pos < max_found)
    {
      // Copy coin data to results
      for(int i = 0; i < 14; i++)
        found_coins[pos * 14 + i] = coin_words[i];
    }
  }
}
