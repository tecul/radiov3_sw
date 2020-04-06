#ifndef __RING__
#define __RING__ 1

void *ring_create(int size_in_kb);
void ring_destroy(void *hdl);
int ring_push(void *hdl, int size_in_byte, void *data);
int ring_pop(void *hdl, int *size_in_byte, void *data);
void ring_reset(void *hdl);
int ring_level(void *hdl);

#endif