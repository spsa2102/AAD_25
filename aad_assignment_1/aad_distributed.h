//
// Distributed DETI coin search - Protocol definitions
//
// Server coordinates multiple clients, distributing work ranges and collecting results
//

#ifndef AAD_DISTRIBUTED_H
#define AAD_DISTRIBUTED_H

#include <stdint.h>
#include "aad_data_types.h"

//
// Protocol constants
//
#define DETI_DEFAULT_PORT 9876
#define DETI_PROTOCOL_VERSION 1
#define WORK_RANGE_SIZE 1000000ULL  // Each client gets 1M nonces at a time

//
// Message types
//
typedef enum {
  MSG_CLIENT_HELLO = 1,      // Client -> Server: Initial connection
  MSG_SERVER_HELLO = 2,      // Server -> Client: Accept connection
  MSG_REQUEST_WORK = 3,      // Client -> Server: Request work range
  MSG_WORK_ASSIGNMENT = 4,   // Server -> Client: Assign nonce range
  MSG_REPORT_COIN = 5,       // Client -> Server: Found a coin
  MSG_WORK_COMPLETE = 6,     // Client -> Server: Finished assigned range
  MSG_NO_WORK = 7,           // Server -> Client: No more work available
  MSG_SHUTDOWN = 8,          // Server -> Client: Server shutting down
  MSG_PING = 9,              // Bidirectional: Keep-alive
  MSG_PONG = 10              // Bidirectional: Keep-alive response
} message_type_t;

//
// Client identification
//
typedef struct {
  char hostname[64];
  char client_type[32];  // "CPU", "SIMD", "SIMD+OpenMP", "CUDA", etc.
  uint32_t capabilities; // bitmask: CPU cores, GPU, SIMD support
  uint32_t version;      // protocol version
} client_info_t;

//
// Work assignment from server to client
//
typedef struct {
  uint64_t start_nonce;
  uint64_t end_nonce;      // exclusive
  uint32_t priority;       // for future use
  uint32_t work_id;        // unique work unit ID
} work_assignment_t;

//
// Coin report from client to server
//
typedef struct {
  uint64_t nonce;
  uint32_t zeros;          // number of leading zero bits
  uint32_t work_id;        // which work assignment found this
  u32_t coin_data[14];     // the actual coin data
  u32_t hash[5];           // SHA1 hash
} coin_report_t;

//
// Work completion report
//
typedef struct {
  uint32_t work_id;
  uint64_t nonces_tested;
  uint32_t coins_found;
  double elapsed_time;     // seconds
} work_completion_t;

//
// Protocol message header
//
typedef struct {
  uint32_t magic;          // 0xDET1C01N
  uint16_t version;        // protocol version
  uint16_t type;           // message_type_t
  uint32_t length;         // payload length in bytes
  uint32_t checksum;       // simple checksum of payload
} message_header_t;

#define PROTOCOL_MAGIC 0xDEA1C01Eu

//
// Helper functions
//

static inline uint32_t simple_checksum(const void *data, size_t len)
{
  uint32_t sum = 0;
  const uint8_t *p = (const uint8_t *)data;
  for(size_t i = 0; i < len; i++)
    sum = (sum << 1) ^ p[i];
  return sum;
}

static inline void init_message_header(message_header_t *hdr, message_type_t type, uint32_t payload_len)
{
  hdr->magic = PROTOCOL_MAGIC;
  hdr->version = DETI_PROTOCOL_VERSION;
  hdr->type = (uint16_t)type;
  hdr->length = payload_len;
  hdr->checksum = 0; // set later
}

#endif // AAD_DISTRIBUTED_H
