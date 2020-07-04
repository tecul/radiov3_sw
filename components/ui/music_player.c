#include "music_player.h"

#include <assert.h>

#include "lvgl/lvgl.h"
#include "esp_log.h"

#include "audio.h"
#include "playlist.h"

static const char* TAG = "rv3.music_player";

#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

enum music_player_button {
	MUSIC_PLAYER_MENU,
	MUSIC_PLAYER_BACK,
	MUSIC_PLAYER_UP,
	MUSIC_PLAYER_DOWN,
	MUSIC_PLAYER_NEXT,
	MUSIC_PLAYER_NB
};

enum music_player_state {
	STATE_INIT,
	STATE_WAIT_SONG_START,
	STATE_SONG_RUNNING,
	STATE_SONG_NEXT,
	STATE_PLAYLIST_DONE
};

struct music_player {
	struct ui_cbs cbs;
	lv_obj_t *prev_scr;
	lv_obj_t *scr;
	lv_obj_t *btn[MUSIC_PLAYER_NB];
	lv_obj_t *label[MUSIC_PLAYER_NB];
	lv_obj_t *msg_label;
	lv_obj_t *bar_level;
	lv_obj_t *sound_level;
	lv_style_t label_style;
	lv_task_t *task_level;
	void *playlist_hdl;
	enum music_player_state state;
};

static inline struct music_player *get_music_player()
{
	struct ui_cbs *cbs = lv_obj_get_user_data(lv_disp_get_scr_act(NULL));
	return container_of(cbs, struct music_player, cbs);
}

static void music_player_destroy(struct music_player *player)
{
	playlist_destroy(player->playlist_hdl);
	lv_task_del(player->task_level);
	audio_music_stop();
	lv_obj_del(player->scr);
	free(player);
}

static void handle_back_event(struct music_player *player)
{
	lv_disp_load_scr(player->prev_scr);
	music_player_destroy(player);
}

static void destroy_chained(struct ui_cbs *cbs)
{
	struct music_player *player = container_of(cbs, struct music_player, cbs);
	ui_hdl prev;

	prev = lv_obj_get_user_data(player->prev_scr);
	prev->destroy_chained(prev);

	music_player_destroy(player);
}

static enum music_player_button get_btn_id_from_obj(struct music_player *player, lv_obj_t *btn)
{
	int i;

	for (i = 0; i < MUSIC_PLAYER_NB; i++) {
		if (player->btn[i] == btn)
			return i;
	}

	return MUSIC_PLAYER_NB;
}

static void music_player_event_cb(lv_obj_t *btn, lv_event_t event)
{
	struct music_player *player = get_music_player();
	enum music_player_button btn_id = get_btn_id_from_obj(player, btn);

	if( event != LV_EVENT_CLICKED && event != LV_EVENT_LONG_PRESSED_REPEAT)
		return;
	if (event == LV_EVENT_LONG_PRESSED_REPEAT && btn_id != MUSIC_PLAYER_UP && btn_id != MUSIC_PLAYER_DOWN)
		return;

	switch (btn_id) {
	case MUSIC_PLAYER_MENU:
		destroy_chained(&player->cbs);
		break;
	case MUSIC_PLAYER_BACK:
		handle_back_event(player);
		break;
	case MUSIC_PLAYER_UP:
		lv_bar_set_value(player->sound_level,  audio_sound_level_up(),
				 LV_ANIM_ON);
		break;
	case MUSIC_PLAYER_DOWN:
		lv_bar_set_value(player->sound_level,  audio_sound_level_down(),
				 LV_ANIM_ON);
		break;
	case MUSIC_PLAYER_NEXT:
		player->state = STATE_SONG_NEXT;
		break;
	default:
		assert(0);
	}
}

static void start_next_song(struct music_player *player)
{
	struct playlist_item item;
	int ret;

	ret = playlist_next(player->playlist_hdl, &item);
	ESP_LOGI(TAG, "start_next_song ret = %d\n", ret);
	if (ret) {
		player->state = STATE_PLAYLIST_DONE;
		return ;
	}
	player->state = STATE_WAIT_SONG_START;
	lv_label_set_text(player->msg_label, item.meta.title);
	audio_music_play(item.filepath);
	playlist_put_item(player->playlist_hdl, &item);
}

