#ifdef __ETS__
#include "stdhdr.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdintw.h"
#define ICACHE_FLASH_ATTR
#define TRACE_LOG printf
#endif
#include "smemblk.h"

// #define SMEMBLK_DEBUG

static int16_t ICACHE_FLASH_ATTR abs16(int16_t n) { return n < 0 ? -n : n; }

static int16_t * ICACHE_FLASH_ATTR block_ptr(smemblk_t *smem, int offset)
{
    return (int16_t *)((int8_t *)&smem[1]+offset);
}

static int16_t ICACHE_FLASH_ATTR block_len(smemblk_t *smem, int offset)
{
    return abs16(*block_ptr(smem, offset));
}

void ICACHE_FLASH_ATTR smemblk_gc(smemblk_t *smem)
{
    int16_t *p, *base_ptr;

    p = base_ptr = block_ptr(smem, smem->start);
    while ((p - base_ptr) < (smem->total_size >> 1) && (((int8_t *) p + abs16(*p)) < (int8_t *) base_ptr + smem->total_size)) {
        if (*p < 0 && p[-*p >> 1] < 0)
            *p += p[-*p >> 1];
        else if (*p < 0)
            p += -*p >> 1;
        else
            p += *p >> 1;
    }
}

// #ifdef SMEMBLK_DEBUG
void ICACHE_FLASH_ATTR smemblk_debug_dump(smemblk_t *smem)
{
    int offset, prev_offset = smem->start, prev_len = 0, total_used = 0, total_free = 0;

    for (offset = smem->start; offset < smem->total_size; offset += block_len(smem, offset)) {
        if (offset != prev_offset + prev_len)
            TRACE_LOG("INTEGRITY ERROR in smemblk offset %d !!\n", offset);
        if (*block_ptr(smem, offset) < 0) {
            TRACE_LOG("%5d: FREE %5d bytes\n", offset, block_len(smem, offset));
            total_free += block_len(smem, offset);
        }
        else {
            TRACE_LOG("%5d: USED %5d bytes\n", offset, block_len(smem, offset));
            total_used += block_len(smem, offset);
        }

        prev_offset = offset;
        prev_len    = block_len(smem, offset);
    }
    TRACE_LOG("first_free = %d, total_used = %d, total_free = %d\n\n", (int) (smem->first_free), total_used, total_free);
}
// #endif

smemblk_t * ICACHE_FLASH_ATTR smemblk_init(char *buffer, int buffer_len)
{
    smemblk_t *smem;
    int16_t   *p;
    int       remain;

    while ((long)buffer % 4) {
        ++buffer;
        buffer_len -= 1;
    }
    smem = (smemblk_t *) buffer;
    smem->total_size = buffer_len;
    smem->total_size -= sizeof(smemblk_t);

    smem->first_free = 0;
    smem->start = 0;
    p = (int16_t *) &smem[1];
    while (((long)p+2) % 4) {
        smem->first_free += 2;
        smem->start += 2;
        smem->total_size -= 2;
        ++p;
    }

    remain = smem->total_size;

    while (remain > 0) {
        // if (((long) &p[1]) % 4 != 0)
        //     TRACE_LOG("ALIGNMENT ERROR\n");
        *p = remain > 0x7ffc ? -0x7ffc : -remain;

        remain += *p;
        p += -*p >> 1;
    }

#ifdef SMEMBLK_DEBUG
    debug_dump(smem);
#endif
    return smem;
}

static void ICACHE_FLASH_ATTR mark_as_allocated(smemblk_t *smem, int16_t *p)
{
    int8_t  *next_p, *base_ptr = (int8_t *) smem + sizeof(*smem);

    *p = -*p;
    if (smem->first_free == (int8_t *) p - base_ptr) {
        for (next_p=(int8_t *)p+*p; next_p-base_ptr < smem->total_size; next_p=(int8_t *)p+*p) {
            p = (int16_t *) next_p;
            if (*p < 0) {
                smem->first_free = (int8_t *) p - base_ptr;
                return;
            }
        }
        smem->first_free = -1;
    }
}

