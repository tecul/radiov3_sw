#ifndef __PLAYLIST__
#define __PLAYLIST__ 1

#include "id3.h"

struct playlist_item {
	char *filepath;
	struct id3_meta meta;
};

void *playlist_create(void *db_hdl);
void playlist_destroy(void *hdl);
int playlist_add_song(void *hdl, char *artist, char *album, char *title);
int playlist_rewind(void *hdl);
int playlist_next(void *hdl, struct playlist_item *item);
int playlist_prev(void *hdl, struct playlist_item *item);
void playlist_put_item(void *hdl, struct playlist_item *item);

#endif