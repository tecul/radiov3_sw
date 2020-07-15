#include "playlist_menu.h"

#include "lvgl/lvgl.h"
#include "esp_log.h"

#include "paging_menu.h"

static const char* TAG = "rv3.playlist_menu";

struct playlist_menu {
	int dummy;
};

static int dir_count_entries(char *dir)
{
	return 2;
}

static void playlist_destroy(void *ctx)
{
	struct playlist_menu *menu = ctx;

	free(menu);
}

static char *playlist_get_item_label(void *ctx, int index)
{
	return "item";
}

static void playlist_put_item_label(void *ctx, char *item_label)
{
	;
}

static void playlist_select_item(void *ctx, char *selected_label, int index)
{
	;
}

static struct paging_cbs cbs = {
	.destroy = playlist_destroy,
	.get_item_label = playlist_get_item_label,
	.put_item_label = playlist_put_item_label,
	.select_item = playlist_select_item,
};

ui_hdl playlist_menu_create(char *dir)
{
	struct playlist_menu *menu;
	int item_nb;
	ui_hdl paging;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;
	memset(menu, 0, sizeof(*menu));

	item_nb = dir_count_entries(dir);
	paging = paging_menu_create(item_nb, &cbs, menu);
	if (!paging) {
		free(menu);
		return NULL;
	}

	return paging;
}
