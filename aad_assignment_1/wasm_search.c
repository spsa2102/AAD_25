// Scalar WebAssembly DETI Coin search
// Build example:
// emcc -O3 wasm_search.c -o wasm_search_scalar.js \
//   -s EXPORTED_FUNCTIONS='["_search_coins","_get_attempts","_get_coins_found","_get_first_coin_nonce","_get_first_coin_ptr","_get_first_coin_length","_malloc","_free"]' \
//   -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_NAME='WasmSearchScalar'

#include <stdint.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

typedef uint8_t  u08_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;

#define DETI_COIN_SIGNATURE 0xAAD20250u

#define ROTL(x,n) (((x) << (n)) | ((x) >> (32 - (n))))
#define F1(x,y,z) (((x) & (y)) | (~(x) & (z)))
#define F2(x,y,z) ((x) ^ (y) ^ (z))
#define F3(x,y,z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define F4(x,y,z) ((x) ^ (y) ^ (z))
#define K1 0x5A827999u
#define K2 0x6ED9EBA1u
#define K3 0x8F1BBCDCu
#define K4 0xCA62C1D6u

static void sha1_55(const u32_t *data,u32_t *hash){
  u32_t a=0x67452301u,b=0xEFCDAB89u,c=0x98BADCFEu,d=0x10325476u,e=0xC3D2E1F0u,w[16],tmp; int t;
  for(t=0;t<14;t++) w[t]=data[t]; w[14]=0; w[15]=440; // length = 55*8
  for(t=0;t<80;t++){
    if(t>=16){ tmp=w[(t-3)&15]^w[(t-8)&15]^w[(t-14)&15]^w[(t-16)&15]; w[t&15]=ROTL(tmp,1); }
    if(t<20)      tmp=ROTL(a,5)+F1(b,c,d)+e+w[t&15]+K1;
    else if(t<40) tmp=ROTL(a,5)+F2(b,c,d)+e+w[t&15]+K2;
    else if(t<60) tmp=ROTL(a,5)+F3(b,c,d)+e+w[t&15]+K3;
    else          tmp=ROTL(a,5)+F4(b,c,d)+e+w[t&15]+K4;
    e=d; d=c; c=ROTL(b,30); b=a; a=tmp;
  }
  hash[0]=a+0x67452301u; hash[1]=b+0xEFCDAB89u; hash[2]=c+0x98BADCFEu; hash[3]=d+0x10325476u; hash[4]=e+0xC3D2E1F0u;
}

static void nonce_hex(u32_t nonce,u08_t *coin){
  static const char hx[]="0123456789abcdef"; int pos;
  for(pos=53; pos>=40; pos--){
    coin[pos^3]=hx[nonce & 0xF];
    nonce >>= 4;
    if(!nonce){ while(--pos>=40) coin[pos^3]=' '; break; }
  }
}

static u64_t g_attempts=0; static u32_t g_coins=0; static u32_t g_first_nonce=0; static u08_t g_first_bytes[55]; static u32_t g_first_len=0;
// Buffer to store textual representations of multiple coins (first 128) for verification
static u08_t g_coins_buf[8192]; // enough for ~128 lines of ~64 chars
static u32_t g_coins_buf_len=0;
static u32_t g_mask = 0xFFFFFFFFu; // difficulty mask (8 hex chars by default)
static u32_t g_hex_chars = 8;      // number of leading hex chars required (1..8)

EXPORT u64_t get_attempts(void){ return g_attempts; }
EXPORT u32_t get_coins_found(void){ return g_coins; }
EXPORT u32_t get_first_coin_nonce(void){ return g_first_nonce; }
EXPORT u32_t get_first_coin_ptr(void){ return g_first_len? (u32_t)(uintptr_t)g_first_bytes : 0; }
EXPORT u32_t get_first_coin_length(void){ return g_first_len; }
EXPORT u32_t get_coins_buffer_ptr(void){ return g_coins_buf_len? (u32_t)(uintptr_t)g_coins_buf : 0; }
EXPORT u32_t get_coins_buffer_length(void){ return g_coins_buf_len; }

