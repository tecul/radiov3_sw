#include "web_server.h"

#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "mongoose.h"
#include "ring.h"

#define MIN(a,b)	((a) < (b) ? (a) : (b))
#define RING_SZ_KB	(64)
#define BUFFER_SZ	(8 * 1024)

static const char* TAG = "rv3.web_server";

static const struct mg_str s_get_method = MG_MK_STR("GET");
static const struct mg_str s_post_method = MG_MK_STR("POST");
static const struct mg_str s_put_method = MG_MK_STR("PUT");
static const struct mg_str s_delete_method = MG_MK_STR("DELETE");
static const char *sdcard_root = "/sdcard";

typedef int (*cb_handle)(struct mg_connection *nc, struct http_message *hm, char *path);

struct web_upload {
	volatile bool is_active;
	TaskHandle_t task;
	SemaphoreHandle_t sem_en_of_task;
	void *ring;
	FILE *f;
};

static struct web_server {
	volatile bool is_active;
	TaskHandle_t task;
	SemaphoreHandle_t sem_en_of_task;
	struct web_upload wu;
} server;

static void server_copy_task(void *arg)
{
	struct web_upload *wu = &server.wu;
	char buffer[BUFFER_SZ];
	int len_copy;
	int wr_pos = 0;
	int ret;
	int total_copy = 0;

	while (1) {
		len_copy = BUFFER_SZ - wr_pos;
		ret = ring_pop(wu->ring, &len_copy, &buffer[wr_pos]);
		if (ret) {
			if (wu->is_active)
				continue;
			break;
		}
		wr_pos += len_copy;
		if (wr_pos == BUFFER_SZ) {
			assert(wu->f);
			total_copy += BUFFER_SZ;
			fwrite(buffer, 1, BUFFER_SZ, wu->f);
			wr_pos = 0;
		}
	}

	if (wr_pos) {
		total_copy += wr_pos;
		fwrite(buffer, 1, wr_pos, wu->f);
	}
	fclose(wu->f);
	ESP_LOGI(TAG, " copy length is %d bytes", total_copy);

	xSemaphoreGive(wu->sem_en_of_task);
	vTaskDelete(NULL);
}

static void start_copy_thread(char *full_path)
{
	struct web_upload *wu = &server.wu;
	BaseType_t res;

	ESP_LOGI(TAG, "Starting copy %s", full_path);
	wu->is_active = true;
	wu->f = NULL;
	wu->ring = ring_create(RING_SZ_KB);
	assert(wu->ring);

	wu->sem_en_of_task = xSemaphoreCreateBinary();
	assert(wu->sem_en_of_task);

	res = xTaskCreatePinnedToCore(server_copy_task, "web_copy", 3 * 4096, NULL,
			tskIDLE_PRIORITY + 1, &wu->task, tskNO_AFFINITY);
	assert(res == pdPASS);
}

static void stop_copy_thread()
{
	struct web_upload *wu = &server.wu;

	ESP_LOGI(TAG, "Stopping copy");
	wu->is_active = false;
	xSemaphoreTake(wu->sem_en_of_task, portMAX_DELAY);
	ring_destroy(wu->ring);
	vSemaphoreDelete(wu->sem_en_of_task);
}

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

static int get_file(struct mg_connection *nc, struct http_message *hm, char *file)
{
	char buffer[128];
	int len;
	int fd;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		return -1;

	mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\n"
		  "Content-Type: application/text; charset=utf-8"
		  "\r\nTransfer-Encoding: chunked\r\n\r\n");

	while (1) {
		len = read(fd, buffer, sizeof(buffer));
		if (len <= 0)
			break;
		mg_send_http_chunk(nc, buffer, len);
	}

	mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
	close(fd);

	return 0;
}

