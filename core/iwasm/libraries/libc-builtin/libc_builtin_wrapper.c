/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "bh_platform.h"
#include "aot_export.h"
#include "../common/wasm_runtime.h"

#if defined(_WIN32) || defined(_WIN32_)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

static void *
runtime_malloc(uint32 size)
{
    return malloc(size);
}

static void
runtime_free(void *ptr)
{
    free(ptr);
}

static bool
get_app_addr_range(uint64 app_offset, uint64 *p_app_start_offset,
                   uint64 *p_app_end_offset)
{
    uint64 memory_size = wasm_get_memory_size();

    if (app_offset >= memory_size) {
        wasm_set_exception(EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS);
        return false;
    }

    if (p_app_start_offset)
        *p_app_start_offset = 0;
    if (p_app_end_offset)
        *p_app_end_offset = memory_size;
    return true;
}

static bool
get_native_addr_range(uint8 *native_ptr, uint8 **p_native_start_addr,
                      uint8 **p_native_end_addr)
{
    uint8 *memory_data = wasm_get_memory();
    uint64 memory_size = wasm_get_memory_size();

    if (native_ptr < memory_data || native_ptr >= memory_data + memory_size) {
        wasm_set_exception(EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS);
        return false;
    }

    if (p_native_start_addr)
        *p_native_start_addr = memory_data;
    if (p_native_end_addr)
        *p_native_end_addr = memory_data + memory_size;
    return true;
}

static bool
validate_app_addr(uint64 offset, uint64 size)
{
    uint64 memory_size = wasm_get_memory_size();

    if (offset < memory_size && size <= memory_size - offset)
        return true;

    wasm_set_exception(EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS);
    return false;
}

static void *
addr_app_to_native(uint64 offset);

static bool
validate_app_str_addr(uint64 app_str_offset)
{
    uint64 app_end_offset;
    const char *str, *str_end;

    if (!get_app_addr_range(app_str_offset, NULL, &app_end_offset))
        goto fail;

    str = addr_app_to_native(app_str_offset);
    str_end = str + (app_end_offset - app_str_offset);
    while (str < str_end && *str != '\0')
        str++;
    if (str == str_end)
        goto fail;

    return true;
fail:
    wasm_set_exception(EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS);
    return false;
}

static bool
validate_native_addr(void *native_ptr, uint64 size)
{
    uint8 *memory_data = wasm_get_memory();
    uint64 memory_size = wasm_get_memory_size();
    uint8 *addr = (uint8 *)native_ptr;

    if (size <= UINT64_MAX - (uint64)(uintptr_t)addr /* integer overflow */
        && memory_data <= (uint8 *)addr
        && addr + size <= memory_data + memory_size) {
        return true;
    }

    wasm_set_exception(EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS);
    return false;
}

static void *
addr_app_to_native(uint64 offset)
{
    uint8 *memory_data = (uint8 *)wasm_get_memory();

    if (!memory_data)
        return NULL;

    return memory_data + offset;
}

static uint64
addr_native_to_app(void *ptr)
{
    uint8 *memory_data = (uint8 *)wasm_get_memory();

    if (!memory_data)
        return 0;

    return (uint64)((uint8 *)ptr - memory_data);
}

static uint32
module_malloc(uint32 size, void **p_native_addr)
{
    void *memory_data = wasm_get_memory();
    void *heap_handle = wasm_get_heap_handle();
    void *mem;

    if (!heap_handle)
        return 0;

    mem = mem_allocator_malloc(heap_handle, size);
    if (!mem)
        return 0;

    if (p_native_addr)
        *p_native_addr = mem;
    return (uint32)((uint8 *)mem - (uint8 *)memory_data);
}

static uint32
module_realloc(uint32 ptr, uint32 new_size, void **p_native_addr)
{
    void *memory_data = wasm_get_memory(), *mem;
    void *heap_handle = wasm_get_heap_handle();
    uint64 memory_size = wasm_get_memory_size();

    if (!heap_handle
        || !(ptr > (uint64)(heap_handle - memory_data) && ptr < memory_size))
        return 0;

    mem = (uint8 *)memory_data + ptr;
    mem = mem_allocator_realloc(heap_handle, mem, new_size);
    if (!mem)
        return 0;

    if (p_native_addr)
        *p_native_addr = mem;
    return (uint32)((uint8 *)mem - (uint8 *)memory_data);
}

