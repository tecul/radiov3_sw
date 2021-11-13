#include "fetch_socket_radio.h"

#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "ring.h"
#include "downloader.h"

static const char* TAG = "rv3.fetch_socket_radio";

struct fetch_socket_radio {
	void *buffer_hdl;
	TaskHandle_t task;
	volatile bool is_active;
	SemaphoreHandle_t sem_en_of_task;
	char url[256];
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

static void fetch_socket_radio_task(void *arg)
{
	struct fetch_socket_radio *self = arg;

	ESP_LOGI(TAG, "request %s", self->url);
	downloader_generic(self->url, write_buffer, arg);

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

void fetch_socket_radio_start(void *hdl, char *url, char *port_nb, char *path)
{
	struct fetch_socket_radio *self = hdl;
	BaseType_t res;

	assert(hdl);

	snprintf(self->url, sizeof(self->url), "http://%s%s", url, path);
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
