#include "bluetooth.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_bt_device.h"
#include "esp_avrc_api.h"
#include "esp_log.h"

static const char* TAG = "rv3.bluetooth";
#define MIN(a,b)				((a)<(b)?(a):(b))

static struct {
	int is_init;
	int is_enable;
	int is_connected;
	esp_bd_addr_t connected_device;
	int tl;
	int is_avrc_connected;
	bt_audio_cfg_cb audio_cfg_cb;
	bt_audio_data_cb audio_data_cb;
	/* track info cb */
	void *hdl_cb;
	audio_track_info_cb track_info_cb;
} bt;

static int get_tl()
{
	int tl = bt.tl;

	bt.tl = (bt.tl + 1) % 16;

	return tl;
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
	switch (event) {
	case ESP_BT_GAP_AUTH_CMPL_EVT:
		ESP_LOGI(TAG, "authentication success: %s", param->auth_cmpl.device_name);
		break;
	default:
		ESP_LOGW(TAG, "Got bt gap unsupported event: %d", event);
	}
}

static void bt_app_a2d_conn_cb(struct a2d_conn_stat_param *conn_stat)
{
	ESP_LOGI(TAG, "state = %d", conn_stat->state);
	if (conn_stat->state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
		if (bt.is_enable)
			esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
		bt.is_connected = 0;
	}
	else if (conn_stat->state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
		bt.is_connected = 1;
		memcpy(&bt.connected_device, &conn_stat->remote_bda, sizeof(esp_bd_addr_t));
		esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_NONE);
	}
}

static int decode_sbc_sample_rate(uint8_t data)
{
	if (data & (1 << 7))
		return 16000;
	if (data & (1 << 6))
		return 32000;
	if (data & (1 << 5))
		return 44100;
	if (data & (1 << 4))
		return 48000;

	ESP_LOGW(TAG, "Unable to decode sample rate from 0x%02x, set 44100 Hz", data);
	return 44100;
}

static int decode_sbc_channel_nb(uint8_t data)
{
	if (data & (1 << 3))
		return 1;

	return 2;
}

static void bt_app_a2d_audio_cfg_cb(struct a2d_audio_cfg_param *audio_cfg)
{
	int sample_rate;
	int channel_nb;

	if (audio_cfg->mcc.type != ESP_A2D_MCT_SBC) {
		ESP_LOGE(TAG, " unsupported codec %d\n", audio_cfg->mcc.type);
		return ;
	}

	sample_rate = decode_sbc_sample_rate(audio_cfg->mcc.cie.sbc[0]);
	channel_nb = decode_sbc_channel_nb(audio_cfg->mcc.cie.sbc[0]);

	if (channel_nb == 1) {
		ESP_LOGE(TAG, "Unsupported single channel mode");
		return ;
	}

	if (bt.audio_cfg_cb)
		bt.audio_cfg_cb(sample_rate);
}

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
	switch (event) {
	case ESP_A2D_CONNECTION_STATE_EVT:
		bt_app_a2d_conn_cb(&param->conn_stat);
		break;
	case ESP_A2D_AUDIO_CFG_EVT:
		bt_app_a2d_audio_cfg_cb(&param->audio_cfg);
		break;
	default:
		ESP_LOGI(TAG, "Got a2dp unsupported event: %d", event);
	}
}

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
	if (bt.audio_data_cb)
		bt.audio_data_cb(data, len);
}

/* avrc stuff */
static void bt_av_new_track_event()
{
	int ret;

	if (!bt.is_avrc_connected)
		return ;

	ESP_LOGI(TAG, "request track change notification");
	ret = esp_avrc_ct_send_metadata_cmd(get_tl(), ESP_AVRC_MD_ATTR_TITLE);
	if (ret)
		ESP_LOGE(TAG, "Unable torequest title info");
	ret = esp_avrc_ct_send_register_notification_cmd(get_tl(), ESP_AVRC_RN_TRACK_CHANGE, 0);
	if (ret)
		ESP_LOGE(TAG, "Unable torequest track change");
}

static void bt_app_rc_ct_state_cb(struct avrc_ct_conn_stat_param *conn_stat)
{
	bt.is_avrc_connected = conn_stat->connected;
	/* kick off track changes if connected */
	ESP_LOGI(TAG, "bt.is_avrc_connected = %d", bt.is_avrc_connected);
	if (bt.is_avrc_connected)
		bt_av_new_track_event();
}

static void bt_app_rc_ct_features_cb(struct avrc_ct_rmt_feats_param *rmt_feats)
{
	ESP_LOGD(TAG, "avrc features mask 0x%08x", rmt_feats->feat_mask);
}

static void bt_app_rc_ct_metadata_title_cb(char *label, int len)
{
	char title[128] = {0};

	if (!bt.is_avrc_connected)
		return ;

	strncpy(title, label, MIN(sizeof(title) - 1, len));
	if (bt.track_info_cb)
		bt.track_info_cb(bt.hdl_cb, title);
}

static void bt_app_rc_ct_metadata_cb(struct avrc_ct_meta_rsp_param *meta_rsp)
{
	switch (meta_rsp->attr_id) {
	case ESP_AVRC_MD_ATTR_TITLE:
		bt_app_rc_ct_metadata_title_cb((char *) meta_rsp->attr_text,
					       meta_rsp->attr_length);
		break;
	default:
		ESP_LOGI(TAG, "unsupported metadata id %d", meta_rsp->attr_id);
	}
}

