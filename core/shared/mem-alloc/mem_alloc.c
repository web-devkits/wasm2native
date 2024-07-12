/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "mem_alloc.h"
#include "ems/ems_gc.h"

mem_allocator_t
mem_allocator_create(void *mem, uint32 size)
{
    return gc_init_with_pool((char *)mem, size);
}

int
mem_allocator_destroy(mem_allocator_t allocator)
{
    return gc_destroy_with_pool((gc_handle_t)allocator);
}

void *
mem_allocator_malloc(mem_allocator_t allocator, uint32 size)
{
    return gc_alloc_vo((gc_handle_t)allocator, size);
}

void *
mem_allocator_realloc(mem_allocator_t allocator, void *ptr, uint32 size)
{
    return gc_realloc_vo((gc_handle_t)allocator, ptr, size);
}

void
mem_allocator_free(mem_allocator_t allocator, void *ptr)
{
    if (ptr)
        gc_free_vo((gc_handle_t)allocator, ptr);
}