static void * ICACHE_FLASH_ATTR smemblk_alloc_intern(smemblk_t *smem, int16_t size)
{
    int16_t offset, *p;

    while (size % 4 != 2) // make sure that p+2 is dividable by 4
        ++size;

    for (offset = smem->first_free; offset >= 0 && offset < smem->total_size; offset += block_len(smem, offset)) {
        p = block_ptr(smem, offset);
        if (*p < 0 && -*p >= size+2) {
            // split the block
            if (-*p != size+2) {
                p[(size+2) >> 1] = -(-*p - (size + 2));
                *p = -(size + 2);
            }
            mark_as_allocated(smem, p);
            // if (((long) &p[1]) % 4 != 0)
            //     TRACE_LOG("ALIGNMENT ERROR\n");
            return &p[1];
        }
    }

    return NULL;
}

void * ICACHE_FLASH_ATTR smemblk_alloc(smemblk_t *smem, int16_t size)
{
    void *p;
    if (smem == NULL)
        return NULL;

    p = smemblk_alloc_intern(smem, size);
    if (p == NULL) {
        smemblk_gc(smem);
        p = smemblk_alloc_intern(smem, size); // try again after garbage collecting
    }
#ifdef SMEMBLK_DEBUG
    debug_dump(smem);
#endif
    return p;
}

void * ICACHE_FLASH_ATTR smemblk_zalloc(smemblk_t *smem, int16_t size)
{
    void *p = smemblk_alloc(smem, size);
    if (p != NULL)
        memset(p, 0, size);
    return p;
}

void * ICACHE_FLASH_ATTR smemblk_realloc(smemblk_t *smem, void *buf, int16_t size)
{
    int16_t *p, *temp, buf_size;
    int8_t  *next_p, *base_ptr = (int8_t *) block_ptr(smem, sizeof(*smem));

    if (buf == NULL)
        return smemblk_alloc(smem, size);

    p = (int16_t *) buf - 1;
    buf_size = *p - 2;

    // check if next block is needed and allocate if free
    for (next_p=(int8_t *)p+*p; next_p-base_ptr < smem->total_size && *p<size+2; next_p=(int8_t *)p+*p) {
        temp = (int16_t *) next_p;
        if (*temp >= 0)
            break;

        *p += -*temp;
        mark_as_allocated(smem, temp);
    }

    if (*p >= size+2) {
        int16_t remain;

        size   += 2;
        while (size % 4 != 0) // make sure that p+2 is dividable by 4
            ++size;

        remain = *p - size;
        if (remain >= 6) {
            // shrink the buffer
            *p = size;

            p = &p[*p >> 1];
            *p = -remain;
            // if (((long) &p[1]) % 4 != 0)
            //     TRACE_LOG("ALIGNMENT ERROR\n");

            if (smem->first_free == -1 || smem->first_free > (p - (int16_t *) &smem[1]) * 2)
                smem->first_free = (p - (int16_t *) &smem[1]) * 2;
        }

#ifdef SMEMBLK_DEBUG
        debug_dump(smem);
#endif
        return buf;
    }

    temp = smemblk_alloc(smem, size);
    if (temp == NULL)
        return NULL;

    memcpy(temp, buf, buf_size);
    smemblk_free(smem, buf);
    return temp;
}

void ICACHE_FLASH_ATTR smemblk_free(smemblk_t *smem, void *buf)
{
    int16_t *p;

    if (buf == NULL)
        return;

    p = (int16_t *) buf - 1;
    if (*p > 0) {
        *p = -(*p); // mark as free
        if (smem->first_free == -1 || smem->first_free > (p - (int16_t *) &smem[1]) * 2)
            smem->first_free = (p - (int16_t *) &smem[1]) * 2;
    }
#ifdef SMEMBLK_DEBUG
    debug_dump(smem);
#endif
}

void ICACHE_FLASH_ATTR smemblk_term(smemblk_t *smem)
{
#ifndef __ETS__
    int offset, prev_offset = smem->start, prev_len = 0;

    for (offset = smem->start; offset < smem->total_size; offset += block_len(smem, offset)) {
        if (offset != prev_offset + prev_len)
            TRACE_LOG("INTEGRITY ERROR in smemblk offset %d !!\n", offset);
        if (*block_ptr(smem, offset) >= 0)
            TRACE_LOG("%5d: memory leak %5d bytes at %p\n", offset, block_len(smem, offset), block_ptr(smem, offset));

        prev_offset = offset;
        prev_len    = block_len(smem, offset);
    }
#endif
}
