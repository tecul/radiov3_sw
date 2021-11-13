#include "fetch_socket_radio.h"

#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "ring.h"
#include "downloader.h"
#include "utils.h"

static const char* TAG = "rv3.fetch_socket_radio";

enum icy_st {
	ST_DATA,
	ST_ICY_LEN,
	ST_ICY_PAYLOAD
};

struct icy {
	enum icy_st st;
	int metaint;
	int pos;
	int icy_len;
	char *icy_buffer;
	int icy_pos;
};

struct fetch_socket_radio {
	void *buffer_hdl;
	TaskHandle_t task;
	volatile bool is_active;
	SemaphoreHandle_t sem_en_of_task;
	char url[256];
	int is_icy;
	struct icy icy;
	void *cb_hdl;
	audio_track_info_cb track_info_cb;
};

static int write_buffer(void *data, int data_len, void *cb_ctx)
{
	struct fetch_socket_radio *self = cb_ctx;
	int res;

retry:
	res = ring_push(self->buffer_hdl, data_len, data);
	if (res) {
		if (self->is_active)
			goto retry;
	}

	return self->is_active ? 0 : 1;
}

static int handle_data(struct fetch_socket_radio *self, char *data, int len)
{
	struct icy *icy = &self->icy;
	int consume;
	int remain;

	if (icy->metaint == 0) {
		write_buffer(data, len, self);
		return len;
	}

	remain = icy->metaint - icy->pos;
	consume = MIN(remain, len);
	write_buffer(data, consume, self);
	icy->pos += consume;

	if (icy->pos == icy->metaint) {
		icy->pos = 0;
		icy->st = ST_ICY_LEN;
	}

	return consume;
}

static int handle_icy_len(struct fetch_socket_radio *self, char *data, int len)
{
	struct icy *icy = &self->icy;

	icy->icy_len = 16 * (unsigned char) data[0];

	if (icy->icy_len) {
		icy->icy_buffer = calloc(1, icy->icy_len);
		assert(icy->icy_buffer);
		icy->icy_pos = 0;
		icy->st = ST_ICY_PAYLOAD;
	} else {
		icy->st = ST_DATA;
	}

	return 1;
}

static void parse_item(struct fetch_socket_radio *self, char *buffer)
{
	if (strncmp(buffer, "StreamTitle=", strlen("StreamTitle=")))
		return ;
	buffer += strlen("StreamTitle=") + 1;
	buffer[strlen(buffer) - 1] = '\0';

	if (strlen(buffer)) {
		char *sep = index(buffer, '-');
		if (sep) {
			*(sep - 1) = '\n';
			*(sep + 1) = '\n';
		}
		ESP_LOGI(TAG, "|%s|", buffer);
		if (self->track_info_cb)
			self->track_info_cb(self->cb_hdl, buffer);
	}
}

static char *split_item(struct fetch_socket_radio *self, char *buffer)
{
	char *next = index(buffer, ';');

	if (!next)
		return NULL;

	*next = '\0';
	parse_item(self, buffer);

	return next + 1;
}

static void parse_icy_payload(struct fetch_socket_radio *self, char *buffer)
{
	while (buffer)
		buffer = split_item(self, buffer);
}

static int handle_icy_payload(struct fetch_socket_radio *self, char *data, int len)
{
	struct icy *icy = &self->icy;
	int consume;

	consume = MIN(icy->icy_len, len);
	memcpy(&icy->icy_buffer[icy->icy_pos], data, consume);
	icy->icy_pos += consume;
	icy->icy_len -= consume;
	if (!icy->icy_len) {
		parse_icy_payload(self, icy->icy_buffer);
		free(icy->icy_buffer);
		icy->st = ST_DATA;
	}

	return consume;
}

static int write_buffer_icy(void *data, int data_len, void *cb_ctx)
{
	struct fetch_socket_radio *self = cb_ctx;
	struct icy *icy = &self->icy;
	int ret = 0;

	while (data_len) {
		switch (icy->st) {
		case ST_DATA:
			ret = handle_data(self, data, data_len);
			break;
		case ST_ICY_LEN:
			ret = handle_icy_len(self, data, data_len);
			break;
		case ST_ICY_PAYLOAD:
			ret = handle_icy_payload(self, data, data_len);
			break;
		}
		data_len -= ret;
		data += ret;
	}

	return self->is_active ? 0 : 1;
}

static void hdr_cb(char *key, char *value, void *cb_ctx)
{
	const char *metaint = "icy-metaint";
	struct fetch_socket_radio *self = cb_ctx;
	struct icy *icy = &self->icy;

	if (strncmp(key, metaint, strlen(metaint)))
		return ;

	icy->metaint = atoi(value);
}

static void fetch_socket_radio_task(void *arg)
{
	struct fetch_socket_radio *self = arg;
	char *headers[3] = {NULL, NULL, NULL};

	ESP_LOGI(TAG, "request %s", self->url);
	memset(&self->icy, 0, sizeof(self->icy));
	if (self->is_icy) {
		headers[0] = "Icy-MetaData";
		headers[1] = "1";
	}
	downloader_generic_with_headers(self->url, write_buffer_icy, hdr_cb ,arg, headers);

	xSemaphoreGive(self->sem_en_of_task);
	vTaskDelete(NULL);
}

/* public api */
void *fetch_socket_radio_create(void *buffer_hdl)
{
	struct fetch_socket_radio *self = malloc(sizeof(struct fetch_socket_radio));
	assert(self);

	self->buffer_hdl = buffer_hdl;
	self->sem_en_of_task = xSemaphoreCreateBinary();
	assert(self->sem_en_of_task);

	return self;
}

void fetch_socket_radio_destroy(void *hdl)
{
	struct fetch_socket_radio *self = hdl;
	assert(hdl);

	vSemaphoreDelete(self->sem_en_of_task);

	free(hdl);
}

void fetch_socket_radio_start(void *hdl, char *url, char *port_nb, char *path, int meta,
			      void *cb_hdl, audio_track_info_cb track_info_cb)
{
	struct fetch_socket_radio *self = hdl;
	BaseType_t res;

	assert(hdl);

	snprintf(self->url, sizeof(self->url), "http://%s%s", url, path);
	self->is_icy = meta;
	self->track_info_cb = track_info_cb;
	self->cb_hdl = cb_hdl;
	self->is_active = true;
	res = xTaskCreatePinnedToCore(fetch_socket_radio_task, "fetch_socket_radio", 4096, hdl,
			tskIDLE_PRIORITY + 1, &self->task, tskNO_AFFINITY);
	assert(res == pdPASS);
}

void fetch_socket_radio_stop(void *hdl)
{
	struct fetch_socket_radio *self = hdl;

	assert(hdl);

	self->is_active = false;
	xSemaphoreTake(self->sem_en_of_task, portMAX_DELAY);
}
