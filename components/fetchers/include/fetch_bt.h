#ifndef __FETCH_BT__
#define __FETCH_BT__ 1

#include "audio.h"

void *fetch_bt_create(void *renderer_hdl, int is2_nb);
void fetch_bt_start(void *hdl, void *hdl_cb, audio_track_info_cb track_info_cb);
void fetch_bt_stop(void *hdl);
void fetch_bt_next(void *hdl);

#endif