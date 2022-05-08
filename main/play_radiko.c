/* Play M3U HTTP Living stream

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "aac_decoder.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "radiko.h"
#include "esp_peripherals.h"
#include "periph_touch.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "board.h"
#include "periph_wifi.h"
#include "board.h"
#include "input_key_service.h"

#if __has_include("esp_idf_version.h")
#include "esp_idf_version.h"
#else
#define ESP_IDF_VERSION_VAL(major, minor, patch) 1
#endif

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif
static const char *TAG = "MAIN";

audio_pipeline_handle_t pipeline;
audio_element_handle_t http_stream_reader, i2s_stream_writer, aac_decoder;

int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    if(msg->event_id == HTTP_STREAM_ON_RESPONSE)
    {
        return ESP_OK;
    }
    else if (msg->event_id == HTTP_STREAM_RESOLVE_ALL_TRACKS) 
    {
        return ESP_OK;
    }
    else if (msg->event_id == HTTP_STREAM_FINISH_TRACK) 
    {
        ESP_LOGE(TAG, "Finish track");
        return http_stream_next_track(msg->el);
    }
    else if (msg->event_id == HTTP_STREAM_FINISH_PLAYLIST) 
    {
        ESP_LOGW(TAG, "Reloading");
        return http_stream_fetch_again(msg->el);
    }
    else
    {
        ESP_LOGD(TAG, "unhandled id: %d", msg->event_id);
    }
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "Initializing board");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
#ifdef CONFIG_M5StickC_SPK_HAT
    ESP_LOGW(TAG, "Volume control is not supported");
#else
    int player_volume;
    audio_hal_set_volume(board_handle->audio_hal, 50);
    audio_hal_get_volume(board_handle->audio_hal, &player_volume);
#endif
    int current_station = 0; 

    ESP_LOGI(TAG, "Initializing audio pipeline");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    ESP_LOGI(TAG, "Initializing HLS Player");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = _http_stream_event_handle;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;
    http_cfg.auto_connect_next_track = false;
#ifdef CONFIG_M5StickC_SPK_HAT
    http_cfg.out_rb_size = 1024 * 4;
    http_cfg.task_stack = 1024 * 4;
#endif
    http_stream_reader = http_stream_init(&http_cfg);
    audio_pipeline_register(pipeline, http_stream_reader, "http");

    ESP_LOGI(TAG, "Initializing I2S DAC");
#ifdef CONFIG_M5StickC_SPK_HAT
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_INTERNAL_DAC_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "Initializing AAC Decoder");
    aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
    aac_cfg.task_core = 1;
#ifdef CONFIG_M5StickC_SPK_HAT
    aac_cfg.task_stack = 4096;
#endif
    aac_cfg.plus_enable = true;
    aac_decoder = aac_decoder_init(&aac_cfg);
    audio_pipeline_register(pipeline, aac_decoder, "aac");

    ESP_LOGI(TAG, "Linking audio pipeline");
    const char *link_tag[3] = {"http", "aac", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "Installing keys");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    audio_board_key_init(set);

    ESP_LOGI(TAG, "Starting Wi-Fi STA");
    periph_wifi_cfg_t wifi_cfg = 
    {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    ESP_LOGI(TAG, "Initializing radiko event group");
    init_radiko();

    ESP_LOGI(TAG, "Getting access token");
    xTaskCreate(auth, "auth", 4096, NULL, configMAX_PRIORITIES - 1, NULL);
    EventBits_t uxBits = xEventGroupWaitBits(radiko_task_group, AUTH_DONE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Getting station list");
    xTaskCreate(get_station_list, "get_station_list", 8192, NULL, configMAX_PRIORITIES - 1, NULL);
    uxBits = xEventGroupWaitBits(radiko_task_group, GOT_STATIONS_BIT, pdTRUE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Getting playlist url");
    xTaskCreate(generate_playlist_url, "generate_playlist_url", 4096, (void*)&stations[current_station], configMAX_PRIORITIES - 1, NULL);
    uxBits = xEventGroupWaitBits(radiko_task_group, PL_GEN_DONE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Registering playlist url: %s", _playlist_url);
    audio_element_set_uri(http_stream_reader, _playlist_url);

    ESP_LOGI(TAG, "Initializing event handler");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "Start to playing the stream");
    //vTaskDelay(10000 / portTICK_PERIOD_MS);
    audio_pipeline_run(pipeline);
    while (1) 
    {
        audio_event_iface_msg_t msg;
        //イベント受信待ち開始
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) 
        {
            continue;
        }

        //ボタンハンドラ
        if ((msg.source_type == PERIPH_ID_TOUCH || msg.source_type == PERIPH_ID_BUTTON || msg.source_type == PERIPH_ID_ADC_BTN)
            && (msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED || msg.cmd == PERIPH_ADC_BUTTON_PRESSED)) 
        {

            if ((int) msg.data == get_input_play_id()) 
            {
                audio_element_state_t el_state = audio_element_get_state(i2s_stream_writer);
                switch (el_state) 
                {
                    case AEL_STATE_INIT :
                        audio_pipeline_run(pipeline);
                        break;
                    case AEL_STATE_RUNNING :
                        audio_pipeline_pause(pipeline);
                        break;
                    case AEL_STATE_PAUSED :
                        audio_pipeline_stop(pipeline);
                        audio_pipeline_wait_for_stop(pipeline);
                        xTaskCreate(generate_playlist_url, "generate_playlist_url", 4096, (void*)&stations[current_station], configMAX_PRIORITIES - 1, NULL);
                        uxBits = xEventGroupWaitBits(radiko_task_group, PL_GEN_DONE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
                        audio_pipeline_reset_ringbuffer(pipeline);
                        audio_pipeline_reset_elements(pipeline);
                        audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
                        audio_element_set_uri(http_stream_reader, _playlist_url);
                        ESP_LOGE(TAG, "URL: %s", _playlist_url);
                        audio_pipeline_run(pipeline);
                        break;
                    case AEL_STATE_FINISHED :
                        xTaskCreate(generate_playlist_url, "generate_playlist_url", 4096, (void*)&stations[current_station], configMAX_PRIORITIES - 1, NULL);
                        uxBits = xEventGroupWaitBits(radiko_task_group, PL_GEN_DONE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
                        audio_pipeline_reset_ringbuffer(pipeline);
                        audio_pipeline_reset_elements(pipeline);
                        audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
                        audio_element_set_uri(http_stream_reader, _playlist_url);
                        audio_pipeline_run(pipeline);
                        break;
                    default :
                        audio_pipeline_wait_for_stop(pipeline);
                        xTaskCreate(generate_playlist_url, "generate_playlist_url", 4096, (void*)&stations[current_station], configMAX_PRIORITIES - 1, NULL);
                        uxBits = xEventGroupWaitBits(radiko_task_group, PL_GEN_DONE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
                        audio_pipeline_reset_ringbuffer(pipeline);
                        audio_pipeline_reset_elements(pipeline);
                        audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
                        audio_element_set_uri(http_stream_reader, _playlist_url);
                        audio_pipeline_run(pipeline);
                        break;
                }
            }
            //選局
            else if ((int) msg.data == get_input_mode_id()) 
            {
                audio_pipeline_stop(pipeline);
                audio_pipeline_wait_for_stop(pipeline);
                audio_element_reset_state(aac_decoder);
                audio_element_reset_state(i2s_stream_writer);
                audio_pipeline_reset_ringbuffer(pipeline);
                audio_pipeline_reset_items_state(pipeline);
                
                current_station ++;
                if(current_station >= (station_count - 1))
                {
                    current_station = 0;
                }
                xTaskCreate(generate_playlist_url, "generate_playlist_url", 4096, (void*)&stations[current_station], configMAX_PRIORITIES - 1, NULL);
                uxBits = xEventGroupWaitBits(radiko_task_group, PL_GEN_DONE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
                ESP_LOGI(TAG, "Setting playlist for the station: %s", ((stations + current_station)->name));
                audio_element_set_uri(http_stream_reader, _playlist_url);
                audio_pipeline_run(pipeline);
            } 
            else if ((int) msg.data == get_input_volup_id()) 
            {
#ifdef CONFIG_M5StickC_SPK_HAT
                ESP_LOGE(TAG, "Volume control is not supported");
#else
                player_volume += 5;
                if (player_volume > 100) 
                {
                    player_volume = 100;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
#endif
            } 
            else if ((int) msg.data == get_input_voldown_id()) 
            {
#ifdef CONFIG_M5StickC_SPK_HAT
                ESP_LOGE(TAG, "Volume control is not supported");
#else
                player_volume -= 10;
                if (player_volume < 0) {
                    player_volume = 0;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
#endif
            }
        }

        else if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
            && msg.source == (void*) aac_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) 
        {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(aac_decoder, &music_info);

            ESP_LOGI(TAG, "Receive music info from aac decoder, sample_rates=%d, bits=%d, ch=%d",
                    music_info.sample_rates, music_info.bits, music_info.channels);
            audio_element_setinfo(i2s_stream_writer, &music_info);
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }

        /* restart stream when the first pipeline element (http_stream_reader in this case) receives stop event (caused by reading errors) */
        else if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void*) http_stream_reader
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int) msg.data == AEL_STATUS_ERROR_OPEN) 
        {
            ESP_LOGW(TAG, "Restarting stream");
            audio_pipeline_stop(pipeline);
            audio_pipeline_wait_for_stop(pipeline);
            audio_element_reset_state(aac_decoder);
            audio_element_reset_state(i2s_stream_writer);
            audio_pipeline_reset_ringbuffer(pipeline);
            audio_pipeline_reset_items_state(pipeline);
            audio_pipeline_run(pipeline);
            continue;
        }
        else
        {
            audio_element_state_t el_state = audio_element_get_state(aac_decoder);
            if((int)el_state == AEL_STATUS_ERROR_OPEN || (int)el_state == AEL_STATUS_ERROR_UNKNOWN)
            {
                ESP_LOGE(TAG, "Failed to playing. Retrying...");
                vTaskDelay(250 / portTICK_PERIOD_MS);
                audio_pipeline_stop(pipeline);
                audio_pipeline_wait_for_stop(pipeline);
                //xTaskCreate(generate_playlist_url, "generate_playlist_url", 4096, (void*)&stations[current_station], configMAX_PRIORITIES - 1, NULL);
                ///uxBits = xEventGroupWaitBits(radiko_task_group, PL_GEN_DONE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
                audio_pipeline_reset_ringbuffer(pipeline);
                audio_pipeline_reset_elements(pipeline);
                audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
                //audio_element_set_uri(http_stream_reader, _playlist_url);
                audio_pipeline_run(pipeline);
            }
            else if((int)el_state == AEL_STATUS_ERROR_TIMEOUT)
            {
                ESP_LOGE(TAG, "Failed to playing. AEL_STATUS_ERROR_TIMEOUT Retrying...");
                audio_pipeline_stop(pipeline);
                audio_pipeline_wait_for_stop(pipeline);
                //xTaskCreate(generate_playlist_url, "generate_playlist_url", 4096, (void*)&stations[current_station], configMAX_PRIORITIES - 1, NULL);
                ///uxBits = xEventGroupWaitBits(radiko_task_group, PL_GEN_DONE_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
                audio_pipeline_reset_ringbuffer(pipeline);
                audio_pipeline_reset_elements(pipeline);
                audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
                //audio_element_set_uri(http_stream_reader, _playlist_url);
                audio_pipeline_run(pipeline);

            }
        }
    }
    ESP_LOGI(TAG, "Aborting audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, http_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, aac_decoder);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(http_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(aac_decoder);
    esp_periph_set_destroy(set);
}
