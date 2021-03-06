#include "flatcc/flatcc_rtconfig.h"
#include "flatcc/flatcc_json_parser.h"

#if FLATCC_USE_GRISU3 && !defined(PORTABLE_USE_GRISU3)
#define PORTABLE_USE_GRISU3 1
#endif
#include "flatcc/portable/pparsefp.h"

#if FLATCC_USE_SSE4_2
#ifdef __SSE4_2__
#define USE_SSE4_2
#endif
#endif

#ifdef USE_SSE4_2
#include <nmmintrin.h>
#define cmpistri(end, haystack, needle, flags)                              \
        if (end - haystack >= 16) do {                                      \
        int i;                                                              \
        __m128i a = _mm_loadu_si128((const __m128i *)(needle));             \
        do {                                                                \
            __m128i b = _mm_loadu_si128((const __m128i *)(haystack));       \
            i = _mm_cmpistri(a, b, flags);                                  \
            haystack += i;                                                  \
        } while (i == 16 && end - haystack >= 16);                          \
        } while(0)
#endif

const char *flatcc_json_parser_error_string(int err)
{
    switch (err) {
#define XX(no, str)                                                         \
    case flatcc_json_parser_error_##no:                                     \
        return str;
        FLATCC_JSON_PARSE_ERROR_MAP(XX)
#undef XX
    default:
        return "unknown";
    }
}

const char *flatcc_json_parser_set_error(flatcc_json_parser_t *ctx, const char *loc, const char *end, int err)
{
    if (!ctx->error) {
        ctx->error = err;
        ctx->pos = (int)(loc - ctx->line_start + 1);
        ctx->error_loc = loc;
    }
    return end;
}

const char *flatcc_json_parser_string_part(flatcc_json_parser_t *ctx, const char *buf, const char *end)
{
/*
 * Disabled because it doesn't catch all control characters, but is
 * useful for performance testing.
 */
#if 0
//#ifdef USE_SSE4_2
    cmpistri(end, buf, "\"\\\0\r\n\t\v\f", _SIDD_POSITIVE_POLARITY);
#else
    /*
     * Testing for signed char >= 0x20 would also capture UTF-8
     * encodings that we could verify, and also invalid encodings like
     * 0xff, but we do not wan't to enforce strict UTF-8.
     */
    while (buf != end && *buf != '\"' && ((unsigned char)*buf) >= 0x20 && *buf != '\\') {
        ++buf;
    }
#endif
    if (buf == end) {
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_unterminated_string);
    }
    if (*buf == '"') {
        return buf;
    }
    if (*buf < 0x20) {
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_character);
    }
    return buf;
}

const char *flatcc_json_parser_space_ext(flatcc_json_parser_t *ctx, const char *buf, const char *end)
{
again:
#ifdef USE_SSE4_2
    /*
     * We can include line break, but then error reporting suffers and
     * it really makes no big difference.
     */
    //cmpistri(end, buf, "\x20\t\v\f\r\n", _SIDD_NEGATIVE_POLARITY);
    cmpistri(end, buf, "\x20\t\v\f", _SIDD_NEGATIVE_POLARITY);
#else
#if FLATCC_ALLOW_UNALIGNED_ACCESS
    while (end - buf >= 16) {
        if (*buf > 0x20) {
            return buf;
        }
#if FLATCC_JSON_PARSE_WIDE_SPACE
        if (((uint64_t *)buf)[0] != 0x2020202020202020) {
descend:
            if (((uint32_t *)buf)[0] == 0x20202020) {
                buf += 4;
            }
#endif
            if (((uint16_t *)buf)[0] == 0x2020) {
                buf += 2;
            }
            if (*buf == 0x20) {
                ++buf;
            }
            if (*buf > 0x20) {
                return buf;
            }
            break;
#if FLATCC_JSON_PARSE_WIDE_SPACE
        }
        if (((uint64_t *)buf)[1] != 0x2020202020202020) {
            buf += 8;
            goto descend;
        }
        buf += 16;
#endif
    }
#endif
#endif
    while (buf != end && *buf == 0x20) {
        ++buf;
    }
    while (buf != end && *buf <= 0x20) {
        switch (*buf) {
        case 0x0d: buf += (end - buf > 1 && buf[1] == 0x0a);
            /* Fall through consuming following LF or treating CR as LF. */
        case 0x0a: ++ctx->line; ctx->line_start = ++buf; continue;
        case 0x09: ++buf; continue;
        case 0x20: goto again; /* Don't consume here, sync with power of 2 spaces. */
        default: return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_unexpected_character);
        }
    }
    return buf;
}

