#include "playlist_menu.h"

#include <string.h>
#include <dirent.h>
#include <string.h>

#include "lvgl.h"
#include "esp_log.h"

#include "paging_menu.h"
#include "db.h"
#include "playlist.h"
#include "music_player.h"

static const char* TAG = "rv3.playlist_menu";

struct playlist_entry {
	int is_dir;
	char *name;
};

struct playlist_menu {
	char *path;
	int entry_nb;
	struct playlist_entry **entries;
};

static char *concat_with_delim(char *str1, char *str2, char delim)
{
	int len = strlen(str1) + 1 + strlen(str2) + 1;
	char *res;
	char *buf;

	res = malloc(len);
	if (res == NULL)
		return NULL;

	buf = res;
	strcpy(buf , str1);
	buf += strlen(str1);
	*buf++ = delim;
	strcpy(buf , str2);
	buf += strlen(str2);
	*buf = '\0';

	return res;
}

static int count_dir_entries(char *dir)
{
	struct dirent *entry;
	DIR *d;
	int count = 0;

	d = opendir(dir);
	if (!d) {
		ESP_LOGW(TAG, "unable to open directory %s", dir);
		return 0;
	}

	entry = readdir(d);
	while (entry) {
		switch (entry->d_type) {
		case DT_REG:
			count++;
			break;
		case DT_DIR:
			if (strcmp(entry->d_name, ".") == 0)
				break;
			if (strcmp(entry->d_name, "..") == 0)
				break;
			count++;
			break;
		default:
			ESP_LOGW(TAG, "unsupported dt_type %d", entry->d_type);
		}
		entry = readdir(d);
	}
	closedir(d);

	return count;
}

static void fetch_entries(struct playlist_menu *menu, char *dir)
{
	struct dirent *entry;
	DIR *d;
	int idx = 0;

	d = opendir(dir);
	if (!d) {
		ESP_LOGE(TAG, "unable to open directory %s", dir);
		return ;
	}

	menu->entries = malloc(menu->entry_nb * sizeof(struct playlist_entry *));
	assert(menu->entries);
	memset(menu->entries, 0, menu->entry_nb * sizeof(struct playlist_entry *));

	entry = readdir(d);
	while (entry) {
		switch (entry->d_type) {
		case DT_REG:
		case DT_DIR:
			if (strcmp(entry->d_name, ".") == 0)
				break;
			if (strcmp(entry->d_name, "..") == 0)
				break;
			assert(idx < menu->entry_nb);
			menu->entries[idx] = malloc(sizeof(struct playlist_entry));
			assert(menu->entries[idx]);
			menu->entries[idx]->is_dir = entry->d_type == DT_DIR;
			menu->entries[idx]->name = strdup(entry->d_name);
			assert(menu->entries[idx]->name);
			idx++;
			break;
		default:
			ESP_LOGW(TAG, "unsupported dt_type %d", entry->d_type);
		}
		entry = readdir(d);
	}
	closedir(d);
}

static void populate_entries(struct playlist_menu *menu, char *dir)
{
	menu->entry_nb = count_dir_entries(dir);

	if (!menu->entry_nb)
		return ;

	menu->path = strdup(dir);
	assert(menu->path);

	fetch_entries(menu, dir);
}

static void cleanup_entries(struct playlist_menu *menu)
{
	int i;

	if (menu->path)
		free(menu->path);

	for (i = 0; i < menu->entry_nb; i++)
		free(menu->entries[i]);
	free(menu->entries);
}

static void playlist_menu_destroy(void *ctx)
{
	struct playlist_menu *menu = ctx;

	cleanup_entries(menu);
	free(menu);
}

static char *playlist_get_item_label(void *ctx, int index)
{
	struct playlist_menu *menu = ctx;

	return concat_with_delim(menu->entries[index]->is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO,
				 menu->entries[index]->name, ' ');
}

static void playlist_put_item_label(void *ctx, char *item_label)
{
	free(item_label);
}

static void playlist_select_dir(struct playlist_menu *menu, int index)
{
	char *dir;

	dir = concat_with_delim(menu->path, menu->entries[index]->name, '/');
	assert(dir);
	playlist_menu_create(dir);
	free(dir);
}

static void playlist_select_pls(struct playlist_menu *menu, int index)
{
	void *playlist_hdl;
	void *db_hdl;
	char *pls;

	/* db will be close on playlist destroy */
	db_hdl = db_open("/sdcard/music.db");
	assert(db_hdl);
	pls = concat_with_delim(menu->path, menu->entries[index]->name, '/');
	assert(pls);
	playlist_hdl = playlist_create_from_file(db_hdl, pls);
	assert(playlist_hdl);
	free(pls);
	music_player_create(playlist_hdl);
}

static void playlist_select_item(void *ctx, char *selected_label, int index)
{
	struct playlist_menu *menu = ctx;

	if (menu->entries[index]->is_dir)
		playlist_select_dir(menu, index);
	else
		playlist_select_pls(menu, index);
}

static struct paging_cbs cbs = {
	.destroy = playlist_menu_destroy,
	.get_item_label = playlist_get_item_label,
	.put_item_label = playlist_put_item_label,
	.select_item = playlist_select_item,
};

ui_hdl playlist_menu_create(char *dir)
{
	struct playlist_menu *menu;
	ui_hdl paging;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;
	memset(menu, 0, sizeof(*menu));

	populate_entries(menu, dir);
	paging = paging_menu_create(menu->entry_nb, &cbs, menu);
	if (!paging) {
		cleanup_entries(menu);
		free(menu);
		return NULL;
	}

	return paging;
}
