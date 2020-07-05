#include "db.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <errno.h>

#include "esp_log.h"
#include "id3.h"

static const char* TAG = "rv3.db";

struct db_info {
	char *filepath;
	struct id3_meta meta;
};

struct song_cache {
	struct db_info info;
};

struct album_cache {
	char *dirname;
	int song_nb;
	/* array of album songs cache info ordered by track_nb */
	struct song_cache *songs;
};

struct db_builder {
	char *dirname;
	char *root_dir;
};

struct db {
	char *dirname;
	struct album_cache cache;
};

struct track_nb_song_name {
	int track_nb;
	char *song_name;
};

typedef int (*walk_dir_entry_cb)(char *root, char *filename, void *arg);

static char *concat(char *str1, char *str2)
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
	*buf++ = '/';
	strcpy(buf , str2);
	buf += strlen(str2);
	*buf = '\0';

	return res;
}

static char *concat3(char *str1, char *str2, char *str3)
{
	int len = strlen(str1) + 1 + strlen(str2) + 1 + strlen(str3) + 1;
	char *res;
	char *buf;

	res = malloc(len);
	if (res == NULL)
		return NULL;

	buf = res;
	strcpy(buf , str1);
	buf += strlen(str1);
	*buf++ = '/';
	strcpy(buf , str2);
	buf += strlen(str2);
	*buf++ = '/';
	strcpy(buf , str3);
	buf += strlen(str3);
	*buf = '\0';

	return res;
}

static char *concat4(char *str1, char *str2, char *str3, char *str4)
{
	int len = strlen(str1) + 1 + strlen(str2) + 1 + strlen(str3) + 1 + strlen(str4) + 1;
	char *res;
	char *buf;

	res = malloc(len);
	if (res == NULL)
		return NULL;

	buf = res;
	strcpy(buf , str1);
	buf += strlen(str1);
	*buf++ = '/';
	strcpy(buf , str2);
	buf += strlen(str2);
	*buf++ = '/';
	strcpy(buf , str3);
	buf += strlen(str3);
	*buf++ = '/';
	strcpy(buf , str4);
	buf += strlen(str4);
	*buf = '\0';

	return res;
}

static int read_db_info_by_filename(char *filename, struct db_info *db_info)
{
	char *buffer_init;
	char *buffer;
	off_t len;
	int ret;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return -1;

	len = lseek(fd, 0, SEEK_END);
	if (len < 0) {
		close(fd);
		return -1;
	}
	lseek(fd, 0, SEEK_SET);

	buffer_init = malloc(len);
	if (!buffer_init) {
		close(fd);
		return -1;
	}

	ret = read(fd , buffer_init, len);
	if (ret != len) {
		free(buffer_init);
		close(fd);
		return -1;
	}

	buffer = buffer_init;
	db_info->filepath = strdup(buffer);
	buffer += strlen(db_info->filepath) + 1;
	db_info->meta.artist = strdup(buffer);
	buffer += strlen(db_info->meta.artist) + 1;
	db_info->meta.album = strdup(buffer);
	buffer += strlen(db_info->meta.album) + 1;
	db_info->meta.title = strdup(buffer);
	buffer += strlen(db_info->meta.title) + 1;
	db_info->meta.track_nb = atoi(buffer);
	buffer += strlen(buffer) + 1;
	db_info->meta.duration_in_ms = atoi(buffer);
	free(buffer_init);
	close(fd);

	return 0;
}

static int read_db_info(void *hdl, char *artist, char *album, char *song, struct db_info *db_info)
{
	struct db *db = hdl;
	char *filename;
	int ret;

	filename = concat4(db->dirname, artist, album, song);
	if (!filename)
		return -1;

	ret = read_db_info_by_filename(filename, db_info);
	free(filename);

	return ret;
}

static void invalidate_cache(void *hdl, struct album_cache *cache)
{
	int i;

	for (i = 0; i < cache->song_nb; i++) {
		db_put_meta(hdl, &cache->songs[i].info.meta);
		free(cache->songs[i].info.filepath);
	}
	free(cache->songs);
	cache->songs = NULL;
	cache->song_nb = 0;
	free(cache->dirname);
	cache->dirname = NULL;
}

static int sort_songs_array(const void *pa, const void *pb)
{
	const struct song_cache *a = pa;
	const struct song_cache *b = pb;

	if (a->info.meta.track_nb < b->info.meta.track_nb)
		return -1;
	if (a->info.meta.track_nb > b->info.meta.track_nb)
		return 1;

	return 0;
}

static struct album_cache *refill_cache(void *hdl, char *dirname)
{
	struct db *db = hdl;
	struct album_cache *cache = &db->cache;
	int count = 0;
	int idx = 0;
	struct dirent *entry;
	DIR *d;
	int res;

