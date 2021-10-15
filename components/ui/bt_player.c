#include "bt_player.h"

#include <stdio.h>
#include <assert.h>

#include "lvgl.h"
#include "esp_log.h"

#include "audio.h"

#include "system_menu.h"
#include "fonts.h"

static const char* TAG = "rv3.bt_player";

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

enum bt_player_button {
	BT_PLAYER_MENU,
	BT_PLAYER_BACK,
	BT_PLAYER_UP,
	BT_PLAYER_DOWN,
	BT_PLAYER_NEXT,
	BT_PLAYER_NB
};

struct bt_player {
	struct ui_cbs cbs;
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *btn[BT_PLAYER_NB];
	lv_obj_t *label[BT_PLAYER_NB];
	lv_obj_t *sound_level;
	lv_obj_t *msg_label;
};

static inline struct bt_player *get_bt_player()
{
	struct ui_cbs *cbs = lv_obj_get_user_data(lv_disp_get_scr_act(NULL));
	return container_of(cbs, struct bt_player, cbs);
}

static void bt_player_track_info_cb(void *hdl, char *track_title)
{
	struct bt_player *player = hdl;

	ESP_LOGI(TAG, "new track %s", track_title);
	lv_label_set_text(player->msg_label, track_title);
}

static void bt_player_destroy(struct bt_player *player)
{
	ESP_LOGI(TAG, "stop bluetooth stack\n");
	audio_bluetooth_stop();
	lv_obj_del(player->scr);
	free(player);
}

static void handle_back_event(struct bt_player *player)
{
	ui_hdl prev = lv_obj_get_user_data(player->prev_scr);

	lv_disp_load_scr(player->prev_scr);
	if (prev->restore_event)
		prev->restore_event(prev);

	bt_player_destroy(player);
}

static void destroy_chained(struct ui_cbs *cbs)
{
	struct bt_player *player = container_of(cbs, struct bt_player, cbs);
	ui_hdl prev;

	prev = lv_obj_get_user_data(player->prev_scr);
	prev->destroy_chained(prev);

	bt_player_destroy(player);
}

static enum bt_player_button get_btn_id_from_obj(struct bt_player *player, lv_obj_t *btn)
{
	int i;

	for (i = 0; i < BT_PLAYER_NB; i++) {
		if (player->btn[i] == btn)
			return i;
	}

	return BT_PLAYER_NB;
}

static void bt_player_event_cb(lv_obj_t *btn, lv_event_t event)
{
	struct bt_player *player = get_bt_player();
	enum bt_player_button btn_id = get_btn_id_from_obj(player, btn);

	if( event != LV_EVENT_CLICKED && event != LV_EVENT_LONG_PRESSED_REPEAT)
		return;
	if (event == LV_EVENT_LONG_PRESSED_REPEAT && btn_id != BT_PLAYER_UP && btn_id != BT_PLAYER_DOWN)
		return;

	switch (btn_id) {
	case BT_PLAYER_MENU:
		destroy_chained(&player->cbs);
		break;
	case BT_PLAYER_BACK:
		handle_back_event(player);
		break;
	case BT_PLAYER_UP:
		lv_bar_set_value(player->sound_level,  audio_sound_level_up(),
				 LV_ANIM_ON);
		break;
	case BT_PLAYER_DOWN:
		lv_bar_set_value(player->sound_level,  audio_sound_level_down(),
				 LV_ANIM_ON);
		break;
	case BT_PLAYER_NEXT:
		ESP_LOGI(TAG,"ask next");
		audio_bluetooth_next();
		break;
	default:
		assert(0);
	}
}

static void setup_new_screen(struct bt_player *player)
{
	if (player->scr)
		lv_obj_del(player->scr);
	player->scr = lv_obj_create(NULL, NULL);
	assert(player->scr);
	lv_obj_set_user_data(player->scr, &player->cbs);
	lv_disp_load_scr(player->scr);
}

static void music_player_screen(struct bt_player *player)
{
	const int sizes[BT_PLAYER_NB][2] = {
		{60, 55}, {60, 55}, {60, 55}, {60, 55}, {60, 55}
	};
	const lv_point_t pos[BT_PLAYER_NB] = {
		{20, 24}, {20, 168}, {240, 24}, {240, 168}, {20, 120 - 27}
	};
	const char *labels[BT_PLAYER_NB] = {
		"MENU", "BACK", "UP", "DOWN", "NEXT"
	};
	int i;

	setup_new_screen(player);

	for (i = 0; i < BT_PLAYER_NB; i++) {
		player->btn[i] = lv_btn_create(player->scr, NULL);
		assert(player->btn[i]);
		lv_obj_set_size(player->btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(player->btn[i], pos[i].x, pos[i].y);
		player->label[i] = lv_label_create(player->btn[i], NULL);
		assert(player->label);
		lv_label_set_text(player->label[i], labels[i]);
		lv_obj_set_event_cb(player->btn[i], bt_player_event_cb);
	}

	player->msg_label = lv_label_create(player->scr, NULL);
	assert(player->msg_label);
	lv_label_set_text(player->msg_label, "");
	lv_obj_set_style_local_text_font(player->msg_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &lv_font_rv3_24);
	lv_obj_align(player->msg_label, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_auto_realign(player->msg_label, true);
	lv_label_set_align(player->msg_label, LV_LABEL_ALIGN_CENTER);
	lv_label_set_long_mode(player->msg_label, LV_LABEL_LONG_BREAK);
	lv_obj_set_width(player->msg_label, 140);

	player->sound_level = lv_bar_create(player->scr, NULL);
	assert(player->sound_level);
	lv_bar_set_range(player->sound_level, 0, audio_sound_get_max_level());
	lv_obj_set_size(player->sound_level, 15, 225);
	lv_obj_set_pos(player->sound_level, 305, 15);
	//lv_bar_set_style(player->sound_level, LV_BAR_STYLE_BG, &lv_style_transp);
	lv_bar_set_value(player->sound_level, audio_sound_get_level(), LV_ANIM_OFF);

	ESP_LOGI(TAG, "start bluetooth stack\n");
	audio_bluetooth_play(player, bt_player_track_info_cb);
}


ui_hdl bt_player_create()
{
	struct bt_player *player;

	player = calloc(1, sizeof(*player));
	if (!player)
		return NULL;

	player->prev_scr = lv_disp_get_scr_act(NULL);
	player->cbs.destroy_chained = destroy_chained;
	music_player_screen(player);
	system_menu_set_user_label("");

	return &player->cbs;
}