const char *flatcc_json_parser_string_escape(flatcc_json_parser_t *ctx, const char *buf, const char *end, flatcc_json_parser_escape_buffer_t code)
{
    char c, v;
    unsigned short u, x;

    if (end - buf < 2 || buf[0] != '\\') {
        code[0] = 0;
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
    }
    switch (buf[1]) {
    case 'x':
        v = 0;
        code[0] = 1;
        if (end - buf < 4) {
            code[0] = 0;
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
        }
        c = buf[2];
        if (c >= '0' && c <= '9') {
            v |= (c - '0') << 4;
        } else {
            /* Lower case. */
            c |= 0x20;
            if (c >= 'a' && c <= 'f') {
                v |= (c - 'a' + 10) << 4;
            } else {
                code[0] = 0;
                return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
            }
        }
        c = buf[3];
        if (c >= '0' && c <= '9') {
            v |= c - '0';
        } else {
            /* Lower case. */
            c |= 0x20;
            if (c >= 'a' && c <= 'f') {
                v |= c - 'a' + 10;
            } else {
                code[0] = 0;
                return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
            }
        }
        code[1] = v;
        return buf + 4;
    case 'u':
        if (end - buf < 6) {
            code[0] = 0;
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
        }
        u = 0;
        c = buf[2];
        if (c >= '0' && c <= '9') {
            x = c - '0';
            u = x << 12;
        } else {
            /* Lower case. */
            c |= 0x20;
            if (c >= 'a' && c <= 'f') {
                x = c - 'a' + 10;
                u |= x << 12;
            } else {
                code[0] = 0;
                return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
            }
        }
        c = buf[3];
        if (c >= '0' && c <= '9') {
            x = c - '0';
            u |= x << 8;
        } else {
            /* Lower case. */
            c |= 0x20;
            if (c >= 'a' && c <= 'f') {
                x = c - 'a' + 10;
                u |= x << 8;
            } else {
                code[0] = 0;
                return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
            }
        }
        c = buf[4];
        if (c >= '0' && c <= '9') {
            x = c - '0';
            u |= x << 4;
        } else {
            /* Lower case. */
            c |= 0x20;
            if (c >= 'a' && c <= 'f') {
                x = c - 'a' + 10;
                u |= x << 4;
            } else {
                code[0] = 0;
                return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
            }
        }
        c = buf[5];
        if (c >= '0' && c <= '9') {
            x = c - '0';
            u |= x;
        } else {
            /* Lower case. */
            c |= 0x20;
            if (c >= 'a' && c <= 'f') {
                x = c - 'a' + 10;
                u |= x;
            } else {
                code[0] = 0;
                return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
            }
        }
        if (u <= 0x7f) {
            code[0] = 1;
            code[1] = (char)u;
        } else if (u <= 0x7ff) {
            code[0] = 2;
            code[1] = (char)(0xc0 | (u >> 6));
            code[2] = (char)(0x80 | (u & 0x3f));
        } else {
            code[0] = 3;
            code[1] = (char)(0xe0 | (u >> 12));
            code[2] = (char)(0x80 | ((u >> 6) & 0x3f));
            code[3] = (char)(0x80 | (u & 0x3f));
            /* We do not report failure on invalid unicode range. */
        }
        return buf + 6;
    case 't':
        code[0] = 1;
        code[1] = '\t';
        return buf + 2;
    case 'n':
        code[0] = 1;
        code[1] = '\n';
        return buf + 2;
    case 'r':
        code[0] = 1;
        code[1] = '\r';
        return buf + 2;
    case 'b':
        code[0] = 1;
        code[1] = '\b';
        return buf + 2;
    case 'f':
        code[0] = 1;
        code[1] = '\f';
        return buf + 2;
    case '\"':
        code[0] = 1;
        code[1] = '\"';
        return buf + 2;
    case '\\':
        code[0] = 1;
        code[1] = '\\';
        return buf + 2;
    case '/':
        code[0] = 1;
        code[1] = '/';
        return buf + 2;
    default:
        code[0] = 0;
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
    }
}

