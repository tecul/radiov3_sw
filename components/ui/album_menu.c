#include "album_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl.h"
#include "esp_log.h"

#include "db.h"
#include "paging_menu.h"
#include "song_menu.h"
#include "music_player.h"
#include "playlist.h"

static const char* TAG = "rv3.album_menu";

struct album_menu {
	void *db_hdl;
	char *artist;
};

static void album_destroy(void *ctx)
{
	free(ctx);
}

static char *album_get_item_label(void *ctx, int index)
{
	struct album_menu *menu = ctx;

	return db_album_get(menu->db_hdl, menu->artist, index);
}

static void album_put_item_label(void *ctx, char *item_label)
{
	struct album_menu *menu = ctx;

	db_put_item(menu->db_hdl, item_label);
}

static void album_select_item(void *ctx, char *selected_label, int index)
{
	struct album_menu *menu = ctx;

	song_menu_create(menu->db_hdl, menu->artist, selected_label);
}

static void long_select_item(void *ctx, char *selected_label, int index)
{
	struct album_menu *menu = ctx;
	void *playlist_hdl;
	char *songname;
	int song_nb;
	int i;

	playlist_hdl = playlist_create(menu->db_hdl);
	song_nb = db_song_get_nb(menu->db_hdl, menu->artist, selected_label);
	for (i = 0 ; i < song_nb; i++) {
		songname = db_song_get(menu->db_hdl, menu->artist, selected_label, i);
		if (!songname)
			continue;
		playlist_add_song(playlist_hdl, menu->artist, selected_label, songname);
		db_put_item(menu->db_hdl, songname);
	}
	music_player_create(playlist_hdl);
}

static struct paging_cbs cbs = {
	.destroy = album_destroy,
	.get_item_label = album_get_item_label,
	.put_item_label = album_put_item_label,
	.select_item = album_select_item,
	.long_select_item = long_select_item,
};

ui_hdl album_menu_create(void *db_hdl, char *artist)
{
	struct album_menu *menu;
	int album_nb;
	ui_hdl paging;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;
	memset(menu, 0, sizeof(*menu));

	menu->db_hdl = db_hdl;
	menu->artist = artist;
	album_nb = db_album_get_nb(menu->db_hdl, artist);
	paging = paging_menu_create(album_nb, &cbs, menu);
	if (!paging) {
		free(menu);
		return NULL;
	}

	return paging;
}
