#include "exportformat.h"

// ── Base64 / UUEncode ────────────────────────────────────────────────────────
//  Ported from base64.c (Bob Trower / catch22)

static const char b64table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char uuetable[65] = " !\"#$%&'()*+,-./"
                                 "0123456789:;<=>?"
                                 "@ABCDEFGHIJKLMNO"
                                 "PQRSTUVWXYZ[\\]^_";

static size_t encode64(const uint8_t *inbuf, size_t inlen, char *outbuf, const char *table, int deadchar)
{
    size_t outlen = 0;

    while (inlen)
    {
        size_t  len   = (inlen < 3) ? inlen : 3;
        uint8_t in[3] = {0, 0, 0};

        for (size_t i = 0; i < len; i++)
            in[i] = *inbuf++;

        if (outbuf)
        {
            outbuf[0] = table[in[0] >> 2];
            outbuf[1] = table[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
            outbuf[2] = table[((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6)];
            outbuf[3] = table[in[2] & 0x3f];

            switch (len)
            {
            case 1:
                outbuf[2] = (char)deadchar; // fall through
            case 2:
                outbuf[3] = (char)deadchar;
                break;
            default:
                break;
            }
            outbuf += 4;
        }

        outlen += 4;
        inlen  -= len;
    }

    return outlen;
}

size_t base64_encode(const uint8_t *inbuf, size_t inlen, char *outbuf)
{
    return encode64(inbuf, inlen, outbuf, b64table, '=');
}

size_t uu_encode(const uint8_t *inbuf, size_t inlen, char *outbuf)
{
    if (outbuf)
        *outbuf++ = uuetable[inlen];

    return encode64(inbuf, inlen, outbuf, uuetable, '`') + 1;
}

// ── Intel hex-record builder ─────────────────────────────────────────────────
//  Ported from intel.c

static int fmtbyte_intel(char *s, unsigned val, unsigned *checksum)
{
    static const char hex[] = "0123456789ABCDEF";
    s[0]                    = hex[(val >> 4) & 0x0f];
    s[1]                    = hex[val & 0x0f];
    if (checksum)
        *checksum += val;
    return 2;
}

size_t intel_frame(char *hrec, int type, size_t count, unsigned long addr, const uint8_t *data)
{
    char    *h        = hrec;
    unsigned checksum = 0;

    *h++  = ':';
    h    += fmtbyte_intel(h, (unsigned)count, &checksum);
    h    += fmtbyte_intel(h, (addr >> 8) & 0xff, &checksum);
    h    += fmtbyte_intel(h, addr & 0xff, &checksum);
    h    += fmtbyte_intel(h, type, &checksum);

    for (size_t i = 0; i < count; i++)
        h += fmtbyte_intel(h, data[i], &checksum);

    h += fmtbyte_intel(h, (~checksum + 1) & 0xff, nullptr);

    return (size_t)(h - hrec);
}

// ── Motorola S-record builder ─────────────────────────────────────────────────
//  Ported from motorola.c

static int fmtbyte_moto(char *s, unsigned val, unsigned *checksum)
{
    static const char hex[] = "0123456789ABCDEF";
    s[0]                    = hex[(val >> 4) & 0x0f];
    s[1]                    = hex[val & 0x0f];
    if (checksum)
        *checksum += val;
    return 2;
}

// Address-field width in bytes per record type
static const int s_sizelook[10] = {2, 2, 3, 4, 0, 2, 0, 4, 3, 2};

size_t motorola_frame(char *srec, int type, size_t count, unsigned long addr, const uint8_t *data)
{
    char    *s        = srec;
    unsigned checksum = 0;

    if (type < 0 || type > 9)
        return 0;

    *s++ = 'S';
    *s++ = (char)('0' + type);

    s += fmtbyte_moto(s, s_sizelook[type] + (unsigned)count + 1, &checksum);

    switch (type)
    {
    case 3:
    case 7:
        s += fmtbyte_moto(s, (addr >> 24) & 0xff, &checksum);
        // fall through
    case 2:
    case 8:
        s += fmtbyte_moto(s, (addr >> 16) & 0xff, &checksum);
        // fall through
    case 0:
    case 1:
    case 5:
    case 9:
        s += fmtbyte_moto(s, (addr >> 8) & 0xff, &checksum);
        s += fmtbyte_moto(s, addr & 0xff, &checksum);
        break;
    }

    for (size_t i = 0; i < count; i++)
        s += fmtbyte_moto(s, data[i], &checksum);

    s += fmtbyte_moto(s, ~checksum & 0xff, nullptr);

    return (size_t)(s - srec);
}

// ── Hex-dump helper ──────────────────────────────────────────────────────────

char toAscii(uint8_t b)
{
    return (b >= 0x20 && b < 0x7f) ? (char)b : '.';
}
