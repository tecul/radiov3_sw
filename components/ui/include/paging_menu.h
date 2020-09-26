#ifndef __PAGING_MENU__
#define __PAGING_MENU__ 1

#include "ui.h"

struct paging_cbs {
	void (*destroy)(void *ctx);
	char *(*get_item_label)(void *ctx, int index);
	void (*put_item_label)(void *ctx, char *item_label);
	void (*select_item)(void *ctx, char *selected_label, int index);
	void (*long_select_item)(void *ctx, char *selected_label, int index);
	int (*is_root)(void *ctx);
};

ui_hdl paging_menu_create(int item_nb, struct paging_cbs *cbs, void *ctx);
void paging_menu_refresh(ui_hdl hdl);

#endif