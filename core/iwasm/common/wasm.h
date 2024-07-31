/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _WASM_H_
#define _WASM_H_

#include "bh_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Value Type */
#define VALUE_TYPE_I32 0x7F
#define VALUE_TYPE_I64 0X7E
#define VALUE_TYPE_F32 0x7D
#define VALUE_TYPE_F64 0x7C
#define VALUE_TYPE_V128 0x7B
#define VALUE_TYPE_FUNCREF 0x70
#define VALUE_TYPE_VOID 0x40
/* Used by AOT */
#define VALUE_TYPE_I1 0x41
/*  Used by loader to represent any type of i32/i64/f32/f64 */
#define VALUE_TYPE_ANY 0x42

#define MAX_PAGE_COUNT_FLAG 0x01
#define SHARED_MEMORY_FLAG 0x02
#define MEMORY64_FLAG 0x04

#define SHARED_TABLE_FLAG 0x02
#define TABLE64_FLAG 0x04

#define DEFAULT_NUM_BYTES_PER_PAGE 65536
#define DEFAULT_MAX_PAGES 65536
#define DEFAULT_MEM64_MAX_PAGES UINT32_MAX

/* Max size of linear memory */
#define MAX_LINEAR_MEMORY_SIZE (4 * (uint64)BH_GB)
/* Roughly 274 TB */
#define MAX_LINEAR_MEM64_MEMORY_SIZE \
    (DEFAULT_MEM64_MAX_PAGES * (uint64)64 * (uint64)BH_KB)
/* Macro to check memory flag and return appropriate memory size */
#define GET_MAX_LINEAR_MEMORY_SIZE(is_memory64) \
    (is_memory64 ? MAX_LINEAR_MEM64_MEMORY_SIZE : MAX_LINEAR_MEMORY_SIZE)

#define TABLE_MAX_SIZE (1024)

#define INIT_EXPR_TYPE_I32_CONST 0x41
#define INIT_EXPR_TYPE_I64_CONST 0x42
#define INIT_EXPR_TYPE_F32_CONST 0x43
#define INIT_EXPR_TYPE_F64_CONST 0x44
#define INIT_EXPR_TYPE_V128_CONST 0xFD
#define INIT_EXPR_TYPE_FUNCREF_CONST 0xD2 /* = WASM_OP_REF_FUNC */
#define INIT_EXPR_TYPE_REFNULL_CONST 0xD0 /* = WASM_OP_REF_NULL */
#define INIT_EXPR_TYPE_GET_GLOBAL 0x23
#define INIT_EXPR_TYPE_ERROR 0xff

#define WASM_MAGIC_NUMBER 0x6d736100
#define WASM_CURRENT_VERSION 1

#define SECTION_TYPE_USER 0
#define SECTION_TYPE_TYPE 1
#define SECTION_TYPE_IMPORT 2
#define SECTION_TYPE_FUNC 3
#define SECTION_TYPE_TABLE 4
#define SECTION_TYPE_MEMORY 5
#define SECTION_TYPE_GLOBAL 6
#define SECTION_TYPE_EXPORT 7
#define SECTION_TYPE_START 8
#define SECTION_TYPE_ELEM 9
#define SECTION_TYPE_CODE 10
#define SECTION_TYPE_DATA 11
#define SECTION_TYPE_DATACOUNT 12

#define SUB_SECTION_TYPE_MODULE 0
#define SUB_SECTION_TYPE_FUNC 1
#define SUB_SECTION_TYPE_LOCAL 2

#define IMPORT_KIND_FUNC 0
#define IMPORT_KIND_TABLE 1
#define IMPORT_KIND_MEMORY 2
#define IMPORT_KIND_GLOBAL 3

#define EXPORT_KIND_FUNC 0
#define EXPORT_KIND_TABLE 1
#define EXPORT_KIND_MEMORY 2
#define EXPORT_KIND_GLOBAL 3

#define LABEL_TYPE_BLOCK 0
#define LABEL_TYPE_LOOP 1
#define LABEL_TYPE_IF 2
#define LABEL_TYPE_FUNCTION 3

