/**
 * stb_jpeg.cpp
 * Wrapper around stb_image for full progressive JPEG decoding.
 *
 * stb_image is a single-header library by Sean Barrett that handles both
 * baseline and progressive JPEG correctly — unlike JPEGDEC which forces
 * JPEG_SCALE_EIGHTH for progressive (DC-only, 1/8 resolution output).
 *
 * All allocations are redirected to PSRAM so the 300x300 intermediate
 * RGB888 buffer (~270KB) doesn't exhaust internal DMA SRAM.
 *
 * Entry point: decodeJPEGProgressiveStb()
 * — Called from ui_album_art.cpp when is_progressive_jpeg=true
 * — Returns RGB565 LE buffer in PSRAM (caller must heap_caps_free)
 */

#include <Arduino.h>
#include <esp_heap_caps.h>

// Redirect all stb_image heap allocations to PSRAM.
// RGB888 intermediate for 300x300 = 270KB; internal malloc would fail.
#define STBI_MALLOC(sz)       heap_caps_malloc((sz), MALLOC_CAP_SPIRAM)
#define STBI_REALLOC(p, sz)   heap_caps_realloc((p), (sz), MALLOC_CAP_SPIRAM)
#define STBI_FREE(p)          heap_caps_free(p)

// Only compile the JPEG decoder — saves ~40KB of flash vs full stb_image
#define STBI_ONLY_JPEG
// Reject absurd dimensions inside stb itself, before it allocates anything.
// Default is 1<<24. Upstream stb has open, unfixed memory-safety issues in the
// progressive-AC decoder this file exists to use, so keep the input surface as
// small as possible. ART_STB_MAX_DIM below applies the same cap explicitly.
#define STBI_MAX_DIMENSIONS 2048
// No stdio — we always decode from memory
#define STBI_NO_STDIO
// Suppress unused-function warnings from the STB header
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma GCC diagnostic pop

/**
 * Decode a progressive (or baseline) JPEG from memory to RGB565 LE in PSRAM.
 * Uses stb_image for full-quality progressive JPEG support.
 *
 * @param buf       Raw JPEG bytes
 * @param len       Byte count
 * @param out       Set to allocated RGB565 buffer in PSRAM (caller frees with heap_caps_free)
 * @param out_w     Set to image width
 * @param out_h     Set to image height
 * @return true on success
 */
bool decodeJPEGProgressiveStb(const uint8_t* buf, size_t len,
                               uint16_t** out, int* out_w, int* out_h) {
    int w = 0, h = 0, channels = 0;

    // Check dimensions BEFORE decoding. stb allocates w*h*3 up front, and this
    // path only ever sees progressive JPEGs, which compress hard enough that a
    // 3000x3000 image fits comfortably inside the 280KB MAX_ART_SIZE download
    // gate — decoding it would ask for a 27MB RGB888 block from the same PSRAM
    // pool that holds the art buffers, both cache slots and four task stacks,
    // only to reject the result afterwards. stbi_info parses the header only.
    // The sibling decoders in ui_album_art.cpp validate before allocating too.
    const int ART_STB_MAX_DIM = 2048;
    int info_w = 0, info_h = 0, info_c = 0;
    if (!stbi_info_from_memory(buf, (int)len, &info_w, &info_h, &info_c)) {
        Serial.printf("[ART] stb_image header unreadable: %s\n", stbi_failure_reason());
        return false;
    }
    if (info_w <= 0 || info_h <= 0 || info_w > ART_STB_MAX_DIM || info_h > ART_STB_MAX_DIM) {
        Serial.printf("[ART] stb_image dimensions rejected pre-decode: %dx%d\n", info_w, info_h);
        return false;
    }

    // stb_image returns RGB888 interleaved (3 bytes per pixel)
    uint8_t* rgb888 = stbi_load_from_memory(buf, (int)len, &w, &h, &channels, 3);
    if (!rgb888) {
        Serial.printf("[ART] stb_image failed: %s\n", stbi_failure_reason());
        return false;
    }

    // Belt-and-braces: the header said one thing, confirm the decode agrees.
    if (w <= 0 || h <= 0 || w > ART_STB_MAX_DIM || h > ART_STB_MAX_DIM) {
        Serial.printf("[ART] stb_image invalid dimensions: %dx%d\n", w, h);
        stbi_image_free(rgb888);
        return false;
    }

    // Convert RGB888 → RGB565 LE (LVGL format on ESP32: little-endian uint16_t)
    uint16_t* rgb565 = (uint16_t*)heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
    if (!rgb565) {
        Serial.printf("[ART] stb RGB565 alloc failed: %d bytes\n", w * h * 2);
        stbi_image_free(rgb888);
        return false;
    }

    const int total = w * h;
    const uint8_t* src = rgb888;
    uint16_t* dst = rgb565;
    for (int i = 0; i < total; i++, src += 3, dst++) {
        *dst = (uint16_t)(((src[0] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[2] >> 3));
    }

    stbi_image_free(rgb888);

    *out   = rgb565;
    *out_w = w;
    *out_h = h;
    Serial.printf("[ART] stb_image decoded: %dx%d (%d bytes RGB565)\n", w, h, w * h * 2);
    return true;
}