static int create_write_file(struct mg_connection *nc, struct http_message *hm, char *file)
{
	int len;
	int fd;

	fd = creat(file, 0666);
	if (fd < 0)
		return -1;

	len = write(fd, hm->body.p, hm->body.len);
	if (len != hm->body.len) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int display_dir(struct mg_connection *nc, struct http_message *hm, char *dir)
{
	struct dirent *entry;
	int is_first = 1;
	DIR *d;

	d = opendir(dir);
	if (!d)
		return -1;

	mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\n"
		  "Content-Type: application/json; charset=utf-8"
		  "\r\nTransfer-Encoding: chunked\r\n\r\n");
	mg_printf_http_chunk(nc, "[");
	entry = readdir(d);
	while (entry) {
		switch (entry->d_type) {
		case DT_REG:
			mg_printf_http_chunk(nc, "%c{\"type\": \"%s\", \"name\": \"%s\"}",
					     is_first ? ' ' : ',', "file" , entry->d_name);
			is_first = 0;
			break;
		case DT_DIR:
			if (strcmp(entry->d_name, ".") == 0)
				break;
			if (strcmp(entry->d_name, "..") == 0)
				break;
			mg_printf_http_chunk(nc, "%c{\"type\": \"%s\", \"name\": \"%s\"}",
					     is_first ? ' ' : ',', "dir" , entry->d_name);
			is_first = 0;
			break;
		default:
			break;
		}
		entry = readdir(d);
	}
	mg_printf_http_chunk(nc, "]\n");
	mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */

	closedir(d);

	return 0;
}

static int create_dir(struct mg_connection *nc, struct http_message *hm, char *dir)
{
	return mkdir(dir, 0777);
}

static int delete_dir(struct mg_connection *nc, struct http_message *hm, char *dir)
{
	return rmdir(dir);
}

static int delete_file(struct mg_connection *nc, struct http_message *hm, char *file)
{
	return unlink(file);
}

static int rename_dir_file(struct mg_connection *nc, struct http_message *hm, char *dir)
{
	char new_name[256];
	char *last_slash_pos;
	char *dir_tmp;
	char *new_path;
	int res;

	mg_get_http_var(&hm->body, "new_name", new_name, sizeof(new_name));

	dir_tmp = strdup(dir);
	if (!dir_tmp)
		return -1;
	last_slash_pos = rindex(dir_tmp, '/');
	if (!last_slash_pos) {
		free(dir_tmp);
		return -1;
	}
	*last_slash_pos = '\0';
	new_path = concat(dir_tmp, new_name);
	free(dir_tmp);
	if (!new_path)
		return -1;

	res = rename(dir, new_path);
	free(new_path);

	return res;
}


static void handle_cb(struct mg_connection *nc, struct http_message *hm,
		      struct mg_str *cmd, cb_handle cb_handle, int is_send_ok)
{
	char *full_path = NULL;
	char *path = NULL;
	int ret;

	path = strndup(cmd->p, cmd->len);
	if (!path)
		goto internal_error;
	full_path = concat((char *) sdcard_root, path);
	if (!full_path)
		goto internal_error;

	while (full_path[strlen(full_path) - 1] == '/')
		full_path[strlen(full_path) - 1] = '\0';

	mg_url_decode(full_path, strlen(full_path) + 1, full_path, strlen(full_path) + 1, 0);
	ret = cb_handle(nc, hm, full_path);
	if (ret)
		goto internal_error;

	if (is_send_ok)
		mg_printf(nc, "%s", "HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n");

	free(full_path);
	free(path);

	return ;

internal_error:
	if (path)
		free(path);
	if (full_path)
		free(full_path);
	mg_printf(nc, "%s", "HTTP/1.0 500 Internal Server Error\r\n"
		  "Content-Length: 0\r\n\r\n");
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
{
	static const struct mg_str dir_api_prefix = MG_MK_STR("/api/v1/dir");
	static const struct mg_str file_api_prefix = MG_MK_STR("/api/v1/file");
	struct mg_serve_http_opts opts = { .document_root = "/sdcard/static"};
	struct http_message *hm = (struct http_message *) ev_data;
	struct mg_str cmd;

	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		if (mg_str_starts_with(hm->uri, dir_api_prefix)) {
			cmd.p = hm->uri.p + dir_api_prefix.len;
			cmd.len = hm->uri.len - dir_api_prefix.len;
			if (mg_strcmp(hm->method, s_get_method) == 0)
				handle_cb(nc, hm, &cmd, display_dir, 0);
			else if (mg_strcmp(hm->method, s_post_method) == 0)
				handle_cb(nc, hm, &cmd, create_dir, 1);
			else if (mg_strcmp(hm->method, s_delete_method) == 0)
				handle_cb(nc, hm, &cmd, delete_dir, 1);
			else if (mg_strcmp(hm->method, s_put_method) == 0)
				handle_cb(nc, hm, &cmd, rename_dir_file, 1);
			else
				goto not_found;
		} else if (mg_str_starts_with(hm->uri, file_api_prefix)) {
			cmd.p = hm->uri.p + file_api_prefix.len;
			cmd.len = hm->uri.len - file_api_prefix.len;
			if (mg_strcmp(hm->method, s_get_method) == 0)
				handle_cb(nc, hm, &cmd, get_file, 0);
			else if (mg_strcmp(hm->method, s_post_method) == 0)
				handle_cb(nc, hm, &cmd, create_write_file, 1);
			else if (mg_strcmp(hm->method, s_delete_method) == 0)
				handle_cb(nc, hm, &cmd, delete_file, 1);
			else if (mg_strcmp(hm->method, s_put_method) == 0)
				handle_cb(nc, hm, &cmd, rename_dir_file, 1);
			else
				goto not_found;
		} else
			mg_serve_http(nc, hm, opts);
		break;
	default:
		break;
	}