#define R_WASM_FUNCTION_INDEX_LEB 0
#define R_WASM_TABLE_INDEX_SLEB 1
#define R_WASM_TABLE_INDEX_I32 2
#define R_WASM_MEMORY_ADDR_LEB 3
#define R_WASM_MEMORY_ADDR_SLEB 4
#define R_WASM_MEMORY_ADDR_I32 5
#define R_WASM_TYPE_INDEX_LEB 6
#define R_WASM_GLOBAL_INDEX_LEB 7
#define R_WASM_FUNCTION_OFFSET_I32 8
#define R_WASM_SECTION_OFFSET_I32 9
#define R_WASM_TAG_INDEX_LEB 10
#define R_WASM_MEMORY_ADDR_REL_SLEB 11
#define R_WASM_TABLE_INDEX_REL_SLEB 12
#define R_WASM_GLOBAL_INDEX_I32 13
#define R_WASM_MEMORY_ADDR_LEB64 14
#define R_WASM_MEMORY_ADDR_SLEB64 15
#define R_WASM_MEMORY_ADDR_I64 16
#define R_WASM_MEMORY_ADDR_REL_SLEB64 17
#define R_WASM_TABLE_INDEX_SLEB64 18
#define R_WASM_TABLE_INDEX_I64 19
#define R_WASM_TABLE_NUMBER_LEB 20
#define R_WASM_MEMORY_ADDR_TLS_SLEB 21
#define R_WASM_FUNCTION_OFFSET_I64 22
#define R_WASM_MEMORY_ADDR_LOCREL_I32 23
#define R_WASM_TABLE_INDEX_REL_SLEB64 24
#define R_WASM_MEMORY_ADDR_TLS_SLEB64 25
#define R_WASM_FUNCTION_INDEX_I32 26

typedef struct WASMModule WASMModule;
typedef struct WASMFunction WASMFunction;
typedef struct WASMGlobal WASMGlobal;

typedef union V128 {
    int8 i8x16[16];
    int16 i16x8[8];
    int32 i32x8[4];
    int64 i64x2[2];
    float32 f32x4[4];
    float64 f64x2[2];
} V128;

typedef union WASMValue {
    int32 i32;
    uint32 u32;
    uint32 global_index;
    uint32 ref_index;
    int64 i64;
    uint64 u64;
    float32 f32;
    float64 f64;
    uintptr_t addr;
    V128 v128;
} WASMValue;

typedef struct InitializerExpression {
    /* type of INIT_EXPR_TYPE_XXX */
    /* it actually is instr, in some places, requires constant only */
    uint8 init_expr_type;
    WASMValue u;
} InitializerExpression;

typedef struct WASMType {
    uint16 param_count;
    uint16 result_count;
    uint16 param_cell_num;
    uint16 ret_cell_num;
    uint16 ref_count;
    /* types of params and results */
    uint8 types[1];
} WASMType;

typedef struct WASMTable {
    char *name;
    uint8 elem_type;
    uint32 flags;
    uint32 init_size;
    /* specified if (flags & 1), else it is 0x10000 */
    uint32 max_size;
    bool possible_grow;
} WASMTable;

typedef struct WASMMemory {
    uint32 flags;
    uint32 num_bytes_per_page;
    uint32 init_page_count;
    uint32 max_page_count;
} WASMMemory;

typedef struct WASMTableImport {
    char *module_name;
    char *field_name;
    uint8 elem_type;
    uint32 flags;
    uint32 init_size;
    /* specified if (flags & 1), else it is 0x10000 */
    uint32 max_size;
    bool possible_grow;
} WASMTableImport;

typedef struct WASMMemoryImport {
    char *module_name;
    char *field_name;
    uint32 flags;
    uint32 num_bytes_per_page;
    uint32 init_page_count;
    uint32 max_page_count;
} WASMMemoryImport;

typedef struct WASMFunctionImport {
    char *module_name;
    char *field_name;
    /* function type */
    WASMType *func_type;
    uint32 func_idx;
} WASMFunctionImport;

