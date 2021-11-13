#include "radio_menu.h"
#include "jsmn.h"

#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "lvgl.h"
#include "esp_log.h"

#include "radio_player.h"
#include "wifi.h"
#include "paging_menu.h"
#include "utils.h"

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

enum entry_type {
	ENTRY_RADIO,
	ENTRY_FOLDER
};

struct db_parser {
	jsmntok_t *t;
	int nb;
	int index;
	char *str;
};

struct entry {
	struct entry *next;
	struct entry *sub;
	char *name;
	enum entry_type type;
};

struct radio {
	struct entry entry;
	char *url;
	char *port;
	char *path;
	char *rate;
	char *meta;
};

struct radio_menu {
	int is_root;
	struct entry *entries;
};


static const char* TAG = "rv3.radio_menu";

static ui_hdl radio_menu_create_with_entry(struct entry *root, int is_root);
static struct entry *radio_db_parse_array(struct db_parser *dbp, jsmntok_t *to, 
					  struct entry *root);

static int db_parser_init(struct db_parser *dbp, char *db_filepath)
{
	jsmn_parser p;
	int fd;
	int size;
	int ret;

	memset(dbp, 0, sizeof(*dbp));
	fd = open(db_filepath, O_RDONLY);
	if (fd < 0)
		return -1;

	size = lseek(fd, 0, SEEK_END);
	if (size < 0)
		goto lseek_error;
	ret = lseek(fd, 0, SEEK_SET);
	if (ret < 0)
		goto lseek_error;

	dbp->str = malloc(size);
	if (!dbp->str)
		goto malloc_error;

	ret = read(fd, dbp->str, size);
	if (ret != size)
		goto read_error;

	jsmn_init(&p);
	dbp->nb = jsmn_parse(&p, dbp->str, size, NULL, 0);
	if (dbp->nb <= 0)
		goto jsmn_parse_error;

	dbp->t = malloc(dbp->nb * sizeof(jsmntok_t));
	if (!dbp->t)
		goto malloc_error2;

	jsmn_init(&p);
	dbp->nb = jsmn_parse(&p, dbp->str, size, dbp->t, dbp->nb);
	if (!dbp->t)
		goto jsmn_parse_error2;
	dbp->index = 0;

	close(fd);

	return 0;


jsmn_parse_error2:
	free(dbp->t);
malloc_error2:
jsmn_parse_error:
read_error:
	free(dbp->str);
malloc_error:
lseek_error:
	close(fd);

	return -1;
}

static void db_parser_close(struct db_parser *dbp)
{
	free(dbp->t);
	free(dbp->str);
}

static int primitive_is_string(struct db_parser *dbp, jsmntok_t *t, char *str)
{
	if (t->type != JSMN_STRING)
		return 0;

	if (strncmp(str, dbp->str + t->start, t->end - t->start))
		return 0;

	return 1;
}

static int primitive_is_folder(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "folder");
}

static int primitive_is_entries(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "entries");
}

static int primitive_is_radio(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "radio");
}

static int primitive_is_url(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "url");
}

static int primitive_is_port(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "port");
}

static int primitive_is_path(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "path");
}

static int primitive_is_rate(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "rate");
}

static int primitive_is_meta(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "meta");
}

static jsmntok_t *fetch_next_token(struct db_parser *dbp)
{
	jsmntok_t *t;

	if (dbp->index == dbp->nb)
		return NULL;

	t = &dbp->t[dbp->index++];

	return t;
}

static jsmntok_t *fetch_next_token_in_range(struct db_parser *dbp, int start, int end)
{
	jsmntok_t *t;

	if (dbp->index == dbp->nb)
		return NULL;

	t = &dbp->t[dbp->index];
	if (t->start < start || t->end > end)
		return NULL;
	dbp->index++;

	return t;
}

static char *dup_string(struct db_parser *dbp, jsmntok_t *t)
{
	if (!t)
		return NULL;

	if (t->type != JSMN_STRING)
		return NULL;

	return strndup(dbp->str + t->start, t->end - t->start);
}

static char *dup_next_string_in_range(struct db_parser *dbp, int start, int end)
{
	return dup_string(dbp, fetch_next_token_in_range(dbp, start, end));
}

static void sync_object_in_range(struct db_parser *dbp, int *start, int *end)
{
	jsmntok_t *t = fetch_next_token_in_range(dbp, *start, *end);

	while (t) {
		if (t->type == JSMN_OBJECT) {
			*start = t->start;
			*end = t->end;
			return ;
		}
		t = fetch_next_token_in_range(dbp, *start, *end);
	}
}

