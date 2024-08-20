/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <errno.h>

#ifndef NULL
#define NULL (void *)0
#endif

#ifndef __cplusplus
#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#ifndef inline
#define inline __inline
#endif
#endif

/* Return the offset of the given field in the given type */
#ifndef offsetof
/* GCC 4.0 and later has the builtin. */
#if defined(__GNUC__) && __GNUC__ >= 4
#define offsetof(Type, field) __builtin_offsetof(Type, field)
#else
#define offsetof(Type, field) ((size_t)(&((Type *)0)->field))
#endif
#endif

typedef uint8_t uint8;
typedef int8_t int8;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef float float32;
typedef double float64;
typedef uint64_t uint64;
typedef int64_t int64;

#if defined(_WIN32) || defined(_WIN32_)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

typedef int (*out_func_t)(int c, void *ctx);

typedef char *_va_list;
#define _va_arg(ap, t)                                   \
    (ap = (_va_list)(((uintptr_t)ap + sizeof(t) * 2 - 1) \
                     & ~(uintptr_t)(sizeof(t) - 1)),     \
     *(t *)(ap - sizeof(t)))

/* clang-format off */
#define PREPARE_TEMP_FORMAT()                                \
    char temp_fmt[32], *s, *fmt_buf = temp_fmt;              \
    uint32 fmt_buf_len = (uint32)sizeof(temp_fmt);           \
    int32 n;                                                 \
                                                             \
    /* additional 2 bytes: one is the format char,           \
       the other is `\0` */                                  \
    if ((uint32)(fmt - fmt_start_addr + 2) >= fmt_buf_len) { \
        assert((uint32)(fmt - fmt_start_addr) <=             \
                  UINT32_MAX - 2);                           \
        fmt_buf_len = (uint32)(fmt - fmt_start_addr + 2);    \
        if (!(fmt_buf = malloc(fmt_buf_len))) {              \
            print_err(out, ctx);                             \
            break;                                           \
        }                                                    \
    }                                                        \
                                                             \
    memset(fmt_buf, 0, fmt_buf_len);                         \
    memcpy(fmt_buf, fmt_start_addr,        \
                (uint32)(fmt - fmt_start_addr + 1));
/* clang-format on */

#define OUTPUT_TEMP_FORMAT()           \
    do {                               \
        if (n > 0) {                   \
            s = buf;                   \
            while (*s)                 \
                out((int)(*s++), ctx); \
        }                              \
                                       \
        if (fmt_buf != temp_fmt) {     \
            free(fmt_buf);             \
        }                              \
    } while (0)

static void
print_err(out_func_t out, void *ctx)
{
    out('E', ctx);
    out('R', ctx);
    out('R', ctx);
}

static bool
_vprintf_wa(out_func_t out, void *ctx, const char *fmt, _va_list ap)
{
    int might_format = 0; /* 1 if encountered a '%' */
    bool long_argument = false;
    const char *fmt_start_addr = NULL;

    while (*fmt) {
        if (!might_format) {
            if (*fmt != '%') {
                out((int)*fmt, ctx);
            }
            else {
                might_format = 1;
                long_argument = false;
                fmt_start_addr = fmt;
            }
        }
        else {
            switch (*fmt) {
                case '.':
                case '+':
                case '-':
                case ' ':
                case '#':
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    goto still_might_format;

                case 't': /* ptrdiff_t */
                case 'z': /* size_t (64bit on wasm) */
                case 'j': /* intmax_t/uintmax_t */
                case 'l':
                    long_argument = true;
                    goto still_might_format;

                case 'h':
                    /* FIXME: do nothing for these modifiers */
                    goto still_might_format;

                case 'p':
                    long_argument = true;
                    /* Fall through */
                case 'o':
                case 'd':
                case 'i':
                case 'u':
                case 'x':
                case 'X':
                case 'c':
                {
                    char buf[64];
                    PREPARE_TEMP_FORMAT();

                    if (!long_argument) {
                        int32 d;

                        d = _va_arg(ap, int32);
                        n = snprintf(buf, sizeof(buf), fmt_buf, d);
                    }
                    else {
                        int64 lld;

                        lld = _va_arg(ap, int64);
                        n = snprintf(buf, sizeof(buf), fmt_buf, lld);
                    }

                    OUTPUT_TEMP_FORMAT();
                    break;
                }

                case 's':
                {
                    char buf_tmp[128], *buf = buf_tmp;
                    char *start;
                    uint64 s_offset;
                    uint64 str_len, buf_len;

                    PREPARE_TEMP_FORMAT();

                    s_offset = _va_arg(ap, uint64);

                    s = start = (char *)(uintptr_t)(s_offset);

                    str_len = start ? strlen(start) : 0;
                    if (str_len >= UINT32_MAX - 64) {
                        print_err(out, ctx);
                        if (fmt_buf != temp_fmt) {
                            free(fmt_buf);
                        }
                        break;
                    }

                    /* reserve 64 more bytes as there may be width description
                     * in the fmt */
                    buf_len = str_len + 64;

                    if (buf_len > (uint32)sizeof(buf_tmp)) {
                        if (!(buf = malloc(buf_len))) {
                            print_err(out, ctx);
                            if (fmt_buf != temp_fmt) {
                                free(fmt_buf);
                            }
                            break;
                        }
                    }

                    n = snprintf(buf, buf_len, fmt_buf, start ? start : NULL);

                    OUTPUT_TEMP_FORMAT();

                    if (buf != buf_tmp) {
                        free(buf);
                    }

                    break;
                }

                case '%':
                {
                    out((int)'%', ctx);
                    break;
                }

                case 'e':
                case 'E':
                case 'g':
                case 'G':
                case 'f':
                case 'F':
                {
                    float64 f64;
                    char buf[64];
                    PREPARE_TEMP_FORMAT();

                    /* Make 8-byte aligned */
                    ap = (_va_list)(((uintptr_t)ap + 7) & ~(uintptr_t)7);
                    f64 = _va_arg(ap, float64);
                    n = snprintf(buf, sizeof(buf), fmt_buf, f64);

                    OUTPUT_TEMP_FORMAT();
                    break;
                }

                case 'n':
                    /* print nothing */
                    break;

                default:
                    out((int)'%', ctx);
                    out((int)*fmt, ctx);
                    break;
            }

            might_format = 0;
        }

    still_might_format:
        ++fmt;
    }
    return true;
}