	ESP_LOGI(TAG, "refill cache for %s", dirname);
	d = opendir(dirname);
	if (!d)
		return NULL;

	/* count entry number */
	entry = readdir(d);
	while (entry) {
		if (entry->d_type == DT_REG)
			count++;
		entry = readdir(d);
	}
	if (count == 0) {
		closedir(d);
		return NULL;
	}

	cache->songs = malloc(count * sizeof(struct song_cache));
	if (cache->songs == NULL) {
		closedir(d);
		return NULL;
	}

	ESP_LOGI(TAG, "cache song entries is %d", count);

	/* fillup info */
	rewinddir(d);
	entry = readdir(d);
	while (entry) {
		char *filename;
		if (entry->d_type != DT_REG)
			goto skip_entry;
		filename = concat(dirname, entry->d_name);
		assert(filename);
		ESP_LOGI(TAG, "Fill cache entry %d %s", idx, filename);
		res = read_db_info_by_filename(filename, &cache->songs[idx++].info);
		assert(res == 0);
		free(filename);
skip_entry:
		entry = readdir(d);
	}
	closedir(d);

	/* order songs by track_nb */
	qsort(cache->songs, count, sizeof(struct song_cache),
	      sort_songs_array);

	cache->song_nb = count;
	cache->dirname = strdup(dirname);

	return cache;
}

static struct album_cache *get_album_cache(void *hdl, char *dirname)
{
	struct db *db = hdl;
	struct album_cache *cache = &db->cache;

	if (cache->dirname && strcmp(dirname, cache->dirname) == 0) {
		return cache;
	}

	if (cache->dirname)
		invalidate_cache(hdl, cache);

	cache = refill_cache(hdl, dirname);

	return cache;
}

static int walk_dir(char *root, walk_dir_entry_cb cb, void *arg)
{
	struct dirent *entry;
	char *new_root_name;
	DIR *d;
	int ret = 0;

	if (root == NULL)
		return -1;

	ESP_LOGI(TAG, "Walk %s", root);
	d = opendir(root);
	if (!d) {
		ESP_LOGW(TAG, "unable to open directory %s", root);
		return -2;
	}

	entry = readdir(d);
	while (entry) {
		switch (entry->d_type) {
		case DT_REG:
			ret = cb(root, entry->d_name, arg);
			if (ret)
				goto error;
			break;
		case DT_DIR:
			if (strcmp(entry->d_name, ".") == 0)
				break;
			if (strcmp(entry->d_name, "..") == 0)
				break;
			new_root_name = concat(root, entry->d_name);
			walk_dir(new_root_name, cb, arg);
			free(new_root_name);
			break;
		default:
			ESP_LOGW(TAG, "unsupported dt_type %d", entry->d_type);
		}
		entry = readdir(d);
	}

error:
	closedir(d);
	ESP_LOGI(TAG, "Walk of %s done", root);

	return ret;
}

static int builder_add_artist(struct db_builder *builder, struct id3_meta *meta)
{
	char *dir_path = concat(builder->dirname, meta->artist);
	int ret;

	if (!dir_path)
		return -1;
	ret = mkdir(dir_path, 0777);
	if (ret && errno == EEXIST)
		ret = 0;
	free(dir_path);

	return 0;
}

static int builder_add_album(struct db_builder *builder, struct id3_meta *meta)
{
	char *dir_path = concat3(builder->dirname, meta->artist, meta->album);
	int ret;

	if (!dir_path)
		return -1;
	ret = mkdir(dir_path, 0777);
	if (ret && errno == EEXIST)
		ret = 0;
	free(dir_path);

	return 0;
}

static int is_song_exist(struct db_builder *builder, struct id3_meta *meta)
{
	char *filepath;
	int fd;

	filepath = concat4(builder->dirname, meta->artist, meta->album, meta->title);
	if (!filepath)
		return 0;

	fd = open(filepath, O_RDONLY);
	free(filepath);
	if (fd >= 0)
		close(fd);

	return fd < 0 ? 0 : 1;
}

