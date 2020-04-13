#include "db.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include "esp_log.h"

static const char* TAG = "rv3.db";

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

static int add_new_song(char *dir, char *name, void *arg)
{
	ESP_LOGI(TAG, "%s/%s", dir, name);

	return 0;
}

int create_db(char *filename, char *root_dir)
{
	int fd;
	int ret = 0;

	/* O_TRUNC is important else open will failed */
	fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		ESP_LOGE(TAG, "unable to create db %s", filename);

		return -1;
	}

	ret = walk_dir(root_dir, add_new_song, (void *) fd);
	if (ret)
		ESP_LOGE(TAG, "failed to populate db %d", ret);

	close(fd);

	return ret;
}
