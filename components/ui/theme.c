#include "theme.h"

#include "lv_core/lv_style.h"
#include "lv_misc/lv_color.h"
#include "lvgl.h"
#include "fonts.h"

#define COLOR_SCR				LV_COLOR_WHITE
#define COLOR_SCR_TEXT				LV_COLOR_BLACK

#define COLOR_BG				COLOR_SCR
#define COLOR_BG_BORDER				COLOR_SCR_TEXT

static lv_theme_t theme;
static struct {
	lv_style_t scr;
	lv_style_t bg;
	lv_style_t btn;
	lv_style_t border;
	lv_style_t color;
	lv_style_t ta_cursor;
} styles;

static void init_styles()
{
	/* scr */ 
	lv_style_reset(&styles.scr);
	lv_style_set_bg_opa(&styles.scr, LV_STATE_DEFAULT, LV_OPA_COVER);
	lv_style_set_bg_color(&styles.scr, LV_STATE_DEFAULT, COLOR_SCR);
	lv_style_set_text_color(&styles.scr, LV_STATE_DEFAULT, COLOR_SCR_TEXT);
	lv_style_set_value_color(&styles.scr, LV_STATE_DEFAULT, COLOR_SCR_TEXT);
	lv_style_set_text_sel_color(&styles.scr, LV_STATE_DEFAULT, COLOR_SCR_TEXT);
	lv_style_set_text_sel_bg_color(&styles.scr, LV_STATE_DEFAULT, theme.color_primary);
	lv_style_set_value_font(&styles.scr, LV_STATE_DEFAULT, theme.font_normal);

	/* bg */
	lv_style_reset(&styles.bg);
	lv_style_set_radius(&styles.bg, LV_STATE_DEFAULT, 0);
	lv_style_set_bg_opa(&styles.bg, LV_STATE_DEFAULT, LV_OPA_COVER);
	lv_style_set_bg_color(&styles.bg, LV_STATE_DEFAULT, COLOR_BG);
	lv_style_set_border_color(&styles.bg, LV_STATE_DEFAULT, COLOR_BG_BORDER);
	lv_style_set_border_post(&styles.bg, LV_STATE_DEFAULT, true);
	lv_style_set_pad_left(&styles.bg, LV_STATE_DEFAULT, 5);
	lv_style_set_pad_right(&styles.bg, LV_STATE_DEFAULT, 5);
	lv_style_set_pad_top(&styles.bg, LV_STATE_DEFAULT, 5);
	lv_style_set_pad_bottom(&styles.bg, LV_STATE_DEFAULT, 5);
	lv_style_set_pad_inner(&styles.bg, LV_STATE_DEFAULT, 4);

	/* btn */
	lv_style_reset(&styles.btn);
	 /* copy old style */
	lv_style_set_bg_color(&styles.btn, LV_STATE_DEFAULT, lv_color_make(0x76, 0xa2, 0xd0));
	lv_style_set_bg_grad_color(&styles.btn, LV_STATE_DEFAULT, lv_color_make(0x19, 0x3a, 0x5d));
	lv_style_set_bg_grad_dir(&styles.btn, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
	lv_style_set_text_color(&styles.btn, LV_STATE_DEFAULT, LV_COLOR_WHITE);
	lv_style_set_radius(&styles.btn, LV_STATE_DEFAULT, 5);
	lv_style_set_bg_color(&styles.btn, LV_STATE_PRESSED, lv_color_make(0x33, 0x62, 0x94));
	lv_style_set_bg_grad_color(&styles.btn, LV_STATE_PRESSED, lv_color_make(0x10, 0x26, 0x3c));
	lv_style_set_bg_grad_dir(&styles.btn, LV_STATE_PRESSED, LV_GRAD_DIR_VER);
	lv_style_set_text_color(&styles.btn, LV_STATE_PRESSED, lv_color_make(0xa4, 0xb5, 0xc6));
	lv_style_set_radius(&styles.btn, LV_STATE_CHECKED, 5);
	lv_style_set_bg_color(&styles.btn, LV_STATE_CHECKED, lv_color_make(0x33, 0x62, 0x94));
	lv_style_set_bg_grad_color(&styles.btn, LV_STATE_CHECKED, lv_color_make(0x10, 0x26, 0x3c));
	lv_style_set_bg_grad_dir(&styles.btn, LV_STATE_CHECKED, LV_GRAD_DIR_VER);
	lv_style_set_text_color(&styles.btn, LV_STATE_CHECKED, lv_color_make(0xa4, 0xb5, 0xc6));

	/* border */
	lv_style_reset(&styles.border);
	lv_style_set_border_width(&styles.border, LV_STATE_DEFAULT, 1);

	/* color */
	lv_style_reset(&styles.color);
	lv_style_set_bg_color(&styles.color, LV_STATE_DEFAULT, lv_color_make(0x6b, 0x9a, 0xc7));
	lv_style_set_line_color(&styles.color, LV_STATE_DEFAULT, COLOR_SCR_TEXT);
	lv_style_set_text_color(&styles.color, LV_STATE_DEFAULT, COLOR_SCR);

	/* ta_cursor */
	lv_style_reset(&styles.ta_cursor);
	lv_style_set_border_color(&styles.ta_cursor, LV_STATE_DEFAULT, COLOR_SCR_TEXT);
	lv_style_set_border_width(&styles.ta_cursor, LV_STATE_DEFAULT, 1);
	lv_style_set_pad_left(&styles.ta_cursor, LV_STATE_DEFAULT, 1);
	lv_style_set_border_side(&styles.ta_cursor, LV_STATE_DEFAULT, LV_BORDER_SIDE_LEFT);
}

static void theme_apply(lv_theme_t * th, lv_obj_t * obj, lv_theme_style_t name)
{
	lv_style_list_t * list;

	switch(name) {
	case LV_THEME_NONE:
		break;
	case LV_THEME_SCR:
		list = lv_obj_get_style_list(obj, LV_OBJ_PART_MAIN);
		_lv_style_list_add_style(list, &styles.scr);
		break;
	case LV_THEME_OBJ:
		list = lv_obj_get_style_list(obj, LV_OBJ_PART_MAIN);
		_lv_style_list_add_style(list, &styles.bg);
		break;
#if LV_USE_CONT
	case LV_THEME_CONT:
		list = lv_obj_get_style_list(obj, LV_CONT_PART_MAIN);
		_lv_style_list_add_style(list, &styles.bg);
		break;
#endif
#if LV_USE_BTN
	case LV_THEME_BTN:
		list = lv_obj_get_style_list(obj, LV_BTN_PART_MAIN);
		_lv_style_list_add_style(list, &styles.bg);
		_lv_style_list_add_style(list, &styles.border);
		_lv_style_list_add_style(list, &styles.btn);
		break;
#endif
#if LV_USE_BTNMATRIX
	case LV_THEME_BTNMATRIX:
		list = lv_obj_get_style_list(obj, LV_BTNMATRIX_PART_BG);
		_lv_style_list_add_style(list, &styles.bg);

		list = lv_obj_get_style_list(obj, LV_BTNMATRIX_PART_BTN);
		_lv_style_list_add_style(list, &styles.bg);
		break;
#endif
#if LV_USE_BAR
	case LV_THEME_BAR:
		list = lv_obj_get_style_list(obj, LV_BAR_PART_BG);
		_lv_style_list_add_style(list, &styles.bg);

		list = lv_obj_get_style_list(obj, LV_BAR_PART_INDIC);
		_lv_style_list_add_style(list, &styles.bg);
		_lv_style_list_add_style(list, &styles.color);
	break;
#endif
#if LV_USE_LABEL
	case LV_THEME_LABEL:
		break;
#endif
#if LV_USE_MSGBOX
	case LV_THEME_MSGBOX:
		list = lv_obj_get_style_list(obj, LV_MSGBOX_PART_BG);
		_lv_style_list_add_style(list, &styles.bg);
		break;

	case LV_THEME_MSGBOX_BTNS:
		list = lv_obj_get_style_list(obj, LV_MSGBOX_PART_BTN_BG);
		_lv_style_list_add_style(list, &styles.bg);

		list = lv_obj_get_style_list(obj, LV_MSGBOX_PART_BTN);
		_lv_style_list_add_style(list, &styles.bg);
		_lv_style_list_add_style(list, &styles.border);
		_lv_style_list_add_style(list, &styles.btn);
	break;
#endif
#if LV_USE_TEXTAREA
	case LV_THEME_TEXTAREA:
		list = lv_obj_get_style_list(obj, LV_TEXTAREA_PART_BG);
		_lv_style_list_add_style(list, &styles.bg);
		_lv_style_list_add_style(list, &styles.border);

		list = lv_obj_get_style_list(obj, LV_TEXTAREA_PART_PLACEHOLDER);
		_lv_style_list_add_style(list, &styles.bg);

		list = lv_obj_get_style_list(obj, LV_TEXTAREA_PART_CURSOR);
		_lv_style_list_add_style(list, &styles.bg);
		_lv_style_list_add_style(list, &styles.ta_cursor);

		list = lv_obj_get_style_list(obj, LV_TEXTAREA_PART_SCROLLBAR);
		_lv_style_list_add_style(list, &styles.bg);
	break;
#endif
	default:
		break;
	}
}

void theme_init()
{
	init_styles();

	theme.color_primary = LV_THEME_DEFAULT_COLOR_PRIMARY;
	theme.color_secondary = LV_THEME_DEFAULT_COLOR_SECONDARY;
	theme.font_small = &lv_font_rv3_16;
	theme.font_normal = &lv_font_rv3_16;
	theme.font_subtitle = &lv_font_rv3_16;
        theme.font_title = &lv_font_rv3_16;
        theme.flags = LV_THEME_DEFAULT_FLAG;

	theme.apply_xcb = NULL;
	theme.apply_cb = theme_apply;

	lv_theme_set_act(&theme);
}
