/* ==========================================================================
 * JSON_StringEscape.h -- the JSON string-escaping helper for JSON_Encoder.c
 *
 * Split out of JSON_Encoder.c for #164, which stopped the encoder writing a
 * free-form string (the WiFi SSID, and defensively the friendly device name)
 * straight onto the wire with a raw "%s". SCPI_SafeParamString() (SCPILAN.c)
 * does a bare memcpy with no character validation, so a '"' or '\' in the
 * SSID landed unescaped and produced a JSON document no client could parse.
 *
 * Header-only and dependency-free ON PURPOSE, the same way
 * HAL/ADC/AD7609Scale.h is (#889) and Util/FixedPointFmt.h is (#250).
 * JSON_Encoder.c pulls in state/board/BoardConfig.h and friends, which drag
 * in the whole Harmony/board graph; a host test cannot include it without
 * faking so much of that graph that the function under test stops
 * resembling the one that ships. Keeping the pure escaping logic here means
 * tests/host/test_json_string_escape.c compiles the EXACT text XC32
 * compiles into the firmware, with no stubs.
 *
 * SCOPE: pure byte-in, byte-out string transformation only -- no hardware
 * access, no logging, no board or runtime config lookups. JSON_Encoder.c
 * includes this header, so its existing callers are unchanged.
 * ========================================================================== */
#ifndef JSON_STRINGESCAPE_H
#define JSON_STRINGESCAPE_H

#include <stddef.h>

/* #164: longest source string escape_json_string() is asked to convert -- the
 * WiFi SSID (WDRV_WINC_MAX_SSID_LEN = 32) and the friendly device name
 * (FRIENDLY_DEVICE_NAME_SIZE - 1 = 31 payload bytes). Worst case is every
 * byte becoming a 6-character \uXXXX escape, plus the NUL. A longer source is
 * not a hazard: escape_json_string() bounds-checks and the caller then omits
 * the field. */
#define JSON_ESC_SRC_MAX_LEN  32u
#define JSON_ESC_MAX_LEN      ((JSON_ESC_SRC_MAX_LEN * 6u) + 1u)    /* 193 */

/**
 * @brief Copy @p in into @p out, escaping everything JSON forbids raw (#164).
 *
 * RFC 8259 section 7 requires '"', '\' and U+0000..U+001F to be escaped. This
 * helper goes one step further and escapes EVERY byte outside printable ASCII
 * (0x20..0x7E) as \uXXXX, because a JSON document must also be valid UTF-8 and
 * an 802.11 SSID is an opaque octet string that may carry arbitrary bytes -- a
 * raw 0x80..0xFF byte would make the whole stream unparseable, not merely
 * render oddly. The cost is that a genuinely UTF-8 SSID renders as its
 * individual bytes' codepoints rather than the intended glyphs; a parseable
 * stream is worth more than a pretty one, and the SSID reaches the device over
 * an ASCII SCPI channel in the first place.
 *
 * Only @p inLen bytes are read, so a source that is not NUL-terminated within
 * its declared field width cannot be over-read (the ssid caller previously
 * clamped its length check but then handed the unclamped array to "%s").
 *
 * @param in      Source bytes (need not be NUL-terminated within @p inLen)
 * @param inLen   Number of source bytes to convert
 * @param out     Destination buffer
 * @param outSize Size of @p out in bytes, including the NUL terminator
 * @return Characters written to @p out excluding the NUL, or 0 if the escaped
 *         form does not fit -- in which case @p out is left empty and the
 *         caller omits the field, matching the "optional field - skip on
 *         buffer full" convention used throughout this encoder.
 */
static inline size_t escape_json_string(const char* in, size_t inLen,
        char* out, size_t outSize) {
    static const char kHex[] = "0123456789ABCDEF";
    size_t o = 0;
    size_t k;

    if (out == NULL || outSize == 0) {
        return 0;
    }
    out[0] = '\0';
    if (in == NULL) {
        return 0;
    }

    for (k = 0; k < inLen; ++k) {
        unsigned char c = (unsigned char) in[k];
        char esc[6];
        size_t escLen;
        size_t m;

        switch (c) {
            case '"':  esc[0] = '\\'; esc[1] = '"';  escLen = 2; break;
            case '\\': esc[0] = '\\'; esc[1] = '\\'; escLen = 2; break;
            case '\b': esc[0] = '\\'; esc[1] = 'b';  escLen = 2; break;
            case '\f': esc[0] = '\\'; esc[1] = 'f';  escLen = 2; break;
            case '\n': esc[0] = '\\'; esc[1] = 'n';  escLen = 2; break;
            case '\r': esc[0] = '\\'; esc[1] = 'r';  escLen = 2; break;
            case '\t': esc[0] = '\\'; esc[1] = 't';  escLen = 2; break;
            default:
                if (c >= 0x20u && c <= 0x7Eu) {
                    esc[0] = (char) c;
                    escLen = 1;
                } else {
                    esc[0] = '\\';
                    esc[1] = 'u';
                    esc[2] = '0';
                    esc[3] = '0';
                    esc[4] = kHex[(c >> 4) & 0x0Fu];
                    esc[5] = kHex[c & 0x0Fu];
                    escLen = 6;
                }
                break;
        }

        /* +1 keeps room for the NUL terminator. */
        if ((o + escLen + 1u) > outSize) {
            out[0] = '\0';
            return 0;
        }
        for (m = 0; m < escLen; ++m) {
            out[o++] = esc[m];
        }
    }

    out[o] = '\0';
    return o;
}

#endif /* JSON_STRINGESCAPE_H */
