#ifndef __SYSTEM__
#define __SYSTEM__ 1

#include <stdint.h>

char *system_get_name(void);
int system_set_name(char *name);
int system_get_sdcard_info(uint64_t *total_size, uint64_t *free_size);

#endif