/* Only applies to unquoted constants during generic parsring, otherwise it is skipped as a string. */
const char *flatcc_json_parser_skip_constant(flatcc_json_parser_t *ctx, const char *buf, const char *end)
{
    char c;
    const char *k;

    while (buf != end) {
        c = *buf;
        if ((c & 0x80) || (c == '_') || (c >= '0' && c <= '9') || c == '.') {
            ++buf;
            continue;
        }
        /* Upper case. */
        c |= 0x20;
        if (c >= 'a' && c <= 'z') {
            ++buf;
            continue;
        }
        buf = flatcc_json_parser_space(ctx, (k = buf), end);
        if (buf == k) {
            return buf;
        }
    }
    return buf;
}

const char *flatcc_json_parser_match_constant(flatcc_json_parser_t *ctx, const char *buf, const char *end, int pos, int *more)
{
    const char *mark = buf, *k = buf + pos;

    if (end - buf <= pos) {
        *more = 0;
        return buf;
    }
#if FLATCC_JSON_PARSE_ALLOW_UNQUOTED
    if (ctx->unquoted) {
        buf = flatcc_json_parser_space(ctx, k, end);
        if (buf == end) {
            /*
             * We cannot make a decision on more.
             * Just return end and let parser handle sync point in
             * case it is able to resume parse later on.
             * For the same reason we do not lower ctx->unquoted.
             */
            *more = 0;
            return buf;
        }
        if (buf != k) {
            char c = *buf;
            /*
             * Space was seen - and thus we have a valid match.
             * If the next char is an identifier start symbol
             * we raise the more flag to support syntax like:
             *
             *     `flags: Hungry Sleepy Awake, ...`
             */
            if (c == '_' || (c & 0x80)) {
                *more = 1;
                return buf;
            }
            c |= 0x20;
            if (c >= 'a' && c <= 'z') {
                *more = 1;
                return buf;
            }
        }
        /*
         * Space was not seen, so the match is only valid if followed
         * by a JSON separator symbol, and there cannot be more values
         * following so `more` is lowered.
         */
        *more = 0;
        if (*buf == ',' || *buf == '}' || *buf == ']') {
            return buf;
        }
        return mark;
    }
#endif
    buf = k;
    if (*buf == 0x20) {
        ++buf;
        while (buf != end && *buf == 0x20) {
            ++buf;
        }
        if (buf == end) {
            *more = 0;
            return buf;
        }
        /* We accept untrimmed space like "  Green  Blue  ". */
        if (*buf != '\"') {
            *more = 1;
            return buf;
        }
    }
    switch (*buf) {
    case '\\':
        *more = 0;
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_escape);
    case '\"':
        buf = flatcc_json_parser_space(ctx, buf + 1, 0);
        *more = 0;
        return buf;
    }
    *more = 0;
    return mark;
}

const char *flatcc_json_parser_unmatched_symbol(flatcc_json_parser_t *ctx, const char *buf, const char *end)
{
    if (ctx->flags & flatcc_json_parser_f_skip_unknown) {
        buf = flatcc_json_parser_symbol_end(ctx, buf, end);
        buf = flatcc_json_parser_space(ctx, buf, end);
        if (buf != end && *buf == ':') {
            ++buf;
            buf = flatcc_json_parser_space(ctx, buf, end);
        } else {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_expected_colon);
        }
        return flatcc_json_parser_generic_json(ctx, buf, end);
    } else {
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_unknown_symbol);
    }
}

