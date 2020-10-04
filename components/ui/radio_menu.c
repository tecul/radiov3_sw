#include "radio_menu.h"
#include "jsmn.h"

#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"

#include "radio_player.h"
#include "wifi.h"
#include "paging_menu.h"

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
};

struct radio_menu {
	int is_root;
	struct entry *entries;
};

static ui_hdl radio_menu_create_with_entry(struct entry *root, int is_root);
static struct entry *radio_db_parse_object(struct db_parser *dbp, jsmntok_t *to, 
					   struct entry *root);

static const char* TAG = "rv3.radio_menu";

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

static jsmntok_t *fetch_next_primitive_in_range(struct db_parser *dbp, int start, int end)
{
	jsmntok_t *t = fetch_next_token_in_range(dbp, start, end);

	while (t) {
		if (t->type == JSMN_PRIMITIVE)
			return t;
		t = fetch_next_token_in_range(dbp, start, end);
	}

	return NULL;
}

static int primitive_is_string(struct db_parser *dbp, jsmntok_t *t, char *str)
{
	if (t->type != JSMN_PRIMITIVE)
		return 0;

	if (strncmp(str, dbp->str + t->start, t->end - t->start))
		return 0;

	return 1;
}

static int primitive_is_radio(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "radio");
}

static int primitive_is_name(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "name");
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

static int primitive_is_dir(struct db_parser *dbp, jsmntok_t *t)
{
	return primitive_is_string(dbp, t, "dir");
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

static struct entry *parse_dir(struct db_parser *dbp, int start, int end)
{
	jsmntok_t *to = fetch_next_token_in_range(dbp, start, end);
	struct entry *dir;
	jsmntok_t *t;
	char *name;

	if (to->type != JSMN_OBJECT) {
		printf("Need object type\n");
		return NULL;
	}

	t = fetch_next_token_in_range(dbp, to->start, to->end);
	if (!primitive_is_name(dbp, t)) {
		printf("Need primitive name\n");
		return NULL;
	}

	name = dup_next_string_in_range(dbp, to->start, to->end);
	if (!name)
		return NULL;

	dir = malloc(sizeof(*dir));
	if (!dir) {
		free(name);
		return NULL;
	}

	memset(dir, 0, sizeof(*dir));
	dir->name = name;
	dir->type = ENTRY_FOLDER;

	dir->sub = radio_db_parse_object(dbp, to, NULL);

	return dir;
}

static struct entry *parse_radio(struct db_parser *dbp, int start, int end)
{
	jsmntok_t *to = fetch_next_token_in_range(dbp, start, end);
	char *name = NULL;
	char *url = NULL;
	char *port = NULL;
	char *path = NULL;
	jsmntok_t *t;
	struct radio *radio;

	if (to->type != JSMN_OBJECT) {
		printf("Need object type\n");
		return NULL;
	}

	t = fetch_next_token_in_range(dbp, to->start, to->end);
	while (t) {
		if (primitive_is_name(dbp, t))
			name = dup_next_string_in_range(dbp, to->start, to->end);
		else if (primitive_is_url(dbp, t))
			url = dup_next_string_in_range(dbp, to->start, to->end);
		else if (primitive_is_port(dbp, t))
			port = dup_next_string_in_range(dbp, to->start, to->end);
		else if (primitive_is_path(dbp, t))
			path = dup_next_string_in_range(dbp, to->start, to->end);
		t = fetch_next_token_in_range(dbp, to->start, to->end);
	}
	radio = malloc(sizeof(*radio));
	if (radio)
		memset(radio, 0, sizeof(*radio));
	if (radio && name && url && port && path) {
		radio->entry.name = name;
		radio->entry.type = ENTRY_RADIO;
		radio->url = url;
		radio->port = port;
		radio->path = path;
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
		radio = NULL;
	}

	return &radio->entry;
}

static struct entry *radio_db_parse_object(struct db_parser *dbp, jsmntok_t *to, 
					   struct entry *root)
{
	struct entry *cursor = NULL;
	struct entry *new_entry;
	int start = to->start;
	int end = to->end;
	jsmntok_t *t;

	if (to->type != JSMN_OBJECT)
		return root;

	t = fetch_next_primitive_in_range(dbp, start, end);
	while (t) {
		if (primitive_is_radio(dbp, t)) {
			new_entry = parse_radio(dbp, start, end);
			if (!new_entry)
				goto next;
			if (!root)
				root = new_entry;
			if (cursor)
				cursor->next = new_entry;
			cursor = new_entry;
		} else if (primitive_is_dir(dbp, t)) {
			new_entry = parse_dir(dbp, start, end);
			if (!new_entry)
				goto next;
			if (!root)
				root = new_entry;
			if (cursor)
				cursor->next = new_entry;
			cursor = new_entry;
		}
next:
		t = fetch_next_primitive_in_range(dbp, start, end);
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

	root = radio_db_parse_object(&parser, fetch_next_token(&parser), NULL);

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
	radio_player_create(entry->name, radio->url, radio->port, radio->path);
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