typedef struct WASMGlobalImport {
    char *module_name;
    char *field_name;
    uint8 type;
    bool is_mutable;
    /* global data after linked */
    WASMValue global_data_linked;
    bool is_linked;
} WASMGlobalImport;

typedef struct WASMImport {
    uint8 kind;
    union {
        WASMFunctionImport function;
        WASMTableImport table;
        WASMMemoryImport memory;
        WASMGlobalImport global;
        struct {
            char *module_name;
            char *field_name;
        } names;
    } u;
} WASMImport;

struct WASMFunction {
    char *name;
    /* the type of function */
    WASMType *func_type;

    uint32 func_idx;

    uint32 local_count;
    uint8 *local_types;

    /* cell num of parameters */
    uint16 param_cell_num;
    /* cell num of return type */
    uint16 ret_cell_num;
    /* cell num of local variables */
    uint16 local_cell_num;
    /* offset of each local, including function parameters
       and local variables */
    uint16 *local_offsets;

    uint32 max_stack_cell_num;
    uint32 max_block_num;
    uint32 code_size;
    uint8 *code;

    /* Whether function has opcode memory.grow */
    bool has_op_memory_grow;
    /* Whether function has opcode call or call_indirect */
    bool has_op_func_call;
    /* Whether function has memory operation opcodes */
    bool has_memory_operations;
    /* Whether function has opcode call_indirect */
    bool has_op_call_indirect;
    /* Whether function has opcode set_global_aux_stack */
    bool has_op_set_global_aux_stack;
};

struct WASMGlobal {
    char *name;
    uint8 type;
    bool is_mutable;
    InitializerExpression init_expr;
};

typedef struct WASMExport {
    char *name;
    uint8 kind;
    uint32 index;
} WASMExport;

typedef struct WASMTableSeg {
    /* 0 to 7 */
    uint32 mode;
    /* funcref */
    uint32 elem_type;
    /* optional, only for active */
    uint32 table_index;
    InitializerExpression base_offset;
    uint32 function_count;
    uint32 *func_indexes;
} WASMTableSeg;

typedef struct WASMDataSeg {
    char *name;
    uint32 memory_index;
    InitializerExpression base_offset;
    uint32 data_length;
    bool is_passive;
    uint32 alignment;
    uint32 flags;
    uint8 *data;
} WASMDataSeg;

typedef struct BlockAddr {
    const uint8 *start_addr;
    uint8 *else_addr;
    uint8 *end_addr;
} BlockAddr;

typedef struct StringNode {
    struct StringNode *next;
    char *str;
} StringNode, *StringList;

typedef struct BrTableCache {
    struct BrTableCache *next;
    /* Address of br_table opcode */
    uint8 *br_table_op_addr;
    uint32 br_count;
    uint32 br_depths[1];
} BrTableCache;

typedef struct WASMCustomSection {
    struct WASMCustomSection *next;
    /* Start address of the section name */
    char *name_addr;
    /* Length of the section name decoded from leb */
    uint32 name_len;
    /* Start address of the content (name len and name skipped) */
    uint8 *content_addr;
    uint32 content_len;
} WASMCustomSection;

typedef struct WASMRelocation {
    uint32 type;
    uint32 offset;
    uint32 index;
    int64 addend;
} WASMRelocation;

/*
 * Linking info, refer to:
 * https://github.com/WebAssembly/tool-conventions/blob/main/Linking.md
 */

/* Symbol subsection types */
#define WASM_SEGMENT_INFO 5
#define WASM_INIT_FUNCS 6
#define WASM_COMDAT_INFO 7
#define WASM_SYMBOL_TABLE 8

/* Symbol types */
#define WASM_SYMBOL_TYPE_FUNCTION 0
#define WASM_SYMBOL_TYPE_DATA 1
#define WASM_SYMBOL_TYPE_GLOBAL 2
#define WASM_SYMBOL_TYPE_SECTION 3
#define WASM_SYMBOL_TYPE_TAG 4
#define WASM_SYMBOL_TYPE_TABLE 5

