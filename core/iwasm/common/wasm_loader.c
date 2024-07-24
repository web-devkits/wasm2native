/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_loader.h"
#include "wasm_opcode.h"
#include "wasm_runtime.h"

/* Read a value of given type from the address pointed to by the given
   pointer and increase the pointer to the position just after the
   value being read.  */
#define TEMPLATE_READ_VALUE(Type, p) \
    (p += sizeof(Type), *(Type *)(p - sizeof(Type)))

static void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string)
{
    if (error_buf != NULL) {
        snprintf(error_buf, error_buf_size, "WASM module load failed: %s",
                 string);
    }
}

static void
set_error_buf_v(char *error_buf, uint32 error_buf_size, const char *format, ...)
{
    va_list args;
    char buf[128];

    if (error_buf != NULL) {
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        snprintf(error_buf, error_buf_size, "WASM module load failed: %s", buf);
    }
}

static bool
check_buf(const uint8 *buf, const uint8 *buf_end, uint32 length,
          char *error_buf, uint32 error_buf_size)
{
    if ((uintptr_t)buf + length < (uintptr_t)buf
        || (uintptr_t)buf + length > (uintptr_t)buf_end) {
        set_error_buf(error_buf, error_buf_size,
                      "unexpected end of section or function");
        return false;
    }
    return true;
}

static bool
check_buf1(const uint8 *buf, const uint8 *buf_end, uint32 length,
           char *error_buf, uint32 error_buf_size)
{
    if ((uintptr_t)buf + length < (uintptr_t)buf
        || (uintptr_t)buf + length > (uintptr_t)buf_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }
    return true;
}

#define CHECK_BUF(buf, buf_end, length)                                    \
    do {                                                                   \
        if (!check_buf(buf, buf_end, length, error_buf, error_buf_size)) { \
            goto fail;                                                     \
        }                                                                  \
    } while (0)

#define CHECK_BUF1(buf, buf_end, length)                                    \
    do {                                                                    \
        if (!check_buf1(buf, buf_end, length, error_buf, error_buf_size)) { \
            goto fail;                                                      \
        }                                                                   \
    } while (0)

#define skip_leb(p) while (*p++ & 0x80)
#define skip_leb_int64(p, p_end) skip_leb(p)
#define skip_leb_uint32(p, p_end) skip_leb(p)
#define skip_leb_int32(p, p_end) skip_leb(p)
#define skip_leb_mem_offset(p, p_end) skip_leb(p)

static bool
read_leb(uint8 **p_buf, const uint8 *buf_end, uint32 maxbits, bool sign,
         uint64 *p_result, char *error_buf, uint32 error_buf_size)
{
    const uint8 *buf = *p_buf;
    uint64 result = 0;
    uint32 shift = 0;
    uint32 offset = 0, bcnt = 0;
    uint64 byte;

    while (true) {
        /* uN or SN must not exceed ceil(N/7) bytes */
        if (bcnt + 1 > (maxbits + 6) / 7) {
            set_error_buf(error_buf, error_buf_size,
                          "integer representation too long");
            return false;
        }

        CHECK_BUF(buf, buf_end, offset + 1);
        byte = buf[offset];
        offset += 1;
        result |= ((byte & 0x7f) << shift);
        shift += 7;
        bcnt += 1;
        if ((byte & 0x80) == 0) {
            break;
        }
    }

    if (!sign && maxbits == 32 && shift >= maxbits) {
        /* The top bits set represent values > 32 bits */
        if (((uint8)byte) & 0xf0)
            goto fail_integer_too_large;
    }
    else if (sign && maxbits == 32) {
        if (shift < maxbits) {
            /* Sign extend, second-highest bit is the sign bit */
            if ((uint8)byte & 0x40)
                result |= (~((uint64)0)) << shift;
        }
        else {
            /* The top bits should be a sign-extension of the sign bit */
            bool sign_bit_set = ((uint8)byte) & 0x8;
            int top_bits = ((uint8)byte) & 0xf0;
            if ((sign_bit_set && top_bits != 0x70)
                || (!sign_bit_set && top_bits != 0))
                goto fail_integer_too_large;
        }
    }
    else if (sign && maxbits == 64) {
        if (shift < maxbits) {
            /* Sign extend, second-highest bit is the sign bit */
            if ((uint8)byte & 0x40)
                result |= (~((uint64)0)) << shift;
        }
        else {
            /* The top bits should be a sign-extension of the sign bit */
            bool sign_bit_set = ((uint8)byte) & 0x1;
            int top_bits = ((uint8)byte) & 0xfe;

            if ((sign_bit_set && top_bits != 0x7e)
                || (!sign_bit_set && top_bits != 0))
                goto fail_integer_too_large;
        }
    }

    *p_buf += offset;
    *p_result = result;
    return true;

fail_integer_too_large:
    set_error_buf(error_buf, error_buf_size, "integer too large");
fail:
    return false;
}

#define read_uint8(p) TEMPLATE_READ_VALUE(uint8, p)
#define read_uint32(p) TEMPLATE_READ_VALUE(uint32, p)

#define read_leb_uint64(p, p_end, res)                                   \
    do {                                                                 \
        uint64 res64;                                                    \
        if (!read_leb((uint8 **)&p, p_end, 64, false, &res64, error_buf, \
                      error_buf_size))                                   \
            goto fail;                                                   \
        res = (uint64)res64;                                             \
    } while (0)

#define read_leb_int64(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, 64, true, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (int64)res64;                                             \
    } while (0)

#define read_leb_uint32(p, p_end, res)                                   \
    do {                                                                 \
        uint64 res64;                                                    \
        if (!read_leb((uint8 **)&p, p_end, 32, false, &res64, error_buf, \
                      error_buf_size))                                   \
            goto fail;                                                   \
        res = (uint32)res64;                                             \
    } while (0)

#define read_leb_int32(p, p_end, res)                                   \
    do {                                                                \
        uint64 res64;                                                   \
        if (!read_leb((uint8 **)&p, p_end, 32, true, &res64, error_buf, \
                      error_buf_size))                                  \
            goto fail;                                                  \
        res = (int32)res64;                                             \
    } while (0)

#define read_leb_mem_offset(p, p_end, res)                                   \
    do {                                                                     \
        uint64 res64;                                                        \
        if (!read_leb((uint8 **)&p, p_end, is_memory64 ? 64 : 32, false,     \
                      &res64, error_buf, error_buf_size)) {                  \
            set_error_buf(error_buf, error_buf_size, "offset out of range"); \
            goto fail;                                                       \
        }                                                                    \
        res = res64;                                                         \
    } while (0)

static bool
has_module_memory64(WASMModule *module)
{
    if (module->import_memory_count > 0)
        return !!(module->import_memories[0].u.memory.flags & MEMORY64_FLAG);
    else if (module->memory_count > 0)
        return !!(module->memories[0].flags & MEMORY64_FLAG);

    return false;
}

static char *
type2str(uint8 type)
{
    char *type_str[] = { "v128", "f64", "f32", "i64", "i32" };

    if (type >= VALUE_TYPE_V128 && type <= VALUE_TYPE_I32)
        return type_str[type - VALUE_TYPE_V128];
    else
        return "unknown type";
}

static bool
is_32bit_type(uint8 type)
{
    if (type == VALUE_TYPE_I32 || type == VALUE_TYPE_F32)
        return true;
    return false;
}

static bool
is_64bit_type(uint8 type)
{
    if (type == VALUE_TYPE_I64 || type == VALUE_TYPE_F64)
        return true;
    return false;
}

static bool
is_value_type(uint8 type)
{
    if (type == VALUE_TYPE_I32 || type == VALUE_TYPE_I64
        || type == VALUE_TYPE_F32 || type == VALUE_TYPE_F64
        || type == VALUE_TYPE_V128)
        return true;
    return false;
}

static bool
is_byte_a_type(uint8 type)
{
    return is_value_type(type) || (type == VALUE_TYPE_VOID);
}

static V128
read_i8x16(uint8 *p_buf, char *error_buf, uint32 error_buf_size)
{
    V128 result;
    uint8 i;

    for (i = 0; i != 16; ++i) {
        result.i8x16[i] = read_uint8(p_buf);
    }

    return result;
}

static void *
loader_malloc(uint64 size, char *error_buf, uint32 error_buf_size)
{
    void *mem;

    if (size >= UINT32_MAX || !(mem = wasm_runtime_malloc((uint32)size))) {
        set_error_buf(error_buf, error_buf_size, "allocate memory failed");
        return NULL;
    }

    memset(mem, 0, (uint32)size);
    return mem;
}

static bool
check_utf8_str(const uint8 *str, uint32 len)
{
    /* The valid ranges are taken from page 125, below link
       https://www.unicode.org/versions/Unicode9.0.0/ch03.pdf */
    const uint8 *p = str, *p_end = str + len;
    uint8 chr;

    while (p < p_end) {
        chr = *p;

        if (chr == 0) {
            LOG_WARNING(
                "LIMITATION: a string which contains '\\00' is unsupported");
            return false;
        }
        else if (chr < 0x80) {
            p++;
        }
        else if (chr >= 0xC2 && chr <= 0xDF && p + 1 < p_end) {
            if (p[1] < 0x80 || p[1] > 0xBF) {
                return false;
            }
            p += 2;
        }
        else if (chr >= 0xE0 && chr <= 0xEF && p + 2 < p_end) {
            if (chr == 0xE0) {
                if (p[1] < 0xA0 || p[1] > 0xBF || p[2] < 0x80 || p[2] > 0xBF) {
                    return false;
                }
            }
            else if (chr == 0xED) {
                if (p[1] < 0x80 || p[1] > 0x9F || p[2] < 0x80 || p[2] > 0xBF) {
                    return false;
                }
            }
            else { /* chr >= 0xE1 && chr <= 0xEF */
                if (p[1] < 0x80 || p[1] > 0xBF || p[2] < 0x80 || p[2] > 0xBF) {
                    return false;
                }
            }
            p += 3;
        }
        else if (chr >= 0xF0 && chr <= 0xF4 && p + 3 < p_end) {
            if (chr == 0xF0) {
                if (p[1] < 0x90 || p[1] > 0xBF || p[2] < 0x80 || p[2] > 0xBF
                    || p[3] < 0x80 || p[3] > 0xBF) {
                    return false;
                }
            }
            else if (chr <= 0xF3) { /* and also chr >= 0xF1 */
                if (p[1] < 0x80 || p[1] > 0xBF || p[2] < 0x80 || p[2] > 0xBF
                    || p[3] < 0x80 || p[3] > 0xBF) {
                    return false;
                }
            }
            else { /* chr == 0xF4 */
                if (p[1] < 0x80 || p[1] > 0x8F || p[2] < 0x80 || p[2] > 0xBF
                    || p[3] < 0x80 || p[3] > 0xBF) {
                    return false;
                }
            }
            p += 4;
        }
        else {
            return false;
        }
    }
    return (p == p_end);
}

static char *
const_str_list_insert(const uint8 *str, uint32 len, WASMModule *module,
                      bool is_load_from_file_buf, char *error_buf,
                      uint32 error_buf_size)
{
    StringNode *node, *node_next;

    if (!check_utf8_str(str, len)) {
        set_error_buf(error_buf, error_buf_size, "invalid UTF-8 encoding");
        return NULL;
    }

    if (len == 0) {
        return "";
    }
    else if (is_load_from_file_buf) {
        /* As the file buffer can be referred to after loading, we use
           the previous byte of leb encoded size to adjust the string:
           move string 1 byte backward and then append '\0' */
        char *c_str = (char *)str - 1;
        bh_memmove_s(c_str, len + 1, c_str + 1, len);
        c_str[len] = '\0';
        return c_str;
    }

    /* Search const str list */
    node = module->const_str_list;
    while (node) {
        node_next = node->next;
        if (strlen(node->str) == len && !memcmp(node->str, str, len))
            break;
        node = node_next;
    }

    if (node) {
        return node->str;
    }

    if (!(node = loader_malloc(sizeof(StringNode) + len + 1, error_buf,
                               error_buf_size))) {
        return NULL;
    }

    node->str = ((char *)node) + sizeof(StringNode);
    bh_memcpy_s(node->str, len + 1, str, len);
    node->str[len] = '\0';

    if (!module->const_str_list) {
        /* set as head */
        module->const_str_list = node;
        node->next = NULL;
    }
    else {
        /* insert it */
        node->next = module->const_str_list;
        module->const_str_list = node;
    }

    return node->str;
}

static void
destroy_wasm_type(WASMType *type)
{
    if (type->ref_count > 1) {
        /* The type is referenced by other types
           of current wasm module */
        type->ref_count--;
        return;
    }

    wasm_runtime_free(type);
}

static bool
load_init_expr(const uint8 **p_buf, const uint8 *buf_end,
               InitializerExpression *init_expr, uint8 type, char *error_buf,
               uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 flag, end_byte, *p_float;
    uint32 i;

    CHECK_BUF(p, p_end, 1);
    init_expr->init_expr_type = read_uint8(p);
    flag = init_expr->init_expr_type;

    switch (flag) {
        /* i32.const */
        case INIT_EXPR_TYPE_I32_CONST:
            if (type != VALUE_TYPE_I32)
                goto fail_type_mismatch;
            read_leb_int32(p, p_end, init_expr->u.i32);
            break;
        /* i64.const */
        case INIT_EXPR_TYPE_I64_CONST:
            if (type != VALUE_TYPE_I64)
                goto fail_type_mismatch;
            read_leb_int64(p, p_end, init_expr->u.i64);
            break;
        /* f32.const */
        case INIT_EXPR_TYPE_F32_CONST:
            if (type != VALUE_TYPE_F32)
                goto fail_type_mismatch;
            CHECK_BUF(p, p_end, 4);
            p_float = (uint8 *)&init_expr->u.f32;
            for (i = 0; i < sizeof(float32); i++)
                *p_float++ = *p++;
            break;
        /* f64.const */
        case INIT_EXPR_TYPE_F64_CONST:
            if (type != VALUE_TYPE_F64)
                goto fail_type_mismatch;
            CHECK_BUF(p, p_end, 8);
            p_float = (uint8 *)&init_expr->u.f64;
            for (i = 0; i < sizeof(float64); i++)
                *p_float++ = *p++;
            break;
        case INIT_EXPR_TYPE_V128_CONST:
        {
            uint64 high, low;

            if (type != VALUE_TYPE_V128)
                goto fail_type_mismatch;

            CHECK_BUF(p, p_end, 1);
            flag = read_uint8(p);
            (void)flag;

            CHECK_BUF(p, p_end, 16);
            wasm_runtime_read_v128(p, &high, &low);
            p += 16;

            init_expr->u.v128.i64x2[0] = high;
            init_expr->u.v128.i64x2[1] = low;
            break;
        }
        /* get_global */
        case INIT_EXPR_TYPE_GET_GLOBAL:
            read_leb_uint32(p, p_end, init_expr->u.global_index);
            break;
        default:
        {
            set_error_buf(error_buf, error_buf_size,
                          "illegal opcode "
                          "or constant expression required "
                          "or type mismatch");
            goto fail;
        }
    }
    CHECK_BUF(p, p_end, 1);
    end_byte = read_uint8(p);
    if (end_byte != 0x0b)
        goto fail_type_mismatch;
    *p_buf = p;
    return true;

fail_type_mismatch:
    set_error_buf(error_buf, error_buf_size,
                  "type mismatch or constant expression required");
fail:
    return false;
}

static bool
load_type_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end, *p_org;
    uint32 type_count, param_count, result_count, i, j;
    uint32 param_cell_num, ret_cell_num;
    uint64 total_size;
    uint8 flag;
    WASMType *type;

    read_leb_uint32(p, p_end, type_count);

    if (type_count) {
        module->type_count = type_count;
        total_size = sizeof(WASMType *) * (uint64)type_count;
        if (!(module->types =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        for (i = 0; i < type_count; i++) {
            CHECK_BUF(p, p_end, 1);
            flag = read_uint8(p);
            if (flag != 0x60) {
                set_error_buf(error_buf, error_buf_size, "invalid type flag");
                return false;
            }

            read_leb_uint32(p, p_end, param_count);

            /* Resolve param count and result count firstly */
            p_org = p;
            CHECK_BUF(p, p_end, param_count);
            p += param_count;
            read_leb_uint32(p, p_end, result_count);
            CHECK_BUF(p, p_end, result_count);
            p = p_org;

            if (param_count > UINT16_MAX || result_count > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "param count or result count too large");
                return false;
            }

            total_size = offsetof(WASMType, types)
                         + sizeof(uint8) * (uint64)(param_count + result_count);
            if (!(type = module->types[i] =
                      loader_malloc(total_size, error_buf, error_buf_size))) {
                return false;
            }

            /* Resolve param types and result types */
            type->ref_count = 1;
            type->param_count = (uint16)param_count;
            type->result_count = (uint16)result_count;
            for (j = 0; j < param_count; j++) {
                CHECK_BUF(p, p_end, 1);
                type->types[j] = read_uint8(p);
            }
            read_leb_uint32(p, p_end, result_count);
            for (j = 0; j < result_count; j++) {
                CHECK_BUF(p, p_end, 1);
                type->types[param_count + j] = read_uint8(p);
            }
            for (j = 0; j < param_count + result_count; j++) {
                if (!is_value_type(type->types[j])) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unknown value type");
                    return false;
                }
            }

            param_cell_num = wasm_get_cell_num(type->types, param_count);
            ret_cell_num =
                wasm_get_cell_num(type->types + param_count, result_count);
            if (param_cell_num > UINT16_MAX || ret_cell_num > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "param count or result count too large");
                return false;
            }
            type->param_cell_num = (uint16)param_cell_num;
            type->ret_cell_num = (uint16)ret_cell_num;

            /* If there is already a same type created, use it instead */
            for (j = 0; j < i; j++) {
                if (wasm_type_equal(type, module->types[j])) {
                    if (module->types[j]->ref_count == UINT16_MAX) {
                        set_error_buf(error_buf, error_buf_size,
                                      "wasm type's ref count too large");
                        return false;
                    }
                    destroy_wasm_type(type);
                    module->types[i] = module->types[j];
                    module->types[j]->ref_count++;
                    break;
                }
            }
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load type section success.\n");
    return true;
fail:
    return false;
}

static void
adjust_table_max_size(uint32 init_size, uint32 max_size_flag, uint32 *max_size)
{
    uint32 default_max_size =
        init_size * 2 > TABLE_MAX_SIZE ? init_size * 2 : TABLE_MAX_SIZE;

    if (max_size_flag) {
        /* module defines the table limitation */
        bh_assert(init_size <= *max_size);

        if (init_size < *max_size) {
            *max_size =
                *max_size < default_max_size ? *max_size : default_max_size;
        }
    }
    else {
        /* partial defined table limitation, gives a default value */
        *max_size = default_max_size;
    }
}

static bool
load_function_import(const uint8 **p_buf, const uint8 *buf_end,
                     const WASMModule *parent_module,
                     const char *sub_module_name, const char *function_name,
                     WASMFunctionImport *function, char *error_buf,
                     uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 declare_type_index = 0;
    WASMType *declare_func_type = NULL;

    read_leb_uint32(p, p_end, declare_type_index);
    *p_buf = p;

    if (declare_type_index >= parent_module->type_count) {
        set_error_buf(error_buf, error_buf_size, "unknown type");
        return false;
    }

    declare_type_index = wasm_get_smallest_type_idx(
        parent_module->types, parent_module->type_count, declare_type_index);

    declare_func_type = parent_module->types[declare_type_index];

    function->module_name = (char *)sub_module_name;
    function->field_name = (char *)function_name;
    function->func_type = declare_func_type;
    return true;
fail:
    return false;
}

static bool
check_table_max_size(uint32 init_size, uint32 max_size, char *error_buf,
                     uint32 error_buf_size)
{
    if (max_size < init_size) {
        set_error_buf(error_buf, error_buf_size,
                      "size minimum must not be greater than maximum");
        return false;
    }
    return true;
}

static bool
load_table_import(const uint8 **p_buf, const uint8 *buf_end,
                  WASMModule *parent_module, const char *sub_module_name,
                  const char *table_name, WASMTableImport *table,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 declare_elem_type = 0, declare_max_size_flag = 0,
           declare_init_size = 0, declare_max_size = 0;

    CHECK_BUF(p, p_end, 1);
    /* 0x70 */
    declare_elem_type = read_uint8(p);
    if (VALUE_TYPE_FUNCREF != declare_elem_type) {
        set_error_buf(error_buf, error_buf_size, "incompatible import type");
        return false;
    }

    read_leb_uint32(p, p_end, declare_max_size_flag);
    if (declare_max_size_flag > 1) {
        set_error_buf(error_buf, error_buf_size, "integer too large");
        return false;
    }

    read_leb_uint32(p, p_end, declare_init_size);

    if (declare_max_size_flag) {
        read_leb_uint32(p, p_end, declare_max_size);
        if (!check_table_max_size(declare_init_size, declare_max_size,
                                  error_buf, error_buf_size))
            return false;
    }

    adjust_table_max_size(declare_init_size, declare_max_size_flag,
                          &declare_max_size);

    *p_buf = p;

    /* now we believe all declaration are ok */
    table->elem_type = declare_elem_type;
    table->init_size = declare_init_size;
    table->flags = declare_max_size_flag;
    table->max_size = declare_max_size;

    (void)parent_module;
    return true;
fail:
    return false;
}

static bool
check_memory_init_size(bool is_memory64, uint32 init_size, char *error_buf,
                       uint32 error_buf_size)
{
    uint32 default_max_size =
        is_memory64 ? DEFAULT_MEM64_MAX_PAGES : DEFAULT_MAX_PAGES;

    if (!is_memory64 && init_size > default_max_size) {
        set_error_buf(error_buf, error_buf_size,
                      "memory size must be at most 65536 pages (4GiB)");
        return false;
    }
    else if (is_memory64 && init_size > default_max_size) {
        set_error_buf(
            error_buf, error_buf_size,
            "memory size must be at most 4,294,967,295 pages (274 Terabyte)");
        return false;
    }

    return true;
}

static bool
check_memory_max_size(bool is_memory64, uint32 init_size, uint32 max_size,
                      char *error_buf, uint32 error_buf_size)
{
    uint32 default_max_size =
        is_memory64 ? DEFAULT_MEM64_MAX_PAGES : DEFAULT_MAX_PAGES;

    if (max_size < init_size) {
        set_error_buf(error_buf, error_buf_size,
                      "size minimum must not be greater than maximum");
        return false;
    }

    if (!is_memory64 && max_size > default_max_size) {
        set_error_buf(error_buf, error_buf_size,
                      "memory size must be at most 65536 pages (4GiB)");
        return false;
    }
    else if (is_memory64 && max_size > default_max_size) {
        set_error_buf(
            error_buf, error_buf_size,
            "memory size must be at most 4,294,967,295 pages (274 Terabyte)");
        return false;
    }

    return true;
}

static bool
check_memory_flags(uint8 memory_flags, char *error_buf, uint32 error_buf_size)
{
    if (memory_flags
        > MAX_PAGE_COUNT_FLAG + SHARED_MEMORY_FLAG + MEMORY64_FLAG) {
        set_error_buf(error_buf, error_buf_size, "invalid limits flags");
        return false;
    }
    else if ((memory_flags & SHARED_MEMORY_FLAG)
             && !(memory_flags & MAX_PAGE_COUNT_FLAG)) {
        set_error_buf(error_buf, error_buf_size,
                      "shared memory must have maximum");
        return false;
    }

    return true;
}

static bool
load_memory_import(const uint8 **p_buf, const uint8 *buf_end,
                   WASMModule *parent_module, const char *sub_module_name,
                   const char *memory_name, WASMMemoryImport *memory,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
    uint32 max_page_count, memory_flags = 0;
    uint32 declare_init_page_count = 0;
    uint32 declare_max_page_count = 0;
    bool is_memory64 = false;

    p_org = p;
    read_leb_uint32(p, p_end, memory_flags);
    is_memory64 = memory_flags & MEMORY64_FLAG;
    if (p - p_org > 1) {
        LOG_VERBOSE("integer representation too long");
        set_error_buf(error_buf, error_buf_size, "invalid limits flags");
        return false;
    }

    if (!check_memory_flags(memory_flags, error_buf, error_buf_size)) {
        return false;
    }

    read_leb_uint32(p, p_end, declare_init_page_count);
    if (!check_memory_init_size(is_memory64, declare_init_page_count, error_buf,
                                error_buf_size)) {
        return false;
    }

    max_page_count = is_memory64 ? DEFAULT_MEM64_MAX_PAGES : DEFAULT_MAX_PAGES;
    if (memory_flags & MAX_PAGE_COUNT_FLAG) {
        read_leb_uint32(p, p_end, declare_max_page_count);
        if (!check_memory_max_size(is_memory64, declare_init_page_count,
                                   declare_max_page_count, error_buf,
                                   error_buf_size)) {
            return false;
        }
        if (declare_max_page_count > max_page_count) {
            declare_max_page_count = max_page_count;
        }
    }
    else {
        /* Limit the maximum memory size to max_page_count */
        declare_max_page_count = max_page_count;
    }

    /* (memory (export "memory") 1 2) */
    if (!strcmp("spectest", sub_module_name)) {
        uint32 spectest_memory_init_page = 1;
        uint32 spectest_memory_max_page = 2;

        if (strcmp("memory", memory_name)) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type or unknown import");
            return false;
        }

        if (declare_init_page_count > spectest_memory_init_page
            || declare_max_page_count < spectest_memory_max_page) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type");
            return false;
        }

        declare_init_page_count = spectest_memory_init_page;
        declare_max_page_count = spectest_memory_max_page;
    }

    /* now we believe all declaration are ok */
    memory->flags = memory_flags;
    memory->init_page_count = declare_init_page_count;
    memory->max_page_count = declare_max_page_count;
    memory->num_bytes_per_page = DEFAULT_NUM_BYTES_PER_PAGE;

    *p_buf = p;

    (void)parent_module;
    return true;
fail:
    return false;
}

typedef struct WASMNativeGlobal {
    const char *module_name;
    const char *global_name;
    uint8 type;
    bool is_mutable;
    WASMValue value;
} WASMNativeGlobal;

