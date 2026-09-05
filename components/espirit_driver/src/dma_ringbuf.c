#include "dma_ringbuf.h"
#include <stdio.h>

// Weak symbol declarations for target backends
__attribute__((weak)) bool target_hw_init(const dma_ringbuf_config_t *config) { (void)config; return false; }
__attribute__((weak)) int8_t* target_hw_get_frame(uint32_t timeout_ms) { (void)timeout_ms; return NULL; }
__attribute__((weak)) void target_hw_stop(void) {}

static bool g_initialized = false;

bool dma_ringbuf_init(const dma_ringbuf_config_t *config) {
    if (!config || config->frame_size_bytes == 0) {
        return false;
    }
    
    g_initialized = target_hw_init(config);
    return g_initialized;
}

int8_t* dma_ringbuf_get_next_frame(uint32_t timeout_ms) {
    if (!g_initialized) {
        return NULL;
    }
    return target_hw_get_frame(timeout_ms);
}

void dma_ringbuf_stop(void) {
    if (g_initialized) {
        target_hw_stop();
        g_initialized = false;
    }
}