#pragma once

#include <cstddef>
#include <cstdint>

// Convert a hex digit character [0-9 a-f A-F] to its value, or -1.
int hex2dec(int ch);

// Parse a full Intel HEX record from `hrec` into type/count/addr/data.
// `data` must point to a buffer of at least 256 bytes. Returns false on error.
bool intel_to_bin(const char *hrec, int *type, int *count, unsigned long *addr, uint8_t *data);

// Parse a Motorola S-record from `srec`. Returns false on error or unsupported type.
bool motorola_to_bin(const char *srec, int *type, int *count, unsigned long *addr, uint8_t *data);

// Decode `inlen` bytes of Base64 from `inbuf` into `outbuf`. Returns bytes written.
size_t base64_decode(const char *inbuf, size_t inlen, uint8_t *outbuf);

// Decode a UU-encoded line (including the leading length character) into `outbuf`.
// Returns bytes written, or 0 on error.
size_t uu_decode(const char *inbuf, size_t inlen, uint8_t *outbuf);
