#include "artist_menu.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl.h"
#include "esp_log.h"

#include "db.h"
#include "paging_menu.h"
#include "album_menu.h"

static const char* TAG = "rv3.artist_menu";

struct artist_menu {
	void *db_hdl;
};

static void artist_destroy(void *ctx)
{
	struct artist_menu *menu = ctx;

	db_close(menu->db_hdl);
	free(menu);
}

static char *artist_get_item_label(void *ctx, int index)
{
	struct artist_menu *menu = ctx;

	return db_artist_get(menu->db_hdl, index);
}

static void artist_put_item_label(void *ctx, char *item_label)
{
	struct artist_menu *menu = ctx;

	db_put_item(menu->db_hdl, item_label);
}

static void artist_select_item(void *ctx, char *selected_label, int index)
{
	struct artist_menu *menu = ctx;

	album_menu_create(menu->db_hdl, selected_label);
}

static struct paging_cbs cbs = {
	.destroy = artist_destroy,
	.get_item_label = artist_get_item_label,
	.put_item_label = artist_put_item_label,
	.select_item = artist_select_item,
};

ui_hdl artist_menu_create()
{
	struct artist_menu *menu;
	int artist_nb;
	ui_hdl paging;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;
	memset(menu, 0, sizeof(*menu));

	menu->db_hdl = db_open("/sdcard/music.db");
	if (!menu->db_hdl) {
		free(menu);
		return NULL;
	}

	artist_nb = db_artist_get_nb(menu->db_hdl);
	paging = paging_menu_create(artist_nb, &cbs, menu);
	if (!paging) {
		db_close(menu->db_hdl);
		free(menu);
		return NULL;
	}

	return paging;
}
