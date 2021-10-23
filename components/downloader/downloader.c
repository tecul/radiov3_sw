#include "downloader.h"

#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char* TAG = "rv3.downloader";
static  const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBs\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j\n" \
"ZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAwMFoXDTMxMTExMDAwMDAwMFowbDEL\n" \
"MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3\n" \
"LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFuY2Ug\n" \
"RVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm\n" \
"+9S75S0tMqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTW\n" \
"PNt0OKRKzE0lgvdKpVMSOO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEM\n" \
"xChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFB\n" \
"Ik5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQNAQTXKFx01p8VdteZOE3\n" \
"hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUeh10aUAsg\n" \
"EsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQF\n" \
"MAMBAf8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaA\n" \
"FLE+w2kD+L9HAdSYJhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3Nec\n" \
"nzyIZgYIVyHbIUf4KmeqvxgydkAQV8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6z\n" \
"eM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFpmyPInngiK3BD41VHMWEZ71jF\n" \
"hS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkKmNEVX58Svnw2\n" \
"Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe\n" \
"vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep\n" \
"+OkuE6N36B9K\n" \
"-----END CERTIFICATE-----\n";

struct cb_info {
	on_data_cb cb;
	void *cb_ctx;
};

struct cb_file_ctx {
	int fd;
};

static int mkdir_p(const char *path)
{
	const size_t len = strlen(path);
	char _path[PATH_MAX];
	char *p;

	errno = 0;

	/* Copy string so its mutable */
	if (len > sizeof(_path)-1) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strcpy(_path, path);

	/* Iterate the string */
	for (p = _path + 1; *p; p++) {
		if (*p == '/') {
			/* Temporarily truncate */
			*p = '\0';

			if (mkdir(_path, S_IRWXU) != 0) {
				if (errno != EEXIST)
					return -1;
			}

			*p = '/';
		}
	}

	if (mkdir(_path, S_IRWXU) != 0) {
		if (errno != EEXIST)
			return -1;
	}

	return 0;
}

char *dirname(char *path)
{
	int len = strlen(path);
	int i;

	for (i = len - 1; i >= 0; i--) {
		if (path[i] != '/')
			continue;
		path[i] = '\0';
		break;
	}

	return path;
}

static int create_directories(char *fullpath)
{
	char *dirpath = strdup(fullpath);
	int ret;

	ret = mkdir_p(dirname(dirpath));
	free(dirpath);

	return ret;
}

static int file_cb(void *data, int data_len, void *cb_ctx)
{
	struct cb_file_ctx *ctx = cb_ctx;
	int ret;

	ret = write(ctx->fd, data, data_len);

	return ret == data_len ? 0 : 1;
}

static esp_err_t http_event_handle(esp_http_client_event_t *evt)
{
	struct cb_info *ctx = evt->user_data;
	int ret = 0;

	switch (evt->event_id) {
	case HTTP_EVENT_ERROR:
		ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
		break;
	case HTTP_EVENT_ON_CONNECTED:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
		break;
	case HTTP_EVENT_HEADERS_SENT:
		ESP_LOGD(TAG, "HTTP_EVENT_HEADERS_SENT");
		break;
	case HTTP_EVENT_ON_HEADER:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER %s: %s", evt->header_key,
			 evt->header_value);
		break;
	case HTTP_EVENT_ON_DATA:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA %d %p: %d", esp_http_client_get_status_code(evt->client), evt->data,
			 evt->data_len);
		if (esp_http_client_get_status_code(evt->client) == 200)
			ret = ctx->cb(evt->data, evt->data_len, ctx->cb_ctx);
		if (ret)
			esp_http_client_close(evt->client);
		break;
	case HTTP_EVENT_ON_FINISH:
		ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
		break;
	case HTTP_EVENT_DISCONNECTED:
		ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
		break;
	}

	return ESP_OK;
}

int downloader_generic(char *url, on_data_cb cb, void *cb_ctx)
{
	esp_http_client_config_t config = {0};
	struct cb_info ctx = {cb, cb_ctx};
	esp_http_client_handle_t client;
	int ret;

	config.url = url;
	/* FIXME : next esp release should allow that. It will then be possible
	 * to remove config.cert_pem = root_ca;
	 */
	//config.crt_bundle_attach = esp_crt_bundle_attach;
	config.cert_pem = root_ca;
	config.event_handler = http_event_handle;
	config.buffer_size = 4096;
	config.buffer_size_tx = 4096;
	config.user_data = &ctx;

	client = esp_http_client_init(&config);
	if (!client) {
		ESP_LOGE(TAG, "unable to init http for %s", url);
		return -1;
	}

	ret = esp_http_client_perform(client);
	if (ret)
		ESP_LOGE(TAG, "unable to perform http for %s", url);

	esp_http_client_cleanup(client);

	return ret;
}

int downloader_file(char *url, char *fullpath)
{
	struct cb_file_ctx cb_ctx;
	int ret;

	ret = create_directories(fullpath);
	if (ret) {
		ESP_LOGE(TAG, "Unable to create directory for %s", fullpath);
		return ret;
	}

	cb_ctx.fd = creat(fullpath, 0666);
	if (cb_ctx.fd < 0) {
		ESP_LOGE(TAG, "Unable to create %s %s", fullpath, strerror(errno));
		return cb_ctx.fd;
	}

	ret = downloader_generic(url, file_cb, &cb_ctx);
	if (ret)
		ESP_LOGE(TAG, "Unable to download %s into %s", url, fullpath);

	close(cb_ctx.fd);

	return ret;
}