/* Symbol flags */
#define WASM_SYM_BINDING_WEAK 0x1
#define WASM_SYM_BINDING_LOCAL 0x2
#define WASM_SYM_VISIBILITY_HIDDEN 0x4
#define WASM_SYM_UNDEFINED 0x10
#define WASM_SYM_EXPORTED 0x20
#define WASM_SYM_EXPLICIT_NAME 0x40
#define WASM_SYM_NO_STRIP 0x80
#define WASM_SYM_TLS 0x100
#define WASM_SYM_ABSOLUTE 0x200

#define WASM_SYMBOL_BINDING_MASK 3

/* The data symbol */
typedef struct WASMSymbolData {
    /* the index of the data segment */
    uint32 seg_index;
    /* the offset within the segment */
    uint64 data_offset;
    /* the size of the symbol */
    uint64 data_size;
} WASMSymbolData;

/* The symbol of a section */
typedef struct WASMSymbolSection {
    /* the index of the target section */
    uint32 index;
} WASMSymbolSection;

typedef struct WASMSymbol {
    uint32 type;
    /* false if it is an import */
    bool is_defined;
    char *name;
    char *import_name;
    char *module_name;
    union {
        WASMImport *import;
        WASMFunction *function;
        WASMGlobal *global;
        WASMTable *table;
        WASMSymbolData sym_data;
        WASMSymbolSection sym_section;
    } u;
} WASMSymbol;

struct WASMModule {
    uint32 type_count;
    uint32 import_count;
    uint32 function_count;
    uint32 table_count;
    uint32 memory_count;
    uint32 global_count;
    uint32 export_count;
    uint32 table_seg_count;
    /* data seg count read from data segment section */
    uint32 data_seg_count;
    /* data count read from datacount section */
    uint32 data_seg_count1;

    uint32 import_function_count;
    uint32 import_table_count;
    uint32 import_memory_count;
    uint32 import_global_count;

    WASMImport *import_functions;
    WASMImport *import_tables;
    WASMImport *import_memories;
    WASMImport *import_globals;

    WASMType **types;
    WASMImport *imports;
    WASMFunction **functions;
    WASMTable *tables;
    WASMMemory *memories;
    WASMGlobal *globals;
    WASMExport *exports;
    WASMTableSeg *table_segments;
    WASMDataSeg **data_segments;
    uint32 start_function;

    /* total global variable size */
    uint32 global_data_size;

    /* the index of auxiliary __data_end global,
       -1 means unexported */
    uint32 aux_data_end_global_index;
    /* auxiliary __data_end exported by wasm app */
    uint64 aux_data_end;

    /* the index of auxiliary __heap_base global,
       -1 means unexported */
    uint32 aux_heap_base_global_index;
    /* auxiliary __heap_base exported by wasm app */
    uint64 aux_heap_base;

    /* the index of auxiliary stack top global,
       -1 means unexported */
    uint32 aux_stack_top_global_index;
    /* auxiliary stack bottom resolved */
    uint64 aux_stack_bottom;
    /* auxiliary stack size resolved */
    uint32 aux_stack_size;

    /* the index of malloc/free function,
       -1 means unexported */
    uint32 malloc_function;
    uint32 free_function;

    /* the index of __retain function,
       -1 means unexported */
    uint32 retain_function;

    /* Whether there is possible memory grow, e.g. memory.grow opcode */
    bool possible_memory_grow;

    StringList const_str_list;
    bh_list br_table_cache_list_head;
    bh_list *br_table_cache_list;

    const uint8 *name_section_buf;
    const uint8 *name_section_buf_end;

    const uint8 *code_section_body;
    const uint8 *data_section_body;

    /* Relocations in "reloc.CODE" section */
    uint32 code_reloc_count;
    WASMRelocation *code_relocs;

    /* Relocations in "reloc.DATA" section */
    uint32 data_reloc_count;
    WASMRelocation *data_relocs;

    uint32 symbol_count;
    WASMSymbol *symbols;
};