static void bt_app_rc_ct_notify_cb(struct avrc_ct_change_notify_param *change_ntf)
{
	uint8_t event_id = change_ntf->event_id;
	ESP_LOGD(TAG, "avrc notification %d %d", change_ntf->event_id, change_ntf->event_parameter);

	switch (event_id) {
	case ESP_AVRC_RN_TRACK_CHANGE:
		bt_av_new_track_event();
		break;
	default:
		ESP_LOGI(TAG, "Got avrc unsupported notify event: %d", event_id);
	}
}

static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
	switch (event) {
	case ESP_AVRC_CT_CONNECTION_STATE_EVT:
		bt_app_rc_ct_state_cb(&param->conn_stat);
		break;
	/*case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
		bt_app_rc_ct_response_cb(&param->psth_rsp);
		break;*/
	case ESP_AVRC_CT_METADATA_RSP_EVT:
		bt_app_rc_ct_metadata_cb(&param->meta_rsp);
		break;
	case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
		bt_app_rc_ct_features_cb(&param->rmt_feats);
		break;
	case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
		bt_app_rc_ct_notify_cb(&param->change_ntf);
		break;
	default:
		ESP_LOGI(TAG, "Got avrc unsupported event: %d", event);
	}
}

int bluetooth_init(char *name)
{
	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
	esp_bt_pin_code_t pin_code = {0};
	int ret;

	ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
	if (ret)
		return ret;

	ret = esp_bt_controller_init(&bt_cfg);
	if (ret)
		return ret;

	ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
	if (ret)
		goto esp_bt_controller_enable_error;

 	ret = esp_bluedroid_init();
	if (ret)
		goto esp_bluedroid_init_error;

	ret = esp_bluedroid_enable();
	if (ret)
		goto esp_bluedroid_enable_error;

	ret = esp_bt_gap_set_pin(pin_type, 4, pin_code);
	if (ret)
		goto deinit;

	ret = esp_bt_dev_set_device_name(name);
	if (ret)
		goto deinit;

	ret = esp_bt_gap_register_callback(bt_app_gap_cb);
	if (ret)
		goto deinit;

	ret = esp_a2d_register_callback(bt_app_a2d_cb);
	if (ret)
		goto deinit;

	ret = esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);
	if (ret)
		goto deinit;

	ret = esp_a2d_sink_init();
	if (ret)
		goto deinit;

	ret = esp_avrc_ct_init();
	if (ret)
		goto deinit_sink;

	ret = esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
	if (ret)
		goto deinit_avrc;

	bt.is_init = 1;
	ESP_LOGI(TAG, "init done\n");

	return 0;

deinit_avrc:
	esp_avrc_ct_deinit();
deinit_sink:
	esp_a2d_sink_deinit();
deinit:
	esp_bluedroid_disable();
esp_bluedroid_enable_error:
	esp_bluedroid_deinit();
esp_bluedroid_init_error:
	esp_bt_controller_disable();
esp_bt_controller_enable_error:
	esp_bt_controller_deinit();

	return ret;
}

int bluetooth_enable(bt_audio_cfg_cb audio_cfg_cb, bt_audio_data_cb audio_data_cb,
		     void *hdl_cb, audio_track_info_cb track_info_cb)
{
	if (!bt.is_init)
		return -1;

	if (bt.is_enable)
		return 0;

	bt.audio_cfg_cb = audio_cfg_cb;
	bt.audio_data_cb = audio_data_cb;
	bt.hdl_cb = hdl_cb;
	bt.track_info_cb = track_info_cb;
	bt.is_enable = 1;
	ESP_LOGI(TAG, "set ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE");
	esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);

	return 0;
}

int bluetooth_disable()
{
	int ret;

	if (!bt.is_enable)
		return 0;

	bt.is_enable = 0;
	esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_NONE);
	if (bt.is_connected) {
		ret = esp_a2d_sink_disconnect(bt.connected_device);
		if (ret) {
			ESP_LOGE(TAG, "disconnect error => %d\n", ret);
			return -1;
		}
	}
	bt.is_connected = 0;
	bt.audio_cfg_cb = NULL;
	bt.audio_data_cb = NULL;
	bt.hdl_cb = NULL;
	bt.track_info_cb = NULL;

	return 0;
}

int bluetooth_cmd_next()
{
	int ret;

	if (!bt.is_avrc_connected)
		return -1;

	ret = esp_avrc_ct_send_passthrough_cmd(get_tl(), ESP_AVRC_PT_CMD_FORWARD,
					       ESP_AVRC_PT_CMD_STATE_PRESSED);
	if (ret)
		return ret;

	ret = esp_avrc_ct_send_passthrough_cmd(get_tl(), ESP_AVRC_PT_CMD_FORWARD,
					       ESP_AVRC_PT_CMD_STATE_RELEASED);
	if (ret)
		return ret;

	return 0;
}

int bluetooth_get_state()
{
	if (!bt.is_enable)
		return BLUETOOTH_DISABLE;

	return bt.is_connected ? BLUETOOTH_CONNECTED : BLUETOOTH_DISCOVERABLE;
}