static int builder_add_song(struct db_builder *builder, char *filename, struct id3_meta *meta)
{
	char *filepath = NULL;
	char buf[32];
	int ret;
	int fd = -1;

	if (is_song_exist(builder, meta))
		return 0;

	ret = builder_add_artist(builder, meta);
	if (ret)
		return ret;

	ret = builder_add_album(builder, meta);
	if (ret)
		return ret;

	filepath = concat4(builder->dirname, meta->artist, meta->album, meta->title);
	if (!filepath)
		goto error;

	fd = creat(filepath, 0666);
	if (fd < 0)
		goto error;

	ret = write(fd, filename, strlen(filename) + 1);
	if (ret != strlen(filename) + 1)
		goto error;

	ret = write(fd, meta->artist, strlen(meta->artist) + 1);
	if (ret != strlen(meta->artist) + 1)
		goto error;

	ret = write(fd, meta->album, strlen(meta->album) + 1);
	if (ret != strlen(meta->album) + 1)
		goto error;

	ret = write(fd, meta->title, strlen(meta->title) + 1);
	if (ret != strlen(meta->title) + 1)
		goto error;

	snprintf(buf, 32, "%d", meta->track_nb);
	ret = write(fd, buf, strlen(buf) + 1);
	if (ret != strlen(buf) + 1)
		goto error;

	snprintf(buf, 32, "%d", meta->duration_in_ms);
	ret = write(fd, buf, strlen(buf) + 1);
	if (ret != strlen(buf) + 1)
		goto error;

	free(filepath);
	close(fd);

	return 0;

error:
	if (filepath)
		free(filepath);

	if (fd >= 0)
		close(fd);

	return -1;
}

static int add_new_song(char *dir, char *name, void *arg)
{
	struct db_builder *builder = arg;
	struct id3_meta meta;
	char *filename;
	int ret;

	filename = concat(dir , name);
	assert(filename);
	ESP_LOGI(TAG, "Adding %s", filename);
	ret = id3_get(filename, &meta);
	if (ret)
		goto cleanup;

	if (!meta.artist)
		goto tag_cleanup;
	if (!meta.album)
		goto tag_cleanup;
	if (!meta.title)
		goto tag_cleanup;

	/*printf("%s\n", filename);
	printf(" - Artist: %s\n", meta.artist);
	printf(" - Album: %s\n", meta.album);
	printf(" - Title: %s\n", meta.title);
	printf(" - Track nb: %d\n", meta.track_nb);
	printf(" - Duration: %d ms\n", meta.duration_in_ms);*/

	ret = builder_add_song(builder, filename, &meta);
	if (ret)
		ESP_LOGW(TAG, " unable to add %s to db", filename);

tag_cleanup:
	id3_put(&meta);

cleanup:
	free(filename);

	return 0;
}

static char *dir_up(char *filename)
{
	char *pos = rindex(filename, '/');

	if (!pos)
		return NULL;

	*pos = '\0';

	return filename;
}

static void cleanup_from_filename(char *filename)
{
	unlink(filename);

	ESP_LOGI(TAG, "remove %s", filename);
	filename = dir_up(filename);
	while (filename && strlen(filename) > 0) {
		if (rmdir(filename))
			break;
		filename = dir_up(filename);
	}
}

static int remove_old_song(char *dir, char *name, void *arg)
{
	struct db_info db_info;
	char *filename;
	int ret;

	filename = concat(dir , name);
	assert(filename);

	ret = read_db_info_by_filename(filename, &db_info);
	if (ret)
		goto cleanup;

	if (access(db_info.filepath, F_OK) == 0)
		goto db_info_cleanup;

	cleanup_from_filename(filename);

db_info_cleanup:
	id3_put(&db_info.meta);
	free(db_info.filepath);

cleanup:
	free(filename);

	return 0;
}

static int builder_open(struct db_builder *builder, char *dirname, char *root_dir)
{
	int ret;

	/* FIXME : add case where dirname is a file ... */
	builder->dirname = dirname;
	builder->root_dir = root_dir;
	ret = mkdir(dirname, 0777);
	if (ret && errno != EEXIST) {
		perror("");
		return ret;
	}

	return 0;
}

static void builder_close(struct db_builder *builder)
{
	;
}

int update_db(char *dirname, char *root_dir)
{
	struct db_builder builder;
	int ret;

	ret = builder_open(&builder, dirname, root_dir);
	if (ret) {
		ESP_LOGE(TAG, "unable to create db %s", dirname);

		return ret;
	}

	ret = walk_dir(root_dir, add_new_song, &builder);
	if (ret)
		ESP_LOGE(TAG, "failed to populate db %d", ret);

	ret = walk_dir(dirname, remove_old_song, &builder);
	if (ret)
		ESP_LOGE(TAG, "failed to cleanup db %d", ret);

	builder_close(&builder);

	return ret;
}

