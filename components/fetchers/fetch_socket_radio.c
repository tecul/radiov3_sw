#include "fetch_socket_radio.h"

#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "ring.h"

static const char* TAG = "rv3.fetch_socket_radio";

static char *url;
static char *port_nb;
static char *path;

static char requestDataBuffer[256];
static char readBuffer[1024];

struct fetch_socket_radio {
	void *buffer_hdl;
	TaskHandle_t task;
	volatile bool is_active;
	SemaphoreHandle_t sem_en_of_task;
};

static void fetch_socket_radio_task(void *arg)
{
	struct fetch_socket_radio *self = arg;
	const struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *addrinfo = NULL;
	int s;
	int res;
	int len;

	res = getaddrinfo(url, port_nb, &hints, &addrinfo);
	if (res) {
		ESP_LOGE(TAG, "unable to get addrinfo\n");
		exit(-1);
	}

	s = socket(addrinfo->ai_family, addrinfo->ai_socktype, 0);
	if (s < 0) {
		ESP_LOGE(TAG, "unable to get socket\n");
		exit(-1);
	}

	res = connect(s, addrinfo->ai_addr, addrinfo->ai_addrlen);
	if (res) {
		ESP_LOGE(TAG, "unable to connect\n");
		exit(-1);
	}
	freeaddrinfo(addrinfo);

	snprintf(requestDataBuffer, sizeof(requestDataBuffer), "GET %s HTTP/1.0\r\nHost: %s\r\nInitial-Burst: 96000\r\nConnection: Keep-Alive\r\n\r\n", path, url);
	ESP_LOGI(TAG, "%s", requestDataBuffer);

	res = write(s, requestDataBuffer, strlen(requestDataBuffer));
	if (res <= 0) {
		ESP_LOGE(TAG, "unable to send request\n");
		exit(-1);
	}

	while (self->is_active) {
		res = read(s, readBuffer, 1024);
		if (res <= 0) {
			ESP_LOGE(TAG, "read error %d\n", res);
			exit(-1);
		}
		len = res;
retry:
		res = ring_push(self->buffer_hdl, len, readBuffer);
		/* test if we got a timeout */
		if (res) {
			if (self->is_active)
				goto retry;
		}
	}

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

void fetch_socket_radio_start(void *hdl, char *url_, char *port_nb_, char *path_)
{
	struct fetch_socket_radio *self = hdl;
	BaseType_t res;

	assert(hdl);

	url = url_;
	port_nb = port_nb_;
	path = path_;

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
