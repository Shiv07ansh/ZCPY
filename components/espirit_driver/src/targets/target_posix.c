#if !defined(ESP_PLATFORM)

#include "dma_ringbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int8_t sim_buf_a[DMA_RINGBUF_FRAME_SIZE];
static int8_t sim_buf_b[DMA_RINGBUF_FRAME_SIZE];
static bool toggle = false;

bool target_hw_init(const dma_ringbuf_config_t *config) {
    (void)config;
    memset(sim_buf_a, 0x01, DMA_RINGBUF_FRAME_SIZE);
    memset(sim_buf_b, 0x02, DMA_RINGBUF_FRAME_SIZE);
    printf("[POSIX_SIM] Simulated DMA Driver Initialized.\n");
    return true;
}

int8_t* target_hw_get_frame(uint32_t timeout_ms) {
    (void)timeout_ms;
    toggle = !toggle;
    return toggle ? sim_buf_a : sim_buf_b;
}

void target_hw_stop(void) {
    printf("[POSIX_SIM] Simulated DMA Driver Stopped.\n");
}

#endif // !ESP_PLATFORM