#include "radio_player.h"

#include <assert.h>

#include "lvgl.h"
#include "esp_log.h"

#include "audio.h"

#include "system_menu.h"
#include "fonts.h"

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

enum radio_player_button {
	RADIO_PLAYER_MENU,
	RADIO_PLAYER_BACK,
	RADIO_PLAYER_UP,
	RADIO_PLAYER_DOWN,
	RADIO_PLAYER_NB
};

struct radio_player {
	struct ui_cbs cbs;
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *btn[RADIO_PLAYER_NB];
	lv_obj_t *label[RADIO_PLAYER_NB];
	lv_obj_t *msg_label;
	lv_obj_t *bar_level;
	lv_obj_t *sound_level;
	lv_task_t *task_level;
};

static inline struct radio_player *get_radio_player()
{
	struct ui_cbs *cbs = lv_obj_get_user_data(lv_disp_get_scr_act(NULL));
	return container_of(cbs, struct radio_player, cbs);
}

static void radio_player_destroy(struct radio_player *player)
{
	lv_task_del(player->task_level);
	audio_radio_stop();
	lv_obj_del(player->scr);
	free(player);
}

static void handle_back_event(struct radio_player *player)
{
	ui_hdl prev = lv_obj_get_user_data(player->prev_scr);

	lv_disp_load_scr(player->prev_scr);
	if (prev->restore_event)
		prev->restore_event(prev);

	radio_player_destroy(player);
}

static void destroy_chained(struct ui_cbs *cbs)
{
	struct radio_player *player = container_of(cbs, struct radio_player, cbs);
	ui_hdl prev;

	prev = lv_obj_get_user_data(player->prev_scr);
	prev->destroy_chained(prev);

	radio_player_destroy(player);
}

static enum radio_player_button get_btn_id_from_obj(struct radio_player *player, lv_obj_t *btn)
{
	int i;

	for (i = 0; i < RADIO_PLAYER_NB; i++) {
		if (player->btn[i] == btn)
			return i;
	}

	return RADIO_PLAYER_NB;
}

static void radio_player_event_cb(lv_obj_t *btn, lv_event_t event)
{
	struct radio_player *player = get_radio_player();
	enum radio_player_button btn_id = get_btn_id_from_obj(player, btn);

	if( event != LV_EVENT_CLICKED && event != LV_EVENT_LONG_PRESSED_REPEAT)
		return;
	if (event == LV_EVENT_LONG_PRESSED_REPEAT && btn_id != RADIO_PLAYER_UP && btn_id != RADIO_PLAYER_DOWN)
		return;

	switch (btn_id) {
	case RADIO_PLAYER_MENU:
		destroy_chained(&player->cbs);
		break;
	case RADIO_PLAYER_BACK:
		handle_back_event(player);
		break;
	case RADIO_PLAYER_UP:
		lv_bar_set_value(player->sound_level,  audio_sound_level_up(),
				 LV_ANIM_ON);
		break;
	case RADIO_PLAYER_DOWN:
		lv_bar_set_value(player->sound_level,  audio_sound_level_down(),
				 LV_ANIM_ON);
		break;
	default:
		assert(0);
	}
}

static void task_level_cb(struct _lv_task_t *task)
{
	struct radio_player *player = get_radio_player();

	lv_bar_set_value(player->bar_level, audio_buffer_level(), LV_ANIM_ON);
}

static void setup_new_screen(struct radio_player *player)
{
	if (player->scr)
		lv_obj_del(player->scr);
	player->scr = lv_obj_create(NULL, NULL);
	assert(player->scr);
	lv_obj_set_user_data(player->scr, &player->cbs);
	lv_disp_load_scr(player->scr);
}

static void radio_player_track_info_cb(void *hdl, char *track_title)
{
	struct radio_player *player = hdl;

	lv_label_set_text(player->msg_label, track_title);
}

static void radio_player_screen(struct radio_player *player, const char *radio_label,
				const char *url, const char * port_nb, const char *path,
				int rate, int meta, int anti_ad)
{
	const int sizes[RADIO_PLAYER_NB][2] = {
		{60, 55}, {60, 55}, {60, 55}, {60, 55}
	};
	const lv_point_t pos[RADIO_PLAYER_NB] = {
		{20, 24}, {20, 168}, {240, 24}, {240, 168}
	};
	const char *labels[RADIO_PLAYER_NB] = {
		"MENU", "BACK", "UP", "DOWN"
	};
	int i;

	setup_new_screen(player);

	for (i = 0; i < RADIO_PLAYER_NB; i++) {
		player->btn[i] = lv_btn_create(player->scr, NULL);
		assert(player->btn[i]);
		lv_obj_set_size(player->btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(player->btn[i], pos[i].x, pos[i].y);
		player->label[i] = lv_label_create(player->btn[i], NULL);
		assert(player->label);
		lv_label_set_text(player->label[i], labels[i]);
		lv_obj_set_event_cb(player->btn[i], radio_player_event_cb);
	}

	player->msg_label = lv_label_create(player->scr, NULL);
	assert(player->msg_label);
	lv_obj_set_style_local_text_font(player->msg_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &lv_font_rv3_24);
	lv_obj_align(player->msg_label, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_auto_realign(player->msg_label, true);
	lv_label_set_align(player->msg_label, LV_LABEL_ALIGN_CENTER);
	lv_label_set_long_mode(player->msg_label, LV_LABEL_LONG_BREAK);
	lv_label_set_text(player->msg_label, radio_label);
	lv_obj_set_width(player->msg_label, 140);

	player->bar_level = lv_bar_create(player->scr, NULL);
	assert(player->bar_level);
	lv_bar_set_range(player->bar_level, 0, 100);
	lv_obj_set_size(player->bar_level, 15, 225);
	lv_obj_set_pos(player->bar_level, 0, 15);
	//lv_bar_set_style(player->bar_level, LV_BAR_STYLE_BG, &lv_style_transp);

	player->sound_level = lv_bar_create(player->scr, NULL);
	assert(player->sound_level);
	lv_bar_set_range(player->sound_level, 0, audio_sound_get_max_level());
	lv_obj_set_size(player->sound_level, 15, 225);
	lv_obj_set_pos(player->sound_level, 305, 15);
	//lv_bar_set_style(player->sound_level, LV_BAR_STYLE_BG, &lv_style_transp);
	lv_bar_set_value(player->sound_level, audio_sound_get_level(), LV_ANIM_OFF);

	player->task_level = lv_task_create(task_level_cb, 250, LV_TASK_PRIO_LOW, NULL);
	assert(player->task_level);

	audio_radio_play((char *) url, (char *) port_nb, (char *) path, rate, meta,
			 anti_ad, player, radio_player_track_info_cb);
}

ui_hdl radio_player_create(const char *radio_label, const char *url,
			   const char *port_nb, const char *path, int rate,
			   int meta, int anti_ad)
{
	struct radio_player *player;

	player = malloc(sizeof(*player));
	if (!player)
		return NULL;
	memset(player, 0, sizeof(*player));

	player->prev_scr = lv_disp_get_scr_act(NULL);
	player->cbs.destroy_chained = destroy_chained;
	radio_player_screen(player, radio_label, url, port_nb, path, rate, meta, anti_ad);
	system_menu_set_user_label("");

	return &player->cbs;
}