static const char *__flatcc_json_parser_number(flatcc_json_parser_t *ctx, const char *buf, const char *end)
{
    if (buf == end) {
        return buf;
    }
    if (*buf == '-') {
        ++buf;
        if (buf == end) {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_numeric);
        }
    }
    if (*buf == '0') {
        ++buf;
    } else {
        if (*buf < '1' || *buf > '9') {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_numeric);
        }
        ++buf;
        while (buf != end && *buf >= '0' && *buf <= '9') {
            ++buf;
        }
    }
    if (buf != end) {
        if (*buf == '.') {
            ++buf;
            if (*buf < '0' || *buf > '9') {
                return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_numeric);
            }
            ++buf;
            while (buf != end && *buf >= '0' && *buf <= '9') {
                ++buf;
            }
        }
    }
    if (buf != end && (*buf == 'e' || *buf == 'E')) {
        ++buf;
        if (buf == end) {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_numeric);
        }
        if (*buf == '+' || *buf == '-') {
            ++buf;
        }
        if (buf == end || *buf < '0' || *buf > '9') {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_numeric);
        }
        ++buf;
        while (buf != end && *buf >= '0' && *buf <= '9') {
            ++buf;
        }
    }

    /*
     * For strtod termination we must ensure the tail is not valid
     * including non-json exponent types. The simplest approach is
     * to accept anything that could be valid json successor
     * characters and reject end of buffer since we expect a closing
     * '}'.
     *
     * The ',' is actually not safe if strtod uses a non-POSIX locale.
     */
    if (buf != end) {
        switch (*buf) {
        case ',':
        case ':':
        case ']':
        case '}':
        case ' ':
        case '\r':
        case '\t':
        case '\n':
        case '\v':
            return buf;
        }
    }
    return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_numeric);
}

const char *flatcc_json_parser_double(flatcc_json_parser_t *ctx, const char *buf, const char *end, double *v)
{
    *v = 0.0;
    buf = parse_double(buf, (int)(end - buf), v);
    if (buf == 0) {
        if (isinf(*v)) {
            if (*v >= 0.0) {
                flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_overflow);
            } else {
                flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_underflow);
            }
        } else {
            flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_numeric);
        }
        return end;
    }
    return buf;
}

const char *flatcc_json_parser_float(flatcc_json_parser_t *ctx, const char *buf, const char *end, float *v)
{
    *v = 0.0;
    buf = parse_float(buf, (int)(end - buf), v);
    if (buf == 0) {
        if (isinf(*v)) {
            if (*v >= 0.0) {
                flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_overflow);
            } else {
                flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_underflow);
            }
        } else {
            flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_invalid_numeric);
        }
        return end;
    }
    return buf;
}

const char *flatcc_json_parser_generic_json(flatcc_json_parser_t *ctx, const char *buf, const char *end)
{
    char stack[FLATCC_JSON_PARSE_GENERIC_MAX_NEST];
    char *sp, *spend;
    const char *k;
    char code[4];
    int more = 0;

    sp = stack;
    spend = sp + FLATCC_JSON_PARSE_GENERIC_MAX_NEST;

again:
    if (buf == end) {
        return buf;
    }
    if (sp != stack && sp[-1] == '}') {
        /* Inside an object, about to read field name. */
        buf = flatcc_json_parser_symbol_start(ctx, buf, end);
        buf = flatcc_json_parser_symbol_end(ctx, buf, end);
        buf = flatcc_json_parser_space(ctx, buf, end);
        if (buf == end) {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_unbalanced_object);
        }
        if (*buf != ':') {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_expected_colon);
        }
        buf = flatcc_json_parser_space(ctx, buf + 1, end);
    }
    switch (*buf) {
    case '\"':
        buf = flatcc_json_parser_string_start(ctx, buf, end);
        while (buf != end && *buf != '\"') {
            buf = flatcc_json_parser_string_part(ctx, buf, end);
            if (buf != end && *buf == '\"') {
                break;
            }
            buf = flatcc_json_parser_string_escape(ctx, buf, end, code);
        }
        buf = flatcc_json_parser_string_end(ctx, buf, end);
        break;
    case '-':
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        buf = __flatcc_json_parser_number(ctx, buf, end);
        break;
#if !FLATCC_JSON_PARSE_ALLOW_UNQUOTED
    case 't': case 'f':
        {
            uint8_t v;
            buf = flatcc_json_parser_bool(ctx, (k = buf), end, &v);
            if (k == buf) {
                return flatcc_json_parser_set_error(ctx, buf, end, end, flatcc_json_parser_error_unexpected_character);
            }
        }
        break;
    case 'n':
        buf = flatcc_json_parser_null(ctx, (k = buf), end);
        if (k == buf) {
            return flatcc_json_parser_set_error(ctx, buf, end, end, flatcc_json_parser_error_unexpected_character);
        }
        break;
#endif
    case '[':
        if (sp == spend) {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_deep_nesting);
        }
        *sp++ = ']';
        buf = flatcc_json_parser_space(ctx, buf + 1, end);
        if (buf != end && *buf == ']') {
            break;
        }
        goto again;
    case '{':
        if (sp == spend) {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_deep_nesting);
        }
        *sp++ = '}';
        buf = flatcc_json_parser_space(ctx, buf + 1, end);
        if (buf != end && *buf == '}') {
            break;
        }
        goto again;

    default:
