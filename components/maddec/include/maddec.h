#ifndef __MADDEC__
#define __MADDEC__ 1

void *maddec_create(void *buffer_hdl, void *renderer_hdl);
void maddec_destroy(void *hdl);
void maddec_start(void *hdl);
void maddec_stop(void *hdl);

#endif