static struct entry *parse_folder_in_range(struct db_parser *dbp, int start, int end, jsmntok_t *t)
{
	struct entry *folder;
	char *name = NULL;

	if (primitive_is_folder(dbp, t)) {
		name = dup_next_string_in_range(dbp, start, end);
		if (!name)
			return NULL;
		t = fetch_next_token_in_range(dbp, start, end);
		if (!t || !primitive_is_entries(dbp, t)) {
			free(name);
			return NULL;
		}
		folder = malloc(sizeof(*folder));
		if (!folder) {
			free(name);
			return NULL;
		}
		memset(folder, 0, sizeof(*folder));
		folder->name = name;
		folder->type = ENTRY_FOLDER;
		folder->sub = radio_db_parse_array(dbp, fetch_next_token_in_range(dbp, start, end), NULL);
	} else {
		folder = malloc(sizeof(*folder));
		if (!folder)
			return NULL;
		folder = malloc(sizeof(*folder));
		if (!folder)
			return NULL;
		memset(folder, 0, sizeof(*folder));
		folder->type = ENTRY_FOLDER;
		folder->sub = radio_db_parse_array(dbp, fetch_next_token_in_range(dbp, start, end), NULL);
		t = fetch_next_token_in_range(dbp, start, end);
		/* FIXME : code below must recursively remove folder if error */
		if (!t || !primitive_is_folder(dbp, t))
			return NULL;
		name = dup_next_string_in_range(dbp, start, end);
		if (!name)
			return NULL;
		folder->name = name;
	}

	return folder;
}

static struct entry *parse_radio_in_range(struct db_parser *dbp, int start, int end, jsmntok_t *t)
{
	struct radio *radio;
	char *name = NULL;
	char *url = NULL;
	char *port = NULL;
	char *path = NULL;
	char *rate = NULL;
	char *meta = NULL;

	while (t) {
		//print_token(dbp, t);
		if (primitive_is_radio(dbp, t))
			name = dup_next_string_in_range(dbp, start, end);
		else if (primitive_is_url(dbp, t))
			url = dup_next_string_in_range(dbp, start, end);
		else if (primitive_is_port(dbp, t))
			port = dup_next_string_in_range(dbp, start, end);
		else if (primitive_is_path(dbp, t))
			path = dup_next_string_in_range(dbp, start, end);
		else if (primitive_is_rate(dbp, t))
			rate = dup_next_string_in_range(dbp, start, end);
		else if (primitive_is_meta(dbp, t))
			meta = dup_next_string_in_range(dbp, start, end);
		t = fetch_next_token_in_range(dbp, start, end);
	}
	radio = malloc(sizeof(*radio));
	if (radio)
		memset(radio, 0, sizeof(*radio));
	if (radio && name && url && port && path && rate) {
		radio->entry.name = name;
		radio->entry.type = ENTRY_RADIO;
		radio->url = url;
		radio->port = port;
		radio->path = path;
		radio->rate = rate;
		radio->meta = meta;
	} else {
		if (radio)
			free(radio);
		if (name)
			free(name);
		if (url)
			free(url);
		if (port)
			free(port);
		if (path)
			free(path);
		if (rate)
			free(rate);
		if (meta)
			free(meta);
		radio = NULL;
		return NULL;
	}

	return &radio->entry;
}

static struct entry *fetch_next_entry_in_range(struct db_parser *dbp, int start, int end)
{
	jsmntok_t *t;

	/* sync on next object in range and update start and end variable with
	 * object range.
	 */
	sync_object_in_range(dbp, &start, &end);

	/* read next token which must be string and that will allow to define
	 * type of next object.
	 */
	t = fetch_next_token_in_range(dbp, start, end);
	if (!t)
		return NULL;
	if (t->type != JSMN_STRING)
		return NULL;

	if (primitive_is_folder(dbp, t) || primitive_is_entries(dbp, t))
		return parse_folder_in_range(dbp, start, end, t);
	else
		return parse_radio_in_range(dbp, start, end, t);
}

static struct entry *radio_db_parse_array(struct db_parser *dbp, jsmntok_t *to, 
					  struct entry *root)
{
	struct entry *cursor = NULL;
	struct entry *new_entry;
	int start = to->start;
	int end = to->end;

	if (to->type != JSMN_ARRAY)
		return root;

	new_entry = fetch_next_entry_in_range(dbp, start, end);
	while (new_entry) {
		if (!root)
			root = new_entry;
		if (cursor)
			cursor->next = new_entry;
		cursor = new_entry;
		new_entry = fetch_next_entry_in_range(dbp, start, end);
	}