static void
module_free(uint32 offset)
{
    void *memory_data = wasm_get_memory();
    void *heap_handle = wasm_get_heap_handle();
    uint64 memory_size = wasm_get_memory_size();

    if (!heap_handle
        || !(offset > (uint64)(heap_handle - memory_data)
             && offset < memory_size))
        return;

    mem_allocator_free(heap_handle, (uint8 *)memory_data + offset);
}

void
print_wrapper()
{
    os_printf("spectestprint()\n");
}

void
print_i32_wrapper(int32 i32)
{
    os_printf("spectestprint_i32(%d)\n", i32);
}

void
print_i32_f32_wrapper(int32 i32, float f32)
{
    os_printf("spectestprint_i32_f32(%d, %f)\n", i32, f32);
}

void
print_f64_f64_wrapper(double f64_1, double f64_2)
{
    os_printf("spectestprint_f64_f64(%f, %f)\n", f64_1, f64_2);
}

void
print_f32_wrapper(float f32)
{
    os_printf("spectestprint_f32(%f)\n", f32);
}

void
print_f64_wrapper(double f64)
{
    os_printf("spectestprint_f64(%f)\n", f64);
}

typedef int (*out_func_t)(int c, void *ctx);

typedef char *_va_list;
#define _INTSIZEOF(n) (((uint32)sizeof(n) + 3) & (uint32)~3)
#define _va_arg(ap, t) (*(t *)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)))

#define CHECK_VA_ARG(ap, t)                                  \
    do {                                                     \
        if ((uint8 *)ap + _INTSIZEOF(t) > native_end_addr) { \
            if (fmt_buf != temp_fmt) {                       \
                runtime_free(fmt_buf);                       \
            }                                                \
            goto fail;                                       \
        }                                                    \
    } while (0)

/* clang-format off */
#define PREPARE_TEMP_FORMAT()                                \
    char temp_fmt[32], *s, *fmt_buf = temp_fmt;              \
    uint32 fmt_buf_len = (uint32)sizeof(temp_fmt);           \
    int32 n;                                                 \
                                                             \
    /* additional 2 bytes: one is the format char,           \
       the other is `\0` */                                  \
    if ((uint32)(fmt - fmt_start_addr + 2) >= fmt_buf_len) { \
        bh_assert((uint32)(fmt - fmt_start_addr) <=          \
                  UINT32_MAX - 2);                           \
        fmt_buf_len = (uint32)(fmt - fmt_start_addr + 2);    \
        if (!(fmt_buf = runtime_malloc(fmt_buf_len))) { \
            print_err(out, ctx);                             \
            break;                                           \
        }                                                    \
    }                                                        \
                                                             \
    memset(fmt_buf, 0, fmt_buf_len);                         \
    bh_memcpy_s(fmt_buf, fmt_buf_len, fmt_start_addr,        \
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
            runtime_free(fmt_buf);     \
        }                              \
    } while (0)

void
print_err(out_func_t out, void *ctx)
{
    out('E', ctx);
    out('R', ctx);
    out('R', ctx);
}

