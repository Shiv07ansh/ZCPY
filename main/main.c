#include <stdio.h>
#include "dma_ringbuf.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"
#include "esp_log.h"
static const char *TAG = "MAIN_APP";
#endif

// Dummy in-place model execution routine
void process_model_inference_in_place(const int8_t *sensor_data) {
    volatile int32_t acc = 0;
    for (int i = 0; i < DMA_RINGBUF_FRAME_SIZE; i++) {
        acc += sensor_data[i] * 2;
    }
}

void run_pipeline(void) {
    dma_ringbuf_config_t cfg = {
        .platform = TARGET_HARDWARE_ESP32S3,
        .frame_size_bytes = DMA_RINGBUF_FRAME_SIZE,
        .sample_rate_hz = 16000,
        .target_config = NULL
    };

    if (!dma_ringbuf_init(&cfg)) {
        printf("Failed to initialize DMA Ring-Buffer Engine!\n");
        return;
    }

    printf("Zero-Copy DMA Streaming Pipeline Active.\n");

    for (int frame = 0; frame < 10; frame++) {
        // Block until DMA fills buffer (Zero CPU overhead while waiting)
        int8_t *frame_ptr = dma_ringbuf_get_next_frame(1000);

        if (frame_ptr != NULL) {
#if defined(ESP_PLATFORM)
            uint32_t start_cycles = esp_cpu_get_cycle_count();
            process_model_inference_in_place(frame_ptr);
            uint32_t elapsed_cycles = esp_cpu_get_cycle_count() - start_cycles;

            ESP_LOGI(TAG, "Frame %d @ %p | Cycles: %lu | Latency: %.2f us",
                     frame + 1, frame_ptr, elapsed_cycles, (float)elapsed_cycles / 240.0f);
#else
            process_model_inference_in_place(frame_ptr);
            printf("[HOST] Processed Frame %d at Address: %p (Sample Byte: 0x%02x)\n",
                   frame + 1, (void*)frame_ptr, frame_ptr[0]);
#endif
        }
    }

    dma_ringbuf_stop();
}

#if defined(ESP_PLATFORM)
void app_main(void) {
    run_pipeline();
}
#else
int main(void) {
    run_pipeline();
    return 0;
}
#endif