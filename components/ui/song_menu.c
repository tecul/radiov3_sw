#include "song_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl.h"
#include "esp_log.h"

#include "db.h"
#include "paging_menu.h"
#include "music_player.h"
#include "playlist.h"

static const char* TAG = "rv3.song_menu";

struct song_menu {
	void *db_hdl;
	char *artist;
	char *album;
};

static void song_destroy(void *ctx)
{
	free(ctx);
}

static char *song_get_item_label(void *ctx, int index)
{
	struct song_menu *menu = ctx;

	return db_song_get(menu->db_hdl, menu->artist, menu->album , index);
}

static void song_put_item_label(void *ctx, char *item_label)
{
	struct song_menu *menu = ctx;

	db_put_item(menu->db_hdl, item_label);
}

static void song_select_item(void *ctx, char *selected_label, int index)
{
	struct song_menu *menu = ctx;
	void *playlist_hdl;

	ESP_LOGI(TAG, "selected %s", selected_label);
	playlist_hdl = playlist_create(menu->db_hdl);
	assert(playlist_hdl);
	playlist_add_song(playlist_hdl, menu->artist, menu->album, selected_label);
	music_player_create(playlist_hdl);
}

static struct paging_cbs cbs = {
	.destroy = song_destroy,
	.get_item_label = song_get_item_label,
	.put_item_label = song_put_item_label,
	.select_item = song_select_item,
};

ui_hdl song_menu_create(void *db_hdl, char *artist, char *album)
{
	struct song_menu *menu;
	int song_nb;
	ui_hdl paging;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;
	memset(menu, 0, sizeof(*menu));

	menu->db_hdl = db_hdl;
	menu->artist = artist;
	menu->album = album;
	ESP_LOGI(TAG, "db_hdl = %p/%p", db_hdl, menu->db_hdl);
	song_nb = db_song_get_nb(menu->db_hdl, artist, album);
	paging = paging_menu_create(song_nb, &cbs, menu);
	if (!paging) {
		free(menu);
		return NULL;
	}

	return paging;
}
