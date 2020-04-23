#include "playlist.h"

#include <string.h>

#include "db.h"
#include "esp_log.h"

static const char* TAG = "rv3.playlist";

struct playlist_song {
	struct playlist_song *next;
	char *artist;
	char *album;
	char *title;
};

struct playlist {
	void *db_hdl;
	struct playlist_song *root;
	struct playlist_song *current;
};

void *playlist_create(void *db_hdl)
{
	struct playlist *playlist;

	playlist = malloc(sizeof(*playlist));
	if (!playlist)
		return NULL;

	memset(playlist, 0, sizeof(*playlist));
	playlist->db_hdl = db_hdl;
	ESP_LOGI(TAG , "create playlist %p", playlist);

	return playlist;
}

void playlist_destroy(void *hdl)
{
	struct playlist *playlist = hdl;
	struct playlist_song *current = playlist->root;
	struct playlist_song *next;

	while (current) {
		ESP_LOGI(TAG , " destroy playlist item %p/%p | %s/%s/%s", playlist, current, current->artist, current->album, current->title);
		next = current->next;
		free(current->artist);
		free(current->album);
		free(current->title);
		free(current);
		current = next;
	}
	free(playlist);
	ESP_LOGI(TAG , "destroy playlist %p", playlist);
}

int playlist_add_song(void *hdl, char *artist, char *album, char *title)
{
	struct playlist *playlist = hdl;
	struct playlist_song *new;

	new = malloc(sizeof(*new));
	if (!new)
		return -1;

	new->next = playlist->root;
	new->artist = strdup(artist);
	new->album = strdup(album);
	new->title = strdup(title);

	playlist->root = new;

	ESP_LOGI(TAG , " add playlist item %p/%p | %s/%s/%s", playlist, new, artist, album, title);

	return 0;
}

int playlist_rewind(void *hdl)
{
	struct playlist *playlist = hdl;

	playlist->current = NULL;

	return 0;
}

int playlist_next(void *hdl, struct playlist_item *item)
{
	struct playlist *playlist = hdl;
	int ret;

	playlist->current = playlist->current ? playlist->current->next : playlist->root;
	if (!playlist->current)
		return -1;

	item->filepath = db_song_get_filepath(playlist->db_hdl,
					      playlist->current->artist,
					      playlist->current->album,
					      playlist->current->title);
	if (!item->filepath)
		return -1;
	ret = db_song_get_meta(playlist->db_hdl,
			       playlist->current->artist,
			       playlist->current->album,
			       playlist->current->title,
			       &item->meta);
	if (ret) {
		db_put_item(playlist->db_hdl, item->filepath);
		return -1;
	}

	ESP_LOGI(TAG , " queue playlist item %p/%p | %s/%s/%s | %s", playlist, playlist->current, item->meta.artist, item->meta.album, item->meta.title, item->filepath);

	return 0;
}

int playlist_prev(void *hdl, struct playlist_item *item)
{
	return -1;
}

void playlist_put_item(void *hdl, struct playlist_item *item)
{
	struct playlist *playlist = hdl;

	ESP_LOGI(TAG , " put playlist item %p | %s/%s/%s", playlist, item->meta.artist, item->meta.album, item->meta.title);

	db_put_item(playlist->db_hdl, item->filepath);
	db_put_meta(playlist->db_hdl, &item->meta);
}
