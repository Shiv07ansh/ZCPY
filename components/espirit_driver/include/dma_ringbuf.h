#ifndef DMA_RINGBUF_H
#define DMA_RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Platform-independent buffer configuration
typedef struct {
    size_t   frame_size_bytes;
    uint32_t sample_rate_hz;
    void*    target_hardware_cfg; // Pointer to chip-specific struct (e.g., ESP32 pinout or STM32 DMA_HandleTypeDef)
} dma_ringbuf_config_t;

// Universal Interface Functions
bool dma_ringbuf_init(const dma_ringbuf_config_t *config);
int8_t* dma_ringbuf_get_next_frame(uint32_t timeout_ms);

#endif // DMA_RINGBUF_H