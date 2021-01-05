#include "system.h"

#include <string.h>
#include <stdio.h>

#include "ff.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char* TAG = "rv3.system";

static char *cache_name;

static char *get_name(nvs_handle hdl)
{
	size_t length;
	esp_err_t err;
	char *res;

	if (cache_name)
		return cache_name;

	err = nvs_get_str(hdl, "name", NULL, &length);
	if (err)
		return NULL;

	res = malloc(length);
	if (!res)
		return NULL;

	err = nvs_get_str(hdl, "name", res, &length);
	if (err) {
		free(res);
		return NULL;
	}
	cache_name = res;

	return res;
}

static void set_default_name()
{
	char name[32];
	uint8_t mac[6];
	esp_err_t err;

	err = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
	if (err)
		return ;

	snprintf(name, sizeof(name), "radiov3_%02x%02x%02x%02x%02x%02x",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	ESP_LOGW(TAG, "name not found setting default name to %s", name);

	system_set_name(name);
}

char *system_get_name()
{
	nvs_handle hdl;
	esp_err_t err;
	char *name = NULL;

	err = nvs_open(TAG, NVS_READWRITE, &hdl);
	if (err) {
		ESP_LOGE(TAG, "Unable to open %s nvm space %s\n", TAG,
			 esp_err_to_name(err));
		return NULL;
	}

	name = get_name(hdl);
	if (name)
		goto cleanup;
	set_default_name();
	name = get_name(hdl);

cleanup:
	nvs_commit(hdl);
	nvs_close(hdl);
	ESP_LOGI(TAG, "device name is %s", name);

	return name;
}

int system_set_name(char *name)
{
	nvs_handle hdl;
	esp_err_t err;

	err = nvs_open(TAG, NVS_READWRITE, &hdl);
	if (err) {
		ESP_LOGE(TAG, "Unable to open %s nvm space %s\n", TAG,
			 esp_err_to_name(err));
		return err;
	}

	err = nvs_set_str(hdl, "name", name);
	if (err) {
		ESP_LOGE(TAG, "Unable to write name with value %s => %s\n",
			 name, esp_err_to_name(err));
	} else {
		ESP_LOGI(TAG, "set device name to %s", name);
		if (cache_name)
			free(cache_name);
		cache_name = strdup(name);
	}

	nvs_commit(hdl);
	nvs_close(hdl);

	return err;
}

int system_get_sdcard_info(uint64_t *total_size, uint64_t *free_size)
{
	uint64_t total_sectors;
	uint64_t free_sectors;
	FATFS *fs;
	DWORD fre_clust;
	int ret;

	ret = f_getfree("/sdcard/", &fre_clust, &fs);
	if (ret) {
		ESP_LOGE(TAG, "f_getfree => %d\n", ret);
		return ret;
	}

	total_sectors = (fs->n_fatent - 2) * fs->csize;
	free_sectors = fre_clust * fs->csize;

	*total_size = total_sectors * fs->ssize;
	*free_size = free_sectors * fs->ssize;

	return 0;
}