#if FLATCC_JSON_PARSE_ALLOW_UNQUOTED
        buf = flatcc_json_parser_skip_constant(ctx, (k = buf), end);
        if (k == buf) {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_unexpected_character);
        }
        break;
#else
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_unexpected_character);
#endif
    }
    while (buf != end && sp != stack) {
        --sp;
        if (*sp == ']') {
            buf = flatcc_json_parser_array_end(ctx, buf, end, &more);
        } else {
            buf = flatcc_json_parser_object_end(ctx, buf, end, &more);
        }
        if (more) {
            ++sp;
            goto again;
        }
    }
    if (buf == end && sp != stack) {
        return flatcc_json_parser_set_error(ctx, buf, end, sp[-1] == ']' ?
                flatcc_json_parser_error_unbalanced_array :
                flatcc_json_parser_error_unbalanced_object);
    }
    /* Any ',', ']', or '}' belongs to parent context. */
    return buf;
}

const char *flatcc_json_parser_integer(flatcc_json_parser_t *ctx, const char *buf, const char *end,
        int *value_sign, uint64_t *value)
{
    uint64_t x0, x = 0;
    const char *k;

    if (buf == end) {
        return buf;
    }
    k = buf;
    *value_sign = *buf == '-';
    buf += *value_sign;
    while (buf != end && *buf >= '0' && *buf <= '9') {
        x0 = x;
        x = x * 10 + *buf - '0';
        if (x0 > x) {
            return flatcc_json_parser_set_error(ctx, buf, end, value_sign ?
                    flatcc_json_parser_error_underflow : flatcc_json_parser_error_overflow);
        }
        ++buf;
    }
    if (buf == k) {
        /* Give up, but don't fail the parse just yet, it might be a valid symbol. */
        return buf;
    }
    if (buf != end && (*buf == 'e' || *buf == 'E' || *buf == '.')) {
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_float_unexpected);
    }
    *value = x;
    return buf;
}


/* UNIONS */

/*
 * Unions are difficult to parse because the type field may appear after
 * the union table and because having two fields opens up for many more
 * possible error scenarios. We must store each union of a table
 * temporarily - this cannot be in the generated table parser function
 * because there could be many unions (about 2^15 with default voffsets)
 * although usually there will be only a few. We can also not store the
 * data encoded in the existing table buffer in builder because we may
 * have to remove it due to schema forwarding and removing it messes up
 * the table layout. We also cannot naively allocate it dynamically for
 * performance reasons. Instead we place the temporary union data in a
 * separate frame from the table buffer, but on a similar stack. This is
 * called the user stack and we manage one frame per table that is known
 * to contain unions.
 *
 * Even the temporary structures in place we still cannot parse a union
 * before we know its type. Due to JSON typically sorting fields
 * alphabetically in various pretty printers, we are likely to receive
 * the type late with (`<union_name>_type` following `<union_name>`.
 * To deal with this we store a backtracking pointer and parses the
 * table generically in a first pass and reparse the table once the type
 * is known. This can happen recursively with nested tables containing
 * unions which is why we need to have a stack frame.
 *
 * If the type field is stored first we just store the type in the
 * custom frame and immediately parses the table with the right type
 * once we see it. The parse will be much faster and we can strongly
 * recommend that flatbuffer serializers do this, but we cannot require
 * it.
 *
 * The actual overhead of dealing with the custom stack frame is fairly
 * cheap once we get past the first custom stack allocation.
 *
 * We cannot update the builder before both the table and table type
 * has been parsed because the the type might have to be ingored due
 * to schema forwarding. Therefore the union type must be cached or
 * reread. This happens trivially be calling the union parser with the
 * type as argument, but it is important to be aware of before
 * refactoring the code.
 *
 * The user frame is created at table start and remains valid until
 * table exit, but we cannot assume the pointers to the frame remain
 * valid. Specifically we cannot use frame pointers after calling
 * the union parser. This means the union type must be cached or reread
 * so it can be added to the table. Because the type is passed to
 * the union parser this caching happens automatically but it is still
 * important to be aware that it is required.
 *
 * The frame reserves temporary information for all unions the table
 * holds, enumerated 0 <= `union_index` < `union_total`
 * where the `union_total` is fixed type specific number.
 *
 * The `type_present` is needed because union types range from 0..255
 * and we need an extra bit do distinguish not present from union type
 * `NONE = 0`.
 */