/* read stuff */
static int count_directories(char *root)
{
	struct dirent *entry;
	DIR *d;
	int count = 0;

	if (root == NULL)
		return -1;

	ESP_LOGI(TAG, "Count %s", root);
	d = opendir(root);
	if (!d) {
		ESP_LOGW(TAG, "unable to open directory %s", root);
		return -2;
	}

	entry = readdir(d);
	while (entry) {
		switch (entry->d_type) {
		case DT_REG:
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
	ESP_LOGI(TAG, "Count of %s done => %d", root, count);

	return count;
}

static int count_files(void *hdl, char *root)
{
	struct album_cache *cache;
	int count;

	if (root == NULL)
		return -1;

	ESP_LOGI(TAG, "Count %s", root);
	cache = get_album_cache(hdl, root);
	if (!cache) {
		ESP_LOGW(TAG, "unable to open directory %s", root);
		return -2;
	}
	count = cache->song_nb;

	ESP_LOGI(TAG, "Count of %s done => %d", root, count);

	return count;
}

static char *dir_name(char *root, int index)
{
	struct dirent *entry;
	DIR *d;
	int count = 0;
	char *res = NULL;

	if (root == NULL)
		return NULL;

	ESP_LOGI(TAG, "Get %s at index %d", root, index);
	d = opendir(root);
	if (!d) {
		ESP_LOGW(TAG, "unable to open directory %s", root);
		return NULL;
	}

	entry = readdir(d);
	while (entry) {
		switch (entry->d_type) {
		case DT_REG:
			break;
		case DT_DIR:
			if (strcmp(entry->d_name, ".") == 0)
				break;
			if (strcmp(entry->d_name, "..") == 0)
				break;
			if (index == count)
				goto exit_loop;
			count++;
			break;
		default:
			ESP_LOGW(TAG, "unsupported dt_type %d", entry->d_type);
		}
		entry = readdir(d);
	}
exit_loop:
	if (entry)
		res = strdup(entry->d_name);

	closedir(d);
	ESP_LOGI(TAG, "get of %s done => %s", root, res);

	return res;
}

static char *file_name(void *hdl, int index, char *root)
{
	struct album_cache *cache;
	char *res = NULL;

	ESP_LOGI(TAG, "Get %s at index %d", root, index);

	cache = get_album_cache(hdl, root);
	assert(cache);

	res = strdup(cache->songs[index].info.meta.title);

	ESP_LOGI(TAG, "get of %s done => %s", root, res);

	return res;
}

void *db_open(char *dirname)
{
	struct db *db;

	db = malloc(sizeof(*db));
	if (!db)
		return NULL;
	memset(db, 0, sizeof(struct db));

	db->dirname = dirname;

	return db;
}

void db_close(void *hdl)
{
	free(hdl);
}

int db_artist_get_nb(void *hdl)
{
	struct db *db = hdl;

	return count_directories(db->dirname);
}

void db_put_item(void *hdl, char *name)
{
	free(name);
}

char *db_artist_get(void *hdl, int index)
{
	struct db *db = hdl;

	return dir_name(db->dirname, index);
}

int db_album_get_nb(void *hdl, char *artist)
{
	struct db *db = hdl;
	char *dirname;
	int ret;

	dirname = concat(db->dirname, artist);
	if (!dirname)
		return 0;
	ret = count_directories(dirname);
	free(dirname);

	return ret;
}

char *db_album_get(void *hdl, char *artist, int index)
{
	struct db *db = hdl;
	char *dirname;
	char *res;

	dirname = concat(db->dirname, artist);
	if (!dirname)
		return NULL;
	res = dir_name(dirname, index);
	free(dirname);

	return res;
}

int db_song_get_nb(void *hdl, char *artist, char *album)
{
	struct db *db = hdl;
	char *dirname;
	int ret;

	dirname = concat3(db->dirname, artist, album);
	if (!dirname)
		return 0;
	ret = count_files(hdl, dirname);
	free(dirname);

	return ret;
}

char *db_song_get(void *hdl, char *artist, char *album, int index)
{
	struct db *db = hdl;
	char *dirname;
	char *res;

	dirname = concat3(db->dirname, artist, album);
	if (!dirname)
		return NULL;
	res = file_name(hdl, index, dirname);
	free(dirname);

	return res;
}

char *db_song_get_filepath(void *hdl, char *artist, char *album, char *song)
{
	struct db_info db_info;
	int ret;

	memset(&db_info, 0, sizeof(db_info));
	ret = read_db_info(hdl, artist, album, song, &db_info);
	if (ret)
		return NULL;
	db_put_meta(hdl, &db_info.meta);

	return db_info.filepath;
}

int db_song_get_meta(void *hdl, char *artist, char *album, char *song, struct id3_meta *meta)
{
	struct db_info db_info;
	int ret;

	memset(&db_info, 0, sizeof(db_info));
	ret = read_db_info(hdl, artist, album, song, &db_info);
	if (ret)
		return -1;
	free(db_info.filepath);
	*meta = db_info.meta;

	return 0;
}

void db_put_meta(void *hdl, struct id3_meta *meta)
{
	id3_put(meta);
}
