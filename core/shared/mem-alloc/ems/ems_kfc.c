/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "ems_gc_internal.h"

static gc_handle_t
gc_init_internal(gc_heap_t *heap, char *base_addr, gc_size_t heap_max_size)
{
    hmu_tree_node_t *root = NULL, *q = NULL;
    //int ret;

    memset(heap, 0, sizeof *heap);

#if 0
    ret = os_mutex_init(&heap->lock);
    if (ret != BHT_OK) {
        os_printf("[GC_ERROR]failed to init lock\n");
        return NULL;
    }
#endif

    /* init all data structures*/
    heap->current_size = heap_max_size;
    heap->base_addr = (gc_uint8 *)base_addr;
    heap->heap_id = (gc_handle_t)heap;

    heap->total_free_size = heap->current_size;
    heap->highmark_size = 0;

    root = heap->kfc_tree_root = (hmu_tree_node_t *)heap->kfc_tree_root_buf;
    memset(root, 0, sizeof *root);
    root->size = sizeof *root;
    hmu_set_ut(&root->hmu_header, HMU_FC);
    hmu_set_size(&root->hmu_header, sizeof *root);

    q = (hmu_tree_node_t *)heap->base_addr;
    memset(q, 0, sizeof *q);
    hmu_set_ut(&q->hmu_header, HMU_FC);
    hmu_set_size(&q->hmu_header, heap->current_size);

    ASSERT_TREE_NODE_ALIGNED_ACCESS(q);
    ASSERT_TREE_NODE_ALIGNED_ACCESS(root);

    hmu_mark_pinuse(&q->hmu_header);
    root->right = q;
    q->parent = root;
    q->size = heap->current_size;

    bh_assert(root->size <= HMU_FC_NORMAL_MAX_SIZE);

    return heap;
}

gc_handle_t
gc_init_with_pool(char *buf, gc_size_t buf_size)
{
    char *buf_end = buf + buf_size;
    char *buf_aligned = (char *)(((uintptr_t)buf + 7) & (uintptr_t)~7);
    char *base_addr = buf_aligned + sizeof(gc_heap_t);
    gc_heap_t *heap = (gc_heap_t *)buf_aligned;
    gc_size_t heap_max_size;

    if (buf_size < APP_HEAP_SIZE_MIN) {
        os_printf("[GC_ERROR]heap init buf size (%" PRIu32 ") < %" PRIu32 "\n",
                  buf_size, (uint32)APP_HEAP_SIZE_MIN);
        return NULL;
    }

    base_addr =
        (char *)(((uintptr_t)base_addr + 7) & (uintptr_t)~7) + GC_HEAD_PADDING;
    heap_max_size = (uint32)(buf_end - base_addr) & (uint32)~7;

#if W2N_ENABLE_MEMORY_TRACING != 0
    os_printf("Heap created, total size: %u\n", buf_size);
    os_printf("   heap struct size: %u\n", sizeof(gc_heap_t));
    os_printf("   actual heap size: %u\n", heap_max_size);
    os_printf("   padding bytes: %u\n",
              buf_size - sizeof(gc_heap_t) - heap_max_size);
#endif
    return gc_init_internal(heap, base_addr, heap_max_size);
}

int
gc_destroy_with_pool(gc_handle_t handle)
{
    gc_heap_t *heap = (gc_heap_t *)handle;
    int ret = GC_SUCCESS;

#if BH_ENABLE_GC_VERIFY != 0
    hmu_t *cur = (hmu_t *)heap->base_addr;
    hmu_t *end = (hmu_t *)((char *)heap->base_addr + heap->current_size);

    if ((hmu_t *)((char *)cur + hmu_get_size(cur)) != end) {
        os_printf("Memory leak detected:\n");
        gci_dump(heap);
        ret = GC_ERROR;
    }
#endif

    // os_mutex_destroy(&heap->lock);
    memset(heap, 0, sizeof(gc_heap_t));
    return ret;
}

#if BH_ENABLE_GC_VERIFY != 0
void
gci_verify_heap(gc_heap_t *heap)
{
    hmu_t *cur = NULL, *end = NULL;

    bh_assert(heap && gci_is_heap_valid(heap));
    cur = (hmu_t *)heap->base_addr;
    end = (hmu_t *)(heap->base_addr + heap->current_size);
    while (cur < end) {
        hmu_verify(heap, cur);
        cur = (hmu_t *)((gc_uint8 *)cur + hmu_get_size(cur));
    }
    bh_assert(cur == end);
}
#endif

void *
gc_heap_stats(void *heap_arg, uint32 *stats, int size)
{
    int i;
    gc_heap_t *heap = (gc_heap_t *)heap_arg;

    for (i = 0; i < size; i++) {
        switch (i) {
            case GC_STAT_TOTAL:
                stats[i] = heap->current_size;
                break;
            case GC_STAT_FREE:
                stats[i] = heap->total_free_size;
                break;
            case GC_STAT_HIGHMARK:
                stats[i] = heap->highmark_size;
                break;
            default:
                break;
        }
    }
    return heap;
}
