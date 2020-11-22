#ifndef __BLUETOOTH__
#define __BLUETOOTH__ 1

#include <stdint.h>
#include "audio.h"

typedef void (*bt_audio_cfg_cb)(int sample_rate);
typedef void (*bt_audio_data_cb)(const uint8_t *data, uint32_t len);

int bluetooth_init(char *name);
int bluetooth_enable(bt_audio_cfg_cb audio_cfg_cb, bt_audio_data_cb audio_data_cb, void *hdl_cb, audio_track_info_cb track_info_cb);
int bluetooth_disable(void);
int bluetooth_cmd_next(void);

#endif