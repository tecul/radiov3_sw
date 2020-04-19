#include "fetch_file.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "ring.h"

#define BUFFER_LEN	512

struct fetch_file {
	void *buffer_hdl;
	TaskHandle_t task;
	volatile bool is_active;
	SemaphoreHandle_t sem_en_of_task;
	char *filename;
	char buffer[BUFFER_LEN];
};

static void fetch_file_task(void *arg)
{
	struct fetch_file *self = arg;
	int fd;
	int ret;

	fd = open(self->filename, O_RDONLY);
	if (fd < 0)
		goto exit;

	while (self->is_active) {
		ret = read(fd, self->buffer, BUFFER_LEN);
		if (ret <= 0)
			goto exit;
retry:
		ret = ring_push(self->buffer_hdl, ret, self->buffer);
		/* test if we got a timeout */
		if (ret) {
			if (self->is_active) {
				vTaskDelay(10);
				goto retry;
			}
		}
	}

exit:
	if (fd >= 0)
		close(fd);
	xSemaphoreGive(self->sem_en_of_task);
	vTaskDelete(NULL);
}

/* public api */
void *fetch_file_create(void *buffer_hdl)
{
	struct fetch_file *self = malloc(sizeof(struct fetch_file));
	assert(self);

	self->buffer_hdl = buffer_hdl;
	self->sem_en_of_task = xSemaphoreCreateBinary();
	assert(self->sem_en_of_task);

	return self;
}

void fetch_file_destroy(void *hdl)
{
	assert(hdl);

	free(hdl);
}

void fetch_file_start(void *hdl, char *filename)
{
	struct fetch_file *self = hdl;
	BaseType_t res;

	assert(hdl);

	self->filename = filename;
	self->is_active = true;
	res = xTaskCreatePinnedToCore(fetch_file_task, "fetch_file", 4096, hdl,
			tskIDLE_PRIORITY + 1, &self->task, tskNO_AFFINITY);
	assert(res == pdPASS);
}
void fetch_file_stop(void *hdl)
{
	struct fetch_file *self = hdl;

	assert(hdl);

	self->is_active = false;
	xSemaphoreTake(self->sem_en_of_task, portMAX_DELAY);
}