static WASMNativeGlobal spectest_import_globals[] = {
    { "spectest", "global_i32", VALUE_TYPE_I32, false, .value.i32 = 666 },
    { "spectest", "global_i64", VALUE_TYPE_I64, false, .value.i64 = 666 },
    { "spectest", "global_f32", VALUE_TYPE_F32, false, .value.f32 = 666.6 },
    { "spectest", "global_f64", VALUE_TYPE_F64, false, .value.f64 = 666.6 },
    { "test", "global-i32", VALUE_TYPE_I32, false, .value.i32 = 0 },
    { "test", "global-f32", VALUE_TYPE_F32, false, .value.f32 = 0 },
    { "test", "global-mut-i32", VALUE_TYPE_I32, true, .value.i32 = 0 },
    { "test", "global-mut-i64", VALUE_TYPE_I64, true, .value.i64 = 0 },
};

static bool
lookup_builtin_import_global(const char *module_name, const char *global_name,
                             WASMGlobalImport *global)
{
    uint32 size = sizeof(spectest_import_globals) / sizeof(WASMNativeGlobal);
    WASMNativeGlobal *global_def = spectest_import_globals;
    WASMNativeGlobal *global_def_end = global_def + size;

    if (!module_name || !global_name || !global)
        return false;

    /* Lookup constant globals which can be defined by table */
    while (global_def < global_def_end) {
        if (!strcmp(global_def->module_name, module_name)
            && !strcmp(global_def->global_name, global_name)) {
            global->type = global_def->type;
            global->is_mutable = global_def->is_mutable;
            global->global_data_linked = global_def->value;
            return true;
        }
        global_def++;
    }

    return false;
}

static bool
load_global_import(const uint8 **p_buf, const uint8 *buf_end,
                   const WASMModule *parent_module, char *sub_module_name,
                   char *global_name, WASMGlobalImport *global, char *error_buf,
                   uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint8 declare_type = 0;
    uint8 declare_mutable = 0;
    bool ret = false;

    CHECK_BUF(p, p_end, 2);
    declare_type = read_uint8(p);
    declare_mutable = read_uint8(p);
    *p_buf = p;

    if (declare_mutable >= 2) {
        set_error_buf(error_buf, error_buf_size, "invalid mutability");
        return false;
    }

    ret = lookup_builtin_import_global(sub_module_name, global_name, global);
    if (ret) {
        if (global->type != declare_type
            || global->is_mutable != declare_mutable) {
            set_error_buf(error_buf, error_buf_size,
                          "incompatible import type");
            return false;
        }
        global->is_linked = true;
    }
    else {
        char buf[128];

        snprintf(buf, sizeof(buf),
                 "warning: failed to link import global (%s, %s)",
                 sub_module_name, global_name);
        os_printf("%s\n", buf);
    }

    global->module_name = sub_module_name;
    global->field_name = global_name;
    global->type = declare_type;
    global->is_mutable = (declare_mutable == 1);

    (void)parent_module;
    (void)ret;
    return true;
fail:
    return false;
}