typedef struct str_context {
    union {
        char *str;
        char **strp;
        FILE *stream;
    } u;
    uint32 max;
    uint32 count;
} str_context;

static int
printf_out(int c, struct str_context *ctx)
{
    printf("%c", c);
    ctx->count++;
    return c;
}

static int
sprintf_out(int c, struct str_context *ctx)
{
    if (!ctx->u.str || ctx->count >= ctx->max) {
        return c;
    }

    ctx->u.str[ctx->count++] = (char)c;

    return c;
}

static int
fprintf_out(int c, struct str_context *ctx)
{
    if (!ctx->u.stream || ctx->count >= ctx->max) {
        ctx->count++;
        return c;
    }

    if (ctx->count == ctx->max - 1) {
        fputc('\0', ctx->u.stream);
        ctx->count++;
    }
    else {
        fputc(c, ctx->u.stream);
        ctx->count++;
    }

    return c;
}

int
printf_wrapper(const char *format, _va_list va_args)
{
    struct str_context ctx = { 0 };

    if (!_vprintf_wa((out_func_t)printf_out, &ctx, format, va_args))
        return 0;

    return (int)ctx.count;
}

int
sprintf_wrapper(char *str, const char *format, _va_list va_args)
{
    struct str_context ctx = { 0 };

    ctx.u.str = str;
    ctx.max = UINT32_MAX;
    ctx.count = 0;

    if (!_vprintf_wa((out_func_t)sprintf_out, &ctx, format, va_args))
        return 0;

    if (ctx.count < ctx.max) {
        str[ctx.count] = '\0';
    }

    return (int)ctx.count;
}

int
snprintf_wrapper(char *str, size_t size, const char *format, _va_list va_args)
{
    struct str_context ctx = { 0 };

    ctx.u.str = str;
    ctx.max = size;
    ctx.count = 0;

    if (!_vprintf_wa((out_func_t)sprintf_out, &ctx, format, va_args))
        return 0;

    if (ctx.count < ctx.max) {
        str[ctx.count] = '\0';
    }

    return (int)ctx.count;
}

int
vasprintf_wrapper(char **strp, const char *format, _va_list va_args)
{
    /* TODO: how to allocate enough memory? */
    char *str = malloc(4096);
    if (!str)
        return -1;
    *strp = str;
    return sprintf_wrapper(str, format, va_args);
}

size_t
fwrite_wrapper(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    switch ((uint64_t)(uintptr_t)stream) {
        case 0:
            stream = stderr;
            break;
        default:
            break;
    }

    return fwrite(ptr, size, nmemb, stream);
}

int
fprintf_wrapper(FILE *stream, const char *format, _va_list va_args)
{
    struct str_context ctx;

    switch ((uint64_t)(uintptr_t)stream) {
        case 0:
            stream = stderr;
            break;
        default:
            break;
    }

    ctx.u.stream = stream;
    ctx.max = UINT32_MAX;
    ctx.count = 0;

    if (!_vprintf_wa((out_func_t)fprintf_out, &ctx, format, va_args))
        return 0;

    if (ctx.count < ctx.max) {
        fputc('\0', stream);
    }

    return (int)ctx.count;
}

int
fclose_wrapper(FILE *stream)
{
    return fclose(stream);
}

int
fputc_wrapper(int c, FILE *stream)
{
    switch ((uint64_t)(uintptr_t)stream) {
        case 0:
            stream = stderr;
            break;
        default:
            break;
    }

    return fputc(c, stream);
}

int
fputs_wrapper(const char *s, FILE *stream)
{
    switch ((uint64_t)(uintptr_t)stream) {
        case 0:
            stream = stderr;
            break;
        default:
            break;
    }

    return fputs(s, stream);
}

int
sscanf_wrapper(const char *str, const char *format, _va_list va_args)
{
    return 0;
}

void
__assert2_wrapper(const char *file, int line, const char *func,
                  const char *expr)
{
    fprintf(stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n",
            expr, func, file, line);
    abort();
}

int *
__errno_wrapper(void)
{
    return &errno;
}

double
log_wrapper(double x)
{
    return log(x);
}
