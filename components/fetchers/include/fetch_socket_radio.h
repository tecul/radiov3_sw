#ifndef __FETCH_SOCKET_RADIO__
#define __FETCH_SOCKET_RADIO__ 1

#include "audio.h"

void *fetch_socket_radio_create(void *buffer_hdl);
void fetch_socket_radio_destroy(void *hdl);
void fetch_socket_radio_start(void *hdl, char *url, char *port_nb, char *path, int meta,
			      void *cb_hdl, audio_track_info_cb track_info_cb);
void fetch_socket_radio_stop(void *hdl);

#endif