static bool
load_table(const uint8 **p_buf, const uint8 *buf_end, WASMModule *module,
           WASMTable *table, char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 max_flags = 7; /* three bits */

    CHECK_BUF(p, p_end, 1);
    /* 0x70 or 0x6F */
    table->elem_type = read_uint8(p);
    if (VALUE_TYPE_FUNCREF != table->elem_type) {
        set_error_buf(error_buf, error_buf_size, "incompatible import type");
        return false;
    }

    read_leb_uint32(p, p_end, table->flags);
    if (table->flags > max_flags) {
        set_error_buf(error_buf, error_buf_size, "invalid limits flags");
        return false;
    }
    if (table->flags & SHARED_TABLE_FLAG) {
        set_error_buf(error_buf, error_buf_size, "tables cannot be shared");
        return false;
    }

    read_leb_uint32(p, p_end, table->init_size);

    if (table->flags) {
        read_leb_uint32(p, p_end, table->max_size);
        if (!check_table_max_size(table->init_size, table->max_size, error_buf,
                                  error_buf_size))
            return false;
    }

    adjust_table_max_size(table->init_size, table->flags, &table->max_size);

    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
load_memory(const uint8 **p_buf, const uint8 *buf_end, WASMMemory *memory,
            char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end, *p_org;
    uint32 max_page_count, memory_flags;
    bool is_memory64 = false;

    p_org = p;
    read_leb_uint32(p, p_end, memory_flags);
    is_memory64 = memory_flags & MEMORY64_FLAG;
    if (p - p_org > 1) {
        LOG_VERBOSE("integer representation too long");
        set_error_buf(error_buf, error_buf_size, "invalid limits flags");
        return false;
    }

    if (!check_memory_flags(memory_flags, error_buf, error_buf_size)) {
        return false;
    }

    read_leb_uint32(p, p_end, memory->init_page_count);
    if (!check_memory_init_size(is_memory64, memory->init_page_count, error_buf,
                                error_buf_size))
        return false;

    max_page_count = is_memory64 ? DEFAULT_MEM64_MAX_PAGES : DEFAULT_MAX_PAGES;
    if (memory_flags & MAX_PAGE_COUNT_FLAG) {
        read_leb_uint32(p, p_end, memory->max_page_count);
        if (!check_memory_max_size(is_memory64, memory->init_page_count,
                                   memory->max_page_count, error_buf,
                                   error_buf_size))
            return false;
        if (memory->max_page_count > max_page_count)
            memory->max_page_count = max_page_count;
    }
    else {
        /* Limit the maximum memory size to max_page_count */
        memory->max_page_count = max_page_count;
    }

    memory->flags = memory_flags;
    memory->num_bytes_per_page = DEFAULT_NUM_BYTES_PER_PAGE;

    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
load_import_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    bool is_load_from_file_buf, char *error_buf,
                    uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end, *p_old;
    uint32 import_count, name_len, type_index, i, u32, flags;
    uint64 total_size;
    WASMImport *import;
    WASMImport *import_functions = NULL, *import_tables = NULL;
    WASMImport *import_memories = NULL, *import_globals = NULL;
    char *sub_module_name, *field_name;
    uint8 u8, kind;

    read_leb_uint32(p, p_end, import_count);

    if (import_count) {
        module->import_count = import_count;
        total_size = sizeof(WASMImport) * (uint64)import_count;
        if (!(module->imports =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        p_old = p;

        /* Scan firstly to get import count of each type */
        for (i = 0; i < import_count; i++) {
            /* module name */
            read_leb_uint32(p, p_end, name_len);
            CHECK_BUF(p, p_end, name_len);
            p += name_len;

            /* field name */
            read_leb_uint32(p, p_end, name_len);
            CHECK_BUF(p, p_end, name_len);
            p += name_len;

            CHECK_BUF(p, p_end, 1);
            /* 0x00/0x01/0x02/0x03 */
            kind = read_uint8(p);

            switch (kind) {
                case IMPORT_KIND_FUNC: /* import function */
                    read_leb_uint32(p, p_end, type_index);
                    module->import_function_count++;
                    break;

                case IMPORT_KIND_TABLE: /* import table */
                    CHECK_BUF(p, p_end, 1);
                    /* 0x70 */
                    u8 = read_uint8(p);
                    read_leb_uint32(p, p_end, flags);
                    read_leb_uint32(p, p_end, u32);
                    if (flags & 1)
                        read_leb_uint32(p, p_end, u32);
                    module->import_table_count++;

                    if (module->import_table_count > 1) {
                        set_error_buf(error_buf, error_buf_size,
                                      "multiple tables");
                        return false;
                    }
                    break;

                case IMPORT_KIND_MEMORY: /* import memory */
                    read_leb_uint32(p, p_end, flags);
                    read_leb_uint32(p, p_end, u32);
                    if (flags & 1)
                        read_leb_uint32(p, p_end, u32);
                    module->import_memory_count++;
                    if (module->import_memory_count > 1) {
                        set_error_buf(error_buf, error_buf_size,
                                      "multiple memories");
                        return false;
                    }
                    break;

                case IMPORT_KIND_GLOBAL: /* import global */
                    CHECK_BUF(p, p_end, 2);
                    p += 2;
                    module->import_global_count++;
                    break;

                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid import kind");
                    return false;
            }
        }

        if (module->import_function_count)
            import_functions = module->import_functions = module->imports;
        if (module->import_table_count)
            import_tables = module->import_tables =
                module->imports + module->import_function_count;
        if (module->import_memory_count)
            import_memories = module->import_memories =
                module->imports + module->import_function_count
                + module->import_table_count;
        if (module->import_global_count)
            import_globals = module->import_globals =
                module->imports + module->import_function_count
                + module->import_table_count + module->import_memory_count;

        p = p_old;

        /* Scan again to resolve the data */
        for (i = 0; i < import_count; i++) {
            /* load module name */
            read_leb_uint32(p, p_end, name_len);
            CHECK_BUF(p, p_end, name_len);
            if (!(sub_module_name = const_str_list_insert(
                      p, name_len, module, is_load_from_file_buf, error_buf,
                      error_buf_size))) {
                return false;
            }
            p += name_len;

            /* load field name */
            read_leb_uint32(p, p_end, name_len);
            CHECK_BUF(p, p_end, name_len);
            if (!(field_name = const_str_list_insert(
                      p, name_len, module, is_load_from_file_buf, error_buf,
                      error_buf_size))) {
                return false;
            }
            p += name_len;

            CHECK_BUF(p, p_end, 1);
            /* 0x00/0x01/0x02/0x03 */
            kind = read_uint8(p);

            switch (kind) {
                case IMPORT_KIND_FUNC: /* import function */
                    bh_assert(import_functions);
                    import = import_functions++;
                    import->u.function.func_idx =
                        (uint32)(import - module->import_functions);
                    if (!load_function_import(
                            &p, p_end, module, sub_module_name, field_name,
                            &import->u.function, error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

                case IMPORT_KIND_TABLE: /* import table */
                    bh_assert(import_tables);
                    import = import_tables++;
                    if (!load_table_import(&p, p_end, module, sub_module_name,
                                           field_name, &import->u.table,
                                           error_buf, error_buf_size)) {
                        LOG_DEBUG("can not import such a table (%s,%s)",
                                  sub_module_name, field_name);
                        return false;
                    }
                    break;

                case IMPORT_KIND_MEMORY: /* import memory */
                    bh_assert(import_memories);
                    import = import_memories++;
                    if (!load_memory_import(&p, p_end, module, sub_module_name,
                                            field_name, &import->u.memory,
                                            error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

                case IMPORT_KIND_GLOBAL: /* import global */
                    bh_assert(import_globals);
                    import = import_globals++;
                    if (!load_global_import(&p, p_end, module, sub_module_name,
                                            field_name, &import->u.global,
                                            error_buf, error_buf_size)) {
                        return false;
                    }
                    break;

                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid import kind");
                    return false;
            }
            import->kind = kind;
            import->u.names.module_name = sub_module_name;
            import->u.names.field_name = field_name;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load import section success.\n");
    (void)u8;
    (void)u32;
    (void)type_index;
    return true;
fail:
    return false;
}

static bool
init_function_local_offsets(WASMFunction *func, char *error_buf,
                            uint32 error_buf_size)
{
    WASMType *param_type = func->func_type;
    uint32 param_count = param_type->param_count;
    uint8 *param_types = param_type->types;
    uint32 local_count = func->local_count;
    uint8 *local_types = func->local_types;
    uint32 i, local_offset = 0;
    uint64 total_size = sizeof(uint16) * ((uint64)param_count + local_count);

    /*
     * Only allocate memory when total_size is not 0,
     * or the return value of malloc(0) might be NULL on some platforms,
     * which causes wasm loader return false.
     */
    if (total_size > 0
        && !(func->local_offsets =
                 loader_malloc(total_size, error_buf, error_buf_size))) {
        return false;
    }

    for (i = 0; i < param_count; i++) {
        func->local_offsets[i] = (uint16)local_offset;
        local_offset += wasm_value_type_cell_num(param_types[i]);
    }

    for (i = 0; i < local_count; i++) {
        func->local_offsets[param_count + i] = (uint16)local_offset;
        local_offset += wasm_value_type_cell_num(local_types[i]);
    }

    bh_assert(local_offset == func->param_cell_num + func->local_cell_num);
    return true;
}

static bool
load_function_section(const uint8 *buf, const uint8 *buf_end,
                      const uint8 *buf_code, const uint8 *buf_code_end,
                      WASMModule *module, char *error_buf,
                      uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    const uint8 *p_code = buf_code, *p_code_end, *p_code_save;
    uint32 func_count;
    uint64 total_size;
    uint32 code_count = 0, code_size, type_index, i, j, k, local_type_index;
    uint32 local_count, local_set_count, sub_local_count, local_cell_num;
    uint8 type;
    WASMFunction *func;

    read_leb_uint32(p, p_end, func_count);

    if (buf_code)
        read_leb_uint32(p_code, buf_code_end, code_count);

    if (func_count != code_count) {
        set_error_buf(error_buf, error_buf_size,
                      "function and code section have inconsistent lengths or "
                      "unexpected end");
        return false;
    }

    if (func_count) {
        module->function_count = func_count;
        total_size = sizeof(WASMFunction *) * (uint64)func_count;
        if (!(module->functions =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        for (i = 0; i < func_count; i++) {
            /* Resolve function type */
            read_leb_uint32(p, p_end, type_index);
            if (type_index >= module->type_count) {
                set_error_buf(error_buf, error_buf_size, "unknown type");
                return false;
            }

            type_index = wasm_get_smallest_type_idx(
                module->types, module->type_count, type_index);

            read_leb_uint32(p_code, buf_code_end, code_size);
            if (code_size == 0 || p_code + code_size > buf_code_end) {
                set_error_buf(error_buf, error_buf_size,
                              "invalid function code size");
                return false;
            }

            /* Resolve local set count */
            p_code_end = p_code + code_size;
            local_count = 0;
            read_leb_uint32(p_code, buf_code_end, local_set_count);
            p_code_save = p_code;

            /* Calculate total local count */
            for (j = 0; j < local_set_count; j++) {
                read_leb_uint32(p_code, buf_code_end, sub_local_count);
                if (sub_local_count > UINT32_MAX - local_count) {
                    set_error_buf(error_buf, error_buf_size, "too many locals");
                    return false;
                }
                CHECK_BUF(p_code, buf_code_end, 1);
                /* 0x7F/0x7E/0x7D/0x7C */
                type = read_uint8(p_code);
                local_count += sub_local_count;
            }

            /* Alloc memory, layout: function structure + local types */
            code_size = (uint32)(p_code_end - p_code);

            total_size = sizeof(WASMFunction) + (uint64)local_count;
            if (!(func = module->functions[i] =
                      loader_malloc(total_size, error_buf, error_buf_size))) {
                return false;
            }

            /* Set function type, local count, code size and code body */
            func->func_type = module->types[type_index];
            func->func_idx = module->import_function_count + i;
            func->local_count = local_count;
            if (local_count > 0)
                func->local_types = (uint8 *)func + sizeof(WASMFunction);
            func->code_size = code_size;
            /*
             * we shall make a copy of code body [p_code, p_code + code_size]
             * when we are worrying about inappropriate releasing behaviour.
             * all code bodies are actually in a buffer which user allocates in
             * his embedding environment and we don't have power on them.
             * it will be like:
             * code_body_cp = malloc(code_size);
             * memcpy(code_body_cp, p_code, code_size);
             * func->code = code_body_cp;
             */
            func->code = (uint8 *)p_code;

            /* Load each local type */
            p_code = p_code_save;
            local_type_index = 0;
            for (j = 0; j < local_set_count; j++) {
                read_leb_uint32(p_code, buf_code_end, sub_local_count);
                /* Note: sub_local_count is allowed to be 0 */
                if (local_type_index > UINT32_MAX - sub_local_count
                    || local_type_index + sub_local_count > local_count) {
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid local count");
                    return false;
                }
                CHECK_BUF(p_code, buf_code_end, 1);
                /* 0x7F/0x7E/0x7D/0x7C */
                type = read_uint8(p_code);
                if (!is_value_type(type)) {
                    if (type == VALUE_TYPE_V128)
                        set_error_buf(error_buf, error_buf_size,
                                      "v128 value type requires simd feature");
                    else
                        set_error_buf_v(error_buf, error_buf_size,
                                        "invalid local type 0x%02X", type);
                    return false;
                }
                for (k = 0; k < sub_local_count; k++) {
                    func->local_types[local_type_index++] = type;
                }
            }

            func->param_cell_num = func->func_type->param_cell_num;
            func->ret_cell_num = func->func_type->ret_cell_num;
            local_cell_num =
                wasm_get_cell_num(func->local_types, func->local_count);

            if (local_cell_num > UINT16_MAX) {
                set_error_buf(error_buf, error_buf_size,
                              "local count too large");
                return false;
            }

            func->local_cell_num = (uint16)local_cell_num;

            if (!init_function_local_offsets(func, error_buf, error_buf_size))
                return false;

            p_code = p_code_end;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load function section success.\n");
    return true;
fail:
    return false;
}

static bool
check_function_index(const WASMModule *module, uint32 function_index,
                     char *error_buf, uint32 error_buf_size)
{
    if (function_index
        >= module->import_function_count + module->function_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown function %d",
                        function_index);
        return false;
    }
    return true;
}

static bool
load_table_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 table_count, i;
    uint64 total_size;
    WASMTable *table;

    read_leb_uint32(p, p_end, table_count);
    if (module->import_table_count + table_count > 1) {
        /* a total of one table is allowed */
        set_error_buf(error_buf, error_buf_size, "multiple tables");
        return false;
    }

    if (table_count) {
        module->table_count = table_count;
        total_size = sizeof(WASMTable) * (uint64)table_count;
        if (!(module->tables =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        /* load each table */
        table = module->tables;
        for (i = 0; i < table_count; i++, table++)
            if (!load_table(&p, p_end, module, table, error_buf,
                            error_buf_size))
                return false;
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load table section success.\n");
    return true;
fail:
    return false;
}

static bool
load_memory_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 memory_count, i;
    uint64 total_size;
    WASMMemory *memory;

    read_leb_uint32(p, p_end, memory_count);
    /* a total of one memory is allowed */
    if (module->import_memory_count + memory_count > 1) {
        set_error_buf(error_buf, error_buf_size, "multiple memories");
        return false;
    }

    if (memory_count) {
        module->memory_count = memory_count;
        total_size = sizeof(WASMMemory) * (uint64)memory_count;
        if (!(module->memories =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        /* load each memory */
        memory = module->memories;
        for (i = 0; i < memory_count; i++, memory++)
            if (!load_memory(&p, p_end, memory, error_buf, error_buf_size))
                return false;
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load memory section success.\n");
    return true;
fail:
    return false;
}

static bool
load_global_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 global_count, i;
    uint64 total_size;
    WASMGlobal *global;
    uint8 mutable;

    read_leb_uint32(p, p_end, global_count);

    if (global_count) {
        module->global_count = global_count;
        total_size = sizeof(WASMGlobal) * (uint64)global_count;
        if (!(module->globals =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        global = module->globals;

        for (i = 0; i < global_count; i++, global++) {
            CHECK_BUF(p, p_end, 2);
            global->type = read_uint8(p);
            mutable = read_uint8(p);
            if (mutable >= 2) {
                set_error_buf(error_buf, error_buf_size, "invalid mutability");
                return false;
            }
            global->is_mutable = mutable ? true : false;

            /* initialize expression */
            if (!load_init_expr(&p, p_end, &(global->init_expr), global->type,
                                error_buf, error_buf_size))
                return false;

            if (INIT_EXPR_TYPE_GET_GLOBAL == global->init_expr.init_expr_type) {
                /**
                 * Currently, constant expressions occurring as initializers
                 * of globals are further constrained in that contained
                 * global.get instructions are
                 * only allowed to refer to imported globals.
                 */
                uint32 target_global_index = global->init_expr.u.global_index;
                if (target_global_index >= module->import_global_count) {
                    set_error_buf(error_buf, error_buf_size, "unknown global");
                    return false;
                }
            }
            else if (INIT_EXPR_TYPE_FUNCREF_CONST
                     == global->init_expr.init_expr_type) {
                if (!check_function_index(module, global->init_expr.u.ref_index,
                                          error_buf, error_buf_size)) {
                    return false;
                }
            }
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load global section success.\n");
    return true;
fail:
    return false;
}

static bool
load_export_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    bool is_load_from_file_buf, char *error_buf,
                    uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 export_count, i, j, index;
    uint64 total_size;
    uint32 str_len;
    WASMExport *export;
    const char *name;

    read_leb_uint32(p, p_end, export_count);

    if (export_count) {
        module->export_count = export_count;
        total_size = sizeof(WASMExport) * (uint64)export_count;
        if (!(module->exports =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        export = module->exports;
        for (i = 0; i < export_count; i++, export ++) {
            if (p == p_end) {
                /* export section with inconsistent count:
                   n export declared, but less than n given */
                set_error_buf(error_buf, error_buf_size,
                              "length out of bounds");
                return false;
            }
            read_leb_uint32(p, p_end, str_len);
            CHECK_BUF(p, p_end, str_len);

            for (j = 0; j < i; j++) {
                name = module->exports[j].name;
                if (strlen(name) == str_len && memcmp(name, p, str_len) == 0) {
                    set_error_buf(error_buf, error_buf_size,
                                  "duplicate export name");
                    return false;
                }
            }

            if (!(export->name = const_str_list_insert(
                      p, str_len, module, is_load_from_file_buf, error_buf,
                      error_buf_size))) {
                return false;
            }

            p += str_len;
            CHECK_BUF(p, p_end, 1);
            export->kind = read_uint8(p);
            read_leb_uint32(p, p_end, index);
            export->index = index;

            switch (export->kind) {
                /* function index */
                case EXPORT_KIND_FUNC:
                    if (index >= module->function_count
                                     + module->import_function_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown function");
                        return false;
                    }
                    break;
                /* table index */
                case EXPORT_KIND_TABLE:
                    if (index
                        >= module->table_count + module->import_table_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown table");
                        return false;
                    }
                    break;
                /* memory index */
                case EXPORT_KIND_MEMORY:
                    if (index
                        >= module->memory_count + module->import_memory_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown memory");
                        return false;
                    }
                    break;
                /* global index */
                case EXPORT_KIND_GLOBAL:
                    if (index
                        >= module->global_count + module->import_global_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown global");
                        return false;
                    }
                    break;
                default:
                    set_error_buf(error_buf, error_buf_size,
                                  "invalid export kind");
                    return false;
            }
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load export section success.\n");
    return true;
fail:
    return false;
}

static bool
check_table_index(const WASMModule *module, uint32 table_index, char *error_buf,
                  uint32 error_buf_size)
{
    if (table_index != 0) {
        set_error_buf(error_buf, error_buf_size, "zero byte expected");
        return false;
    }

    if (table_index >= module->import_table_count + module->table_count) {
        set_error_buf_v(error_buf, error_buf_size, "unknown table %d",
                        table_index);
        return false;
    }
    return true;
}

static bool
load_table_index(const uint8 **p_buf, const uint8 *buf_end, WASMModule *module,
                 uint32 *p_table_index, char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 table_index;

    read_leb_uint32(p, p_end, table_index);
    if (!check_table_index(module, table_index, error_buf, error_buf_size)) {
        return false;
    }

    *p_table_index = table_index;
    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
load_func_index_vec(const uint8 **p_buf, const uint8 *buf_end,
                    WASMModule *module, WASMTableSeg *table_segment,
                    bool use_init_expr, char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = *p_buf, *p_end = buf_end;
    uint32 function_count, function_index = 0, i;
    uint64 total_size;

    read_leb_uint32(p, p_end, function_count);
    table_segment->function_count = function_count;
    total_size = sizeof(uint32) * (uint64)function_count;
    if (total_size > 0
        && !(table_segment->func_indexes = (uint32 *)loader_malloc(
                 total_size, error_buf, error_buf_size))) {
        return false;
    }

    for (i = 0; i < function_count; i++) {
        InitializerExpression init_expr = { 0 };

        read_leb_uint32(p, p_end, function_index);
        (void)use_init_expr;

        /* since we are using -1 to indicate ref.null */
        if (init_expr.init_expr_type != INIT_EXPR_TYPE_REFNULL_CONST
            && !check_function_index(module, function_index, error_buf,
                                     error_buf_size)) {
            return false;
        }
        table_segment->func_indexes[i] = function_index;
    }

    *p_buf = p;
    return true;
fail:
    return false;
}

static bool
load_table_segment_section(const uint8 *buf, const uint8 *buf_end,
                           WASMModule *module, char *error_buf,
                           uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 table_segment_count, i;
    uint64 total_size;
    WASMTableSeg *table_segment;
    bool is_table64;

    read_leb_uint32(p, p_end, table_segment_count);

    if (table_segment_count) {
        module->table_seg_count = table_segment_count;
        total_size = sizeof(WASMTableSeg) * (uint64)table_segment_count;
        if (!(module->table_segments =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        table_segment = module->table_segments;
        for (i = 0; i < table_segment_count; i++, table_segment++) {
            if (p >= p_end) {
                set_error_buf(error_buf, error_buf_size,
                              "invalid value type or "
                              "invalid elements segment kind");
                return false;
            }

            /*
             * like:      00  41 05 0b               04 00 01 00 01
             * for: (elem 0   (offset (i32.const 5)) $f1 $f2 $f1 $f2)
             */
            if (!load_table_index(&p, p_end, module,
                                  &table_segment->table_index, error_buf,
                                  error_buf_size))
                return false;
            is_table64 =
                module->tables[table_segment->table_index].flags & TABLE64_FLAG
                    ? true
                    : false;
            if (!load_init_expr(&p, p_end, &table_segment->base_offset,
                                is_table64 ? VALUE_TYPE_I64 : VALUE_TYPE_I32,
                                error_buf, error_buf_size))
                return false;
            if (!load_func_index_vec(&p, p_end, module, table_segment, false,
                                     error_buf, error_buf_size))
                return false;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load table segment section success.\n");
    return true;
fail:
    return false;
}

static bool
load_data_segment_section(const uint8 *buf, const uint8 *buf_end,
                          WASMModule *module, char *error_buf,
                          uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 data_seg_count, i, mem_index, data_seg_len;
    uint64 total_size;
    WASMDataSeg *dataseg;
    InitializerExpression init_expr;
    bool is_passive = false;
    uint32 mem_flag;
    uint8 mem_offset_type = VALUE_TYPE_I32;

    module->data_section_body = p;

    read_leb_uint32(p, p_end, data_seg_count);

    if ((module->data_seg_count1 != 0)
        && (data_seg_count != module->data_seg_count1)) {
        set_error_buf(error_buf, error_buf_size,
                      "data count and data section have inconsistent lengths");
        return false;
    }

    if (data_seg_count) {
        module->data_seg_count = data_seg_count;
        total_size = sizeof(WASMDataSeg *) * (uint64)data_seg_count;
        if (!(module->data_segments =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }

        for (i = 0; i < data_seg_count; i++) {
            read_leb_uint32(p, p_end, mem_index);
            is_passive = false;
            mem_flag = mem_index & 0x03;
            switch (mem_flag) {
                case 0x01:
                    is_passive = true;
                    break;
                case 0x00:
                    /* no memory index, treat index as 0 */
                    mem_index = 0;
                    goto check_mem_index;
                case 0x02:
                    /* read following memory index */
                    read_leb_uint32(p, p_end, mem_index);
                check_mem_index:
                    if (mem_index
                        >= module->import_memory_count + module->memory_count) {
                        set_error_buf_v(error_buf, error_buf_size,
                                        "unknown memory %d", mem_index);
                        return false;
                    }
                    break;
                case 0x03:
                default:
                    set_error_buf(error_buf, error_buf_size, "unknown memory");
                    return false;
                    break;
            }

            if (!is_passive) {
                uint8 memory_flag;

                if (module->import_memory_count > 0) {
                    memory_flag =
                        module->import_memories[mem_index].u.memory.flags;
                }
                else {
                    memory_flag =
                        module
                            ->memories[mem_index - module->import_memory_count]
                            .flags;
                }
                mem_offset_type = memory_flag & MEMORY64_FLAG ? VALUE_TYPE_I64
                                                              : VALUE_TYPE_I32;

                if (!load_init_expr(&p, p_end, &init_expr, mem_offset_type,
                                    error_buf, error_buf_size))
                    return false;
            }

            read_leb_uint32(p, p_end, data_seg_len);

            if (!(dataseg = module->data_segments[i] = loader_malloc(
                      sizeof(WASMDataSeg), error_buf, error_buf_size))) {
                return false;
            }

            dataseg->is_passive = is_passive;
            if (!is_passive) {
                bh_memcpy_s(&dataseg->base_offset,
                            sizeof(InitializerExpression), &init_expr,
                            sizeof(InitializerExpression));

                dataseg->memory_index = mem_index;
            }

            dataseg->data_length = data_seg_len;
            CHECK_BUF(p, p_end, data_seg_len);
            dataseg->data = (uint8 *)p;
            p += data_seg_len;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load data segment section success.\n");
    return true;
fail:
    return false;
}

static bool
load_datacount_section(const uint8 *buf, const uint8 *buf_end,
                       WASMModule *module, char *error_buf,
                       uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 data_seg_count1 = 0;

    read_leb_uint32(p, p_end, data_seg_count1);
    module->data_seg_count1 = data_seg_count1;

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load datacount section success.\n");
    return true;
fail:
    return false;
}

static bool
load_code_section(const uint8 *buf, const uint8 *buf_end, const uint8 *buf_func,
                  const uint8 *buf_func_end, WASMModule *module,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    const uint8 *p_func = buf_func;
    uint32 func_count = 0, code_count;

    module->code_section_body = p;

    /* code has been loaded in function section, so pass it here, just check
     * whether function and code section have inconsistent lengths */
    read_leb_uint32(p, p_end, code_count);

    if (buf_func)
        read_leb_uint32(p_func, buf_func_end, func_count);

    if (func_count != code_count) {
        set_error_buf(error_buf, error_buf_size,
                      "function and code section have inconsistent lengths");
        return false;
    }

    LOG_VERBOSE("Load code segment section success.\n");
    (void)module;
    return true;
fail:
    return false;
}

static bool
load_start_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                   char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    WASMType *type;
    uint32 start_function;

    read_leb_uint32(p, p_end, start_function);

    if (start_function
        >= module->function_count + module->import_function_count) {
        set_error_buf(error_buf, error_buf_size, "unknown function");
        return false;
    }

    if (start_function < module->import_function_count)
        type = module->import_functions[start_function].u.function.func_type;
    else
        type = module->functions[start_function - module->import_function_count]
                   ->func_type;
    if (type->param_count != 0 || type->result_count != 0) {
        set_error_buf(error_buf, error_buf_size, "invalid start function");
        return false;
    }

    module->start_function = start_function;

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    LOG_VERBOSE("Load start section success.\n");
    return true;
fail:
    return false;
}

/**
 * Parse "linking" section, refer to function
 *   Error WasmObjectFile::parseLinkingSection(ReadContext &Ctx)
 * https://github.com/llvm/llvm-project/blob/main/llvm/lib/Object/WasmObjectFile.cpp
 */
static bool
handle_linking_section(const uint8 *buf, const uint8 *buf_end,
                       WASMModule *module, char *error_buf,
                       uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    char *name;
    WASMSymbol *symbol;
    uint32 version, sub_section_type, sub_section_size;
    uint32 symbol_count, symbol_type, symbol_flags;
    uint32 seg_count, elem_idx, name_len, i;
    uint64 total_size;
    bool is_defined;

    if (p >= p_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }

    read_leb_uint32(p, p_end, version);
    if (version != 2) {
        set_error_buf(error_buf, error_buf_size,
                      "unsupported version of linking metadata");
        return false;
    }

    while (p < p_end) {
        read_leb_uint32(p, p_end, sub_section_type);
        read_leb_uint32(p, p_end, sub_section_size);

        if (sub_section_type == WASM_SYMBOL_TABLE) {
            read_leb_uint32(p, p_end, symbol_count);

            total_size = (uint64)sizeof(WASMSymbol) * symbol_count;
            if (total_size > 0
                && !(module->symbols = symbol = loader_malloc(
                         total_size, error_buf, error_buf_size))) {
                return false;
            }
            module->symbol_count = symbol_count;

            for (i = 0; i < symbol_count; i++, symbol++) {
                CHECK_BUF(p, p_end, 1);
                symbol_type = symbol->type = read_uint8(p);
                read_leb_uint32(p, p_end, symbol_flags);
                is_defined = symbol->is_defined =
                    (symbol_flags & WASM_SYM_UNDEFINED) == 0 ? true : false;

                switch (symbol_type) {
                    case WASM_SYMBOL_TYPE_FUNCTION:
                    {
                        read_leb_uint32(p, p_end, elem_idx);
                        if (elem_idx >= module->import_function_count
                                            + module->function_count
                            || (!is_defined
                                && elem_idx >= module->import_function_count)
                            || (is_defined
                                && elem_idx < module->import_function_count)) {
                            set_error_buf_v(error_buf, error_buf_size,
                                            "invalid function symbol index %u",
                                            elem_idx);
                            return false;
                        }
                        if (is_defined) {
                            WASMFunction *function = symbol->u.function =
                                module->functions
                                    [elem_idx - module->import_function_count];
                            read_leb_uint32(p, p_end, name_len);
                            CHECK_BUF(p, p_end, name_len);
                            if (!(name = const_str_list_insert(
                                      p, name_len, module, true, error_buf,
                                      error_buf_size))) {
                                return false;
                            }
                            symbol->name = name;
                            if (!function->name)
                                function->name = name;
                            p += name_len;
                        }
                        else {
                            WASMImport *func_import = symbol->u.import =
                                &module->import_functions[elem_idx];
                            if (symbol_flags & WASM_SYM_EXPLICIT_NAME) {
                                read_leb_uint32(p, p_end, name_len);
                                CHECK_BUF(p, p_end, name_len);
                                if (!(name = const_str_list_insert(
                                          p, name_len, module, true, error_buf,
                                          error_buf_size))) {
                                    return false;
                                }
                                symbol->name = name;
                                symbol->import_name =
                                    func_import->u.names.field_name;
                                p += name_len;
                            }
                            else {
                                symbol->name = func_import->u.names.field_name;
                            }
                            symbol->module_name =
                                func_import->u.names.module_name;
                        }
                        break;
                    }
                    case WASM_SYMBOL_TYPE_GLOBAL:
                    {
                        read_leb_uint32(p, p_end, elem_idx);
                        if (elem_idx >= module->import_global_count
                                            + module->global_count
                            || (!is_defined
                                && elem_idx >= module->import_global_count)
                            || (is_defined
                                && elem_idx < module->import_global_count)) {
                            set_error_buf_v(error_buf, error_buf_size,
                                            "invalid global symbol index %u",
                                            elem_idx);
                            return false;
                        }
                        if (!is_defined
                            && (symbol_flags & WASM_SYMBOL_BINDING_MASK)
                                   == WASM_SYM_BINDING_WEAK) {
                            set_error_buf(error_buf, error_buf_size,
                                          "undefined weak global symbol");
                            return false;
                        }
                        if (is_defined) {
                            WASMGlobal *global = symbol->u.global =
                                &module->globals[elem_idx
                                                 - module->import_global_count];
                            read_leb_uint32(p, p_end, name_len);
                            CHECK_BUF(p, p_end, name_len);
                            if (!(name = const_str_list_insert(
                                      p, name_len, module, true, error_buf,
                                      error_buf_size))) {
                                return false;
                            }
                            symbol->name = name;
                            if (!global->name)
                                global->name = name;
                            p += name_len;
                        }
                        else {
                            WASMImport *global_import = symbol->u.import =
                                &module->import_globals[elem_idx];
                            if (symbol_flags & WASM_SYM_EXPLICIT_NAME) {
                                read_leb_uint32(p, p_end, name_len);
                                CHECK_BUF(p, p_end, name_len);
                                if (!(name = const_str_list_insert(
                                          p, name_len, module, true, error_buf,
                                          error_buf_size))) {
                                    return false;
                                }
                                symbol->name = name;
                                symbol->import_name =
                                    global_import->u.names.field_name;
                                p += name_len;
                            }
                            else {
                                symbol->name =
                                    global_import->u.names.field_name;
                            }
                            symbol->module_name =
                                global_import->u.names.module_name;
                        }
                        break;
                    }
                    case WASM_SYMBOL_TYPE_TABLE:
                    {
                        read_leb_uint32(p, p_end, elem_idx);
                        if (elem_idx >= module->import_table_count
                                            + module->table_count
                            || (!is_defined
                                && elem_idx >= module->import_table_count)
                            || (is_defined
                                && elem_idx < module->import_table_count)) {
                            set_error_buf_v(error_buf, error_buf_size,
                                            "invalid table symbol index %u",
                                            elem_idx);
                            return false;
                        }
                        if (!is_defined
                            && (symbol_flags & WASM_SYMBOL_BINDING_MASK)
                                   == WASM_SYM_BINDING_WEAK) {
                            set_error_buf(error_buf, error_buf_size,
                                          "undefined weak table symbol");
                            return false;
                        }
                        if (is_defined) {
                            WASMTable *table = symbol->u.table =
                                &module->tables[elem_idx
                                                - module->import_table_count];
                            read_leb_uint32(p, p_end, name_len);
                            CHECK_BUF(p, p_end, name_len);
                            if (!(name = const_str_list_insert(
                                      p, name_len, module, true, error_buf,
                                      error_buf_size))) {
                                return false;
                            }
                            symbol->name = name;
                            if (!table->name)
                                table->name = name;
                            p += name_len;
                        }
                        else {
                            WASMImport *table_import = symbol->u.import =
                                &module->import_tables[elem_idx];
                            if (symbol_flags & WASM_SYM_EXPLICIT_NAME) {
                                read_leb_uint32(p, p_end, name_len);
                                CHECK_BUF(p, p_end, name_len);
                                if (!(name = const_str_list_insert(
                                          p, name_len, module, true, error_buf,
                                          error_buf_size))) {
                                    return false;
                                }
                                symbol->name = name;
                                symbol->import_name =
                                    table_import->u.names.field_name;
                                p += name_len;
                            }
                            else {
                                symbol->name = table_import->u.names.field_name;
                            }
                            symbol->module_name =
                                table_import->u.names.module_name;
                        }
                        break;
                    }
                    case WASM_SYMBOL_TYPE_DATA:
                    {
                        read_leb_uint32(p, p_end, name_len);
                        CHECK_BUF(p, p_end, name_len);
                        if (!(name = const_str_list_insert(p, name_len, module,
                                                           true, error_buf,
                                                           error_buf_size))) {
                            return false;
                        }
                        symbol->name = name;
                        p += name_len;
                        if (is_defined) {
                            uint32 seg_index;
                            uint64 data_offset, data_size;
                            read_leb_uint32(p, p_end, seg_index);
                            read_leb_uint64(p, p_end, data_offset);
                            read_leb_uint64(p, p_end, data_size);
                            symbol->u.sym_data.seg_index = seg_index;
                            symbol->u.sym_data.data_offset = data_offset;
                            symbol->u.sym_data.data_size = data_size;
                            if (!(symbol_flags & WASM_SYM_ABSOLUTE)) {
                                if (seg_index >= module->data_seg_count) {
                                    set_error_buf_v(
                                        error_buf, error_buf_size,
                                        "invalid data segment index %u",
                                        seg_index);
                                    return false;
                                }
                                if (data_offset
                                    > module->data_segments[seg_index]
                                          ->data_length) {
                                    set_error_buf(error_buf, error_buf_size,
                                                  "invalid data symbol offset");
                                    return false;
                                }
                            }
                        }
                        break;
                    }
                    case WASM_SYMBOL_TYPE_SECTION:
                    {
                        if ((symbol_flags & WASM_SYMBOL_BINDING_MASK)
                            != WASM_SYM_BINDING_LOCAL) {
                            set_error_buf(
                                error_buf, error_buf_size,
                                "section symbols must have local binding");
                            return false;
                        }
                        read_leb_uint32(p, p_end, elem_idx);
                        symbol->u.sym_section.index = elem_idx;
                        /* TODO */
#if 0
                        /* Use somewhat unique section name as symbol name */
                        StringRef SectionName = Sections[Info.ElementIndex].Name;
                        Info.Name = SectionName;
#endif
                        break;
                    }
                    case WASM_SYMBOL_TYPE_TAG:
                    {
                        read_leb_uint32(p, p_end, elem_idx);
                        if (is_defined) {
                            read_leb_uint32(p, p_end, name_len);
                            CHECK_BUF(p, p_end, name_len);
                            p += name_len;
                        }
                        break;
                    }
                    default:
                    {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid symbol type");
                        return false;
                    }
                }
            }
        }
        else if (sub_section_type == WASM_SEGMENT_INFO) {
            read_leb_uint32(p, p_end, seg_count);
            if (seg_count > module->data_seg_count) {
                set_error_buf(error_buf, error_buf_size,
                              "too many segment names");
                return false;
            }
            for (i = 0; i < seg_count; i++) {
                read_leb_uint32(p, p_end, name_len);
                CHECK_BUF(p, p_end, name_len);
                if (!(name =
                          const_str_list_insert(p, name_len, module, true,
                                                error_buf, error_buf_size))) {
                    return false;
                }
                module->data_segments[i]->name = name;
                p += name_len;

                read_leb_uint32(p, p_end, module->data_segments[i]->alignment);
                read_leb_uint32(p, p_end, module->data_segments[i]->flags);
            }
        }
        else {
            LOG_WARNING(
                "warning: ignore sub section (type=%u) in linking metadata",
                sub_section_type);
            p += sub_section_size;
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    return true;
fail:
    return false;
}

/**
 * Parse "reloc.CODE" and "reloc.DATA" sections, refer to function
 *   Error WasmObjectFile::parseRelocSection(StringRef Name, ReadContext &Ctx)
 * in
 * https://github.com/llvm/llvm-project/blob/main/llvm/lib/Object/WasmObjectFile.cpp
 */

static bool
is_valid_global_symbol(const WASMModule *module, uint32 index)
{
    return (index < module->import_global_count + module->global_count) ? true
                                                                        : false;
}

static bool
is_valid_data_symbol(const WASMModule *module, uint32 index)
{
    return (index < module->symbol_count
            && module->symbols[index].type == WASM_SYMBOL_TYPE_DATA)
               ? true
               : false;
}

static bool
is_valid_func_symbol(const WASMModule *module, uint32 index)
{
    return (index < module->symbol_count
            && module->symbols[index].type == WASM_SYMBOL_TYPE_FUNCTION)
               ? true
               : false;
}

static bool
is_valid_table_symbol(const WASMModule *module, uint32 index)
{
    return (index < module->symbol_count
            && module->symbols[index].type == WASM_SYMBOL_TYPE_TABLE)
               ? true
               : false;
}
static bool
is_valid_tag_symbol(const WASMModule *module, uint32 index)
{
    return (index < module->symbol_count
            && module->symbols[index].type == WASM_SYMBOL_TYPE_TAG)
               ? true
               : false;
}

static bool
is_valid_section_symbol(const WASMModule *module, uint32 index)
{
    return (index < module->symbol_count
            && module->symbols[index].type == WASM_SYMBOL_TYPE_TAG)
               ? true
               : false;
}

static const WASMSection *
get_wasm_section(const WASMSection *sections, uint32 section_index)
{
    const WASMSection *section = sections;

    while (section) {
        if (section->section_index == section_index)
            return section;
        section = section->next;
    }

    return NULL;
}

static bool
handle_reloc_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                     bool is_code_reloc, const WASMSection *sections,
                     char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    WASMRelocation *relocs, *reloc;
    const WASMSection *section;
    uint32 section_index, reloc_count, i;
    uint32 previous_offset = 0, end_offset, size;
    uint64 total_size;

    if (p >= p_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }

    read_leb_uint32(p, p_end, section_index);
    if (!(section = get_wasm_section(sections, section_index))
        || (is_code_reloc && section->section_type != SECTION_TYPE_CODE)
        || (!is_code_reloc && section->section_type != SECTION_TYPE_DATA)) {
        set_error_buf(error_buf, error_buf_size,
                      "invalid section index in relocation section");
        return false;
    }
    end_offset = section->section_body_size;

    read_leb_uint32(p, p_end, reloc_count);
    if (reloc_count > 0) {
        total_size = (uint64)sizeof(WASMRelocation) * reloc_count;
        if (!(relocs = reloc =
                  loader_malloc(total_size, error_buf, error_buf_size))) {
            return false;
        }
        if (is_code_reloc) {
            module->code_relocs = relocs;
            module->code_reloc_count = reloc_count;
        }
        else {
            module->data_relocs = relocs;
            module->data_reloc_count = reloc_count;
        }

        for (i = 0; i < reloc_count; i++, reloc++) {
            read_leb_uint32(p, p_end, reloc->type);
            read_leb_uint32(p, p_end, reloc->offset);
            read_leb_uint32(p, p_end, reloc->index);

            if (reloc->offset < previous_offset) {
                set_error_buf(error_buf, error_buf_size,
                              "relocations not in offset order");
                return false;
            }
            previous_offset = reloc->offset;

            switch (reloc->type) {
                case R_WASM_FUNCTION_INDEX_LEB:
                case R_WASM_FUNCTION_INDEX_I32:
                case R_WASM_TABLE_INDEX_SLEB:
                case R_WASM_TABLE_INDEX_SLEB64:
                case R_WASM_TABLE_INDEX_I32:
                case R_WASM_TABLE_INDEX_I64:
                case R_WASM_TABLE_INDEX_REL_SLEB:
                case R_WASM_TABLE_INDEX_REL_SLEB64:
                    if (!is_valid_func_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid function relocation");
                        return false;
                    }
                    break;
                case R_WASM_TABLE_NUMBER_LEB:
                    if (!is_valid_table_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid table relocation");
                        return false;
                    }
                    break;
                case R_WASM_TYPE_INDEX_LEB:
                    if (reloc->index >= module->type_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown type");
                        return false;
                    }
                    break;
                case R_WASM_GLOBAL_INDEX_LEB:
                    /* R_WASM_GLOBAL_INDEX_LEB are can be used against function
                       and data symbols to refer to their GOT entries. */
                    if (!is_valid_global_symbol(module, reloc->index)
                        && !is_valid_data_symbol(module, reloc->index)
                        && !is_valid_func_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid global relocation");
                        return false;
                    }
                    break;
                case R_WASM_GLOBAL_INDEX_I32:
                    if (!is_valid_global_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid global relocation");
                        return false;
                    }
                    break;
                case R_WASM_TAG_INDEX_LEB:
                    if (!is_valid_tag_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid tag relocation");
                        return false;
                    }
                    break;
                case R_WASM_MEMORY_ADDR_LEB:
                case R_WASM_MEMORY_ADDR_SLEB:
                case R_WASM_MEMORY_ADDR_I32:
                case R_WASM_MEMORY_ADDR_REL_SLEB:
                case R_WASM_MEMORY_ADDR_TLS_SLEB:
                case R_WASM_MEMORY_ADDR_LOCREL_I32:
                    if (!is_valid_data_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid data relocation");
                        return false;
                    }
                    read_leb_int32(p, p_end, reloc->addend);
                    break;
                case R_WASM_MEMORY_ADDR_LEB64:
                case R_WASM_MEMORY_ADDR_SLEB64:
                case R_WASM_MEMORY_ADDR_I64:
                case R_WASM_MEMORY_ADDR_REL_SLEB64:
                case R_WASM_MEMORY_ADDR_TLS_SLEB64:
                    if (!is_valid_data_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid data relocation");
                        return false;
                    }
                    read_leb_int64(p, p_end, reloc->addend);
                    break;
                case R_WASM_FUNCTION_OFFSET_I32:
                    if (!is_valid_func_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid function relocation");
                        return false;
                    }
                    read_leb_int32(p, p_end, reloc->addend);
                    break;
                case R_WASM_FUNCTION_OFFSET_I64:
                    if (!is_valid_func_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid function relocation");
                        return false;
                    }
                    read_leb_int64(p, p_end, reloc->addend);
                    break;
                case R_WASM_SECTION_OFFSET_I32:
                    if (!is_valid_section_symbol(module, reloc->index)) {
                        set_error_buf(error_buf, error_buf_size,
                                      "invalid section relocation");
                        return false;
                    }
                    read_leb_int32(p, p_end, reloc->addend);
                    break;
                default:
                    set_error_buf_v(error_buf, error_buf_size,
                                    "invalid relocation type %u", reloc->type);
                    return false;
            }

            /*
             * Relocations must fit inside the section, and must appear
             * in order. They also shouldn't overlap a function/element
             * boundary, but we don't bother to check that.
             */
            size = 5;
            if (reloc->type == R_WASM_MEMORY_ADDR_LEB64
                || reloc->type == R_WASM_MEMORY_ADDR_SLEB64
                || reloc->type == R_WASM_MEMORY_ADDR_REL_SLEB64)
                size = 10;
            else if (reloc->type == R_WASM_TABLE_INDEX_I32
                     || reloc->type == R_WASM_MEMORY_ADDR_I32
                     || reloc->type == R_WASM_MEMORY_ADDR_LOCREL_I32
                     || reloc->type == R_WASM_SECTION_OFFSET_I32
                     || reloc->type == R_WASM_FUNCTION_OFFSET_I32
                     || reloc->type == R_WASM_FUNCTION_INDEX_I32
                     || reloc->type == R_WASM_GLOBAL_INDEX_I32)
                size = 4;
            else if (reloc->type == R_WASM_TABLE_INDEX_I64
                     || reloc->type == R_WASM_MEMORY_ADDR_I64
                     || reloc->type == R_WASM_FUNCTION_OFFSET_I64)
                size = 8;
            if (reloc->offset + size > end_offset) {
                set_error_buf(error_buf, error_buf_size,
                              "invalid relocation offset");
                return false;
            }
        }
    }

    if (p != p_end) {
        set_error_buf(error_buf, error_buf_size, "section size mismatch");
        return false;
    }

    return true;
fail:
    return false;
}

static bool
handle_name_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                    bool is_load_from_file_buf, char *error_buf,
                    uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    uint32 name_type, subsection_size;
    uint32 previous_name_type = 0;
    uint32 num_func_name;
    uint32 func_index;
    uint32 previous_func_index = ~0U;
    uint32 func_name_len;
    uint32 name_index;
    int i = 0;

    if (p >= p_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }

    while (p < p_end) {
        read_leb_uint32(p, p_end, name_type);
        if (i != 0) {
            if (name_type == previous_name_type) {
                set_error_buf(error_buf, error_buf_size,
                              "duplicate sub-section");
                return false;
            }
            if (name_type < previous_name_type) {
                set_error_buf(error_buf, error_buf_size,
                              "out-of-order sub-section");
                return false;
            }
        }
        previous_name_type = name_type;
        read_leb_uint32(p, p_end, subsection_size);
        CHECK_BUF(p, p_end, subsection_size);
        switch (name_type) {
            case SUB_SECTION_TYPE_FUNC:
                if (subsection_size) {
                    read_leb_uint32(p, p_end, num_func_name);
                    for (name_index = 0; name_index < num_func_name;
                         name_index++) {
                        read_leb_uint32(p, p_end, func_index);
                        if (func_index == previous_func_index) {
                            set_error_buf(error_buf, error_buf_size,
                                          "duplicate function name");
                            return false;
                        }
                        if (func_index < previous_func_index
                            && previous_func_index != ~0U) {
                            set_error_buf(error_buf, error_buf_size,
                                          "out-of-order function index ");
                            return false;
                        }
                        previous_func_index = func_index;
                        read_leb_uint32(p, p_end, func_name_len);
                        CHECK_BUF(p, p_end, func_name_len);
                        /* Skip the import functions */
                        if (func_index >= module->import_function_count) {
                            func_index -= module->import_function_count;
                            if (func_index >= module->function_count) {
                                set_error_buf(error_buf, error_buf_size,
                                              "out-of-range function index");
                                return false;
                            }
                            if (!(module->functions[func_index]->name =
                                      const_str_list_insert(
                                          p, func_name_len, module, false,
                                          error_buf, error_buf_size))) {
                                return false;
                            }
                        }
                        p += func_name_len;
                    }
                }
                break;
            case SUB_SECTION_TYPE_MODULE: /* TODO: Parse module sub section */
            case SUB_SECTION_TYPE_LOCAL:  /* TODO: Parse local sub section */
            default:
                p = p + subsection_size;
                break;
        }
        i++;
    }

    return true;
fail:
    return false;
}

static bool
load_user_section(const uint8 *buf, const uint8 *buf_end, WASMModule *module,
                  bool is_load_from_file_buf, WASMSection *sections,
                  char *error_buf, uint32 error_buf_size)
{
    const uint8 *p = buf, *p_end = buf_end;
    char section_name[32];
    uint32 name_len, buffer_len;

    if (p >= p_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }

    read_leb_uint32(p, p_end, name_len);

    if (p + name_len > p_end) {
        set_error_buf(error_buf, error_buf_size, "unexpected end");
        return false;
    }

    if (!check_utf8_str(p, name_len)) {
        set_error_buf(error_buf, error_buf_size, "invalid UTF-8 encoding");
        return false;
    }

    buffer_len = sizeof(section_name);
    memset(section_name, 0, buffer_len);
    if (name_len < buffer_len) {
        bh_memcpy_s(section_name, buffer_len, p, name_len);
    }
    else {
        bh_memcpy_s(section_name, buffer_len, p, buffer_len - 4);
        memset(section_name + buffer_len - 4, '.', 3);
    }

    if (name_len == 4 && !memcmp(p, "name", 4)) {
        module->name_section_buf = buf;
        module->name_section_buf_end = buf_end;
        p += name_len;
        if (!handle_name_section(p, p_end, module, is_load_from_file_buf,
                                 error_buf, error_buf_size)) {
            return false;
        }
        LOG_VERBOSE("Load custom name section success.");
    }
    else if (name_len == 7 && !memcmp(p, "linking", 7)) {
        p += name_len;
        if (!handle_linking_section(p, p_end, module, error_buf,
                                    error_buf_size)) {
            return false;
        }
        LOG_VERBOSE("Load linking section success.");
    }
    else if (name_len == 10 && !memcmp(p, "reloc.CODE", 10)) {
        p += name_len;
        if (!handle_reloc_section(p, p_end, module, true, sections, error_buf,
                                  error_buf_size)) {
            return false;
        }
        LOG_VERBOSE("Load reloc.CODE section success.");
    }
    else if (name_len == 10 && !memcmp(p, "reloc.DATA", 10)) {
        p += name_len;
        if (!handle_reloc_section(p, p_end, module, false, sections, error_buf,
                                  error_buf_size)) {
            return false;
        }
        LOG_VERBOSE("Load reloc.DATA section success.");
    }

    WASMCustomSection *section =
        loader_malloc(sizeof(WASMCustomSection), error_buf, error_buf_size);

    if (!section) {
        return false;
    }

    section->name_addr = (char *)p;
    section->name_len = name_len;
    section->content_addr = (uint8 *)(p + name_len);
    section->content_len = (uint32)(p_end - p - name_len);
    section->next = module->custom_section_list;
    module->custom_section_list = section;

    LOG_VERBOSE("Load custom section [%s] success.", section_name);

    return true;
fail:
    return false;
}

static void
calculate_global_data_offset(WASMModule *module)
{
    uint32 i, data_offset;

    data_offset = 0;
    for (i = 0; i < module->import_global_count; i++) {
        WASMGlobalImport *import_global =
            &((module->import_globals + i)->u.global);
        data_offset += wasm_value_type_size(import_global->type);
    }

    for (i = 0; i < module->global_count; i++) {
        WASMGlobal *global = module->globals + i;
        data_offset += wasm_value_type_size(global->type);
    }

    module->global_data_size = data_offset;
}

static bool
wasm_loader_prepare_bytecode(WASMModule *module, WASMFunction *func,
                             uint32 cur_func_idx, char *error_buf,
                             uint32 error_buf_size);

static bool
load_from_sections(WASMModule *module, WASMSection *sections,
                   bool is_load_from_file_buf, char *error_buf,
                   uint32 error_buf_size)
{
    WASMExport *export;
    WASMSection *section = sections;
    const uint8 *buf, *buf_end, *buf_code = NULL, *buf_code_end = NULL,
                                *buf_func = NULL, *buf_func_end = NULL;
    WASMGlobal *aux_data_end_global = NULL, *aux_heap_base_global = NULL;
    WASMGlobal *aux_stack_top_global = NULL, *global;
    uint64 aux_data_end = (uint64)-1LL, aux_heap_base = (uint64)-1LL,
           aux_stack_top = (uint64)-1LL;
    uint32 global_index, func_index, i;
    uint32 aux_data_end_global_index = (uint32)-1;
    uint32 aux_heap_base_global_index = (uint32)-1;
    WASMType *func_type;

    /* Find code and function sections if have */
    while (section) {
        if (section->section_type == SECTION_TYPE_CODE) {
            buf_code = section->section_body;
            buf_code_end = buf_code + section->section_body_size;
        }
        else if (section->section_type == SECTION_TYPE_FUNC) {
            buf_func = section->section_body;
            buf_func_end = buf_func + section->section_body_size;
        }
        section = section->next;
    }

    section = sections;
    while (section) {
        buf = section->section_body;
        buf_end = buf + section->section_body_size;
        switch (section->section_type) {
            case SECTION_TYPE_USER:
                /* unsupported user section, ignore it. */
                if (!load_user_section(buf, buf_end, module,
                                       is_load_from_file_buf, sections,
                                       error_buf, error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_TYPE:
                if (!load_type_section(buf, buf_end, module, error_buf,
                                       error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_IMPORT:
                if (!load_import_section(buf, buf_end, module,
                                         is_load_from_file_buf, error_buf,
                                         error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_FUNC:
                if (!load_function_section(buf, buf_end, buf_code, buf_code_end,
                                           module, error_buf, error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_TABLE:
                if (!load_table_section(buf, buf_end, module, error_buf,
                                        error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_MEMORY:
                if (!load_memory_section(buf, buf_end, module, error_buf,
                                         error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_GLOBAL:
                if (!load_global_section(buf, buf_end, module, error_buf,
                                         error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_EXPORT:
                if (!load_export_section(buf, buf_end, module,
                                         is_load_from_file_buf, error_buf,
                                         error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_START:
                if (!load_start_section(buf, buf_end, module, error_buf,
                                        error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_ELEM:
                if (!load_table_segment_section(buf, buf_end, module, error_buf,
                                                error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_CODE:
                if (!load_code_section(buf, buf_end, buf_func, buf_func_end,
                                       module, error_buf, error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_DATA:
                if (!load_data_segment_section(buf, buf_end, module, error_buf,
                                               error_buf_size))
                    return false;
                break;
            case SECTION_TYPE_DATACOUNT:
                if (!load_datacount_section(buf, buf_end, module, error_buf,
                                            error_buf_size))
                    return false;
                break;
            default:
                set_error_buf(error_buf, error_buf_size, "invalid section id");
                return false;
        }

        section = section->next;
    }

    module->aux_data_end_global_index = (uint32)-1;
    module->aux_heap_base_global_index = (uint32)-1;
    module->aux_stack_top_global_index = (uint32)-1;

    /* Resolve auxiliary data/stack/heap info and reset memory info */
    export = module->exports;
    for (i = 0; i < module->export_count; i++, export ++) {
        if (export->kind == EXPORT_KIND_GLOBAL) {
            if (!strcmp(export->name, "__heap_base")) {
                global_index = export->index - module->import_global_count;
                global = module->globals + global_index;
                if (global->type == VALUE_TYPE_I32 && !global->is_mutable
                    && global->init_expr.init_expr_type
                           == INIT_EXPR_TYPE_I32_CONST) {
                    aux_heap_base_global = global;
                    aux_heap_base = (uint64)global->init_expr.u.u32;
                    aux_heap_base_global_index = export->index;
                    LOG_VERBOSE("Found aux __heap_base global, value: %" PRIu64,
                                aux_heap_base);
                }
            }
            else if (!strcmp(export->name, "__data_end")) {
                global_index = export->index - module->import_global_count;
                global = module->globals + global_index;
                if (global->type == VALUE_TYPE_I32 && !global->is_mutable
                    && global->init_expr.init_expr_type
                           == INIT_EXPR_TYPE_I32_CONST) {
                    aux_data_end_global = global;
                    aux_data_end = (uint64)global->init_expr.u.u32;
                    aux_data_end_global_index = export->index;
                    LOG_VERBOSE("Found aux __data_end global, value: %" PRIu64,
                                aux_data_end);

                    aux_data_end = align_uint64(aux_data_end, 16);
                }
            }

            /* For module compiled with -pthread option, the global is:
                [0] stack_top       <-- 0
                [1] tls_pointer
                [2] tls_size
                [3] data_end        <-- 3
                [4] global_base
                [5] heap_base       <-- 5
                [6] dso_handle

                For module compiled without -pthread option:
                [0] stack_top       <-- 0
                [1] data_end        <-- 1
                [2] global_base
                [3] heap_base       <-- 3
                [4] dso_handle
            */
            if (aux_data_end_global && aux_heap_base_global
                && aux_data_end <= aux_heap_base) {
                module->aux_data_end_global_index = aux_data_end_global_index;
                module->aux_data_end = aux_data_end;
                module->aux_heap_base_global_index = aux_heap_base_global_index;
                module->aux_heap_base = aux_heap_base;

                /* Resolve aux stack top global */
                for (global_index = 0; global_index < module->global_count;
                     global_index++) {
                    global = module->globals + global_index;
                    if (global->is_mutable /* heap_base and data_end is
                                              not mutable */
                        && global->type == VALUE_TYPE_I32
                        && global->init_expr.init_expr_type
                               == INIT_EXPR_TYPE_I32_CONST
                        && (uint64)global->init_expr.u.u32 <= aux_heap_base) {
                        aux_stack_top_global = global;
                        aux_stack_top = (uint64)global->init_expr.u.u32;
                        module->aux_stack_top_global_index =
                            module->import_global_count + global_index;
                        module->aux_stack_bottom = aux_stack_top;
                        module->aux_stack_size =
                            aux_stack_top > aux_data_end
                                ? (uint32)(aux_stack_top - aux_data_end)
                                : (uint32)aux_stack_top;
                        LOG_VERBOSE(
                            "Found aux stack top global, value: %" PRIu64 ", "
                            "global index: %d, stack size: %d",
                            aux_stack_top, global_index,
                            module->aux_stack_size);
                        break;
                    }
                }
                if (!aux_stack_top_global) {
                    /* Auxiliary stack global isn't found, it must be unused
                       in the wasm app, as if it is used, the global must be
                       defined. Here we set it to __heap_base global and set
                       its size to 0. */
                    aux_stack_top_global = aux_heap_base_global;
                    aux_stack_top = aux_heap_base;
                    module->aux_stack_top_global_index =
                        module->aux_heap_base_global_index;
                    module->aux_stack_bottom = aux_stack_top;
                    module->aux_stack_size = 0;
                }
                break;
            }
        }
    }

    module->malloc_function = (uint32)-1;
    module->free_function = (uint32)-1;
    module->retain_function = (uint32)-1;

    /* Resolve malloc/free function exported by wasm module */
    export = module->exports;
    for (i = 0; i < module->export_count; i++, export ++) {
        if (export->kind == EXPORT_KIND_FUNC) {
            if (!strcmp(export->name, "malloc")
                && export->index >= module->import_function_count) {
                func_index = export->index - module->import_function_count;
                func_type = module->functions[func_index]->func_type;
                if (func_type->param_count == 1 && func_type->result_count == 1
                    && func_type->types[0] == VALUE_TYPE_I32
                    && func_type->types[1] == VALUE_TYPE_I32) {
                    bh_assert(module->malloc_function == (uint32)-1);
                    module->malloc_function = export->index;
                    LOG_VERBOSE("Found malloc function, name: %s, index: %u",
                                export->name, export->index);
                }
            }
            else if (!strcmp(export->name, "__new")
                     && export->index >= module->import_function_count) {
                /* __new && __pin for AssemblyScript */
                func_index = export->index - module->import_function_count;
                func_type = module->functions[func_index]->func_type;
                if (func_type->param_count == 2 && func_type->result_count == 1
                    && func_type->types[0] == VALUE_TYPE_I32
                    && func_type->types[1] == VALUE_TYPE_I32
                    && func_type->types[2] == VALUE_TYPE_I32) {
                    uint32 j;
                    WASMExport *export_tmp;

                    bh_assert(module->malloc_function == (uint32)-1);
                    module->malloc_function = export->index;
                    LOG_VERBOSE("Found malloc function, name: %s, index: %u",
                                export->name, export->index);

                    /* resolve retain function.
                       If not found, reset malloc function index */
                    export_tmp = module->exports;
                    for (j = 0; j < module->export_count; j++, export_tmp++) {
                        if ((export_tmp->kind == EXPORT_KIND_FUNC)
                            && (!strcmp(export_tmp->name, "__retain")
                                || (!strcmp(export_tmp->name, "__pin")))
                            && (export_tmp->index
                                >= module->import_function_count)) {
                            func_index = export_tmp->index
                                         - module->import_function_count;
                            func_type =
                                module->functions[func_index]->func_type;
                            if (func_type->param_count == 1
                                && func_type->result_count == 1
                                && func_type->types[0] == VALUE_TYPE_I32
                                && func_type->types[1] == VALUE_TYPE_I32) {
                                bh_assert(module->retain_function
                                          == (uint32)-1);
                                module->retain_function = export_tmp->index;
                                LOG_VERBOSE("Found retain function, name: %s, "
                                            "index: %u",
                                            export_tmp->name,
                                            export_tmp->index);
                                break;
                            }
                        }
                    }
                    if (j == module->export_count) {
                        module->malloc_function = (uint32)-1;
                        LOG_VERBOSE("Can't find retain function,"
                                    "reset malloc function index to -1");
                    }
                }
            }
            else if (((!strcmp(export->name, "free"))
                      || (!strcmp(export->name, "__release"))
                      || (!strcmp(export->name, "__unpin")))
                     && export->index >= module->import_function_count) {
                func_index = export->index - module->import_function_count;
                func_type = module->functions[func_index]->func_type;
                if (func_type->param_count == 1 && func_type->result_count == 0
                    && func_type->types[0] == VALUE_TYPE_I32) {
                    bh_assert(module->free_function == (uint32)-1);
                    module->free_function = export->index;
                    LOG_VERBOSE("Found free function, name: %s, index: %u",
                                export->name, export->index);
                }
            }
        }
    }

    for (i = 0; i < module->function_count; i++) {
        WASMFunction *func = module->functions[i];
        if (!wasm_loader_prepare_bytecode(module, func, i, error_buf,
                                          error_buf_size)) {
            return false;
        }

        if (i == module->function_count - 1
            && func->code + func->code_size != buf_code_end) {
            set_error_buf(error_buf, error_buf_size,
                          "code section size mismatch");
            return false;
        }
    }

    if (!module->possible_memory_grow) {
        WASMMemoryImport *memory_import;
        WASMMemory *memory;

        if (aux_data_end_global && aux_heap_base_global
            && aux_stack_top_global) {
            uint64 init_memory_size;
            uint64 shrunk_memory_size = align_uint64(aux_heap_base, 8);

            /* Only resize(shrunk) the memory size if num_bytes_per_page is in
             * valid range of uint32 */
            if (shrunk_memory_size <= UINT32_MAX) {
                if (module->import_memory_count) {
                    memory_import = &module->import_memories[0].u.memory;
                    init_memory_size = (uint64)memory_import->num_bytes_per_page
                                       * memory_import->init_page_count;
                    if (shrunk_memory_size <= init_memory_size) {
                        /* Reset memory info to decrease memory usage */
                        memory_import->num_bytes_per_page = shrunk_memory_size;
                        memory_import->init_page_count = 1;
                        LOG_VERBOSE("Shrink import memory size to %" PRIu64,
                                    shrunk_memory_size);
                    }
                }

                if (module->memory_count) {
                    memory = &module->memories[0];
                    init_memory_size = (uint64)memory->num_bytes_per_page
                                       * memory->init_page_count;
                    if (shrunk_memory_size <= init_memory_size) {
                        /* Reset memory info to decrease memory usage */
                        memory->num_bytes_per_page = shrunk_memory_size;
                        memory->init_page_count = 1;
                        LOG_VERBOSE("Shrink memory size to %" PRIu64,
                                    shrunk_memory_size);
                    }
                }
            }
        }

        if (module->import_memory_count) {
            memory_import = &module->import_memories[0].u.memory;
            /* Only resize the memory to one big page if num_bytes_per_page is
               in valid range of uint32 */
            if (memory_import->init_page_count < DEFAULT_MAX_PAGES) {
                memory_import->num_bytes_per_page *=
                    memory_import->init_page_count;

                if (memory_import->init_page_count > 0)
                    memory_import->init_page_count =
                        memory_import->max_page_count = 1;
                else
                    memory_import->init_page_count =
                        memory_import->max_page_count = 0;
            }
        }
        if (module->memory_count) {
            memory = &module->memories[0];
            /* Only resize(shrunk) the memory size if num_bytes_per_page is in
               valid range of uint32 */
            if (memory->init_page_count < DEFAULT_MAX_PAGES) {
                memory->num_bytes_per_page *= memory->init_page_count;

                if (memory->init_page_count > 0)
                    memory->init_page_count = memory->max_page_count = 1;
                else
                    memory->init_page_count = memory->max_page_count = 0;
            }
        }
    }

    calculate_global_data_offset(module);
    return true;
}

static WASMModule *
create_module(char *error_buf, uint32 error_buf_size)
{
    WASMModule *module =
        loader_malloc(sizeof(WASMModule), error_buf, error_buf_size);
    bh_list_status ret;

    if (!module) {
        return NULL;
    }

    /* Set start_function to -1, means no start function */
    module->start_function = (uint32)-1;

    module->br_table_cache_list = &module->br_table_cache_list_head;
    ret = bh_list_init(module->br_table_cache_list);
    bh_assert(ret == BH_LIST_SUCCESS);

    (void)ret;
    return module;
}

static void
destroy_sections(WASMSection *section_list)
{
    WASMSection *section = section_list, *next;
    while (section) {
        next = section->next;
        wasm_runtime_free(section);
        section = next;
    }
}

/* clang-format off */
static uint8 section_ids[] = {
    SECTION_TYPE_USER,
    SECTION_TYPE_TYPE,
    SECTION_TYPE_IMPORT,
    SECTION_TYPE_FUNC,
    SECTION_TYPE_TABLE,
    SECTION_TYPE_MEMORY,
    SECTION_TYPE_GLOBAL,
    SECTION_TYPE_EXPORT,
    SECTION_TYPE_START,
    SECTION_TYPE_ELEM,
    SECTION_TYPE_DATACOUNT,
    SECTION_TYPE_CODE,
    SECTION_TYPE_DATA
};
/* clang-format on */

static uint8
get_section_index(uint8 section_type)
{
    uint8 max_id = sizeof(section_ids) / sizeof(uint8);

    for (uint8 i = 0; i < max_id; i++) {
        if (section_type == section_ids[i])
            return i;
    }

    return (uint8)-1;
}

static bool
create_sections(const uint8 *buf, uint32 size, WASMSection **p_section_list,
                char *error_buf, uint32 error_buf_size)
{
    WASMSection *section_list_end = NULL, *section;
    const uint8 *p = buf, *p_end = buf + size;
    uint8 section_type, section_index, last_section_index = (uint8)-1;
    uint32 section_size, section_count = 0;

    bh_assert(!*p_section_list);

    p += 8;
    while (p < p_end) {
        CHECK_BUF(p, p_end, 1);
        section_type = read_uint8(p);
        section_index = get_section_index(section_type);
        if (section_index != (uint8)-1) {
            if (section_type != SECTION_TYPE_USER) {
                /* Custom sections may be inserted at any place,
                   while other sections must occur at most once
                   and in prescribed order. */
                if (last_section_index != (uint8)-1
                    && (section_index <= last_section_index)) {
                    set_error_buf(error_buf, error_buf_size,
                                  "unexpected content after last section or "
                                  "junk after last section");
                    return false;
                }
                last_section_index = section_index;
            }
            read_leb_uint32(p, p_end, section_size);
            CHECK_BUF1(p, p_end, section_size);

            if (!(section = loader_malloc(sizeof(WASMSection), error_buf,
                                          error_buf_size))) {
                return false;
            }

            section->section_type = section_type;
            section->section_index = section_count++;
            section->section_body = (uint8 *)p;
            section->section_body_size = section_size;

            if (!section_list_end)
                *p_section_list = section_list_end = section;
            else {
                section_list_end->next = section;
                section_list_end = section;
            }

            p += section_size;
        }
        else {
            set_error_buf(error_buf, error_buf_size, "invalid section id");
            return false;
        }
    }

    return true;
fail:
    return false;
}

static void
exchange32(uint8 *p_data)
{
    uint8 value = *p_data;
    *p_data = *(p_data + 3);
    *(p_data + 3) = value;

    value = *(p_data + 1);
    *(p_data + 1) = *(p_data + 2);
    *(p_data + 2) = value;
}

static union {
    int a;
    char b;
} __ue = { .a = 1 };

#define is_little_endian() (__ue.b == 1)

static bool
load(const uint8 *buf, uint32 size, WASMModule *module, char *error_buf,
     uint32 error_buf_size)
{
    const uint8 *buf_end = buf + size;
    const uint8 *p = buf, *p_end = buf_end;
    uint32 magic_number, version;
    WASMSection *section_list = NULL;

    CHECK_BUF1(p, p_end, sizeof(uint32));
    magic_number = read_uint32(p);
    if (!is_little_endian())
        exchange32((uint8 *)&magic_number);

    if (magic_number != WASM_MAGIC_NUMBER) {
        set_error_buf(error_buf, error_buf_size, "magic header not detected");
        return false;
    }

    CHECK_BUF1(p, p_end, sizeof(uint32));
    version = read_uint32(p);
    if (!is_little_endian())
        exchange32((uint8 *)&version);

    if (version != WASM_CURRENT_VERSION) {
        set_error_buf(error_buf, error_buf_size, "unknown binary version");
        return false;
    }

    if (!create_sections(buf, size, &section_list, error_buf, error_buf_size)
        || !load_from_sections(module, section_list, true, error_buf,
                               error_buf_size)) {
        destroy_sections(section_list);
        return false;
    }

    destroy_sections(section_list);
    return true;
fail:
    return false;
}

WASMModule *
wasm_loader_load(uint8 *buf, uint32 size, char *error_buf,
                 uint32 error_buf_size)
{
    WASMModule *module = create_module(error_buf, error_buf_size);
    if (!module) {
        return NULL;
    }

    if (!load(buf, size, module, error_buf, error_buf_size)) {
        goto fail;
    }

    LOG_VERBOSE("Load module success.\n");
    return module;

fail:
    wasm_loader_unload(module);
    return NULL;
}

void
wasm_loader_unload(WASMModule *module)
{
    uint32 i;

    if (!module)
        return;

    if (module->types) {
        for (i = 0; i < module->type_count; i++) {
            if (module->types[i])
                destroy_wasm_type(module->types[i]);
        }
        wasm_runtime_free(module->types);
    }

    if (module->imports)
        wasm_runtime_free(module->imports);

    if (module->functions) {
        for (i = 0; i < module->function_count; i++) {
            if (module->functions[i]) {
                if (module->functions[i]->local_offsets)
                    wasm_runtime_free(module->functions[i]->local_offsets);
                wasm_runtime_free(module->functions[i]);
            }
        }
        wasm_runtime_free(module->functions);
    }

    if (module->tables)
        wasm_runtime_free(module->tables);

    if (module->memories)
        wasm_runtime_free(module->memories);

    if (module->globals)
        wasm_runtime_free(module->globals);

    if (module->exports)
        wasm_runtime_free(module->exports);

    if (module->table_segments) {
        for (i = 0; i < module->table_seg_count; i++) {
            if (module->table_segments[i].func_indexes)
                wasm_runtime_free(module->table_segments[i].func_indexes);
        }
        wasm_runtime_free(module->table_segments);
    }

    if (module->data_segments) {
        for (i = 0; i < module->data_seg_count; i++) {
            if (module->data_segments[i])
                wasm_runtime_free(module->data_segments[i]);
        }
        wasm_runtime_free(module->data_segments);
    }

    if (module->const_str_list) {
        StringNode *node = module->const_str_list, *node_next;
        while (node) {
            node_next = node->next;
            wasm_runtime_free(node);
            node = node_next;
        }
    }

    if (module->br_table_cache_list) {
        BrTableCache *node = bh_list_first_elem(module->br_table_cache_list);
        BrTableCache *node_next;
        while (node) {
            node_next = bh_list_elem_next(node);
            wasm_runtime_free(node);
            node = node_next;
        }
    }

    wasm_runtime_destroy_custom_sections(module->custom_section_list);

    if (module->code_relocs)
        wasm_runtime_free(module->code_relocs);

    if (module->data_relocs)
        wasm_runtime_free(module->data_relocs);

    if (module->symbols)
        wasm_runtime_free(module->symbols);

    wasm_runtime_free(module);
}

bool
wasm_loader_find_block_addr(BlockAddr *block_addr_cache,
                            const uint8 *start_addr, const uint8 *code_end_addr,
                            uint8 label_type, uint8 **p_else_addr,
                            uint8 **p_end_addr)
{
    const uint8 *p = start_addr, *p_end = code_end_addr;
    uint8 *else_addr = NULL;
    char error_buf[128];
    uint32 block_nested_depth = 1, count, i, j, t;
    uint32 error_buf_size = sizeof(error_buf);
    uint8 opcode, u8;
    BlockAddr block_stack[16] = { { 0 } }, *block;

    i = ((uintptr_t)start_addr) & (uintptr_t)(BLOCK_ADDR_CACHE_SIZE - 1);
    block = block_addr_cache + BLOCK_ADDR_CONFLICT_SIZE * i;

    for (j = 0; j < BLOCK_ADDR_CONFLICT_SIZE; j++) {
        if (block[j].start_addr == start_addr) {
            /* Cache hit */
            *p_else_addr = block[j].else_addr;
            *p_end_addr = block[j].end_addr;
            return true;
        }
    }

    /* Cache unhit */
    block_stack[0].start_addr = start_addr;

    while (p < code_end_addr) {
        opcode = *p++;
        switch (opcode) {
            case WASM_OP_UNREACHABLE:
            case WASM_OP_NOP:
                break;

            case WASM_OP_BLOCK:
            case WASM_OP_LOOP:
            case WASM_OP_IF:
                /* block result type: 0x40/0x7F/0x7E/0x7D/0x7C */
                u8 = read_uint8(p);
                if (block_nested_depth
                    < sizeof(block_stack) / sizeof(BlockAddr)) {
                    block_stack[block_nested_depth].start_addr = p;
                    block_stack[block_nested_depth].else_addr = NULL;
                }
                block_nested_depth++;
                break;

            case EXT_OP_BLOCK:
            case EXT_OP_LOOP:
            case EXT_OP_IF:
                /* block type */
                skip_leb_uint32(p, p_end);
                if (block_nested_depth
                    < sizeof(block_stack) / sizeof(BlockAddr)) {
                    block_stack[block_nested_depth].start_addr = p;
                    block_stack[block_nested_depth].else_addr = NULL;
                }
                block_nested_depth++;
                break;

            case WASM_OP_ELSE:
                if (label_type == LABEL_TYPE_IF && block_nested_depth == 1)
                    else_addr = (uint8 *)(p - 1);
                if (block_nested_depth - 1
                    < sizeof(block_stack) / sizeof(BlockAddr))
                    block_stack[block_nested_depth - 1].else_addr =
                        (uint8 *)(p - 1);
                break;

            case WASM_OP_END:
                if (block_nested_depth == 1) {
                    if (label_type == LABEL_TYPE_IF)
                        *p_else_addr = else_addr;
                    *p_end_addr = (uint8 *)(p - 1);

                    block_stack[0].end_addr = (uint8 *)(p - 1);
                    for (t = 0; t < sizeof(block_stack) / sizeof(BlockAddr);
                         t++) {
                        start_addr = block_stack[t].start_addr;
                        if (start_addr) {
                            i = ((uintptr_t)start_addr)
                                & (uintptr_t)(BLOCK_ADDR_CACHE_SIZE - 1);
                            block =
                                block_addr_cache + BLOCK_ADDR_CONFLICT_SIZE * i;
                            for (j = 0; j < BLOCK_ADDR_CONFLICT_SIZE; j++)
                                if (!block[j].start_addr)
                                    break;

                            if (j == BLOCK_ADDR_CONFLICT_SIZE) {
                                memmove(block + 1, block,
                                        (BLOCK_ADDR_CONFLICT_SIZE - 1)
                                            * sizeof(BlockAddr));
                                j = 0;
                            }
                            block[j].start_addr = block_stack[t].start_addr;
                            block[j].else_addr = block_stack[t].else_addr;
                            block[j].end_addr = block_stack[t].end_addr;
                        }
                        else
                            break;
                    }
                    return true;
                }
                else {
                    block_nested_depth--;
                    if (block_nested_depth
                        < sizeof(block_stack) / sizeof(BlockAddr))
                        block_stack[block_nested_depth].end_addr =
                            (uint8 *)(p - 1);
                }
                break;

            case WASM_OP_BR:
            case WASM_OP_BR_IF:
                skip_leb_uint32(p, p_end); /* labelidx */
                break;

            case WASM_OP_BR_TABLE:
                read_leb_uint32(p, p_end, count); /* lable num */
                p += count + 1;
                while (*p == WASM_OP_NOP)
                    p++;
                break;

            case EXT_OP_BR_TABLE_CACHE:
                read_leb_uint32(p, p_end, count); /* lable num */
                while (*p == WASM_OP_NOP)
                    p++;
                break;

            case WASM_OP_RETURN:
                break;

            case WASM_OP_CALL:
            case WASM_OP_RETURN_CALL:
                skip_leb_uint32(p, p_end); /* funcidx */
                break;

            case WASM_OP_CALL_INDIRECT:
            case WASM_OP_RETURN_CALL_INDIRECT:
                skip_leb_uint32(p, p_end); /* typeidx */
                CHECK_BUF(p, p_end, 1);
                u8 = read_uint8(p); /* 0x00 */
                break;

            case WASM_OP_DROP:
            case WASM_OP_SELECT:
            case WASM_OP_DROP_64:
            case WASM_OP_SELECT_64:
                break;

            case WASM_OP_GET_LOCAL:
            case WASM_OP_SET_LOCAL:
            case WASM_OP_TEE_LOCAL:
            case WASM_OP_GET_GLOBAL:
            case WASM_OP_SET_GLOBAL:
            case WASM_OP_GET_GLOBAL_64:
            case WASM_OP_SET_GLOBAL_64:
            case WASM_OP_SET_GLOBAL_AUX_STACK:
                skip_leb_uint32(p, p_end); /* local index */
                break;

            case EXT_OP_GET_LOCAL_FAST:
            case EXT_OP_SET_LOCAL_FAST:
            case EXT_OP_TEE_LOCAL_FAST:
                CHECK_BUF(p, p_end, 1);
                p++;
                break;

            case WASM_OP_I32_LOAD:
            case WASM_OP_I64_LOAD:
            case WASM_OP_F32_LOAD:
            case WASM_OP_F64_LOAD:
            case WASM_OP_I32_LOAD8_S:
            case WASM_OP_I32_LOAD8_U:
            case WASM_OP_I32_LOAD16_S:
            case WASM_OP_I32_LOAD16_U:
            case WASM_OP_I64_LOAD8_S:
            case WASM_OP_I64_LOAD8_U:
            case WASM_OP_I64_LOAD16_S:
            case WASM_OP_I64_LOAD16_U:
            case WASM_OP_I64_LOAD32_S:
            case WASM_OP_I64_LOAD32_U:
            case WASM_OP_I32_STORE:
            case WASM_OP_I64_STORE:
            case WASM_OP_F32_STORE:
            case WASM_OP_F64_STORE:
            case WASM_OP_I32_STORE8:
            case WASM_OP_I32_STORE16:
            case WASM_OP_I64_STORE8:
            case WASM_OP_I64_STORE16:
            case WASM_OP_I64_STORE32:
                skip_leb_uint32(p, p_end);     /* align */
                skip_leb_mem_offset(p, p_end); /* offset */
                break;

            case WASM_OP_MEMORY_SIZE:
            case WASM_OP_MEMORY_GROW:
                skip_leb_uint32(p, p_end); /* 0x00 */
                break;

            case WASM_OP_I32_CONST:
                skip_leb_int32(p, p_end);
                break;
            case WASM_OP_I64_CONST:
                skip_leb_int64(p, p_end);
                break;
            case WASM_OP_F32_CONST:
                p += sizeof(float32);
                break;
            case WASM_OP_F64_CONST:
                p += sizeof(float64);
                break;

            case WASM_OP_I32_EQZ:
            case WASM_OP_I32_EQ:
            case WASM_OP_I32_NE:
            case WASM_OP_I32_LT_S:
            case WASM_OP_I32_LT_U:
            case WASM_OP_I32_GT_S:
            case WASM_OP_I32_GT_U:
            case WASM_OP_I32_LE_S:
            case WASM_OP_I32_LE_U:
            case WASM_OP_I32_GE_S:
            case WASM_OP_I32_GE_U:
            case WASM_OP_I64_EQZ:
            case WASM_OP_I64_EQ:
            case WASM_OP_I64_NE:
            case WASM_OP_I64_LT_S:
            case WASM_OP_I64_LT_U:
            case WASM_OP_I64_GT_S:
            case WASM_OP_I64_GT_U:
            case WASM_OP_I64_LE_S:
            case WASM_OP_I64_LE_U:
            case WASM_OP_I64_GE_S:
            case WASM_OP_I64_GE_U:
            case WASM_OP_F32_EQ:
            case WASM_OP_F32_NE:
            case WASM_OP_F32_LT:
            case WASM_OP_F32_GT:
            case WASM_OP_F32_LE:
            case WASM_OP_F32_GE:
            case WASM_OP_F64_EQ:
            case WASM_OP_F64_NE:
            case WASM_OP_F64_LT:
            case WASM_OP_F64_GT:
            case WASM_OP_F64_LE:
            case WASM_OP_F64_GE:
            case WASM_OP_I32_CLZ:
            case WASM_OP_I32_CTZ:
            case WASM_OP_I32_POPCNT:
            case WASM_OP_I32_ADD:
            case WASM_OP_I32_SUB:
            case WASM_OP_I32_MUL:
            case WASM_OP_I32_DIV_S:
            case WASM_OP_I32_DIV_U:
            case WASM_OP_I32_REM_S:
            case WASM_OP_I32_REM_U:
            case WASM_OP_I32_AND:
            case WASM_OP_I32_OR:
            case WASM_OP_I32_XOR:
            case WASM_OP_I32_SHL:
            case WASM_OP_I32_SHR_S:
            case WASM_OP_I32_SHR_U:
            case WASM_OP_I32_ROTL:
            case WASM_OP_I32_ROTR:
            case WASM_OP_I64_CLZ:
            case WASM_OP_I64_CTZ:
            case WASM_OP_I64_POPCNT:
            case WASM_OP_I64_ADD:
            case WASM_OP_I64_SUB:
            case WASM_OP_I64_MUL:
            case WASM_OP_I64_DIV_S:
            case WASM_OP_I64_DIV_U:
            case WASM_OP_I64_REM_S:
            case WASM_OP_I64_REM_U:
            case WASM_OP_I64_AND:
            case WASM_OP_I64_OR:
            case WASM_OP_I64_XOR:
            case WASM_OP_I64_SHL:
            case WASM_OP_I64_SHR_S:
            case WASM_OP_I64_SHR_U:
            case WASM_OP_I64_ROTL:
            case WASM_OP_I64_ROTR:
            case WASM_OP_F32_ABS:
            case WASM_OP_F32_NEG:
            case WASM_OP_F32_CEIL:
            case WASM_OP_F32_FLOOR:
            case WASM_OP_F32_TRUNC:
            case WASM_OP_F32_NEAREST:
            case WASM_OP_F32_SQRT:
            case WASM_OP_F32_ADD:
            case WASM_OP_F32_SUB:
            case WASM_OP_F32_MUL:
            case WASM_OP_F32_DIV:
            case WASM_OP_F32_MIN:
            case WASM_OP_F32_MAX:
            case WASM_OP_F32_COPYSIGN:
            case WASM_OP_F64_ABS:
            case WASM_OP_F64_NEG:
            case WASM_OP_F64_CEIL:
            case WASM_OP_F64_FLOOR:
            case WASM_OP_F64_TRUNC:
            case WASM_OP_F64_NEAREST:
            case WASM_OP_F64_SQRT:
            case WASM_OP_F64_ADD:
            case WASM_OP_F64_SUB:
            case WASM_OP_F64_MUL:
            case WASM_OP_F64_DIV:
            case WASM_OP_F64_MIN:
            case WASM_OP_F64_MAX:
            case WASM_OP_F64_COPYSIGN:
            case WASM_OP_I32_WRAP_I64:
            case WASM_OP_I32_TRUNC_S_F32:
            case WASM_OP_I32_TRUNC_U_F32:
            case WASM_OP_I32_TRUNC_S_F64:
            case WASM_OP_I32_TRUNC_U_F64:
            case WASM_OP_I64_EXTEND_S_I32:
            case WASM_OP_I64_EXTEND_U_I32:
            case WASM_OP_I64_TRUNC_S_F32:
            case WASM_OP_I64_TRUNC_U_F32:
            case WASM_OP_I64_TRUNC_S_F64:
            case WASM_OP_I64_TRUNC_U_F64:
            case WASM_OP_F32_CONVERT_S_I32:
            case WASM_OP_F32_CONVERT_U_I32:
            case WASM_OP_F32_CONVERT_S_I64:
            case WASM_OP_F32_CONVERT_U_I64:
            case WASM_OP_F32_DEMOTE_F64:
            case WASM_OP_F64_CONVERT_S_I32:
            case WASM_OP_F64_CONVERT_U_I32:
            case WASM_OP_F64_CONVERT_S_I64:
            case WASM_OP_F64_CONVERT_U_I64:
            case WASM_OP_F64_PROMOTE_F32:
            case WASM_OP_I32_REINTERPRET_F32:
            case WASM_OP_I64_REINTERPRET_F64:
            case WASM_OP_F32_REINTERPRET_I32:
            case WASM_OP_F64_REINTERPRET_I64:
            case WASM_OP_I32_EXTEND8_S:
            case WASM_OP_I32_EXTEND16_S:
            case WASM_OP_I64_EXTEND8_S:
            case WASM_OP_I64_EXTEND16_S:
            case WASM_OP_I64_EXTEND32_S:
                break;
            case WASM_OP_MISC_PREFIX:
            {
                uint32 opcode1;

                read_leb_uint32(p, p_end, opcode1);
                /* opcode1 was checked in wasm_loader_prepare_bytecode and
                   is no larger than UINT8_MAX */
                opcode = (uint8)opcode1;

                switch (opcode) {
                    case WASM_OP_I32_TRUNC_SAT_S_F32:
                    case WASM_OP_I32_TRUNC_SAT_U_F32:
                    case WASM_OP_I32_TRUNC_SAT_S_F64:
                    case WASM_OP_I32_TRUNC_SAT_U_F64:
                    case WASM_OP_I64_TRUNC_SAT_S_F32:
                    case WASM_OP_I64_TRUNC_SAT_U_F32:
                    case WASM_OP_I64_TRUNC_SAT_S_F64:
                    case WASM_OP_I64_TRUNC_SAT_U_F64:
                        break;
                    case WASM_OP_MEMORY_INIT:
                        skip_leb_uint32(p, p_end);
                        /* skip memory idx */
                        p++;
                        break;
                    case WASM_OP_DATA_DROP:
                        skip_leb_uint32(p, p_end);
                        break;
                    case WASM_OP_MEMORY_COPY:
                        /* skip two memory idx */
                        p += 2;
                        break;
                    case WASM_OP_MEMORY_FILL:
                        /* skip memory idx */
                        p++;
                        break;
                    default:
                        return false;
                }
                break;
            }

            case WASM_OP_SIMD_PREFIX:
            {
                uint32 opcode1;

                read_leb_uint32(p, p_end, opcode1);
                /* opcode1 was checked in wasm_loader_prepare_bytecode and
                   is no larger than UINT8_MAX */
                opcode = (uint8)opcode1;

                /* follow the order of enum WASMSimdEXTOpcode in wasm_opcode.h
                 */
                switch (opcode) {
                    case SIMD_v128_load:
                    case SIMD_v128_load8x8_s:
                    case SIMD_v128_load8x8_u:
                    case SIMD_v128_load16x4_s:
                    case SIMD_v128_load16x4_u:
                    case SIMD_v128_load32x2_s:
                    case SIMD_v128_load32x2_u:
                    case SIMD_v128_load8_splat:
                    case SIMD_v128_load16_splat:
                    case SIMD_v128_load32_splat:
                    case SIMD_v128_load64_splat:
                    case SIMD_v128_store:
                        /* memarg align */
                        skip_leb_uint32(p, p_end);
                        /* memarg offset*/
                        skip_leb_mem_offset(p, p_end);
                        break;

                    case SIMD_v128_const:
                    case SIMD_v8x16_shuffle:
                        /* immByte[16] immLaneId[16] */
                        CHECK_BUF1(p, p_end, 16);
                        p += 16;
                        break;

                    case SIMD_i8x16_extract_lane_s:
                    case SIMD_i8x16_extract_lane_u:
                    case SIMD_i8x16_replace_lane:
                    case SIMD_i16x8_extract_lane_s:
                    case SIMD_i16x8_extract_lane_u:
                    case SIMD_i16x8_replace_lane:
                    case SIMD_i32x4_extract_lane:
                    case SIMD_i32x4_replace_lane:
                    case SIMD_i64x2_extract_lane:
                    case SIMD_i64x2_replace_lane:
                    case SIMD_f32x4_extract_lane:
                    case SIMD_f32x4_replace_lane:
                    case SIMD_f64x2_extract_lane:
                    case SIMD_f64x2_replace_lane:
                        /* ImmLaneId */
                        CHECK_BUF(p, p_end, 1);
                        p++;
                        break;

                    case SIMD_v128_load8_lane:
                    case SIMD_v128_load16_lane:
                    case SIMD_v128_load32_lane:
                    case SIMD_v128_load64_lane:
                    case SIMD_v128_store8_lane:
                    case SIMD_v128_store16_lane:
                    case SIMD_v128_store32_lane:
                    case SIMD_v128_store64_lane:
                        /* memarg align */
                        skip_leb_uint32(p, p_end);
                        /* memarg offset*/
                        skip_leb_mem_offset(p, p_end);
                        /* ImmLaneId */
                        CHECK_BUF(p, p_end, 1);
                        p++;
                        break;

                    case SIMD_v128_load32_zero:
                    case SIMD_v128_load64_zero:
                        /* memarg align */
                        skip_leb_uint32(p, p_end);
                        /* memarg offset*/
                        skip_leb_mem_offset(p, p_end);
                        break;

                    default:
                        /*
                         * since latest SIMD specific used almost every value
                         * from 0x00 to 0xff, the default branch will present
                         * all opcodes without imm
                         * https://github.com/WebAssembly/simd/blob/main/proposals/simd/NewOpcodes.md
                         */
                        break;
                }
                break;
            }

            case WASM_OP_ATOMIC_PREFIX:
            {
                uint32 opcode1;

                read_leb_uint32(p, p_end, opcode1);
                /* opcode1 was checked in wasm_loader_prepare_bytecode and
                   is no larger than UINT8_MAX */
                opcode = (uint8)opcode1;

                if (opcode != WASM_OP_ATOMIC_FENCE) {
                    skip_leb_uint32(p, p_end);     /* align */
                    skip_leb_mem_offset(p, p_end); /* offset */
                }
                else {
                    /* atomic.fence doesn't have memarg */
                    p++;
                }
                break;
            }
            default:
                return false;
        }
    }

    (void)u8;
    return false;
fail:
    return false;
}

#define REF_ANY VALUE_TYPE_ANY
#define REF_I32 VALUE_TYPE_I32
#define REF_F32 VALUE_TYPE_F32
#define REF_I64_1 VALUE_TYPE_I64
#define REF_I64_2 VALUE_TYPE_I64
#define REF_F64_1 VALUE_TYPE_F64
#define REF_F64_2 VALUE_TYPE_F64
#define REF_V128_1 VALUE_TYPE_V128
#define REF_V128_2 VALUE_TYPE_V128
#define REF_V128_3 VALUE_TYPE_V128
#define REF_V128_4 VALUE_TYPE_V128

typedef struct BranchBlock {
    uint8 label_type;
    BlockType block_type;
    uint8 *start_addr;
    uint8 *else_addr;
    uint8 *end_addr;
    uint32 stack_cell_num;

    /* Indicate the operand stack is in polymorphic state.
     * If the opcode is one of unreachable/br/br_table/return, stack is marked
     * to polymorphic state until the block's 'end' opcode is processed.
     * If stack is in polymorphic state and stack is empty, instruction can
     * pop any type of value directly without decreasing stack top pointer
     * and stack cell num. */
    bool is_stack_polymorphic;
} BranchBlock;

typedef struct WASMLoaderContext {
    /* frame ref stack */
    uint8 *frame_ref;
    uint8 *frame_ref_bottom;
    uint8 *frame_ref_boundary;
    uint32 frame_ref_size;
    uint32 stack_cell_num;
    uint32 max_stack_cell_num;

    /* frame csp stack */
    BranchBlock *frame_csp;
    BranchBlock *frame_csp_bottom;
    BranchBlock *frame_csp_boundary;
    uint32 frame_csp_size;
    uint32 csp_num;
    uint32 max_csp_num;
} WASMLoaderContext;

typedef struct Const {
    WASMValue value;
    uint16 slot_index;
    uint8 value_type;
} Const;

static void *
memory_realloc(void *mem_old, uint32 size_old, uint32 size_new, char *error_buf,
               uint32 error_buf_size)
{
    uint8 *mem_new;
    bh_assert(size_new > size_old);
    if ((mem_new = loader_malloc(size_new, error_buf, error_buf_size))) {
        bh_memcpy_s(mem_new, size_new, mem_old, size_old);
        memset(mem_new + size_old, 0, size_new - size_old);
        wasm_runtime_free(mem_old);
    }
    return mem_new;
}

#define MEM_REALLOC(mem, size_old, size_new)                               \
    do {                                                                   \
        void *mem_new = memory_realloc(mem, size_old, size_new, error_buf, \
                                       error_buf_size);                    \
        if (!mem_new)                                                      \
            goto fail;                                                     \
        mem = mem_new;                                                     \
    } while (0)

#define CHECK_CSP_PUSH()                                                  \
    do {                                                                  \
        if (ctx->frame_csp >= ctx->frame_csp_boundary) {                  \
            MEM_REALLOC(                                                  \
                ctx->frame_csp_bottom, ctx->frame_csp_size,               \
                (uint32)(ctx->frame_csp_size + 8 * sizeof(BranchBlock))); \
            ctx->frame_csp_size += (uint32)(8 * sizeof(BranchBlock));     \
            ctx->frame_csp_boundary =                                     \
                ctx->frame_csp_bottom                                     \
                + ctx->frame_csp_size / sizeof(BranchBlock);              \
            ctx->frame_csp = ctx->frame_csp_bottom + ctx->csp_num;        \
        }                                                                 \
    } while (0)

#define CHECK_CSP_POP()                                             \
    do {                                                            \
        if (ctx->csp_num < 1) {                                     \
            set_error_buf(error_buf, error_buf_size,                \
                          "type mismatch: "                         \
                          "expect data but block stack was empty"); \
            goto fail;                                              \
        }                                                           \
    } while (0)

static bool
check_stack_push(WASMLoaderContext *ctx, char *error_buf, uint32 error_buf_size)
{
    if (ctx->frame_ref >= ctx->frame_ref_boundary) {
        MEM_REALLOC(ctx->frame_ref_bottom, ctx->frame_ref_size,
                    ctx->frame_ref_size + 16);
        ctx->frame_ref_size += 16;
        ctx->frame_ref_boundary = ctx->frame_ref_bottom + ctx->frame_ref_size;
        ctx->frame_ref = ctx->frame_ref_bottom + ctx->stack_cell_num;
    }
    return true;
fail:
    return false;
}

static bool
check_stack_top_values(uint8 *frame_ref, int32 stack_cell_num, uint8 type,
                       char *error_buf, uint32 error_buf_size)
{
    if ((is_32bit_type(type) && stack_cell_num < 1)
        || (is_64bit_type(type) && stack_cell_num < 2)
        || (type == VALUE_TYPE_V128 && stack_cell_num < 4)) {
        set_error_buf(error_buf, error_buf_size,
                      "type mismatch: expect data but stack was empty");
        return false;
    }

    if ((is_32bit_type(type) && *(frame_ref - 1) != type)
        || (is_64bit_type(type)
            && (*(frame_ref - 2) != type || *(frame_ref - 1) != type))
        || (type == VALUE_TYPE_V128
            && (*(frame_ref - 4) != REF_V128_1 || *(frame_ref - 3) != REF_V128_2
                || *(frame_ref - 2) != REF_V128_3
                || *(frame_ref - 1) != REF_V128_4))) {
        set_error_buf_v(error_buf, error_buf_size, "%s%s%s",
                        "type mismatch: expect ", type2str(type),
                        " but got other");
        return false;
    }

    return true;
}

static bool
check_stack_pop(WASMLoaderContext *ctx, uint8 type, char *error_buf,
                uint32 error_buf_size)
{
    int32 block_stack_cell_num =
        (int32)(ctx->stack_cell_num - (ctx->frame_csp - 1)->stack_cell_num);

    if (block_stack_cell_num > 0 && *(ctx->frame_ref - 1) == VALUE_TYPE_ANY) {
        /* the stack top is a value of any type, return success */
        return true;
    }

    if (!check_stack_top_values(ctx->frame_ref, block_stack_cell_num, type,
                                error_buf, error_buf_size))
        return false;

    return true;
}

static void
wasm_loader_ctx_destroy(WASMLoaderContext *ctx)
{
    if (ctx) {
        if (ctx->frame_ref_bottom)
            wasm_runtime_free(ctx->frame_ref_bottom);
        if (ctx->frame_csp_bottom) {
            wasm_runtime_free(ctx->frame_csp_bottom);
        }
        wasm_runtime_free(ctx);
    }
}

static WASMLoaderContext *
wasm_loader_ctx_init(WASMFunction *func, char *error_buf, uint32 error_buf_size)
{
    WASMLoaderContext *loader_ctx =
        loader_malloc(sizeof(WASMLoaderContext), error_buf, error_buf_size);
    if (!loader_ctx)
        return NULL;

    loader_ctx->frame_ref_size = 32;
    if (!(loader_ctx->frame_ref_bottom = loader_ctx->frame_ref = loader_malloc(
              loader_ctx->frame_ref_size, error_buf, error_buf_size)))
        goto fail;
    loader_ctx->frame_ref_boundary = loader_ctx->frame_ref_bottom + 32;

    loader_ctx->frame_csp_size = sizeof(BranchBlock) * 8;
    if (!(loader_ctx->frame_csp_bottom = loader_ctx->frame_csp = loader_malloc(
              loader_ctx->frame_csp_size, error_buf, error_buf_size)))
        goto fail;
    loader_ctx->frame_csp_boundary = loader_ctx->frame_csp_bottom + 8;

    return loader_ctx;

fail:
    wasm_loader_ctx_destroy(loader_ctx);
    return NULL;
}

static bool
wasm_loader_push_frame_ref(WASMLoaderContext *ctx, uint8 type, char *error_buf,
                           uint32 error_buf_size)
{
    if (type == VALUE_TYPE_VOID)
        return true;

    if (!check_stack_push(ctx, error_buf, error_buf_size))
        return false;

    *ctx->frame_ref++ = type;
    ctx->stack_cell_num++;
    if (is_32bit_type(type) || type == VALUE_TYPE_ANY)
        goto check_stack_and_return;

    if (!check_stack_push(ctx, error_buf, error_buf_size))
        return false;

    *ctx->frame_ref++ = type;
    ctx->stack_cell_num++;

    if (type == VALUE_TYPE_V128) {
        if (!check_stack_push(ctx, error_buf, error_buf_size))
            return false;
        *ctx->frame_ref++ = type;
        ctx->stack_cell_num++;
        if (!check_stack_push(ctx, error_buf, error_buf_size))
            return false;
        *ctx->frame_ref++ = type;
        ctx->stack_cell_num++;
    }

check_stack_and_return:
    if (ctx->stack_cell_num > ctx->max_stack_cell_num) {
        ctx->max_stack_cell_num = ctx->stack_cell_num;
        if (ctx->max_stack_cell_num > UINT16_MAX) {
            set_error_buf(error_buf, error_buf_size,
                          "operand stack depth limit exceeded");
            return false;
        }
    }
    return true;
}

static bool
wasm_loader_pop_frame_ref(WASMLoaderContext *ctx, uint8 type, char *error_buf,
                          uint32 error_buf_size)
{
    BranchBlock *cur_block = ctx->frame_csp - 1;
    int32 available_stack_cell =
        (int32)(ctx->stack_cell_num - cur_block->stack_cell_num);

    /* Directly return success if current block is in stack
     * polymorphic state while stack is empty. */
    if (available_stack_cell <= 0 && cur_block->is_stack_polymorphic)
        return true;

    if (type == VALUE_TYPE_VOID)
        return true;

    if (!check_stack_pop(ctx, type, error_buf, error_buf_size))
        return false;

    ctx->frame_ref--;
    ctx->stack_cell_num--;

    if (is_32bit_type(type) || *ctx->frame_ref == VALUE_TYPE_ANY)
        return true;

    ctx->frame_ref--;
    ctx->stack_cell_num--;

    if (type == VALUE_TYPE_V128) {
        ctx->frame_ref -= 2;
        ctx->stack_cell_num -= 2;
    }

    return true;
}

static bool
wasm_loader_push_pop_frame_ref(WASMLoaderContext *ctx, uint8 pop_cnt,
                               uint8 type_push, uint8 type_pop, char *error_buf,
                               uint32 error_buf_size)
{
    for (int i = 0; i < pop_cnt; i++) {
        if (!wasm_loader_pop_frame_ref(ctx, type_pop, error_buf,
                                       error_buf_size))
            return false;
    }
    if (!wasm_loader_push_frame_ref(ctx, type_push, error_buf, error_buf_size))
        return false;
    return true;
}

static bool
wasm_loader_push_frame_csp(WASMLoaderContext *ctx, uint8 label_type,
                           BlockType block_type, uint8 *start_addr,
                           char *error_buf, uint32 error_buf_size)
{
    CHECK_CSP_PUSH();
    memset(ctx->frame_csp, 0, sizeof(BranchBlock));
    ctx->frame_csp->label_type = label_type;
    ctx->frame_csp->block_type = block_type;
    ctx->frame_csp->start_addr = start_addr;
    ctx->frame_csp->stack_cell_num = ctx->stack_cell_num;
    ctx->frame_csp++;
    ctx->csp_num++;
    if (ctx->csp_num > ctx->max_csp_num) {
        ctx->max_csp_num = ctx->csp_num;
        if (ctx->max_csp_num > UINT16_MAX) {
            set_error_buf(error_buf, error_buf_size,
                          "label stack depth limit exceeded");
            return false;
        }
    }
    return true;
fail:
    return false;
}

static bool
wasm_loader_pop_frame_csp(WASMLoaderContext *ctx, char *error_buf,
                          uint32 error_buf_size)
{
    CHECK_CSP_POP();
    ctx->frame_csp--;
    ctx->csp_num--;

    return true;
fail:
    return false;
}

#define TEMPLATE_PUSH(Type)                                             \
    do {                                                                \
        if (!(wasm_loader_push_frame_ref(loader_ctx, VALUE_TYPE_##Type, \
                                         error_buf, error_buf_size)))   \
            goto fail;                                                  \
    } while (0)

#define TEMPLATE_PUSH_TYPE(Type)                                      \
    do {                                                              \
        if (!(wasm_loader_push_frame_ref(loader_ctx, Type, error_buf, \
                                         error_buf_size)))            \
            goto fail;                                                \
    } while (0)

#define TEMPLATE_POP(Type)                                             \
    do {                                                               \
        if (!(wasm_loader_pop_frame_ref(loader_ctx, VALUE_TYPE_##Type, \
                                        error_buf, error_buf_size)))   \
            goto fail;                                                 \
    } while (0)

#define TEMPLATE_POP_TYPE(Type)                                      \
    do {                                                             \
        if (!(wasm_loader_pop_frame_ref(loader_ctx, Type, error_buf, \
                                        error_buf_size)))            \
            goto fail;                                               \
    } while (0)

#define POP_AND_PUSH(type_pop, type_push)                              \
    do {                                                               \
        if (!(wasm_loader_push_pop_frame_ref(loader_ctx, 1, type_push, \
                                             type_pop, error_buf,      \
                                             error_buf_size)))         \
            goto fail;                                                 \
    } while (0)

/* type of POPs should be the same */
#define POP2_AND_PUSH(type_pop, type_push)                             \
    do {                                                               \
        if (!(wasm_loader_push_pop_frame_ref(loader_ctx, 2, type_push, \
                                             type_pop, error_buf,      \
                                             error_buf_size)))         \
            goto fail;                                                 \
    } while (0)

#define PUSH_I32() TEMPLATE_PUSH(I32)
#define PUSH_F32() TEMPLATE_PUSH(F32)
#define PUSH_I64() TEMPLATE_PUSH(I64)
#define PUSH_F64() TEMPLATE_PUSH(F64)
#define PUSH_V128() TEMPLATE_PUSH(V128)
#define PUSH_MEM_OFFSET() TEMPLATE_PUSH_TYPE(mem_offset_type)
#define PUSH_PAGE_COUNT() PUSH_MEM_OFFSET()

#define POP_I32() TEMPLATE_POP(I32)
#define POP_F32() TEMPLATE_POP(F32)
#define POP_I64() TEMPLATE_POP(I64)
#define POP_F64() TEMPLATE_POP(F64)
#define POP_V128() TEMPLATE_POP(V128)
#define POP_MEM_OFFSET() TEMPLATE_POP_TYPE(mem_offset_type)

#define PUSH_TYPE(type)                                               \
    do {                                                              \
        if (!(wasm_loader_push_frame_ref(loader_ctx, type, error_buf, \
                                         error_buf_size)))            \
            goto fail;                                                \
    } while (0)

#define POP_TYPE(type)                                               \
    do {                                                             \
        if (!(wasm_loader_pop_frame_ref(loader_ctx, type, error_buf, \
                                        error_buf_size)))            \
            goto fail;                                               \
    } while (0)

#define PUSH_CSP(label_type, block_type, _start_addr)                       \
    do {                                                                    \
        if (!wasm_loader_push_frame_csp(loader_ctx, label_type, block_type, \
                                        _start_addr, error_buf,             \
                                        error_buf_size))                    \
            goto fail;                                                      \
    } while (0)

#define POP_CSP()                                                              \
    do {                                                                       \
        if (!wasm_loader_pop_frame_csp(loader_ctx, error_buf, error_buf_size)) \
            goto fail;                                                         \
    } while (0)

#define GET_LOCAL_INDEX_TYPE_AND_OFFSET()                              \
    do {                                                               \
        read_leb_uint32(p, p_end, local_idx);                          \
        if (local_idx >= param_count + local_count) {                  \
            set_error_buf(error_buf, error_buf_size, "unknown local"); \
            goto fail;                                                 \
        }                                                              \
        local_type = local_idx < param_count                           \
                         ? param_types[local_idx]                      \
                         : local_types[local_idx - param_count];       \
        local_offset = local_offsets[local_idx];                       \
    } while (0)

#define CHECK_BR(depth)                                         \
    do {                                                        \
        if (!wasm_loader_check_br(loader_ctx, depth, error_buf, \
                                  error_buf_size))              \
            goto fail;                                          \
    } while (0)

static bool
check_memory(WASMModule *module, char *error_buf, uint32 error_buf_size)
{
    if (module->memory_count == 0 && module->import_memory_count == 0) {
        set_error_buf(error_buf, error_buf_size, "unknown memory");
        return false;
    }
    return true;
}

#define CHECK_MEMORY()                                        \
    do {                                                      \
        if (!check_memory(module, error_buf, error_buf_size)) \
            goto fail;                                        \
    } while (0)

static bool
check_memory_access_align(uint8 opcode, uint32 align, char *error_buf,
                          uint32 error_buf_size)
{
    uint8 mem_access_aligns[] = {
        2, 3, 2, 3, 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, /* loads */
        2, 3, 2, 3, 0, 1, 0, 1, 2                 /* stores */
    };
    bh_assert(opcode >= WASM_OP_I32_LOAD && opcode <= WASM_OP_I64_STORE32);
    if (align > mem_access_aligns[opcode - WASM_OP_I32_LOAD]) {
        set_error_buf(error_buf, error_buf_size,
                      "alignment must not be larger than natural");
        return false;
    }
    return true;
}

static bool
check_simd_memory_access_align(uint8 opcode, uint32 align, char *error_buf,
                               uint32 error_buf_size)
{
    uint8 mem_access_aligns[] = {
        4,                /* load */
        3, 3, 3, 3, 3, 3, /* load and extend */
        0, 1, 2, 3,       /* load and splat */
        4,                /* store */
    };

    uint8 mem_access_aligns_load_lane[] = {
        0, 1, 2, 3, /* load lane */
        0, 1, 2, 3, /* store lane */
        2, 3        /* store zero */
    };

    if (!((opcode <= SIMD_v128_store)
          || (SIMD_v128_load8_lane <= opcode
              && opcode <= SIMD_v128_load64_zero))) {
        set_error_buf(error_buf, error_buf_size,
                      "the opcode doesn't include memarg");
        return false;
    }

    if ((opcode <= SIMD_v128_store
         && align > mem_access_aligns[opcode - SIMD_v128_load])
        || (SIMD_v128_load8_lane <= opcode && opcode <= SIMD_v128_load64_zero
            && align > mem_access_aligns_load_lane[opcode
                                                   - SIMD_v128_load8_lane])) {
        set_error_buf(error_buf, error_buf_size,
                      "alignment must not be larger than natural");
        return false;
    }

    return true;
}

static bool
check_simd_access_lane(uint8 opcode, uint8 lane, char *error_buf,
                       uint32 error_buf_size)
{
    switch (opcode) {
        case SIMD_i8x16_extract_lane_s:
        case SIMD_i8x16_extract_lane_u:
        case SIMD_i8x16_replace_lane:
            if (lane >= 16) {
                goto fail;
            }
            break;
        case SIMD_i16x8_extract_lane_s:
        case SIMD_i16x8_extract_lane_u:
        case SIMD_i16x8_replace_lane:
            if (lane >= 8) {
                goto fail;
            }
            break;
        case SIMD_i32x4_extract_lane:
        case SIMD_i32x4_replace_lane:
        case SIMD_f32x4_extract_lane:
        case SIMD_f32x4_replace_lane:
            if (lane >= 4) {
                goto fail;
            }
            break;
        case SIMD_i64x2_extract_lane:
        case SIMD_i64x2_replace_lane:
        case SIMD_f64x2_extract_lane:
        case SIMD_f64x2_replace_lane:
            if (lane >= 2) {
                goto fail;
            }
            break;

        case SIMD_v128_load8_lane:
        case SIMD_v128_load16_lane:
        case SIMD_v128_load32_lane:
        case SIMD_v128_load64_lane:
        case SIMD_v128_store8_lane:
        case SIMD_v128_store16_lane:
        case SIMD_v128_store32_lane:
        case SIMD_v128_store64_lane:
        case SIMD_v128_load32_zero:
        case SIMD_v128_load64_zero:
        {
            uint8 max_lanes[] = { 16, 8, 4, 2, 16, 8, 4, 2, 4, 2 };
            if (lane >= max_lanes[opcode - SIMD_v128_load8_lane]) {
                goto fail;
            }
            break;
        }
        default:
            goto fail;
    }

    return true;
fail:
    set_error_buf(error_buf, error_buf_size, "invalid lane index");
    return false;
}

static bool
check_simd_shuffle_mask(V128 mask, char *error_buf, uint32 error_buf_size)
{
    uint8 i;
    for (i = 0; i != 16; ++i) {
        if (mask.i8x16[i] < 0 || mask.i8x16[i] >= 32) {
            set_error_buf(error_buf, error_buf_size, "invalid lane index");
            return false;
        }
    }
    return true;
}

static bool
check_memory_align_equal(uint8 opcode, uint32 align, char *error_buf,
                         uint32 error_buf_size)
{
    uint8 wait_notify_aligns[] = { 2, 2, 3 };
    uint8 mem_access_aligns[] = {
        2, 3, 0, 1, 0, 1, 2,
    };
    uint8 expect;

    bh_assert((opcode <= WASM_OP_ATOMIC_WAIT64)
              || (opcode >= WASM_OP_ATOMIC_I32_LOAD
                  && opcode <= WASM_OP_ATOMIC_RMW_I64_CMPXCHG32_U));
    if (opcode <= WASM_OP_ATOMIC_WAIT64) {
        expect = wait_notify_aligns[opcode - WASM_OP_ATOMIC_NOTIFY];
    }
    else {
        /* 7 opcodes in every group */
        expect = mem_access_aligns[(opcode - WASM_OP_ATOMIC_I32_LOAD) % 7];
    }
    if (align != expect) {
        set_error_buf(error_buf, error_buf_size,
                      "alignment isn't equal to natural");
        return false;
    }
    return true;
}

static bool
wasm_loader_check_br(WASMLoaderContext *loader_ctx, uint32 depth,
                     char *error_buf, uint32 error_buf_size)
{
    BranchBlock *target_block, *cur_block;
    BlockType *target_block_type;
    uint8 *types = NULL, *frame_ref;
    uint32 arity = 0;
    int32 i, available_stack_cell;
    uint16 cell_num;

    bh_assert(loader_ctx->csp_num > 0);
    if (loader_ctx->csp_num - 1 < depth) {
        set_error_buf(error_buf, error_buf_size,
                      "unknown label, "
                      "unexpected end of section or function");
        return false;
    }

    cur_block = loader_ctx->frame_csp - 1;
    target_block = loader_ctx->frame_csp - (depth + 1);
    target_block_type = &target_block->block_type;
    frame_ref = loader_ctx->frame_ref;

    /* Note: loop's arity is different from if and block. loop's arity is
     * its parameter count while if and block arity is result count.
     */
    if (target_block->label_type == LABEL_TYPE_LOOP)
        arity = block_type_get_param_types(target_block_type, &types);
    else
        arity = block_type_get_result_types(target_block_type, &types);

    /* If the stack is in polymorphic state, just clear the stack
     * and then re-push the values to make the stack top values
     * match block type. */
    if (cur_block->is_stack_polymorphic) {
        for (i = (int32)arity - 1; i >= 0; i--) {
            POP_TYPE(types[i]);
        }
        for (i = 0; i < (int32)arity; i++) {
            PUSH_TYPE(types[i]);
        }
        return true;
    }

    available_stack_cell =
        (int32)(loader_ctx->stack_cell_num - cur_block->stack_cell_num);

    /* Check stack top values match target block type */
    for (i = (int32)arity - 1; i >= 0; i--) {
        if (!check_stack_top_values(frame_ref, available_stack_cell, types[i],
                                    error_buf, error_buf_size))
            return false;
        cell_num = wasm_value_type_cell_num(types[i]);
        frame_ref -= cell_num;
        available_stack_cell -= cell_num;
    }

    return true;

fail:
    return false;
}

static BranchBlock *
check_branch_block(WASMLoaderContext *loader_ctx, uint8 **p_buf, uint8 *buf_end,
                   char *error_buf, uint32 error_buf_size)
{
    uint8 *p = *p_buf, *p_end = buf_end;
    BranchBlock *frame_csp_tmp;
    uint32 depth;

    read_leb_uint32(p, p_end, depth);
    CHECK_BR(depth);
    frame_csp_tmp = loader_ctx->frame_csp - depth - 1;

    *p_buf = p;
    return frame_csp_tmp;
fail:
    return NULL;
}

static bool
check_block_stack(WASMLoaderContext *loader_ctx, BranchBlock *block,
                  char *error_buf, uint32 error_buf_size)
{
    BlockType *block_type = &block->block_type;
    uint8 *return_types = NULL;
    uint32 return_count = 0;
    int32 available_stack_cell, return_cell_num, i;
    uint8 *frame_ref = NULL;

    available_stack_cell =
        (int32)(loader_ctx->stack_cell_num - block->stack_cell_num);

    return_count = block_type_get_result_types(block_type, &return_types);
    return_cell_num =
        return_count > 0 ? wasm_get_cell_num(return_types, return_count) : 0;

    /* If the stack is in polymorphic state, just clear the stack
     * and then re-push the values to make the stack top values
     * match block type. */
    if (block->is_stack_polymorphic) {
        for (i = (int32)return_count - 1; i >= 0; i--) {
            POP_TYPE(return_types[i]);
        }

        /* Check stack is empty */
        if (loader_ctx->stack_cell_num != block->stack_cell_num) {
            set_error_buf(
                error_buf, error_buf_size,
                "type mismatch: stack size does not match block type");
            goto fail;
        }

        for (i = 0; i < (int32)return_count; i++) {
            PUSH_TYPE(return_types[i]);
        }
        return true;
    }

    /* Check stack cell num equals return cell num */
    if (available_stack_cell != return_cell_num) {
        set_error_buf(error_buf, error_buf_size,
                      "type mismatch: stack size does not match block type");
        goto fail;
    }

    /* Check stack values match return types */
    frame_ref = loader_ctx->frame_ref;
    for (i = (int32)return_count - 1; i >= 0; i--) {
        if (!check_stack_top_values(frame_ref, available_stack_cell,
                                    return_types[i], error_buf, error_buf_size))
            return false;
        frame_ref -= wasm_value_type_cell_num(return_types[i]);
        available_stack_cell -= wasm_value_type_cell_num(return_types[i]);
    }

    return true;

fail:
    return false;
}

#define RESET_STACK()                                                  \
    do {                                                               \
        loader_ctx->stack_cell_num =                                   \
            (loader_ctx->frame_csp - 1)->stack_cell_num;               \
        loader_ctx->frame_ref =                                        \
            loader_ctx->frame_ref_bottom + loader_ctx->stack_cell_num; \
    } while (0)

/* set current block's stack polymorphic state */
#define SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(flag)          \
    do {                                                     \
        BranchBlock *_cur_block = loader_ctx->frame_csp - 1; \
        _cur_block->is_stack_polymorphic = flag;             \
    } while (0)

#define BLOCK_HAS_PARAM(block_type) \
    (!block_type.is_value_type && block_type.u.type->param_count > 0)

const uint8 *
wasm_loader_get_custom_section(WASMModule *module, const char *name,
                               uint32 *len)
{
    WASMCustomSection *section = module->custom_section_list;

    while (section) {
        if ((section->name_len == strlen(name))
            && (memcmp(section->name_addr, name, section->name_len) == 0)) {
            if (len) {
                *len = section->content_len;
            }
            return section->content_addr;
        }

        section = section->next;
    }

    return NULL;
}

static bool
wasm_loader_prepare_bytecode(WASMModule *module, WASMFunction *func,
                             uint32 cur_func_idx, char *error_buf,
                             uint32 error_buf_size)
{
    uint8 *p = func->code, *p_end = func->code + func->code_size, *p_org;
    uint32 param_count, local_count, global_count;
    uint8 *param_types, *local_types, local_type, global_type, mem_offset_type;
    BlockType func_block_type;
    uint16 *local_offsets, local_offset;
    uint32 type_idx, func_idx, local_idx, global_idx, table_idx;
    uint32 table_seg_idx, data_seg_idx, count, align, i;
    uint64 mem_offset;
    int32 i32_const = 0;
    int64 i64_const;
    uint8 opcode;
    WASMLoaderContext *loader_ctx;
    BranchBlock *frame_csp_tmp;
    bool is_memory64, return_value = false;

    is_memory64 = has_module_memory64(module);
    mem_offset_type = is_memory64 ? VALUE_TYPE_I64 : VALUE_TYPE_I32;

    global_count = module->import_global_count + module->global_count;

    param_count = func->func_type->param_count;
    param_types = func->func_type->types;

    func_block_type.is_value_type = false;
    func_block_type.u.type = func->func_type;

    local_count = func->local_count;
    local_types = func->local_types;
    local_offsets = func->local_offsets;

    if (!(loader_ctx = wasm_loader_ctx_init(func, error_buf, error_buf_size))) {
        goto fail;
    }

    PUSH_CSP(LABEL_TYPE_FUNCTION, func_block_type, p);

    while (p < p_end) {
        opcode = *p++;
        switch (opcode) {
            case WASM_OP_UNREACHABLE:
                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                break;

            case WASM_OP_NOP:
                break;

            case WASM_OP_IF:
            {
                POP_I32();
                goto handle_op_block_and_loop;
            }
            case WASM_OP_BLOCK:
            case WASM_OP_LOOP:
            handle_op_block_and_loop:
            {
                uint8 value_type;
                BlockType block_type;

                p_org = p - 1;
                CHECK_BUF(p, p_end, 1);
                value_type = read_uint8(p);
                if (is_byte_a_type(value_type)) {
                    /* If the first byte is one of these special values:
                     * 0x40/0x7F/0x7E/0x7D/0x7C, take it as the type of
                     * the single return value. */
                    block_type.is_value_type = true;
                    block_type.u.value_type = value_type;
                }
                else {
                    uint32 type_index;
                    /* Resolve the leb128 encoded type index as block type */
                    p--;
                    read_leb_uint32(p, p_end, type_index);
                    if (type_index >= module->type_count) {
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown type");
                        goto fail;
                    }
                    block_type.is_value_type = false;
                    block_type.u.type = module->types[type_index];
                    /* If block use type index as block type, change the opcode
                     * to new extended opcode so that interpreter can resolve
                     * the block quickly.
                     */
                    *p_org = EXT_OP_BLOCK + (opcode - WASM_OP_BLOCK);
                }

                /* Pop block parameters from stack */
                if (BLOCK_HAS_PARAM(block_type)) {
                    WASMType *wasm_type = block_type.u.type;

                    BranchBlock *cur_block = loader_ctx->frame_csp - 1;
                    for (i = 0; i < block_type.u.type->param_count; i++) {

                        int32 available_stack_cell =
                            (int32)(loader_ctx->stack_cell_num
                                    - cur_block->stack_cell_num);
                        if (available_stack_cell <= 0
                            && cur_block->is_stack_polymorphic) {
                            break;
                        }

                        POP_TYPE(
                            wasm_type->types[wasm_type->param_count - i - 1]);
                    }
                }

                PUSH_CSP(LABEL_TYPE_BLOCK + (opcode - WASM_OP_BLOCK),
                         block_type, p);

                /* Pass parameters to block */
                if (BLOCK_HAS_PARAM(block_type)) {
                    for (i = 0; i < block_type.u.type->param_count; i++) {
                        PUSH_TYPE(block_type.u.type->types[i]);
                    }
                }

                break;
            }

            case WASM_OP_ELSE:
            {
                BranchBlock *block = NULL;
                BlockType block_type;

                if (loader_ctx->csp_num < 2
                    || (loader_ctx->frame_csp - 1)->label_type
                           != LABEL_TYPE_IF) {
                    set_error_buf(
                        error_buf, error_buf_size,
                        "opcode else found without matched opcode if");
                    goto fail;
                }
                block = loader_ctx->frame_csp - 1;

                /* check whether if branch's stack matches its result type */
                if (!check_block_stack(loader_ctx, block, error_buf,
                                       error_buf_size))
                    goto fail;

                block->else_addr = p - 1;
                block_type = block->block_type;

                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(false);

                /* Pass parameters to if-false branch */
                if (BLOCK_HAS_PARAM(block_type)) {
                    for (i = 0; i < block_type.u.type->param_count; i++)
                        PUSH_TYPE(block_type.u.type->types[i]);
                }

                break;
            }

            case WASM_OP_END:
            {
                BranchBlock *cur_block = loader_ctx->frame_csp - 1;

                /* check whether block stack matches its result type */
                if (!check_block_stack(loader_ctx, cur_block, error_buf,
                                       error_buf_size))
                    goto fail;

                /* if no else branch, and return types do not match param types,
                 * fail */
                if (cur_block->label_type == LABEL_TYPE_IF
                    && !cur_block->else_addr) {
                    uint32 block_param_count = 0, block_ret_count = 0;
                    uint8 *block_param_types = NULL, *block_ret_types = NULL;
                    BlockType *cur_block_type = &cur_block->block_type;
                    if (cur_block_type->is_value_type) {
                        if (cur_block_type->u.value_type != VALUE_TYPE_VOID) {
                            block_ret_count = 1;
                            block_ret_types = &cur_block_type->u.value_type;
                        }
                    }
                    else {
                        block_param_count = cur_block_type->u.type->param_count;
                        block_ret_count = cur_block_type->u.type->result_count;
                        block_param_types = cur_block_type->u.type->types;
                        block_ret_types =
                            cur_block_type->u.type->types + block_param_count;
                    }
                    if (block_param_count != block_ret_count
                        || (block_param_count
                            && memcmp(block_param_types, block_ret_types,
                                      block_param_count))) {
                        set_error_buf(error_buf, error_buf_size,
                                      "type mismatch: else branch missing");
                        goto fail;
                    }
                }

                POP_CSP();

                if (loader_ctx->csp_num > 0) {
                    loader_ctx->frame_csp->end_addr = p - 1;
                }
                else {
                    /* end of function block, function will return */
                    if (p < p_end) {
                        set_error_buf(error_buf, error_buf_size,
                                      "section size mismatch");
                        goto fail;
                    }
                }

                break;
            }

            case WASM_OP_BR:
            {
                if (!(frame_csp_tmp = check_branch_block(
                          loader_ctx, &p, p_end, error_buf, error_buf_size)))
                    goto fail;

                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                break;
            }

            case WASM_OP_BR_IF:
            {
                POP_I32();

                if (!(frame_csp_tmp = check_branch_block(
                          loader_ctx, &p, p_end, error_buf, error_buf_size)))
                    goto fail;

                break;
            }

            case WASM_OP_BR_TABLE:
            {
                uint8 *ret_types = NULL;
                uint32 ret_count = 0;
                uint8 *p_depth_begin, *p_depth;
                uint32 depth, j;
                BrTableCache *br_table_cache = NULL;

                p_org = p - 1;

                read_leb_uint32(p, p_end, count);
                POP_I32();

                p_depth_begin = p_depth = p;
                for (i = 0; i <= count; i++) {
                    if (!(frame_csp_tmp =
                              check_branch_block(loader_ctx, &p, p_end,
                                                 error_buf, error_buf_size)))
                        goto fail;

                    if (i == 0) {
                        if (frame_csp_tmp->label_type != LABEL_TYPE_LOOP)
                            ret_count = block_type_get_result_types(
                                &frame_csp_tmp->block_type, &ret_types);
                        else
                            ret_count = block_type_get_param_types(
                                &frame_csp_tmp->block_type, &ret_types);
                    }
                    else {
                        uint8 *tmp_ret_types = NULL;
                        uint32 tmp_ret_count = 0;

                        /* Check whether all table items have the same return
                         * type */
                        if (frame_csp_tmp->label_type != LABEL_TYPE_LOOP)
                            tmp_ret_count = block_type_get_result_types(
                                &frame_csp_tmp->block_type, &tmp_ret_types);
                        else
                            tmp_ret_count = block_type_get_param_types(
                                &frame_csp_tmp->block_type, &tmp_ret_types);

                        if (ret_count != tmp_ret_count
                            || (ret_count
                                && 0
                                       != memcmp(ret_types, tmp_ret_types,
                                                 ret_count))) {
                            set_error_buf(
                                error_buf, error_buf_size,
                                "type mismatch: br_table targets must "
                                "all use same result type");
                            goto fail;
                        }
                    }

                    depth = (uint32)(loader_ctx->frame_csp - 1 - frame_csp_tmp);
                    if (br_table_cache) {
                        br_table_cache->br_depths[i] = depth;
                    }
                    else {
                        if (depth > 255) {
                            /* The depth cannot be stored in one byte,
                               create br_table cache to store each depth */
                            if (!(br_table_cache = loader_malloc(
                                      offsetof(BrTableCache, br_depths)
                                          + sizeof(uint32)
                                                * (uint64)(count + 1),
                                      error_buf, error_buf_size))) {
                                goto fail;
                            }
                            *p_org = EXT_OP_BR_TABLE_CACHE;
                            br_table_cache->br_table_op_addr = p_org;
                            br_table_cache->br_count = count;
                            /* Copy previous depths which are one byte */
                            for (j = 0; j < i; j++) {
                                br_table_cache->br_depths[j] = p_depth_begin[j];
                            }
                            br_table_cache->br_depths[i] = depth;
                            bh_list_insert(module->br_table_cache_list,
                                           br_table_cache);
                        }
                        else {
                            /* The depth can be stored in one byte, use the
                               byte of the leb to store it */
                            *p_depth++ = (uint8)depth;
                        }
                    }
                }

                /* Set the tailing bytes to nop */
                if (br_table_cache)
                    p_depth = p_depth_begin;
                while (p_depth < p)
                    *p_depth++ = WASM_OP_NOP;

                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                break;
            }

            case WASM_OP_RETURN:
            {
                int32 idx;
                uint8 ret_type;
                for (idx = (int32)func->func_type->result_count - 1; idx >= 0;
                     idx--) {
                    ret_type = *(func->func_type->types
                                 + func->func_type->param_count + idx);
                    POP_TYPE(ret_type);
                }

                RESET_STACK();
                SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);

                break;
            }

            case WASM_OP_CALL:
            case WASM_OP_RETURN_CALL:
            {
                WASMType *func_type;
                int32 idx;

                read_leb_uint32(p, p_end, func_idx);

                if (!check_function_index(module, func_idx, error_buf,
                                          error_buf_size)) {
                    goto fail;
                }

                if (func_idx < module->import_function_count)
                    func_type =
                        module->import_functions[func_idx].u.function.func_type;
                else
                    func_type = module
                                    ->functions[func_idx
                                                - module->import_function_count]
                                    ->func_type;

                if (func_type->param_count > 0) {
                    for (idx = (int32)(func_type->param_count - 1); idx >= 0;
                         idx--) {
                        POP_TYPE(func_type->types[idx]);
                    }
                }

                if (opcode == WASM_OP_CALL) {
                    for (i = 0; i < func_type->result_count; i++) {
                        PUSH_TYPE(func_type->types[func_type->param_count + i]);
                    }
                }
                else {
                    uint8 type;
                    if (func_type->result_count
                        != func->func_type->result_count) {
                        set_error_buf_v(error_buf, error_buf_size, "%s%u%s",
                                        "type mismatch: expect ",
                                        func->func_type->result_count,
                                        " return values but got other");
                        goto fail;
                    }
                    for (i = 0; i < func_type->result_count; i++) {
                        type = func->func_type
                                   ->types[func->func_type->param_count + i];
                        if (func_type->types[func_type->param_count + i]
                            != type) {
                            set_error_buf_v(error_buf, error_buf_size, "%s%s%s",
                                            "type mismatch: expect ",
                                            type2str(type), " but got other");
                            goto fail;
                        }
                    }
                    RESET_STACK();
                    SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                }
                func->has_op_func_call = true;
                break;
            }

            /*
             * if disable reference type: call_indirect typeidx, 0x00
             * if enable reference type:  call_indirect typeidx, tableidx
             */
            case WASM_OP_CALL_INDIRECT:
            case WASM_OP_RETURN_CALL_INDIRECT:
            {
                int32 idx;
                WASMType *func_type;
                bool is_table64;

                read_leb_uint32(p, p_end, type_idx);
                CHECK_BUF(p, p_end, 1);
                table_idx = read_uint8(p);
                if (!check_table_index(module, table_idx, error_buf,
                                       error_buf_size)) {
                    goto fail;
                }

                is_table64 = module->tables[table_idx].flags & TABLE64_FLAG
                                 ? true
                                 : false;

                /* skip elem idx */
                if (is_table64)
                    POP_I64();
                else
                    POP_I32();

                if (type_idx >= module->type_count) {
                    set_error_buf(error_buf, error_buf_size, "unknown type");
                    goto fail;
                }

                func_type = module->types[type_idx];

                if (func_type->param_count > 0) {
                    for (idx = (int32)(func_type->param_count - 1); idx >= 0;
                         idx--) {
                        POP_TYPE(func_type->types[idx]);
                    }
                }

                if (opcode == WASM_OP_CALL_INDIRECT) {
                    for (i = 0; i < func_type->result_count; i++) {
                        PUSH_TYPE(func_type->types[func_type->param_count + i]);
                    }
                }
                else {
                    uint8 type;
                    if (func_type->result_count
                        != func->func_type->result_count) {
                        set_error_buf_v(error_buf, error_buf_size, "%s%u%s",
                                        "type mismatch: expect ",
                                        func->func_type->result_count,
                                        " return values but got other");
                        goto fail;
                    }
                    for (i = 0; i < func_type->result_count; i++) {
                        type = func->func_type
                                   ->types[func->func_type->param_count + i];
                        if (func_type->types[func_type->param_count + i]
                            != type) {
                            set_error_buf_v(error_buf, error_buf_size, "%s%s%s",
                                            "type mismatch: expect ",
                                            type2str(type), " but got other");
                            goto fail;
                        }
                    }
                    RESET_STACK();
                    SET_CUR_BLOCK_STACK_POLYMORPHIC_STATE(true);
                }
                func->has_op_func_call = true;
                func->has_op_call_indirect = true;
                break;
            }

            case WASM_OP_DROP:
            {
                BranchBlock *cur_block = loader_ctx->frame_csp - 1;
                int32 available_stack_cell =
                    (int32)(loader_ctx->stack_cell_num
                            - cur_block->stack_cell_num);

                if (available_stack_cell <= 0
                    && !cur_block->is_stack_polymorphic) {
                    set_error_buf(error_buf, error_buf_size,
                                  "type mismatch, opcode drop was found "
                                  "but stack was empty");
                    goto fail;
                }

                if (available_stack_cell > 0) {
                    if (is_32bit_type(*(loader_ctx->frame_ref - 1))
                        || *(loader_ctx->frame_ref - 1) == VALUE_TYPE_ANY) {
                        loader_ctx->frame_ref--;
                        loader_ctx->stack_cell_num--;
                    }
                    else if (is_64bit_type(*(loader_ctx->frame_ref - 1))) {
                        loader_ctx->frame_ref -= 2;
                        loader_ctx->stack_cell_num -= 2;
                        *(p - 1) = WASM_OP_DROP_64;
                    }
                    else if (*(loader_ctx->frame_ref - 1) == REF_V128_1) {
                        loader_ctx->frame_ref -= 4;
                        loader_ctx->stack_cell_num -= 4;
                    }
                    else {
                        set_error_buf(error_buf, error_buf_size,
                                      "type mismatch");
                        goto fail;
                    }
                }
                break;
            }

            case WASM_OP_SELECT:
            {
                uint8 ref_type;
                BranchBlock *cur_block = loader_ctx->frame_csp - 1;
                int32 available_stack_cell;

                POP_I32();

                available_stack_cell = (int32)(loader_ctx->stack_cell_num
                                               - cur_block->stack_cell_num);

                if (available_stack_cell <= 0
                    && !cur_block->is_stack_polymorphic) {
                    set_error_buf(error_buf, error_buf_size,
                                  "type mismatch or invalid result arity, "
                                  "opcode select was found "
                                  "but stack was empty");
                    goto fail;
                }

                if (available_stack_cell > 0) {
                    switch (*(loader_ctx->frame_ref - 1)) {
                        case REF_I32:
                        case REF_F32:
                            break;
                        case REF_I64_2:
                        case REF_F64_2:
                            *(p - 1) = WASM_OP_SELECT_64;
                            break;
                        case REF_V128_4:
                            break;
                        default:
                        {
                            set_error_buf(error_buf, error_buf_size,
                                          "type mismatch");
                            goto fail;
                        }
                    }

                    ref_type = *(loader_ctx->frame_ref - 1);
                    POP2_AND_PUSH(ref_type, ref_type);
                }
                else {
                    PUSH_TYPE(VALUE_TYPE_ANY);
                }
                break;
            }

            case WASM_OP_GET_LOCAL:
            {
                p_org = p - 1;
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();
                PUSH_TYPE(local_type);
                break;
            }

            case WASM_OP_SET_LOCAL:
            {
                p_org = p - 1;
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();
                POP_TYPE(local_type);
                break;
            }

            case WASM_OP_TEE_LOCAL:
            {
                p_org = p - 1;
                GET_LOCAL_INDEX_TYPE_AND_OFFSET();
                POP_TYPE(local_type);
                PUSH_TYPE(local_type);
                break;
            }

            case WASM_OP_GET_GLOBAL:
            {
                p_org = p - 1;
                read_leb_uint32(p, p_end, global_idx);
                if (global_idx >= global_count) {
                    set_error_buf(error_buf, error_buf_size, "unknown global");
                    goto fail;
                }

                global_type =
                    global_idx < module->import_global_count
                        ? module->import_globals[global_idx].u.global.type
                        : module
                              ->globals[global_idx
                                        - module->import_global_count]
                              .type;

                PUSH_TYPE(global_type);

                if (global_type == VALUE_TYPE_I64
                    || global_type == VALUE_TYPE_F64) {
                    *p_org = WASM_OP_GET_GLOBAL_64;
                }
                break;
            }

            case WASM_OP_SET_GLOBAL:
            {
                bool is_mutable = false;

                p_org = p - 1;
                read_leb_uint32(p, p_end, global_idx);
                if (global_idx >= global_count) {
                    set_error_buf(error_buf, error_buf_size, "unknown global");
                    goto fail;
                }

                is_mutable =
                    global_idx < module->import_global_count
                        ? module->import_globals[global_idx].u.global.is_mutable
                        : module
                              ->globals[global_idx
                                        - module->import_global_count]
                              .is_mutable;
                if (!is_mutable) {
                    set_error_buf(error_buf, error_buf_size,
                                  "global is immutable");
                    goto fail;
                }

                global_type =
                    global_idx < module->import_global_count
                        ? module->import_globals[global_idx].u.global.type
                        : module
                              ->globals[global_idx
                                        - module->import_global_count]
                              .type;

                if (global_type == VALUE_TYPE_I64
                    || global_type == VALUE_TYPE_F64) {
                    *p_org = WASM_OP_SET_GLOBAL_64;
                }
                else if (module->aux_stack_size > 0
                         && global_idx == module->aux_stack_top_global_index) {
                    *p_org = WASM_OP_SET_GLOBAL_AUX_STACK;
                    func->has_op_set_global_aux_stack = true;
                }
                POP_TYPE(global_type);

                break;
            }

            /* load */
            case WASM_OP_I32_LOAD:
            case WASM_OP_I32_LOAD8_S:
            case WASM_OP_I32_LOAD8_U:
            case WASM_OP_I32_LOAD16_S:
            case WASM_OP_I32_LOAD16_U:
            case WASM_OP_I64_LOAD:
            case WASM_OP_I64_LOAD8_S:
            case WASM_OP_I64_LOAD8_U:
            case WASM_OP_I64_LOAD16_S:
            case WASM_OP_I64_LOAD16_U:
            case WASM_OP_I64_LOAD32_S:
            case WASM_OP_I64_LOAD32_U:
            case WASM_OP_F32_LOAD:
            case WASM_OP_F64_LOAD:
            /* store */
            case WASM_OP_I32_STORE:
            case WASM_OP_I32_STORE8:
            case WASM_OP_I32_STORE16:
            case WASM_OP_I64_STORE:
            case WASM_OP_I64_STORE8:
            case WASM_OP_I64_STORE16:
            case WASM_OP_I64_STORE32:
            case WASM_OP_F32_STORE:
            case WASM_OP_F64_STORE:
            {
                CHECK_MEMORY();
                read_leb_uint32(p, p_end, align);          /* align */
                read_leb_mem_offset(p, p_end, mem_offset); /* offset */
                if (!check_memory_access_align(opcode, align, error_buf,
                                               error_buf_size)) {
                    goto fail;
                }
                func->has_memory_operations = true;
                switch (opcode) {
                    /* load */
                    case WASM_OP_I32_LOAD:
                    case WASM_OP_I32_LOAD8_S:
                    case WASM_OP_I32_LOAD8_U:
                    case WASM_OP_I32_LOAD16_S:
                    case WASM_OP_I32_LOAD16_U:
                        POP_AND_PUSH(mem_offset_type, VALUE_TYPE_I32);
                        break;
                    case WASM_OP_I64_LOAD:
                    case WASM_OP_I64_LOAD8_S:
                    case WASM_OP_I64_LOAD8_U:
                    case WASM_OP_I64_LOAD16_S:
                    case WASM_OP_I64_LOAD16_U:
                    case WASM_OP_I64_LOAD32_S:
                    case WASM_OP_I64_LOAD32_U:
                        POP_AND_PUSH(mem_offset_type, VALUE_TYPE_I64);
                        break;
                    case WASM_OP_F32_LOAD:
                        POP_AND_PUSH(mem_offset_type, VALUE_TYPE_F32);
                        break;
                    case WASM_OP_F64_LOAD:
                        POP_AND_PUSH(mem_offset_type, VALUE_TYPE_F64);
                        break;
                    /* store */
                    case WASM_OP_I32_STORE:
                    case WASM_OP_I32_STORE8:
                    case WASM_OP_I32_STORE16:
                        POP_I32();
                        POP_MEM_OFFSET();
                        break;
                    case WASM_OP_I64_STORE:
                    case WASM_OP_I64_STORE8:
                    case WASM_OP_I64_STORE16:
                    case WASM_OP_I64_STORE32:
                        POP_I64();
                        POP_MEM_OFFSET();
                        break;
                    case WASM_OP_F32_STORE:
                        POP_F32();
                        POP_MEM_OFFSET();
                        break;
                    case WASM_OP_F64_STORE:
                        POP_F64();
                        POP_MEM_OFFSET();
                        break;
                    default:
                        break;
                }
                break;
            }

            case WASM_OP_MEMORY_SIZE:
                CHECK_MEMORY();
                /* reserved byte 0x00 */
                if (*p++ != 0x00) {
                    set_error_buf(error_buf, error_buf_size,
                                  "zero byte expected");
                    goto fail;
                }
                PUSH_PAGE_COUNT();

                module->possible_memory_grow = true;
                func->has_memory_operations = true;
                break;

            case WASM_OP_MEMORY_GROW:
                CHECK_MEMORY();
                /* reserved byte 0x00 */
                if (*p++ != 0x00) {
                    set_error_buf(error_buf, error_buf_size,
                                  "zero byte expected");
                    goto fail;
                }
                POP_AND_PUSH(mem_offset_type, mem_offset_type);

                module->possible_memory_grow = true;
                func->has_op_memory_grow = true;
                func->has_memory_operations = true;
                break;

            case WASM_OP_I32_CONST:
                read_leb_int32(p, p_end, i32_const);
                PUSH_I32();
                (void)i32_const;
                break;

            case WASM_OP_I64_CONST:
                read_leb_int64(p, p_end, i64_const);
                PUSH_I64();
                (void)i64_const;
                break;

            case WASM_OP_F32_CONST:
                p += sizeof(float32);
                PUSH_F32();
                break;

            case WASM_OP_F64_CONST:
                p += sizeof(float64);
                PUSH_F64();
                break;

            case WASM_OP_I32_EQZ:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
                break;

            case WASM_OP_I32_EQ:
            case WASM_OP_I32_NE:
            case WASM_OP_I32_LT_S:
            case WASM_OP_I32_LT_U:
            case WASM_OP_I32_GT_S:
            case WASM_OP_I32_GT_U:
            case WASM_OP_I32_LE_S:
            case WASM_OP_I32_LE_U:
            case WASM_OP_I32_GE_S:
            case WASM_OP_I32_GE_U:
                POP2_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
                break;

            case WASM_OP_I64_EQZ:
                POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I32);
                break;

            case WASM_OP_I64_EQ:
            case WASM_OP_I64_NE:
            case WASM_OP_I64_LT_S:
            case WASM_OP_I64_LT_U:
            case WASM_OP_I64_GT_S:
            case WASM_OP_I64_GT_U:
            case WASM_OP_I64_LE_S:
            case WASM_OP_I64_LE_U:
            case WASM_OP_I64_GE_S:
            case WASM_OP_I64_GE_U:
                POP2_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I32);
                break;

            case WASM_OP_F32_EQ:
            case WASM_OP_F32_NE:
            case WASM_OP_F32_LT:
            case WASM_OP_F32_GT:
            case WASM_OP_F32_LE:
            case WASM_OP_F32_GE:
                POP2_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I32);
                break;

            case WASM_OP_F64_EQ:
            case WASM_OP_F64_NE:
            case WASM_OP_F64_LT:
            case WASM_OP_F64_GT:
            case WASM_OP_F64_LE:
            case WASM_OP_F64_GE:
                POP2_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I32);
                break;

            case WASM_OP_I32_CLZ:
            case WASM_OP_I32_CTZ:
            case WASM_OP_I32_POPCNT:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
                break;

            case WASM_OP_I32_ADD:
            case WASM_OP_I32_SUB:
            case WASM_OP_I32_MUL:
            case WASM_OP_I32_DIV_S:
            case WASM_OP_I32_DIV_U:
            case WASM_OP_I32_REM_S:
            case WASM_OP_I32_REM_U:
            case WASM_OP_I32_AND:
            case WASM_OP_I32_OR:
            case WASM_OP_I32_XOR:
            case WASM_OP_I32_SHL:
            case WASM_OP_I32_SHR_S:
            case WASM_OP_I32_SHR_U:
            case WASM_OP_I32_ROTL:
            case WASM_OP_I32_ROTR:
                POP2_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
                break;

            case WASM_OP_I64_CLZ:
            case WASM_OP_I64_CTZ:
            case WASM_OP_I64_POPCNT:
                POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I64);
                break;

            case WASM_OP_I64_ADD:
            case WASM_OP_I64_SUB:
            case WASM_OP_I64_MUL:
            case WASM_OP_I64_DIV_S:
            case WASM_OP_I64_DIV_U:
            case WASM_OP_I64_REM_S:
            case WASM_OP_I64_REM_U:
            case WASM_OP_I64_AND:
            case WASM_OP_I64_OR:
            case WASM_OP_I64_XOR:
            case WASM_OP_I64_SHL:
            case WASM_OP_I64_SHR_S:
            case WASM_OP_I64_SHR_U:
            case WASM_OP_I64_ROTL:
            case WASM_OP_I64_ROTR:
                POP2_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I64);
                break;

            case WASM_OP_F32_ABS:
            case WASM_OP_F32_NEG:
            case WASM_OP_F32_CEIL:
            case WASM_OP_F32_FLOOR:
            case WASM_OP_F32_TRUNC:
            case WASM_OP_F32_NEAREST:
            case WASM_OP_F32_SQRT:
                POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_F32);
                break;

            case WASM_OP_F32_ADD:
            case WASM_OP_F32_SUB:
            case WASM_OP_F32_MUL:
            case WASM_OP_F32_DIV:
            case WASM_OP_F32_MIN:
            case WASM_OP_F32_MAX:
            case WASM_OP_F32_COPYSIGN:
                POP2_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_F32);
                break;

            case WASM_OP_F64_ABS:
            case WASM_OP_F64_NEG:
            case WASM_OP_F64_CEIL:
            case WASM_OP_F64_FLOOR:
            case WASM_OP_F64_TRUNC:
            case WASM_OP_F64_NEAREST:
            case WASM_OP_F64_SQRT:
                POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_F64);
                break;

            case WASM_OP_F64_ADD:
            case WASM_OP_F64_SUB:
            case WASM_OP_F64_MUL:
            case WASM_OP_F64_DIV:
            case WASM_OP_F64_MIN:
            case WASM_OP_F64_MAX:
            case WASM_OP_F64_COPYSIGN:
                POP2_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_F64);
                break;

            case WASM_OP_I32_WRAP_I64:
                POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I32);
                break;

            case WASM_OP_I32_TRUNC_S_F32:
            case WASM_OP_I32_TRUNC_U_F32:
                POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I32);
                break;

            case WASM_OP_I32_TRUNC_S_F64:
            case WASM_OP_I32_TRUNC_U_F64:
                POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I32);
                break;

            case WASM_OP_I64_EXTEND_S_I32:
            case WASM_OP_I64_EXTEND_U_I32:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I64);
                break;

            case WASM_OP_I64_TRUNC_S_F32:
            case WASM_OP_I64_TRUNC_U_F32:
                POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I64);
                break;

            case WASM_OP_I64_TRUNC_S_F64:
            case WASM_OP_I64_TRUNC_U_F64:
                POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I64);
                break;

            case WASM_OP_F32_CONVERT_S_I32:
            case WASM_OP_F32_CONVERT_U_I32:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_F32);
                break;

            case WASM_OP_F32_CONVERT_S_I64:
            case WASM_OP_F32_CONVERT_U_I64:
                POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_F32);
                break;

            case WASM_OP_F32_DEMOTE_F64:
                POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_F32);
                break;

            case WASM_OP_F64_CONVERT_S_I32:
            case WASM_OP_F64_CONVERT_U_I32:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_F64);
                break;

            case WASM_OP_F64_CONVERT_S_I64:
            case WASM_OP_F64_CONVERT_U_I64:
                POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_F64);
                break;

            case WASM_OP_F64_PROMOTE_F32:
                POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_F64);
                break;

            case WASM_OP_I32_REINTERPRET_F32:
                POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I32);
                break;

            case WASM_OP_I64_REINTERPRET_F64:
                POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I64);
                break;

            case WASM_OP_F32_REINTERPRET_I32:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_F32);
                break;

            case WASM_OP_F64_REINTERPRET_I64:
                POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_F64);
                break;

            case WASM_OP_I32_EXTEND8_S:
            case WASM_OP_I32_EXTEND16_S:
                POP_AND_PUSH(VALUE_TYPE_I32, VALUE_TYPE_I32);
                break;

            case WASM_OP_I64_EXTEND8_S:
            case WASM_OP_I64_EXTEND16_S:
            case WASM_OP_I64_EXTEND32_S:
                POP_AND_PUSH(VALUE_TYPE_I64, VALUE_TYPE_I64);
                break;

            case WASM_OP_MISC_PREFIX:
            {
                uint32 opcode1;

                read_leb_uint32(p, p_end, opcode1);

                switch (opcode1) {
                    case WASM_OP_I32_TRUNC_SAT_S_F32:
                    case WASM_OP_I32_TRUNC_SAT_U_F32:
                        POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I32);
                        break;
                    case WASM_OP_I32_TRUNC_SAT_S_F64:
                    case WASM_OP_I32_TRUNC_SAT_U_F64:
                        POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I32);
                        break;
                    case WASM_OP_I64_TRUNC_SAT_S_F32:
                    case WASM_OP_I64_TRUNC_SAT_U_F32:
                        POP_AND_PUSH(VALUE_TYPE_F32, VALUE_TYPE_I64);
                        break;
                    case WASM_OP_I64_TRUNC_SAT_S_F64:
                    case WASM_OP_I64_TRUNC_SAT_U_F64:
                        POP_AND_PUSH(VALUE_TYPE_F64, VALUE_TYPE_I64);
                        break;

                    case WASM_OP_MEMORY_INIT:
                    {
                        read_leb_uint32(p, p_end, data_seg_idx);

                        if (module->import_memory_count == 0
                            && module->memory_count == 0)
                            goto fail_unknown_memory;

                        if (*p++ != 0x00)
                            goto fail_zero_byte_expected;

                        if (data_seg_idx >= module->data_seg_count) {
                            set_error_buf_v(error_buf, error_buf_size,
                                            "unknown data segment %d",
                                            data_seg_idx);
                            goto fail;
                        }

                        if (module->data_seg_count1 == 0)
                            goto fail_data_cnt_sec_require;

                        POP_I32();
                        POP_I32();
                        POP_MEM_OFFSET();
                        func->has_memory_operations = true;
                        break;
                    }
                    case WASM_OP_DATA_DROP:
                    {
                        read_leb_uint32(p, p_end, data_seg_idx);
                        if (data_seg_idx >= module->data_seg_count) {
                            set_error_buf(error_buf, error_buf_size,
                                          "unknown data segment");
                            goto fail;
                        }

                        if (module->data_seg_count1 == 0)
                            goto fail_data_cnt_sec_require;

                        func->has_memory_operations = true;
                        break;
                    }
                    case WASM_OP_MEMORY_COPY:
                    {
                        /* both src and dst memory index should be 0 */
                        if (*(int16 *)p != 0x0000)
                            goto fail_zero_byte_expected;

                        p += 2;

                        if (module->import_memory_count == 0
                            && module->memory_count == 0)
                            goto fail_unknown_memory;

                        POP_MEM_OFFSET();
                        POP_MEM_OFFSET();
                        POP_MEM_OFFSET();

                        func->has_memory_operations = true;
                        break;
                    }
                    case WASM_OP_MEMORY_FILL:
                    {
                        if (*p++ != 0x00) {
                            goto fail_zero_byte_expected;
                        }
                        if (module->import_memory_count == 0
                            && module->memory_count == 0) {
                            goto fail_unknown_memory;
                        }

                        POP_MEM_OFFSET();
                        POP_I32();
                        POP_MEM_OFFSET();

                        func->has_memory_operations = true;
                        break;
                    }
                    fail_zero_byte_expected:
                        set_error_buf(error_buf, error_buf_size,
                                      "zero byte expected");
                        goto fail;
                    fail_unknown_memory:
                        set_error_buf(error_buf, error_buf_size,
                                      "unknown memory 0");
                        goto fail;
                    fail_data_cnt_sec_require:
                        set_error_buf(error_buf, error_buf_size,
                                      "data count section required");
                        goto fail;

                    default:
                        set_error_buf_v(error_buf, error_buf_size,
                                        "%s %02x %02x", "unsupported opcode",
                                        0xfc, opcode1);
                        goto fail;
                }
                break;
            }

            case WASM_OP_SIMD_PREFIX:
            {
                uint32 opcode1;

                read_leb_uint32(p, p_end, opcode1);

                switch (opcode1) {
                    /* memory instruction */
                    case SIMD_v128_load:
                    case SIMD_v128_load8x8_s:
                    case SIMD_v128_load8x8_u:
                    case SIMD_v128_load16x4_s:
                    case SIMD_v128_load16x4_u:
                    case SIMD_v128_load32x2_s:
                    case SIMD_v128_load32x2_u:
                    case SIMD_v128_load8_splat:
                    case SIMD_v128_load16_splat:
                    case SIMD_v128_load32_splat:
                    case SIMD_v128_load64_splat:
                    {
                        CHECK_MEMORY();

                        read_leb_uint32(p, p_end, align); /* align */
                        if (!check_simd_memory_access_align(
                                opcode1, align, error_buf, error_buf_size)) {
                            goto fail;
                        }

                        read_leb_mem_offset(p, p_end, mem_offset); /* offset */

                        POP_AND_PUSH(mem_offset_type, VALUE_TYPE_V128);
                        func->has_memory_operations = true;
                        break;
                    }

                    case SIMD_v128_store:
                    {
                        CHECK_MEMORY();

                        read_leb_uint32(p, p_end, align); /* align */
                        if (!check_simd_memory_access_align(
                                opcode1, align, error_buf, error_buf_size)) {
                            goto fail;
                        }

                        read_leb_mem_offset(p, p_end, mem_offset); /* offset */

                        POP_V128();
                        POP_MEM_OFFSET();
                        func->has_memory_operations = true;
                        break;
                    }

                    /* basic operation */
                    case SIMD_v128_const:
                    {
                        CHECK_BUF1(p, p_end, 16);
                        p += 16;
                        PUSH_V128();
                        break;
                    }

                    case SIMD_v8x16_shuffle:
                    {
                        V128 mask;

                        CHECK_BUF1(p, p_end, 16);
                        mask = read_i8x16(p, error_buf, error_buf_size);
                        p += 16;
                        if (!check_simd_shuffle_mask(mask, error_buf,
                                                     error_buf_size)) {
                            goto fail;
                        }

                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_v8x16_swizzle:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    /* splat operation */
                    case SIMD_i8x16_splat:
                    case SIMD_i16x8_splat:
                    case SIMD_i32x4_splat:
                    case SIMD_i64x2_splat:
                    case SIMD_f32x4_splat:
                    case SIMD_f64x2_splat:
                    {
                        uint8 pop_type[] = { VALUE_TYPE_I32, VALUE_TYPE_I32,
                                             VALUE_TYPE_I32, VALUE_TYPE_I64,
                                             VALUE_TYPE_F32, VALUE_TYPE_F64 };
                        POP_AND_PUSH(pop_type[opcode1 - SIMD_i8x16_splat],
                                     VALUE_TYPE_V128);
                        break;
                    }

                    /* lane operation */
                    case SIMD_i8x16_extract_lane_s:
                    case SIMD_i8x16_extract_lane_u:
                    case SIMD_i8x16_replace_lane:
                    case SIMD_i16x8_extract_lane_s:
                    case SIMD_i16x8_extract_lane_u:
                    case SIMD_i16x8_replace_lane:
                    case SIMD_i32x4_extract_lane:
                    case SIMD_i32x4_replace_lane:
                    case SIMD_i64x2_extract_lane:
                    case SIMD_i64x2_replace_lane:
                    case SIMD_f32x4_extract_lane:
                    case SIMD_f32x4_replace_lane:
                    case SIMD_f64x2_extract_lane:
                    case SIMD_f64x2_replace_lane:
                    {
                        uint8 lane;
                        /* clang-format off */
                        uint8 replace[] = {
                            /*i8x16*/ 0x0, 0x0, VALUE_TYPE_I32,
                            /*i16x8*/ 0x0, 0x0, VALUE_TYPE_I32,
                            /*i32x4*/ 0x0, VALUE_TYPE_I32,
                            /*i64x2*/ 0x0, VALUE_TYPE_I64,
                            /*f32x4*/ 0x0, VALUE_TYPE_F32,
                            /*f64x2*/ 0x0, VALUE_TYPE_F64,
                        };
                        uint8 push_type[] = {
                            /*i8x16*/ VALUE_TYPE_I32, VALUE_TYPE_I32,
                                      VALUE_TYPE_V128,
                            /*i16x8*/ VALUE_TYPE_I32, VALUE_TYPE_I32,
                                      VALUE_TYPE_V128,
                            /*i32x4*/ VALUE_TYPE_I32, VALUE_TYPE_V128,
                            /*i64x2*/ VALUE_TYPE_I64, VALUE_TYPE_V128,
                            /*f32x4*/ VALUE_TYPE_F32, VALUE_TYPE_V128,
                            /*f64x2*/ VALUE_TYPE_F64, VALUE_TYPE_V128,
                        };
                        /* clang-format on */

                        CHECK_BUF(p, p_end, 1);
                        lane = read_uint8(p);
                        if (!check_simd_access_lane(opcode1, lane, error_buf,
                                                    error_buf_size)) {
                            goto fail;
                        }

                        if (replace[opcode1 - SIMD_i8x16_extract_lane_s]) {
                            if (!(wasm_loader_pop_frame_ref(
                                    loader_ctx,
                                    replace[opcode1
                                            - SIMD_i8x16_extract_lane_s],
                                    error_buf, error_buf_size)))
                                goto fail;
                        }

                        POP_AND_PUSH(
                            VALUE_TYPE_V128,
                            push_type[opcode1 - SIMD_i8x16_extract_lane_s]);
                        break;
                    }

                    /* i8x16 compare operation */
                    case SIMD_i8x16_eq:
                    case SIMD_i8x16_ne:
                    case SIMD_i8x16_lt_s:
                    case SIMD_i8x16_lt_u:
                    case SIMD_i8x16_gt_s:
                    case SIMD_i8x16_gt_u:
                    case SIMD_i8x16_le_s:
                    case SIMD_i8x16_le_u:
                    case SIMD_i8x16_ge_s:
                    case SIMD_i8x16_ge_u:
                    /* i16x8 compare operation */
                    case SIMD_i16x8_eq:
                    case SIMD_i16x8_ne:
                    case SIMD_i16x8_lt_s:
                    case SIMD_i16x8_lt_u:
                    case SIMD_i16x8_gt_s:
                    case SIMD_i16x8_gt_u:
                    case SIMD_i16x8_le_s:
                    case SIMD_i16x8_le_u:
                    case SIMD_i16x8_ge_s:
                    case SIMD_i16x8_ge_u:
                    /* i32x4 compare operation */
                    case SIMD_i32x4_eq:
                    case SIMD_i32x4_ne:
                    case SIMD_i32x4_lt_s:
                    case SIMD_i32x4_lt_u:
                    case SIMD_i32x4_gt_s:
                    case SIMD_i32x4_gt_u:
                    case SIMD_i32x4_le_s:
                    case SIMD_i32x4_le_u:
                    case SIMD_i32x4_ge_s:
                    case SIMD_i32x4_ge_u:
                    /* f32x4 compare operation */
                    case SIMD_f32x4_eq:
                    case SIMD_f32x4_ne:
                    case SIMD_f32x4_lt:
                    case SIMD_f32x4_gt:
                    case SIMD_f32x4_le:
                    case SIMD_f32x4_ge:
                    /* f64x2 compare operation */
                    case SIMD_f64x2_eq:
                    case SIMD_f64x2_ne:
                    case SIMD_f64x2_lt:
                    case SIMD_f64x2_gt:
                    case SIMD_f64x2_le:
                    case SIMD_f64x2_ge:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    /* v128 operation */
                    case SIMD_v128_not:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_v128_and:
                    case SIMD_v128_andnot:
                    case SIMD_v128_or:
                    case SIMD_v128_xor:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_v128_bitselect:
                    {
                        POP_V128();
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_v128_any_true:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_I32);
                        break;
                    }

                    /* Load Lane Operation */
                    case SIMD_v128_load8_lane:
                    case SIMD_v128_load16_lane:
                    case SIMD_v128_load32_lane:
                    case SIMD_v128_load64_lane:
                    case SIMD_v128_store8_lane:
                    case SIMD_v128_store16_lane:
                    case SIMD_v128_store32_lane:
                    case SIMD_v128_store64_lane:
                    {
                        uint8 lane;

                        CHECK_MEMORY();

                        read_leb_uint32(p, p_end, align); /* align */
                        if (!check_simd_memory_access_align(
                                opcode1, align, error_buf, error_buf_size)) {
                            goto fail;
                        }

                        read_leb_mem_offset(p, p_end, mem_offset); /* offset */

                        CHECK_BUF(p, p_end, 1);
                        lane = read_uint8(p);
                        if (!check_simd_access_lane(opcode1, lane, error_buf,
                                                    error_buf_size)) {
                            goto fail;
                        }

                        POP_V128();
                        POP_MEM_OFFSET();
                        if (opcode1 < SIMD_v128_store8_lane) {
                            PUSH_V128();
                        }
                        func->has_memory_operations = true;
                        break;
                    }

                    case SIMD_v128_load32_zero:
                    case SIMD_v128_load64_zero:
                    {
                        CHECK_MEMORY();

                        read_leb_uint32(p, p_end, align); /* align */
                        if (!check_simd_memory_access_align(
                                opcode1, align, error_buf, error_buf_size)) {
                            goto fail;
                        }

                        read_leb_mem_offset(p, p_end, mem_offset); /* offset */

                        POP_AND_PUSH(mem_offset_type, VALUE_TYPE_V128);
                        func->has_memory_operations = true;
                        break;
                    }

                    /* Float conversion */
                    case SIMD_f32x4_demote_f64x2_zero:
                    case SIMD_f64x2_promote_low_f32x4_zero:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    /* i8x16 Operation */
                    case SIMD_i8x16_abs:
                    case SIMD_i8x16_neg:
                    case SIMD_i8x16_popcnt:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i8x16_all_true:
                    case SIMD_i8x16_bitmask:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_I32);
                        break;
                    }

                    case SIMD_i8x16_narrow_i16x8_s:
                    case SIMD_i8x16_narrow_i16x8_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_f32x4_ceil:
                    case SIMD_f32x4_floor:
                    case SIMD_f32x4_trunc:
                    case SIMD_f32x4_nearest:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i8x16_shl:
                    case SIMD_i8x16_shr_s:
                    case SIMD_i8x16_shr_u:
                    {
                        POP_I32();
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i8x16_add:
                    case SIMD_i8x16_add_sat_s:
                    case SIMD_i8x16_add_sat_u:
                    case SIMD_i8x16_sub:
                    case SIMD_i8x16_sub_sat_s:
                    case SIMD_i8x16_sub_sat_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_f64x2_ceil:
                    case SIMD_f64x2_floor:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i8x16_min_s:
                    case SIMD_i8x16_min_u:
                    case SIMD_i8x16_max_s:
                    case SIMD_i8x16_max_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_f64x2_trunc:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i8x16_avgr_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i16x8_extadd_pairwise_i8x16_s:
                    case SIMD_i16x8_extadd_pairwise_i8x16_u:
                    case SIMD_i32x4_extadd_pairwise_i16x8_s:
                    case SIMD_i32x4_extadd_pairwise_i16x8_u:
                    /* i16x8 operation */
                    case SIMD_i16x8_abs:
                    case SIMD_i16x8_neg:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i16x8_q15mulr_sat_s:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i16x8_all_true:
                    case SIMD_i16x8_bitmask:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_I32);
                        break;
                    }

                    case SIMD_i16x8_narrow_i32x4_s:
                    case SIMD_i16x8_narrow_i32x4_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i16x8_extend_low_i8x16_s:
                    case SIMD_i16x8_extend_high_i8x16_s:
                    case SIMD_i16x8_extend_low_i8x16_u:
                    case SIMD_i16x8_extend_high_i8x16_u:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i16x8_shl:
                    case SIMD_i16x8_shr_s:
                    case SIMD_i16x8_shr_u:
                    {
                        POP_I32();
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i16x8_add:
                    case SIMD_i16x8_add_sat_s:
                    case SIMD_i16x8_add_sat_u:
                    case SIMD_i16x8_sub:
                    case SIMD_i16x8_sub_sat_s:
                    case SIMD_i16x8_sub_sat_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_f64x2_nearest:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i16x8_mul:
                    case SIMD_i16x8_min_s:
                    case SIMD_i16x8_min_u:
                    case SIMD_i16x8_max_s:
                    case SIMD_i16x8_max_u:
                    case SIMD_i16x8_avgr_u:
                    case SIMD_i16x8_extmul_low_i8x16_s:
                    case SIMD_i16x8_extmul_high_i8x16_s:
                    case SIMD_i16x8_extmul_low_i8x16_u:
                    case SIMD_i16x8_extmul_high_i8x16_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    /* i32x4 operation */
                    case SIMD_i32x4_abs:
                    case SIMD_i32x4_neg:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i32x4_all_true:
                    case SIMD_i32x4_bitmask:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_I32);
                        break;
                    }

                    case SIMD_i32x4_narrow_i64x2_s:
                    case SIMD_i32x4_narrow_i64x2_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i32x4_extend_low_i16x8_s:
                    case SIMD_i32x4_extend_high_i16x8_s:
                    case SIMD_i32x4_extend_low_i16x8_u:
                    case SIMD_i32x4_extend_high_i16x8_u:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i32x4_shl:
                    case SIMD_i32x4_shr_s:
                    case SIMD_i32x4_shr_u:
                    {
                        POP_I32();
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i32x4_add:
                    case SIMD_i32x4_sub:
                    case SIMD_i32x4_mul:
                    case SIMD_i32x4_min_s:
                    case SIMD_i32x4_min_u:
                    case SIMD_i32x4_max_s:
                    case SIMD_i32x4_max_u:
                    case SIMD_i32x4_dot_i16x8_s:
                    case SIMD_i32x4_avgr_u:
                    case SIMD_i32x4_extmul_low_i16x8_s:
                    case SIMD_i32x4_extmul_high_i16x8_s:
                    case SIMD_i32x4_extmul_low_i16x8_u:
                    case SIMD_i32x4_extmul_high_i16x8_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    /* i64x2 operation */
                    case SIMD_i64x2_abs:
                    case SIMD_i64x2_neg:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i64x2_all_true:
                    case SIMD_i64x2_bitmask:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_I32);
                        break;
                    }

                    case SIMD_i64x2_extend_low_i32x4_s:
                    case SIMD_i64x2_extend_high_i32x4_s:
                    case SIMD_i64x2_extend_low_i32x4_u:
                    case SIMD_i64x2_extend_high_i32x4_u:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i64x2_shl:
                    case SIMD_i64x2_shr_s:
                    case SIMD_i64x2_shr_u:
                    {
                        POP_I32();
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i64x2_add:
                    case SIMD_i64x2_sub:
                    case SIMD_i64x2_mul:
                    case SIMD_i64x2_eq:
                    case SIMD_i64x2_ne:
                    case SIMD_i64x2_lt_s:
                    case SIMD_i64x2_gt_s:
                    case SIMD_i64x2_le_s:
                    case SIMD_i64x2_ge_s:
                    case SIMD_i64x2_extmul_low_i32x4_s:
                    case SIMD_i64x2_extmul_high_i32x4_s:
                    case SIMD_i64x2_extmul_low_i32x4_u:
                    case SIMD_i64x2_extmul_high_i32x4_u:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    /* f32x4 operation */
                    case SIMD_f32x4_abs:
                    case SIMD_f32x4_neg:
                    case SIMD_f32x4_round:
                    case SIMD_f32x4_sqrt:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_f32x4_add:
                    case SIMD_f32x4_sub:
                    case SIMD_f32x4_mul:
                    case SIMD_f32x4_div:
                    case SIMD_f32x4_min:
                    case SIMD_f32x4_max:
                    case SIMD_f32x4_pmin:
                    case SIMD_f32x4_pmax:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    /* f64x2 operation */
                    case SIMD_f64x2_abs:
                    case SIMD_f64x2_neg:
                    case SIMD_f64x2_round:
                    case SIMD_f64x2_sqrt:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_f64x2_add:
                    case SIMD_f64x2_sub:
                    case SIMD_f64x2_mul:
                    case SIMD_f64x2_div:
                    case SIMD_f64x2_min:
                    case SIMD_f64x2_max:
                    case SIMD_f64x2_pmin:
                    case SIMD_f64x2_pmax:
                    {
                        POP2_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    case SIMD_i32x4_trunc_sat_f32x4_s:
                    case SIMD_i32x4_trunc_sat_f32x4_u:
                    case SIMD_f32x4_convert_i32x4_s:
                    case SIMD_f32x4_convert_i32x4_u:
                    case SIMD_i32x4_trunc_sat_f64x2_s_zero:
                    case SIMD_i32x4_trunc_sat_f64x2_u_zero:
                    case SIMD_f64x2_convert_low_i32x4_s:
                    case SIMD_f64x2_convert_low_i32x4_u:
                    {
                        POP_AND_PUSH(VALUE_TYPE_V128, VALUE_TYPE_V128);
                        break;
                    }

                    default:
                    {
                        if (error_buf != NULL) {
                            snprintf(error_buf, error_buf_size,
                                     "WASM module load failed: "
                                     "invalid opcode 0xfd %02x.",
                                     opcode1);
                        }
                        goto fail;
                    }
                }
                break;
            }

            case WASM_OP_ATOMIC_PREFIX:
            {
                uint32 opcode1;

                read_leb_uint32(p, p_end, opcode1);

                if (opcode1 != WASM_OP_ATOMIC_FENCE) {
                    CHECK_MEMORY();
                    read_leb_uint32(p, p_end, align);          /* align */
                    read_leb_mem_offset(p, p_end, mem_offset); /* offset */
                    if (!check_memory_align_equal(opcode1, align, error_buf,
                                                  error_buf_size)) {
                        goto fail;
                    }
                }
                func->has_memory_operations = true;
                switch (opcode1) {
                    case WASM_OP_ATOMIC_NOTIFY:
                        POP_I32();
                        POP_MEM_OFFSET();
                        PUSH_I32();
                        break;
                    case WASM_OP_ATOMIC_WAIT32:
                        POP_I64();
                        POP_I32();
                        POP_MEM_OFFSET();
                        PUSH_I32();
                        break;
                    case WASM_OP_ATOMIC_WAIT64:
                        POP_I64();
                        POP_I64();
                        POP_MEM_OFFSET();
                        PUSH_I32();
                        break;
                    case WASM_OP_ATOMIC_FENCE:
                        /* reserved byte 0x00 */
                        if (*p++ != 0x00) {
                            set_error_buf(error_buf, error_buf_size,
                                          "zero byte expected");
                            goto fail;
                        }
                        break;
                    case WASM_OP_ATOMIC_I32_LOAD:
                    case WASM_OP_ATOMIC_I32_LOAD8_U:
                    case WASM_OP_ATOMIC_I32_LOAD16_U:
                        POP_AND_PUSH(mem_offset_type, VALUE_TYPE_I32);
                        break;
                    case WASM_OP_ATOMIC_I32_STORE:
                    case WASM_OP_ATOMIC_I32_STORE8:
                    case WASM_OP_ATOMIC_I32_STORE16:
                        POP_I32();
                        POP_MEM_OFFSET();
                        break;
                    case WASM_OP_ATOMIC_I64_LOAD:
                    case WASM_OP_ATOMIC_I64_LOAD8_U:
                    case WASM_OP_ATOMIC_I64_LOAD16_U:
                    case WASM_OP_ATOMIC_I64_LOAD32_U:
                        POP_AND_PUSH(mem_offset_type, VALUE_TYPE_I64);
                        break;
                    case WASM_OP_ATOMIC_I64_STORE:
                    case WASM_OP_ATOMIC_I64_STORE8:
                    case WASM_OP_ATOMIC_I64_STORE16:
                    case WASM_OP_ATOMIC_I64_STORE32:
                        POP_I64();
                        POP_MEM_OFFSET();
                        break;
                    case WASM_OP_ATOMIC_RMW_I32_ADD:
                    case WASM_OP_ATOMIC_RMW_I32_ADD8_U:
                    case WASM_OP_ATOMIC_RMW_I32_ADD16_U:
                    case WASM_OP_ATOMIC_RMW_I32_SUB:
                    case WASM_OP_ATOMIC_RMW_I32_SUB8_U:
                    case WASM_OP_ATOMIC_RMW_I32_SUB16_U:
                    case WASM_OP_ATOMIC_RMW_I32_AND:
                    case WASM_OP_ATOMIC_RMW_I32_AND8_U:
                    case WASM_OP_ATOMIC_RMW_I32_AND16_U:
                    case WASM_OP_ATOMIC_RMW_I32_OR:
                    case WASM_OP_ATOMIC_RMW_I32_OR8_U:
                    case WASM_OP_ATOMIC_RMW_I32_OR16_U:
                    case WASM_OP_ATOMIC_RMW_I32_XOR:
                    case WASM_OP_ATOMIC_RMW_I32_XOR8_U:
                    case WASM_OP_ATOMIC_RMW_I32_XOR16_U:
                    case WASM_OP_ATOMIC_RMW_I32_XCHG:
                    case WASM_OP_ATOMIC_RMW_I32_XCHG8_U:
                    case WASM_OP_ATOMIC_RMW_I32_XCHG16_U:
                        POP_I32();
                        POP_MEM_OFFSET();
                        PUSH_I32();
                        break;
                    case WASM_OP_ATOMIC_RMW_I64_ADD:
                    case WASM_OP_ATOMIC_RMW_I64_ADD8_U:
                    case WASM_OP_ATOMIC_RMW_I64_ADD16_U:
                    case WASM_OP_ATOMIC_RMW_I64_ADD32_U:
                    case WASM_OP_ATOMIC_RMW_I64_SUB:
                    case WASM_OP_ATOMIC_RMW_I64_SUB8_U:
                    case WASM_OP_ATOMIC_RMW_I64_SUB16_U:
                    case WASM_OP_ATOMIC_RMW_I64_SUB32_U:
                    case WASM_OP_ATOMIC_RMW_I64_AND:
                    case WASM_OP_ATOMIC_RMW_I64_AND8_U:
                    case WASM_OP_ATOMIC_RMW_I64_AND16_U:
                    case WASM_OP_ATOMIC_RMW_I64_AND32_U:
                    case WASM_OP_ATOMIC_RMW_I64_OR:
                    case WASM_OP_ATOMIC_RMW_I64_OR8_U:
                    case WASM_OP_ATOMIC_RMW_I64_OR16_U:
                    case WASM_OP_ATOMIC_RMW_I64_OR32_U:
                    case WASM_OP_ATOMIC_RMW_I64_XOR:
                    case WASM_OP_ATOMIC_RMW_I64_XOR8_U:
                    case WASM_OP_ATOMIC_RMW_I64_XOR16_U:
                    case WASM_OP_ATOMIC_RMW_I64_XOR32_U:
                    case WASM_OP_ATOMIC_RMW_I64_XCHG:
                    case WASM_OP_ATOMIC_RMW_I64_XCHG8_U:
                    case WASM_OP_ATOMIC_RMW_I64_XCHG16_U:
                    case WASM_OP_ATOMIC_RMW_I64_XCHG32_U:
                        POP_I64();
                        POP_MEM_OFFSET();
                        PUSH_I64();
                        break;
                    case WASM_OP_ATOMIC_RMW_I32_CMPXCHG:
                    case WASM_OP_ATOMIC_RMW_I32_CMPXCHG8_U:
                    case WASM_OP_ATOMIC_RMW_I32_CMPXCHG16_U:
                        POP_I32();
                        POP_I32();
                        POP_MEM_OFFSET();
                        PUSH_I32();
                        break;
                    case WASM_OP_ATOMIC_RMW_I64_CMPXCHG:
                    case WASM_OP_ATOMIC_RMW_I64_CMPXCHG8_U:
                    case WASM_OP_ATOMIC_RMW_I64_CMPXCHG16_U:
                    case WASM_OP_ATOMIC_RMW_I64_CMPXCHG32_U:
                        POP_I64();
                        POP_I64();
                        POP_MEM_OFFSET();
                        PUSH_I64();
                        break;
                    default:
                        set_error_buf_v(error_buf, error_buf_size,
                                        "%s %02x %02x", "unsupported opcode",
                                        0xfe, opcode1);
                        goto fail;
                }
                break;
            }

            default:
                set_error_buf_v(error_buf, error_buf_size, "%s %02x",
                                "unsupported opcode", opcode);
                goto fail;
        }
    }

    if (loader_ctx->csp_num > 0) {
        if (cur_func_idx < module->function_count - 1)
            /* Function with missing end marker (between two functions) */
            set_error_buf(error_buf, error_buf_size, "END opcode expected");
        else
            /* Function with missing end marker
               (at EOF or end of code sections) */
            set_error_buf(error_buf, error_buf_size,
                          "unexpected end of section or function, "
                          "or section size mismatch");
        goto fail;
    }

    func->max_stack_cell_num = loader_ctx->max_stack_cell_num;
    func->max_block_num = loader_ctx->max_csp_num;
    return_value = true;

fail:
    wasm_loader_ctx_destroy(loader_ctx);

    (void)table_idx;
    (void)table_seg_idx;
    (void)data_seg_idx;
    (void)i64_const;
    (void)local_offset;
    (void)p_org;
    (void)mem_offset;
    (void)align;
    return return_value;
}
