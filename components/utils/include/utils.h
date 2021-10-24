#ifndef __UTILS__
#define __UTILS__ 1

#include <stddef.h>

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define ARRAY_SIZE(a)		(sizeof(a)/sizeof(a[0]))

typedef int (*walk_dir_entry_cb)(char *root, char *filename, void *arg);
typedef int (*walk_dir_leave_cb)(char *root, void *arg);

struct walk_dir_cbs {
	int (*reg_cb)(char *root, char *filename, void *arg);
	int (*dir_cb)(char *root, char *dirname, void *arg);
	int (*enter_cb)(char *root, void *arg);
	int (*exit_cb)(char *root, void *arg);
};

char *concat(char *str1, char *str2);
int walk_dir(char *root, struct walk_dir_cbs *cbs, void *arg);
int remove_directories(char *dir);

#endif
