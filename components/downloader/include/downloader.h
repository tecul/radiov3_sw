#ifndef __DOWNLOADER__
#define __DOWNLOADER__ 1

typedef int (*on_data_cb)(void *data, int data_len, void *cb_ctx);
typedef void (*on_header_cb)(char *key, char *value, void *cb_ctx);

int downloader_generic(char *url, on_data_cb cb, void *cb_ctx);
int downloader_generic_with_headers(char *url, on_data_cb cb, on_header_cb hdr_cb,
				    void *cb_ctx, char **headers);
int downloader_file(char *url, char *fullpath);

#endif
