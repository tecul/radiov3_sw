#include "playlist.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "db.h"
#include "esp_log.h"

static const char* TAG = "rv3.playlist";

struct playlist_song {
	struct playlist_song *next;
	char *artist;
	char *album;
	char *title;
	int index;
};

struct playlist {
	void *db_hdl;
	int is_db_close_on_exit;
	struct playlist_song *unplayed;
	int unplayed_nb;
	struct playlist_song *played;
	int played_nb;
	int song_nb;
};

/* taken from https://raw.githubusercontent.com/ivanrad/getline/master/getline.c */
/* getline.c
 *
 * getdelim(), getline() - read a delimited record from stream, ersatz implementation
 *
 * For more details, see: http://pubs.opengroup.org/onlinepubs/9699919799/functions/getline.html
 *
 */

//#include "getline.h"
#define SSIZE_MAX	4096
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream) {
    char *cur_pos, *new_lineptr;
    size_t new_lineptr_len;
    int c;

    if (lineptr == NULL || n == NULL || stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (*lineptr == NULL) {
        *n = 128; /* init len */
        if ((*lineptr = (char *)malloc(*n)) == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }

    cur_pos = *lineptr;
    for (;;) {
        c = getc(stream);

        if (ferror(stream) || (c == EOF && cur_pos == *lineptr))
            return -1;

        if (c == EOF)
            break;

        if ((*lineptr + *n - cur_pos) < 2) {
            if (SSIZE_MAX / 2 < *n) {
#ifdef EOVERFLOW
                errno = EOVERFLOW;
#else
                errno = ERANGE; /* no EOVERFLOW defined */
#endif
                return -1;
            }
            new_lineptr_len = *n * 2;

            if ((new_lineptr = (char *)realloc(*lineptr, new_lineptr_len)) == NULL) {
                errno = ENOMEM;
                return -1;
            }
            cur_pos = new_lineptr + (cur_pos - *lineptr);
            *lineptr = new_lineptr;
            *n = new_lineptr_len;
        }

        *cur_pos++ = (char)c;

        if (c == delim)
            break;
    }

    *cur_pos = '\0';
    return (ssize_t)(cur_pos - *lineptr);
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    return getdelim(lineptr, n, '\n', stream);
}

static struct playlist_song *list_tail_elem(struct playlist_song *tail)
{
	if (!tail)
		return NULL;

	while (tail->next)
		tail = tail->next;

	return tail;
}

static void populate_playlist_from_string(struct playlist *playlist, char *filename)
{
	struct id3_meta meta;
	int ret;

	ESP_LOGI(TAG, "filename = %s", filename);

	ret = db_song_get_meta_from_file(playlist->db_hdl, filename, &meta);
	if (ret) {
		ESP_LOGW(TAG, "Unable to read meta from %s", filename);
		return ;
	}

	playlist_add_song(playlist, meta.artist, meta.album, meta.title);
	db_put_meta(playlist->db_hdl, &meta);
}

static void populate_playlist_from_file(struct playlist *playlist, char *pls)
{
	char *line = NULL;
	size_t len = 0;
	FILE *f;
	int sz;

	f = fopen(pls, "r");
	if (!f) {
		ESP_LOGW(TAG, "Unable to open playlist %s", pls);
		return ;
	}

	while (1) {
		sz = getline(&line, &len, f);
		if (sz < 0)
			break;
		if (line[sz - 1] == '\n')
			line[sz - 1] = '\0';
		populate_playlist_from_string(playlist, line);
	}
	free(line);

	fclose(f);
}

static void list_destroy_elem(struct playlist_song *current)
{
	struct playlist_song *next;

	while (current) {
		ESP_LOGI(TAG , " destroy playlist item %p | %s/%s/%s", current, current->artist, current->album, current->title);
		next = current->next;
		free(current->artist);
		free(current->album);
		free(current->title);
		free(current);
		current = next;
	}
}

static struct playlist_song *list_remove_at_index(struct playlist_song *current,
						  int index,
						  struct playlist_song **remove)
{
	struct playlist_song *head = current;
	struct playlist_song *prev = NULL;
	struct playlist_song *next;

	while (index--) {
		prev = current;
		current = current->next;
	}
	*remove = current;

	next = current->next;
	current->next = NULL;

	if (head == current)
		return next;

	prev->next = next;

	return head;
}

static struct playlist_song *select_next(struct playlist *playlist, int is_random)
{
	struct playlist_song *current;
	int index;

	if (!playlist->unplayed_nb)
		return NULL;

	index = is_random ? rand() % playlist->unplayed_nb : 0;
	ESP_LOGD(TAG , "select index %d among [%d:%d[ / mode is %d",
		 index, 0, playlist->unplayed_nb, is_random);

	playlist->unplayed = list_remove_at_index(playlist->unplayed, index, &current);
	playlist->unplayed_nb--;

	current->next = playlist->played;
	playlist->played = current;
	playlist->played_nb++;

	return current;
}

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

void *playlist_create_from_file(void *db_hdl, char *pls)
{
	struct playlist *playlist = playlist_create(db_hdl);

	if (!playlist)
		return NULL;

	playlist->is_db_close_on_exit = 1;
	populate_playlist_from_file(playlist, pls);

	return playlist;
}

void playlist_destroy(void *hdl)
{
	struct playlist *playlist = hdl;

	list_destroy_elem(playlist->played);
	list_destroy_elem(playlist->unplayed);

	if (playlist->is_db_close_on_exit)
		db_close(playlist->db_hdl);
	free(playlist);
	ESP_LOGI(TAG , "destroy playlist %p", playlist);
}

int playlist_add_song(void *hdl, char *artist, char *album, char *title)
{
	struct playlist *playlist = hdl;
	struct playlist_song *new;
	struct playlist_song *tail = list_tail_elem(playlist->unplayed);

	new = malloc(sizeof(*new));
	if (!new)
		return -1;

	new->next = NULL;
	new->artist = strdup(artist);
	new->album = strdup(album);
	new->title = strdup(title);
	new->index = playlist->song_nb++;
	playlist->unplayed_nb++;
	if (tail)
		tail->next = new;
	else
		playlist->unplayed = new;

	ESP_LOGI(TAG , " add playlist item %p/%p | %s/%s/%s", playlist, new, artist, album, title);

	return 0;
}

int playlist_rewind(void *hdl)
{
	return -1;
}

int playlist_next(void *hdl, struct playlist_item *item, int is_random)
{
	struct playlist *playlist = hdl;
	struct playlist_song *current;
	int ret;


	current = select_next(playlist, is_random);
	if (!current)
		return -1;

	item->filepath = db_song_get_filepath(playlist->db_hdl,
					      current->artist,
					      current->album,
					      current->title);
	if (!item->filepath) {
		ESP_LOGW(TAG, "unable to fetch filepath for playlist %p | %s/%s/%s", playlist, current->artist, current->album, current->title);
		return -1;
	}
	ret = db_song_get_meta(playlist->db_hdl,
			       current->artist,
			       current->album,
			       current->title,
			       &item->meta);
	if (ret) {
		ESP_LOGW(TAG, "unable for fetch metadata for playlist %p | %s/%s/%s", playlist, current->artist, current->album, current->title);
		db_put_item(playlist->db_hdl, item->filepath);
		return -1;
	}

	ESP_LOGI(TAG , " queue playlist item %p/%p | %s/%s/%s | %s", playlist, current, item->meta.artist, item->meta.album, item->meta.title, item->filepath);

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

int playlist_song_nb(void *hdl)
{
	struct playlist *playlist = hdl;

	return playlist->song_nb;
}

int playlist_song_idx(void *hdl)
{
	struct playlist *playlist = hdl;

	return playlist->played_nb;
}
