#pragma once

#include <cstddef>
#include <cstdint>

// Encode `inlen` bytes from `inbuf` as Base64 into `outbuf` (caller-allocated,
// needs at least ceil(inlen/3)*4 bytes). Returns the number of chars written.
// outbuf is NOT null-terminated.
size_t base64_encode(const uint8_t *inbuf, size_t inlen, char *outbuf);

// UU-encode `inlen` bytes from `inbuf` into `outbuf`. The output is prefixed
// with the UU length character. Returns the total number of chars written.
size_t uu_encode(const uint8_t *inbuf, size_t inlen, char *outbuf);

// Build an Intel HEX record into `hrec`. Returns the number of chars written.
// `data` may be nullptr when count == 0 (e.g. for EOF/address records).
size_t intel_frame(char *hrec, int type, size_t count, unsigned long addr, const uint8_t *data);

// Build a Motorola S-record into `srec`. Returns the number of chars written,
// or 0 for an invalid type. `data` may be nullptr when count == 0.
size_t motorola_frame(char *srec, int type, size_t count, unsigned long addr, const uint8_t *data);

// Map a byte value to its printable ASCII representation, substituting '.'
// for non-printable bytes (< 0x20 or >= 0x7F).
char toAscii(uint8_t b);