typedef struct {
    const char *backtrace;
    const char *line_start;
    uint8_t type_present;
    uint8_t type;
    int line;
} __flatcc_json_parser_union_entry_t;

typedef struct {
    size_t union_total;
    size_t union_count;
    __flatcc_json_parser_union_entry_t unions[1];
} __flatcc_json_parser_union_frame_t;

const char *flatcc_json_parser_prepare_unions(flatcc_json_parser_t *ctx,
        const char *buf, const char *end, size_t union_total)
{
    __flatcc_json_parser_union_frame_t *f;

    if (!(f = flatcc_builder_enter_user_frame(ctx->ctx,
                sizeof(__flatcc_json_parser_union_frame_t) + (union_total - 1) *
                sizeof(__flatcc_json_parser_union_entry_t)))) {
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_runtime);
    }
    /* Frames have zeroed memory. */
    f->union_total = union_total;
    return buf;
}

const char *flatcc_json_parser_finalize_unions(flatcc_json_parser_t *ctx,
        const char *buf, const char *end)
{
    __flatcc_json_parser_union_frame_t *f = flatcc_builder_get_user_frame(ctx->ctx);

    if (f->union_count) {
        buf = flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_union_incomplete);
    }
    flatcc_builder_exit_user_frame(ctx->ctx);
    return buf;
}

const char *flatcc_json_parser_union(flatcc_json_parser_t *ctx,
        const char *buf, const char *end, size_t union_index,
        flatbuffers_voffset_t id, flatcc_json_parser_union_f *parse)
{
    __flatcc_json_parser_union_frame_t *f = flatcc_builder_get_user_frame(ctx->ctx);
    __flatcc_json_parser_union_entry_t *e = &f->unions[union_index];

    if (e->backtrace) {
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_duplicate);
    }
    if (!e->type_present) {
        /* If we supported table: null, we should not count it, but we don't. */
        ++f->union_count;
        e->line = ctx->line;
        e->line_start = ctx->line_start;
        buf = flatcc_json_parser_generic_json(ctx, (e->backtrace = buf), end);
    } else {
        if (e->type == 0) {
            return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_union_none);
        }
        --f->union_count;
        buf = parse(ctx, buf, end, e->type, id);
    }
    return buf;
}

const char *flatcc_json_parser_union_type(flatcc_json_parser_t *ctx,
        const char *buf, const char *end, size_t union_index, flatbuffers_voffset_t id,
        flatcc_json_parser_integral_symbol_f *type_parsers[],
        flatcc_json_parser_union_f *union_parser)
{
    __flatcc_json_parser_union_frame_t *f = flatcc_builder_get_user_frame(ctx->ctx);
    __flatcc_json_parser_union_entry_t *e = f->unions + union_index;

    const char *mark;
    int line;
    const char *line_start;

    if (e->type_present) {
        return flatcc_json_parser_set_error(ctx, buf, end, flatcc_json_parser_error_duplicate);
    }
    e->type_present = 1;
    buf = flatcc_json_parser_uint8(ctx, (mark = buf), end, &e->type);
    if (mark == buf) {
        buf = flatcc_json_parser_symbolic_uint8(ctx, buf, end, type_parsers, &e->type);
    }
    /* Only count the union if the type is not NONE. */
    if (e->backtrace == 0) {
        f->union_count += e->type != 0;
        return buf;
    }
    assert(f->union_count);
    --f->union_count;
    /*
     * IMPORTANT: we cannot access any value in the frame or entry
     * pointer after calling union parse because it might cause the
     * stack to reallocate. We should read the frame pointer again if
     * needed - we don't but remember it if refactoring code.
     *
     * IMPORTANT 2: Do not assign buf here. We are backtracking.
     */
    line = ctx->line;
    line_start = ctx->line_start;
    ctx->line = e->line;
    ctx->line_start = e->line_start;
    if (end == union_parser(ctx, e->backtrace, end, e->type, id)) {
        return end;
    }
    ctx->line = line;
    ctx->line_start = line_start;
    return buf;
}
