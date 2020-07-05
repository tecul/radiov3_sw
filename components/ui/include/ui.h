#ifndef __UI__
#define __UI__ 1

struct ui_cbs {
	void (*destroy_chained)(struct ui_cbs *);
	void (*restore_event)(struct ui_cbs *);
};
typedef struct ui_cbs *ui_hdl;

#endif