#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include "esp_log.h"

static const char* TAG = "rv3.utils";

static int remove_file(char *dir, char *name, void *arg)
{
	char *fp;
	int ret;

	fp = concat(dir, name);
	ret = remove(fp);
	free(fp);

	return ret;
}

static int remove_dir(char *dir, void *arg)
{
	return remove(dir);
}

/* public api */
char *concat(char *str1, char *str2)
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

int walk_dir(char *root, struct walk_dir_cbs *cbs, void *arg)
{
	struct dirent *entry;
	char *new_root_name;
	DIR *d;
	int ret = 0;

	if (cbs->enter_cb)
		ret = cbs->enter_cb(root, arg);
	if (ret)
		return ret;

	if (root == NULL)
		return -1;

	ESP_LOGD(TAG, "Walk %s", root);
	d = opendir(root);
	if (!d) {
		ESP_LOGW(TAG, "unable to open directory %s", root);
		return -2;
	}

	entry = readdir(d);
	while (entry) {
		switch (entry->d_type) {
		case DT_REG:
			if (cbs->reg_cb)
				ret = cbs->reg_cb(root, entry->d_name, arg);
			if (ret)
				goto error;
			break;
		case DT_DIR:
			if (cbs->dir_cb)
				ret = cbs->dir_cb(root, entry->d_name, arg);
			if (ret)
				goto error;
			if (strcmp(entry->d_name, ".") == 0)
				break;
			if (strcmp(entry->d_name, "..") == 0)
				break;
			new_root_name = concat(root, entry->d_name);
			ret = walk_dir(new_root_name, cbs, arg);
			free(new_root_name);
			if (ret)
				goto error;
			break;
		default:
			ESP_LOGW(TAG, "unsupported dt_type %d", entry->d_type);
		}
		entry = readdir(d);
	}

	if (cbs->exit_cb)
		ret = cbs->exit_cb(root, arg);
error:
	closedir(d);
	ESP_LOGD(TAG, "Walk of %s done", root);

	return ret;
}

int remove_directories(char *dir)
{
	struct walk_dir_cbs cbs = {NULL};

	cbs.reg_cb = remove_file;
	cbs.exit_cb = remove_dir;
	return walk_dir(dir, &cbs, NULL);
}
