#ifndef __DOWNLOADER__
#define __DOWNLOADER__ 1

typedef int (*on_data_cb)(void *data, int data_len, void *cb_ctx);

int downloader_generic(char *url, on_data_cb cb, void *cb_ctx);

#endif