	return root;
}

static struct entry *radio_db_parse(char *db_filename)
{
	struct db_parser parser;
	struct entry *root;
	int ret;

	ret = db_parser_init(&parser, db_filename);
	if (ret)
		return NULL;

	root = radio_db_parse_array(&parser, fetch_next_token(&parser), NULL);

	db_parser_close(&parser);

	return root;
}

static void radio_db_delete(struct entry *root)
{
	struct entry *current = root;
	struct entry *next;

	while (current) {
		next = current->next;

		if (current->type == ENTRY_RADIO) {
			struct radio *radio = container_of(current, struct radio, entry);

			free(current->name);
			free(radio->url);
			free(radio->port);
			free(radio->path);
			free(radio->rate);
			if (radio->meta)
				free(radio->meta);
			free(radio);
		} else if (current->type == ENTRY_FOLDER) {
			radio_db_delete(current->sub);
			free(current->name);
			free(current);
		} else
			assert(0);

		current = next;
	}
}

static int entry_get_nb(struct entry *current)
{
	int nb = 0;

	while (current) {
		nb++;
		current = current->next;
	}

	return nb;
}

static struct entry *entry_get_at_index(struct entry *current, int index)
{
	while (index && current) {
		index--;
		current = current->next;
	}

	return current;
}

static void radio_menu_destroy(void *ctx)
{
	struct radio_menu *menu = ctx;

	if (menu->is_root)
		radio_db_delete(menu->entries);

	free(menu);
}

static char *radio_menu_get_item_label(void *ctx, int index)
{
	struct radio_menu *menu = ctx;
	struct entry *entry = entry_get_at_index(menu->entries, index);

	if (!entry) {
		ESP_LOGE(TAG, "Got empty entry at index %d", index);
		return NULL;
	}

	return concat_with_delim(entry->type == ENTRY_FOLDER ? LV_SYMBOL_DIRECTORY : "",
				 entry->name, ' ');
}

static void radio_menu_put_item_label(void *ctx, char *item_label)
{
	free(item_label);
}

static void radio_menu_select_radio(struct radio_menu *menu, struct entry *entry)
{
	struct radio *radio = container_of(entry, struct radio, entry);

	ESP_LOGI(TAG, "select radio %s", entry->name);
	radio_player_create(entry->name, radio->url, radio->port, radio->path, atoi(radio->rate),
			    radio->meta ? atoi(radio->meta) : 0);
}

static void radio_menu_select_folder(struct radio_menu *menu, struct entry *entry)
{
	struct entry *sub = entry->sub;

	if (!sub) {
		ESP_LOGW(TAG, "No sub entry available");
		return ;
	}

	ESP_LOGI(TAG, "opening sub entry %s", entry->name);
	radio_menu_create_with_entry(sub, 0);
}

static void radio_menu_select_item(void *ctx, char *selected_label, int index)
{
	struct radio_menu *menu = ctx;
	struct entry *entry = entry_get_at_index(menu->entries, index);

	if (!entry) {
		ESP_LOGE(TAG, "Got empty entry at index %d", index);
		return ;
	}

	switch (entry->type) {
	case ENTRY_RADIO:
		radio_menu_select_radio(menu, entry);
		break;
	case ENTRY_FOLDER:
		radio_menu_select_folder(menu, entry);
		break;
	default:
		ESP_LOGE(TAG, "Got invalid entry type %d at index %d", entry->type, index);
		return ;
	}
}

static struct paging_cbs cbs = {
	.destroy = radio_menu_destroy,
	.get_item_label = radio_menu_get_item_label,
	.put_item_label= radio_menu_put_item_label,
	.select_item = radio_menu_select_item,
};

static ui_hdl radio_menu_create_with_entry(struct entry *root, int is_root)
{
	struct radio_menu *menu;
	ui_hdl paging;

	menu = malloc(sizeof(*menu));
	if (!menu)
		return NULL;
	memset(menu, 0, sizeof(*menu));

	menu->is_root = is_root;
	menu->entries = root;
	paging = paging_menu_create(entry_get_nb(root), &cbs, menu);
	if (!paging) {
		if (is_root)
			radio_db_delete(root);
		free(menu);
		return NULL;
	}

	return paging;
}

ui_hdl radio_menu_create(char *db_filename)
{
	struct entry *root = radio_db_parse(db_filename);

	if (!root)
		return NULL;

	return radio_menu_create_with_entry(root, 1);
}
