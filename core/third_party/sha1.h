#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
} SHA1_CTX;

void SHA1Init(SHA1_CTX *ctx);
void SHA1Update(SHA1_CTX *ctx, const uint8_t *data, size_t len);
void SHA1Final(uint8_t digest[20], SHA1_CTX *ctx);

/* Convenience: hash data in one call */
void sha1(const uint8_t *data, size_t len, uint8_t digest[20]);

/* Base64 encode src (len bytes) into dst. dst must be >= ((len+2)/3)*4+1 bytes. */
void base64_encode(const uint8_t *src, size_t len, char *dst);