typedef struct BlockType {
    /* Block type may be expressed in one of two forms:
     * either by the type of the single return value or
     * by a type index of module.
     */
    union {
        uint8 value_type;
        WASMType *type;
    } u;
    bool is_value_type;
} BlockType;

typedef struct WASMBranchBlock {
    uint8 *begin_addr;
    uint8 *target_addr;
    uint32 *frame_sp;
    uint32 cell_num;
} WASMBranchBlock;

/**
 * Align an unsigned value on a alignment boundary.
 *
 * @param v the value to be aligned
 * @param b the alignment boundary (2, 4, 8, ...)
 *
 * @return the aligned value
 */
inline static unsigned
align_uint(unsigned v, unsigned b)
{
    unsigned m = b - 1;
    return (v + m) & ~m;
}

inline static uint64
align_uint64(uint64 v, uint64 b)
{
    uint64 m = b - 1;
    return (v + m) & ~m;
}

/**
 * Return the hash value of c string.
 */
inline static uint32
wasm_string_hash(const char *str)
{
    unsigned h = (unsigned)strlen(str);
    const uint8 *p = (uint8 *)str;
    const uint8 *end = p + h;

    while (p != end)
        h = ((h << 5) - h) + *p++;
    return h;
}

/**
 * Whether two c strings are equal.
 */
inline static bool
wasm_string_equal(const char *s1, const char *s2)
{
    return strcmp(s1, s2) == 0 ? true : false;
}

/**
 * Return the byte size of value type.
 *
 */
inline static uint32
wasm_value_type_size(uint8 value_type)
{
    switch (value_type) {
        case VALUE_TYPE_I32:
        case VALUE_TYPE_F32:
            return sizeof(int32);
        case VALUE_TYPE_I64:
        case VALUE_TYPE_F64:
            return sizeof(int64);
        case VALUE_TYPE_V128:
            return sizeof(int64) * 2;
        case VALUE_TYPE_VOID:
            return 0;
        default:
            bh_assert(0);
    }
    return 0;
}

inline static uint16
wasm_value_type_cell_num(uint8 value_type)
{
    return wasm_value_type_size(value_type) / 4;
}

inline static uint32
wasm_get_cell_num(const uint8 *types, uint32 type_count)
{
    uint32 cell_num = 0;
    uint32 i;
    for (i = 0; i < type_count; i++)
        cell_num += wasm_value_type_cell_num(types[i]);
    return cell_num;
}

inline static bool
wasm_type_equal(const WASMType *type1, const WASMType *type2)
{
    if (type1 == type2) {
        return true;
    }
    return (type1->param_count == type2->param_count
            && type1->result_count == type2->result_count
            && memcmp(type1->types, type2->types,
                      (uint32)(type1->param_count + type1->result_count))
                   == 0)
               ? true
               : false;
}

inline static uint32
wasm_get_smallest_type_idx(WASMType **types, uint32 type_count,
                           uint32 cur_type_idx)
{
    uint32 i;

    for (i = 0; i < cur_type_idx; i++) {
        if (wasm_type_equal(types[cur_type_idx], types[i]))
            return i;
    }
    (void)type_count;
    return cur_type_idx;
}

static inline uint32
block_type_get_param_types(BlockType *block_type, uint8 **p_param_types)
{
    uint32 param_count = 0;
    if (!block_type->is_value_type) {
        WASMType *wasm_type = block_type->u.type;
        *p_param_types = wasm_type->types;
        param_count = wasm_type->param_count;
    }
    else {
        *p_param_types = NULL;
        param_count = 0;
    }

    return param_count;
}

static inline uint32
block_type_get_result_types(BlockType *block_type, uint8 **p_result_types)
{
    uint32 result_count = 0;
    if (block_type->is_value_type) {
        if (block_type->u.value_type != VALUE_TYPE_VOID) {
            *p_result_types = &block_type->u.value_type;
            result_count = 1;
        }
    }
    else {
        WASMType *wasm_type = block_type->u.type;
        *p_result_types = wasm_type->types + wasm_type->param_count;
        result_count = wasm_type->result_count;
    }
    return result_count;
}

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _WASM_H_ */
