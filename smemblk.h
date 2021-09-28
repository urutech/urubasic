#ifndef __SMEMBLK_H
#define __SMEMBLK_H

#ifndef ICACHE_FLASH_ATTR
#define ICACHE_FLASH_ATTR
#endif

typedef struct {
    int16_t first_free;
    int16_t start;
    int     total_size;
} smemblk_t;


smemblk_t * ICACHE_FLASH_ATTR smemblk_init(char *buffer, int buffer_len);
void * ICACHE_FLASH_ATTR smemblk_alloc(smemblk_t *smem, int16_t size);
void * ICACHE_FLASH_ATTR smemblk_zalloc(smemblk_t *smem, int16_t size);
void * ICACHE_FLASH_ATTR smemblk_realloc(smemblk_t *smem, void *buf, int16_t size);
void ICACHE_FLASH_ATTR smemblk_free(smemblk_t *smem, void *buf);
void ICACHE_FLASH_ATTR smemblk_gc(smemblk_t *smem);
void ICACHE_FLASH_ATTR smemblk_term(smemblk_t *smem);

#endif
