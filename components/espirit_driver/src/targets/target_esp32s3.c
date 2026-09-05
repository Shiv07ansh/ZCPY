#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ESP_PLATFORM)

#include "dma_ringbuf.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "DMA_TARGET_ESP32S3";

// ESP32-S3 specific configuration payload
typedef struct {
    int gpio_bclk;
    int gpio_ws;
    int gpio_din;
} esp32s3_i2s_config_t;

// Aligned double-buffers placed in internal DMA SRAM
static DMA_ATTR __attribute__((aligned(32))) int8_t buf_a[DMA_RINGBUF_FRAME_SIZE];
static DMA_ATTR __attribute__((aligned(32))) int8_t buf_b[DMA_RINGBUF_FRAME_SIZE];

static i2s_chan_handle_t rx_chan = NULL;
static QueueHandle_t frame_queue = NULL;

static bool IRAM_ATTR esp32s3_dma_rx_callback(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx) {
    BaseType_t high_task_woken = pdFALSE;
    xQueueSendFromISR(frame_queue, &event->data, &high_task_woken);
    return high_task_woken == pdTRUE;
}

bool target_hw_init(const dma_ringbuf_config_t *config) {
    frame_queue = xQueueCreate(2, sizeof(void*));
    if (!frame_queue) return false;

    esp32s3_i2s_config_t *esp_cfg = (esp32s3_i2s_config_t*)config->target_config;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, NULL, &rx_chan) != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate_hz),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = esp_cfg ? esp_cfg->gpio_bclk : 4,
            .ws   = esp_cfg ? esp_cfg->gpio_ws   : 5,
            .dout = I2S_GPIO_UNUSED,
            .din  = esp_cfg ? esp_cfg->gpio_din  : 6,
        },
    };

    if (i2s_channel_init_std_rx(rx_chan, &std_cfg) != ESP_OK) return false;

    i2s_event_callbacks_t cbs = { .on_recv = esp32s3_dma_rx_callback };
    i2s_channel_register_event_callbacks(rx_chan, &cbs, NULL);
    i2s_channel_enable(rx_chan);

    ESP_LOGI(TAG, "ESP32-S3 Zero-Copy DMA Driver initialized.");
    return true;
}

int8_t* target_hw_get_frame(uint32_t timeout_ms) {
    int8_t *frame_ptr = NULL;
    if (xQueueReceive(frame_queue, &frame_ptr, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return frame_ptr;
    }
    return NULL;
}

void target_hw_stop(void) {
    if (rx_chan) {
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        rx_chan = NULL;
    }
    if (frame_queue) {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
    }
}

#endif // ESP_PLATFORM