	return;

not_found:
	mg_printf(nc, "%s", "HTTP/1.0 404 Not Found\r\n"
		  "Content-Length: 0\r\n\r\n");
}

static struct mg_str upload_fname(struct mg_connection *nc, struct mg_str fname)
{
	struct mg_str lfn;
	char *full_path = NULL;
	char *path = NULL;

	path = strndup(fname.p, fname.len);
	if (!path)
		goto error;
	full_path = concat((char *) sdcard_root, path);
	if (!full_path)
		goto error;

	if (access(full_path, F_OK) == 0)
		goto error;

	lfn.len = strlen(full_path);
	lfn.p = full_path;
	free(path);

	start_copy_thread(full_path);

	return lfn;

error:
	if (path)
		free(path);
	if (full_path)
		free(full_path);
	lfn.len = 0;
	lfn.p = NULL;

	return lfn;
}

size_t upload_fwrite(const void *ptr, size_t size, size_t count, FILE *f)
{
	struct web_upload *wu = &server.wu;
	int ret;

	if (!wu->f)
		wu->f = f;

	do {
		ret = ring_push(wu->ring, count, (void *) ptr);
	} while (ret);

	return count;
}

int upload_fclose(FILE *f)
{
	stop_copy_thread();

	return 0;
}

static void handle_upload(struct mg_connection *nc, int ev, void *p)
{
	switch (ev) {
	case MG_EV_HTTP_PART_DATA:
	case MG_EV_HTTP_PART_BEGIN:
	case MG_EV_HTTP_PART_END:
	case MG_EV_HTTP_MULTIPART_REQUEST_END:
		mg_file_upload_handler(nc, ev, p, upload_fname, upload_fwrite, upload_fclose);
		break;
	default:
		break;
	}
}

static void server_task(void *arg)
{
	struct mg_bind_opts bind_opts;
	struct mg_mgr mgr;
	struct mg_connection *nc;

	ESP_LOGI(TAG, "Starting");
	mg_mgr_init(&mgr, NULL);
	memset(&bind_opts, 0, sizeof(bind_opts));
	nc = mg_bind_opt(&mgr, "8000", ev_handler, bind_opts);
	if (!nc) {
		ESP_LOGE(TAG, "Unable to start server");
		goto exit;
	}
	mg_register_http_endpoint(nc, "/api/v1/upload", handle_upload MG_UD_ARG(NULL));
	mg_set_protocol_http_websocket(nc);
	while (server.is_active) {
		mg_mgr_poll(&mgr, 100);
	}
	mg_mgr_free(&mgr);

exit:
	ESP_LOGI(TAG, "Stopping");
	xSemaphoreGive(server.sem_en_of_task);
	vTaskDelete(NULL);
}

int web_server_start()
{
	BaseType_t res;

	server.sem_en_of_task = xSemaphoreCreateBinary();
	assert(server.sem_en_of_task);

	server.is_active = true;
	res = xTaskCreatePinnedToCore(server_task, "web_server", 3 * 4096, NULL,
			tskIDLE_PRIORITY + 1, &server.task, tskNO_AFFINITY);
	assert(res == pdPASS);

	return 0;
}

void web_server_stop()
{
	server.is_active = false;
	xSemaphoreTake(server.sem_en_of_task, portMAX_DELAY);
	vSemaphoreDelete(server.sem_en_of_task);
}