static void update_state(struct music_player *player)
{
	switch (player->state) {
	case STATE_INIT:
		break;
	case STATE_WAIT_SONG_START:
		if (audio_buffer_level())
			player->state = STATE_SONG_RUNNING;
		break;
	case STATE_SONG_RUNNING:
		if (audio_buffer_level() == 0) {
			audio_music_stop();
			start_next_song(player);
		}
		break;
	case STATE_PLAYLIST_DONE:
		handle_back_event(player);
		break;
	case STATE_SONG_NEXT:
		audio_music_stop();
		start_next_song(player);
		break;
	default:
		assert(0);
	}
}

static void task_level_cb(struct _lv_task_t *task)
{
	struct music_player *player = get_music_player();

	update_state(player);
	lv_bar_set_value(player->bar_level, audio_buffer_level(), LV_ANIM_ON);
}

static void setup_new_screen(struct music_player *player)
{
	if (player->scr)
		lv_obj_del(player->scr);
	player->scr = lv_obj_create(NULL, NULL);
	assert(player->scr);
	lv_obj_set_user_data(player->scr, &player->cbs);
	lv_disp_load_scr(player->scr);
}

static void music_player_screen(struct music_player *player)
{
	const int sizes[MUSIC_PLAYER_NB][2] = {
		{60, 55}, {60, 55}, {60, 55}, {60, 55}, {60, 55}
	};
	const lv_point_t pos[MUSIC_PLAYER_NB] = {
		{20, 24}, {20, 168}, {240, 24}, {240, 168}, {20, 120 - 27}
	};
	const char *labels[MUSIC_PLAYER_NB] = {
		"MENU", "BACK", "UP", "DOWN", "NEXT"
	};
	int i;

	setup_new_screen(player);

	lv_style_copy(&player->label_style, &lv_style_plain);
	player->label_style.text.font = &lv_font_roboto_28;

	for (i = 0; i < MUSIC_PLAYER_NB; i++) {
		player->btn[i] = lv_btn_create(player->scr, NULL);
		assert(player->btn[i]);
		lv_obj_set_size(player->btn[i], sizes[i][0], sizes[i][1]);
		lv_obj_set_pos(player->btn[i], pos[i].x, pos[i].y);
		player->label[i] = lv_label_create(player->btn[i], NULL);
		assert(player->label);
		lv_label_set_text(player->label[i], labels[i]);
		lv_obj_set_event_cb(player->btn[i], music_player_event_cb);
	}

	player->msg_label = lv_label_create(player->scr, NULL);
	assert(player->msg_label);
	lv_label_set_style(player->msg_label, LV_LABEL_STYLE_MAIN, &player->label_style);
	lv_obj_align(player->msg_label, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_auto_realign(player->msg_label, true);
	lv_label_set_align(player->msg_label, LV_LABEL_ALIGN_CENTER);
	lv_label_set_long_mode(player->msg_label, LV_LABEL_LONG_BREAK);
	lv_obj_set_width(player->msg_label, 140);

	player->bar_level = lv_bar_create(player->scr, NULL);
	assert(player->bar_level);
	lv_bar_set_range(player->bar_level, 0, 100);
	lv_obj_set_size(player->bar_level, 15, 225);
	lv_obj_set_pos(player->bar_level, 0, 15);
	lv_bar_set_style(player->bar_level, LV_BAR_STYLE_BG, &lv_style_transp);

	player->sound_level = lv_bar_create(player->scr, NULL);
	assert(player->sound_level);
	lv_bar_set_range(player->sound_level, 0, audio_sound_get_max_level());
	lv_obj_set_size(player->sound_level, 15, 225);
	lv_obj_set_pos(player->sound_level, 305, 15);
	lv_bar_set_style(player->sound_level, LV_BAR_STYLE_BG, &lv_style_transp);
	lv_bar_set_value(player->sound_level, audio_sound_get_level(), LV_ANIM_OFF);

	player->task_level = lv_task_create(task_level_cb, 250, LV_TASK_PRIO_LOW, NULL);
	assert(player->task_level);

	start_next_song(player);
}

ui_hdl music_player_create(void *playlist_hdl)
{
	struct music_player *player;

	player = malloc(sizeof(*player));
	if (!player)
		return NULL;
	memset(player, 0, sizeof(*player));

	player->playlist_hdl = playlist_hdl;
	player->state = STATE_INIT;
	player->prev_scr = lv_disp_get_scr_act(NULL);
	player->cbs.destroy_chained = destroy_chained;
	music_player_screen(player);

	return &player->cbs;
}