bool
_vprintf_wa(out_func_t out, void *ctx, const char *fmt, _va_list ap)
{
    int might_format = 0; /* 1 if encountered a '%' */
    int long_ctr = 0;
    uint8 *memory_data;
    uint8 *native_end_addr;
    uint64 memory_data_size;
    const char *fmt_start_addr = NULL;

    memory_data = wasm_get_memory();
    memory_data_size = wasm_get_memory_size();
    native_end_addr = memory_data + memory_data_size;

    if ((uint8 *)ap < memory_data || (uint8 *)ap >= native_end_addr)
        goto fail;

    /* fmt has already been adjusted if needed */

    while (*fmt) {
        if (!might_format) {
            if (*fmt != '%') {
                out((int)*fmt, ctx);
            }
            else {
                might_format = 1;
                long_ctr = 0;
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
                case 'z': /* size_t (32bit on wasm) */
                    long_ctr = 1;
                    goto still_might_format;

                case 'j':
                    /* intmax_t/uintmax_t */
                    long_ctr = 2;
                    goto still_might_format;

                case 'l':
                    long_ctr++;
                    /* Fall through */
                case 'h':
                    /* FIXME: do nothing for these modifiers */
                    goto still_might_format;

                case 'o':
                case 'd':
                case 'i':
                case 'u':
                case 'p':
                case 'x':
                case 'X':
                case 'c':
                {
                    char buf[64];
                    PREPARE_TEMP_FORMAT();

                    if (long_ctr < 2) {
                        int32 d;

                        CHECK_VA_ARG(ap, uint32);
                        d = _va_arg(ap, int32);

                        if (long_ctr == 1) {
                            uint32 fmt_end_idx = (uint32)(fmt - fmt_start_addr);

                            if (fmt_buf[fmt_end_idx - 1] == 'l'
                                || fmt_buf[fmt_end_idx - 1] == 'z'
                                || fmt_buf[fmt_end_idx - 1] == 't') {
                                /* The %ld, %zd and %td should be treated as
                                 * 32bit integer in wasm */
                                fmt_buf[fmt_end_idx - 1] = fmt_buf[fmt_end_idx];
                                fmt_buf[fmt_end_idx] = '\0';
                            }
                        }

                        n = snprintf(buf, sizeof(buf), fmt_buf, d);
                    }
                    else {
                        int64 lld;

                        /* Make 8-byte aligned */
                        ap = (_va_list)(((uintptr_t)ap + 7) & ~(uintptr_t)7);
                        CHECK_VA_ARG(ap, uint64);
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
                    uint32 s_offset, str_len, buf_len;

                    PREPARE_TEMP_FORMAT();

                    CHECK_VA_ARG(ap, int32);
                    s_offset = _va_arg(ap, uint32);

                    if (!validate_app_str_addr(s_offset)) {
                        if (fmt_buf != temp_fmt) {
                            runtime_free(fmt_buf);
                        }
                        return false;
                    }

                    s = start = addr_app_to_native(s_offset);

                    str_len = (uint32)strlen(start);
                    if (str_len >= UINT32_MAX - 64) {
                        print_err(out, ctx);
                        if (fmt_buf != temp_fmt) {
                            runtime_free(fmt_buf);
                        }
                        break;
                    }

                    /* reserve 64 more bytes as there may be width description
                     * in the fmt */
                    buf_len = str_len + 64;

                    if (buf_len > (uint32)sizeof(buf_tmp)) {
                        if (!(buf = runtime_malloc(buf_len))) {
                            print_err(out, ctx);
                            if (fmt_buf != temp_fmt) {
                                runtime_free(fmt_buf);
                            }
                            break;
                        }
                    }

                    n = snprintf(buf, buf_len, fmt_buf,
                                 (s_offset == 0 && str_len == 0) ? NULL
                                                                 : start);

                    OUTPUT_TEMP_FORMAT();

                    if (buf != buf_tmp) {
                        runtime_free(buf);
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
                    CHECK_VA_ARG(ap, float64);
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

fail:
    wasm_set_exception(EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS);
    return false;
}

struct str_context {
    char *str;
    uint32 max;
    uint32 count;
};

int
sprintf_out(int c, struct str_context *ctx)
{
    if (!ctx->str || ctx->count >= ctx->max) {
        ctx->count++;
        return c;
    }

    if (ctx->count == ctx->max - 1) {
        ctx->str[ctx->count++] = '\0';
    }
    else {
        ctx->str[ctx->count++] = (char)c;
    }

    return c;
}

int
printf_out(int c, struct str_context *ctx)
{
    os_printf("%c", c);
    ctx->count++;
    return c;
}

int
printf_wrapper(uint32 fmt_offset, uint32 va_args_offset)
{
    struct str_context ctx = { NULL, 0, 0 };
    const char *format;
    _va_list va_args;

    if (!validate_app_addr(fmt_offset, sizeof(int32))
        || !validate_app_addr(va_args_offset, sizeof(int32)))
        return 0;

    format = addr_app_to_native(fmt_offset);
    va_args = addr_app_to_native(va_args_offset);

    if (!_vprintf_wa((out_func_t)printf_out, &ctx, format, va_args))
        return 0;

    return (int)ctx.count;
}

int
sprintf_wrapper(uint32 str_offset, uint32 fmt_offset, uint32 va_args_offset)
{
    uint8 *native_end_offset;
    struct str_context ctx;
    char *str;
    const char *format;
    _va_list va_args;

    if (!validate_app_str_addr(str_offset)
        || !validate_app_addr(fmt_offset, sizeof(uint32))
        || !validate_app_addr(va_args_offset, sizeof(uint32)))
        return 0;

    str = addr_app_to_native(str_offset);
    format = addr_app_to_native(fmt_offset);
    va_args = addr_app_to_native(va_args_offset);

    if (!get_native_addr_range((uint8 *)str, NULL, &native_end_offset)) {
        return false;
    }

    ctx.str = str;
    ctx.max = (uint32)(native_end_offset - (uint8 *)str);
    ctx.count = 0;

    if (!_vprintf_wa((out_func_t)sprintf_out, &ctx, format, va_args))
        return 0;

    if (ctx.count < ctx.max) {
        str[ctx.count] = '\0';
    }

    return (int)ctx.count;
}

int
snprintf_wrapper(uint32 str_offset, uint32 size, uint32 fmt_offset,
                 uint32 va_args_offset)
{
    uint8 *native_end_offset;
    struct str_context ctx;
    char *str;
    const char *format;
    _va_list va_args;

    if (!validate_app_str_addr(str_offset)
        || !validate_app_addr(fmt_offset, sizeof(uint32))
        || !validate_app_addr(va_args_offset, sizeof(uint32)))
        return 0;

    str = addr_app_to_native(str_offset);
    format = addr_app_to_native(fmt_offset);
    va_args = addr_app_to_native(va_args_offset);

    if (!get_native_addr_range((uint8 *)str, NULL, &native_end_offset)) {
        return false;
    }

    ctx.str = str;
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
puts_wrapper(uint32 str_offset)
{
    char *str = (char *)addr_app_to_native(str_offset);

    if (!str)
        return 0;

    return os_printf("%s\n", str);
}

int
putchar_wrapper(int c)
{
    os_printf("%c", c);
    return 1;
}

uint32
strdup_wrapper(uint32 str_offset)
{
    const char *str;
    char *str_ret;
    uint32 len;
    uint32 str_ret_offset = 0;

    if (!validate_app_str_addr(str_offset))
        return 0;

    str = addr_app_to_native(str_offset);

    if (str) {
        len = (uint32)strlen(str) + 1;

        str_ret_offset = module_malloc(len, (void **)&str_ret);
        if (str_ret_offset) {
            memcpy(str_ret, str, len);
        }
    }

    return str_ret_offset;
}

int32
memcmp_wrapper(uint32 s1_offset, uint32 s2_offset, uint32 size)
{
    const void *s1, *s2;

    if (!validate_app_addr(s1_offset, size)
        || !validate_app_addr(s2_offset, size))
        return 0;

    s1 = addr_app_to_native(s1_offset);
    s2 = addr_app_to_native(s2_offset);

    return memcmp(s1, s2, size);
}

uint32
memcpy_wrapper(uint32 dst_offset, uint32 src_offset, uint32 size)
{
    void *dst;
    const void *src;

    if (!validate_app_addr(dst_offset, size)
        || !validate_app_addr(src_offset, size))
        return 0;

    dst = addr_app_to_native(dst_offset);
    src = addr_app_to_native(src_offset);

    if (size == 0)
        return dst_offset;

    memcpy(dst, src, size);
    return dst_offset;
}

uint32
memmove_wrapper(uint32 dst_offset, uint32 src_offset, uint32 size)
{
    void *dst, *src;

    if (!validate_app_addr(dst_offset, size)
        || !validate_app_addr(src_offset, size))
        return 0;

    dst = addr_app_to_native(dst_offset);
    src = addr_app_to_native(src_offset);

    if (size == 0)
        return dst_offset;

    memmove(dst, src, size);
    return dst_offset;
}

uint32
memset_wrapper(uint32 s_offset, int32 c, uint32 size)
{
    void *s;

    if (!validate_app_addr(s_offset, size))
        return 0;

    s = addr_app_to_native(s_offset);

    memset(s, c, size);
    return s_offset;
}

uint32
strchr_wrapper(uint32 s_offset, int32 c)
{
    const char *s;
    char *ret;

    if (!validate_app_str_addr(s_offset))
        return 0;

    s = addr_app_to_native(s_offset);

    ret = strchr(s, c);
    return ret ? addr_native_to_app(ret) : 0;
}

int32
strcmp_wrapper(uint32 s1_offset, uint32 s2_offset)
{
    const char *s1, *s2;

    if (!validate_app_str_addr(s1_offset) || !validate_app_str_addr(s2_offset))
        return 0;

    s1 = addr_app_to_native(s1_offset);
    s2 = addr_app_to_native(s2_offset);

    return strcmp(s1, s2);
}

int32
strncmp_wrapper(uint32 s1_offset, uint32 s2_offset, uint32 size)
{
    const char *s1, *s2;

    if (!validate_app_str_addr(s1_offset) || !validate_app_str_addr(s2_offset))
        return 0;

    s1 = addr_app_to_native(s1_offset);
    s2 = addr_app_to_native(s2_offset);

    return strncmp(s1, s2, size);
}

uint32
strcpy_wrapper(uint32 dst_offset, uint32 src_offset)
{
    char *dst;
    const char *src;
    uint32 len;

    if (!validate_app_str_addr(src_offset))
        return 0;

    dst = addr_app_to_native(dst_offset);
    src = addr_app_to_native(src_offset);

    len = (uint32)strlen(src) + 1;
    if (!validate_native_addr(dst, len))
        return 0;

#if defined(_WIN32) || defined(_WIN32_)
    strncpy_s(dst, len, src, len);
#else
    strncpy(dst, src, len);
#endif
    return dst_offset;
}

uint32
strncpy_wrapper(uint32 dst_offset, uint32 src_offset, uint32 size)
{
    char *dst;
    const char *src;

    if (!validate_app_str_addr(src_offset))
        return 0;

    dst = addr_app_to_native(dst_offset);
    src = addr_app_to_native(src_offset);

    if (!validate_native_addr(dst, size))
        return 0;

#if defined(_WIN32) || defined(_WIN32_)
    strncpy_s(dst, size, src, size);
#else
    strncpy(dst, src, size);
#endif
    return addr_native_to_app(dst);
}

uint32
strlen_wrapper(uint32 s_offset)
{
    const char *s;

    if (!validate_app_str_addr(s_offset))
        return 0;

    s = addr_app_to_native(s_offset);

    return (uint32)strlen(s);
}

uint32
malloc_wrapper(uint32 size)
{
    return module_malloc(size, NULL);
}

uint32
calloc_wrapper(uint32 nmemb, uint32 size)
{
    uint64 total_size = (uint64)nmemb * (uint64)size;
    uint32 ret_offset = 0;
    uint8 *ret_ptr;

    if (total_size >= UINT32_MAX)
        return 0;

    ret_offset = module_malloc((uint32)total_size, (void **)&ret_ptr);
    if (ret_offset) {
        memset(ret_ptr, 0, (uint32)total_size);
    }

    return ret_offset;
}

uint32
realloc_wrapper(uint32 ptr, uint32 new_size)
{
    return module_realloc(ptr, new_size, NULL);
}

void
free_wrapper(uint32 ptr_offset)
{
    if (!validate_app_addr(ptr_offset, sizeof(uint32)))
        return;

    module_free(ptr_offset);
}

int32
atoi_wrapper(uint32 s_offset)
{
    const char *s;

    if (!validate_app_str_addr(s_offset))
        return 0;

    s = addr_app_to_native(s_offset);
    return atoi(s);
}

void
exit_wrapper(int32 status)
{
    LOG_WARNING("wasm app exit with %d\n", status);
    wasm_set_exception(EXCE_UNREACHABLE);
}

int32
strtol_wrapper(uint32 nptr_offset, uint32 endptr_offset, int32 base)
{
    const char *nptr;
    char **endptr;
    int32 num = 0;

    if (!validate_app_str_addr(nptr_offset)
        || !validate_app_addr(endptr_offset, sizeof(uint32)))
        return 0;

    nptr = addr_app_to_native(nptr_offset);
    endptr = addr_app_to_native(endptr_offset);

    num = (int32)strtol(nptr, endptr, base);
    *(uint32 *)endptr = addr_native_to_app(*endptr);
    return num;
}

uint32
strtoul_wrapper(uint32 nptr_offset, uint32 endptr_offset, int32 base)
{
    const char *nptr;
    char **endptr;
    int32 num = 0;

    if (!validate_app_str_addr(nptr_offset)
        || !validate_app_addr(endptr_offset, sizeof(uint32)))
        return 0;

    nptr = addr_app_to_native(nptr_offset);
    endptr = addr_app_to_native(endptr_offset);

    num = (int32)strtoul(nptr, endptr, base);
    *(uint32 *)endptr = addr_native_to_app(*endptr);
    return num;
}

uint32
memchr_wrapper(uint32 s_offset, int32 c, uint32 n)
{
    const void *s;
    void *res;

    if (!validate_app_addr(s_offset, n))
        return 0;

    s = addr_app_to_native(s_offset);

    res = memchr(s, c, n);
    return addr_native_to_app(res);
}

int32
strncasecmp_wrapper(uint32 s1_offset, uint32 s2_offset, uint32 n)
{
    const char *s1, *s2;

    if (!validate_app_str_addr(s1_offset) || !validate_app_str_addr(s2_offset))
        return 0;

    s1 = addr_app_to_native(s1_offset);
    s2 = addr_app_to_native(s2_offset);

    return strncasecmp(s1, s2, n);
}

uint32
strspn_wrapper(uint32 s_offset, uint32 accept_offset)
{
    const char *s, *accept;

    if (!validate_app_str_addr(s_offset)
        || !validate_app_str_addr(accept_offset))
        return 0;

    s = addr_app_to_native(s_offset);
    accept = addr_app_to_native(accept_offset);

    return (uint32)strspn(s, accept);
}

uint32
strcspn_wrapper(uint32 s_offset, uint32 reject_offset)
{
    const char *s, *reject;

    if (!validate_app_str_addr(s_offset)
        || !validate_app_str_addr(reject_offset))
        return 0;

    s = addr_app_to_native(s_offset);
    reject = addr_app_to_native(reject_offset);

    return (uint32)strcspn(s, reject);
}

uint32
strstr_wrapper(uint32 s_offset, uint32 find_offset)
{
    const char *s, *find;
    char *res;

    if (!validate_app_str_addr(s_offset) || !validate_app_str_addr(find_offset))
        return 0;

    s = addr_app_to_native(s_offset);
    find = addr_app_to_native(find_offset);

    res = strstr(s, find);
    return addr_native_to_app(res);
}

int32
isupper_wrapper(int32 c)
{
    return isupper(c);
}

int32
isalpha_wrapper(int32 c)
{
    return isalpha(c);
}

int32
isspace_wrapper(int32 c)
{
    return isspace(c);
}

int32
isgraph_wrapper(int32 c)
{
    return isgraph(c);
}

int32
isprint_wrapper(int32 c)
{
    return isprint(c);
}

int32
isdigit_wrapper(int32 c)
{
    return isdigit(c);
}

int32
isxdigit_wrapper(int32 c)
{
    return isxdigit(c);
}

int32
tolower_wrapper(int32 c)
{
    return tolower(c);
}

int32
toupper_wrapper(int32 c)
{
    return toupper(c);
}

int32
isalnum_wrapper(int32 c)
{
    return isalnum(c);
}

struct timespec_app {
    int64 tv_sec;
    int32 tv_nsec;
};

uint32
clock_gettime_wrapper(uint32 clk_id, uint32 ts_app_offset)
{
    struct timespec_app *ts_app;
    uint64 time;

    (void)clk_id;

    if (!validate_app_addr(ts_app_offset, sizeof(struct timespec_app)))
        return (uint32)-1;

    ts_app = addr_app_to_native(ts_app_offset);

    time = os_time_get_boot_us();
    ts_app->tv_sec = time / 1000000;
    ts_app->tv_nsec = (time % 1000000) * 1000;

    return (uint32)0;
}

uint32
fwrite_wrapper(uint32 buf_offset, uint32 size, uint32 nmemb,
               uint32 stream_offset)
{
    if (size * nmemb < size || !validate_app_addr(buf_offset, size * nmemb))
        return 0;

    char *buf = addr_app_to_native(buf_offset);

    bh_assert(stream_offset == 0);
    return fwrite(buf, size, nmemb, stdout);
}

uint64
clock_wrapper()
{
    return os_time_get_boot_us() * 1000;
}