EXPORT void set_difficulty(u32_t hex_chars){
  if(hex_chars<1) hex_chars=1; if(hex_chars>8) hex_chars=8;
  g_hex_chars = hex_chars;
  if(hex_chars==8) g_mask = 0xFFFFFFFFu; else g_mask = 0xFFFFFFFFu << ((8-hex_chars)*4);
}

EXPORT void search_coins(u32_t start_nonce,u32_t iterations,const char *prefix){
  g_attempts=0; g_coins=0; g_first_nonce=0; g_first_len=0; g_coins_buf_len=0;
  union { u08_t c[64]; u32_t i[16]; } coin; u32_t hash[5];
  memset(&coin,0,sizeof(coin));
  const char *hdr="DETI coin 2 ";
  for(int i=0;i<12;i++) coin.c[i^3]=(u08_t)hdr[i];
  u64_t seed = (u64_t)start_nonce * 6364136223846793005ULL + 1442695040888963407ULL;
  for(int i=12;i<40;i++){ seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; u08_t ch=(u08_t)(32 + (seed % 95)); if(ch=='\n') ch='_'; coin.c[i^3]=ch; }
  if(prefix){ int plen=0; while(prefix[plen] && plen<28) plen++; for(int i=0;i<plen;i++){ u08_t ch=(u08_t)prefix[i]; if(ch=='\n') ch='_'; coin.c[(12+i)^3]=ch; } }
  coin.c[54^3]='\n'; coin.c[55^3]=0x80; coin.i[15]=440;
  for(u32_t k=0;k<iterations;k++){
    u32_t nonce=start_nonce+k; nonce_hex(nonce,coin.c); sha1_55(coin.i,hash);
    if( (hash[0] & g_mask) == (DETI_COIN_SIGNATURE & g_mask) ){
      if(g_coins==0){ g_first_nonce=nonce; for(int b=0;b<55;b++) g_first_bytes[b]=coin.c[b^3]; g_first_len=55; }
      g_coins++;
      // Append textual line to buffer if space permits
      if(g_coins_buf_len < sizeof(g_coins_buf) - 70){
        g_coins_buf[g_coins_buf_len++]='V';
        g_coins_buf[g_coins_buf_len++]=(u08_t)('0'+(g_hex_chars/10));
        g_coins_buf[g_coins_buf_len++]=(u08_t)('0'+(g_hex_chars%10));
        g_coins_buf[g_coins_buf_len++]=':';
        for(int b=0;b<55;b++){ u08_t ch=coin.c[b^3]; if(ch=='\n') break; g_coins_buf[g_coins_buf_len++]=ch; }
        g_coins_buf[g_coins_buf_len++]='\n';
      }
    }
    g_attempts++;
  }
  // C-side fallback: if at least one coin found but buffer empty, add one line
  if(g_coins>0 && g_coins_buf_len==0 && g_first_len){
    g_coins_buf[g_coins_buf_len++]='V';
    g_coins_buf[g_coins_buf_len++]=(u08_t)('0'+(g_hex_chars/10));
    g_coins_buf[g_coins_buf_len++]=(u08_t)('0'+(g_hex_chars%10));
    g_coins_buf[g_coins_buf_len++]=':';
    for(u32_t b=0;b<g_first_len;b++){ u08_t ch=g_first_bytes[b]; if(ch=='\n') break; g_coins_buf[g_coins_buf_len++]=ch; }
    g_coins_buf[g_coins_buf_len++]='\n';
  }
}

// Simple pseudo-random nonce generator (xorshift-like LCG), exported for convenience.
EXPORT u32_t get_random_nonce(void){
  static u64_t x = 0x123456789ABCDEF0ull;
  x = x * 6364136223846793005ULL + 1442695040888963407ULL;
  return (u32_t)(x >> 32);
}
