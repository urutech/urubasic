#ifndef __URUBASIC_H
#define __URUBASIC_H

#ifndef ICACHE_FLASH_ATTR
#define ICACHE_FLASH_ATTR
#endif

struct urubasic_type {
    uint16_t type; // STRING or NUMBER
    int     value; // the value itself or a string offset
};

int ICACHE_FLASH_ATTR urubasic_init(void *mem, int max_mem, int (*read_from_stdin)(void *), void *arg);

void ICACHE_FLASH_ATTR urubasic_add_function(char *name, int (*func)(int n, struct urubasic_type *arg, void *user), void *user);

void ICACHE_FLASH_ATTR urubasic_execute(int insn);

void ICACHE_FLASH_ATTR urubasic_term(void);


// argument (urubasic_type) handling
int ICACHE_FLASH_ATTR urubasic_is_number(struct urubasic_type *arg);
int ICACHE_FLASH_ATTR urubasic_is_string(struct urubasic_type *arg);
int ICACHE_FLASH_ATTR urubasic_get_number(struct urubasic_type *arg);
int ICACHE_FLASH_ATTR urubasic_set_number(struct urubasic_type *arg, int num);
char * ICACHE_FLASH_ATTR urubasic_get_string(struct urubasic_type *arg);
int ICACHE_FLASH_ATTR urubasic_alloc_string(struct urubasic_type *arg, int len);

#endif
