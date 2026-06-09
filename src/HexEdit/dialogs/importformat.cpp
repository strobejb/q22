#include "importformat.h"

#include <cctype>

// ── Hex digit helpers ─────────────────────────────────────────────────────────

int hex2dec(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

// Parse two hex digits from s into *val, accumulate into *checksum. Returns s+2 or nullptr.
static const char *getbyte(const char *s, unsigned *val, unsigned *checksum = nullptr)
{
    if (!std::isxdigit((unsigned char)s[0]) || !std::isxdigit((unsigned char)s[1]))
        return nullptr;
    *val = (unsigned)(hex2dec(s[0]) << 4) | (unsigned)hex2dec(s[1]);
    if (checksum)
        *checksum += *val;
    return s + 2;
}

// ── Intel HEX record parser ───────────────────────────────────────────────────

bool intel_to_bin(const char *hrec, int *type, int *count, unsigned long *addr, uint8_t *data)
{
    if (*hrec++ != ':')
        return false;

    unsigned    checksum = 0, val = 0;
    const char *p = hrec;

    if (!(p = getbyte(p, (unsigned *)count, &checksum)))
        return false;
    if (!(p = getbyte(p, &val, &checksum)))
        return false;
    *addr = val << 8;
    if (!(p = getbyte(p, &val, &checksum)))
        return false;
    *addr |= val;
    if (!(p = getbyte(p, (unsigned *)type, &checksum)))
        return false;

    for (int i = 0; i < *count; i++)
    {
        if (!(p = getbyte(p, &val, &checksum)))
            return false;
        data[i] = (uint8_t)val;
    }

    // Read but don't verify checksum
    if (!(p = getbyte(p, &val)))
        return false;
    return true;
}

// ── Motorola S-record parser ──────────────────────────────────────────────────

// Address-field width in bytes per record type (0 = unsupported)
static const int s_addrlen[10] = {2, 2, 3, 4, 0, 2, 0, 4, 3, 2};

bool motorola_to_bin(const char *srec, int *type, int *count, unsigned long *addr, uint8_t *data)
{
    if (*srec++ != 'S')
        return false;
    *type = *srec++ - '0';
    if (*type < 0 || *type > 9 || s_addrlen[*type] == 0)
        return false;

    unsigned    checksum = 0, val = 0;
    const char *p         = srec;
    unsigned    bytecount = 0;

    if (!(p = getbyte(p, &bytecount, &checksum)))
        return false;
    *count = (int)bytecount - s_addrlen[*type] - 1;

    *addr = 0;
    for (int i = 0; i < s_addrlen[*type]; i++)
    {
        if (!(p = getbyte(p, &val, &checksum)))
            return false;
        *addr = (*addr << 8) | val;
    }

    for (int i = 0; i < *count; i++)
    {
        if (!(p = getbyte(p, &val, &checksum)))
            return false;
        data[i] = (uint8_t)val;
    }

    // Read but don't verify checksum
    if (!(p = getbyte(p, &val)))
        return false;
    return true;
}

// ── Base64 / UUEncode decode ──────────────────────────────────────────────────
//  Ported from base64.c

// clang-format off
static const char b64rev[128] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
};

static const char uuerev[128] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
    16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
    32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
    48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
     0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

// clang-format on

static size_t decode64(const char *inbuf, size_t inlen, uint8_t *outbuf, const char *table)
{
    size_t outlen = 0;
    size_t i      = 0;

    while (i + 3 < inlen)
    {
        if (inbuf[i] == '\r' || inbuf[i] == '\n')
        {
            i++;
            continue;
        }
        if (inbuf[i] == '\0')
            break;

        uint8_t in[4] = {0, 0, 0, 0};
        int     valid = 0;
        for (int k = 0; k < 4 && i < inlen; k++, i++)
        {
            int v = (unsigned char)inbuf[i];
            if (v > 127 || table[v] == -1)
                break;
            if (table[v] == -2)
                break; // padding char '='
            in[k] = (uint8_t)table[v];
            valid = k + 1;
        }
        if (valid < 2)
            break;

        outbuf[outlen++] = (uint8_t)((in[0] << 2) | (in[1] >> 4));
        if (valid >= 3)
            outbuf[outlen++] = (uint8_t)((in[1] << 4) | (in[2] >> 2));
        if (valid >= 4)
            outbuf[outlen++] = (uint8_t)((in[2] << 6) | in[3]);
    }
    return outlen;
}

size_t base64_decode(const char *inbuf, size_t inlen, uint8_t *outbuf)
{
    return decode64(inbuf, inlen, outbuf, b64rev);
}

size_t uu_decode(const char *inbuf, size_t inlen, uint8_t *outbuf)
{
    if (inlen < 1)
        return 0;
    int linelen = (unsigned char)inbuf[0];
    if (linelen > 127 || uuerev[linelen] == -1)
        return 0;
    return decode64(inbuf + 1, inlen - 1, outbuf, uuerev);
}
