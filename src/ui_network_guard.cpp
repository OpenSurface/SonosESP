#include "ui_network_guard.h"
#include "ui_common.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

static inline bool _aborted(volatile bool* f1, volatile bool* f2) {
    return (f1 && *f1) || (f2 && *f2);
}

bool sdioPreWait(const char* tag, uint32_t flags,
                 volatile bool* abort_flag1, volatile bool* abort_flag2)
{
    // ── 1. General cooldown ───────────────────────────────────────────────────
    // Minimum gap between any two network operations (HTTP or HTTPS).
    // Even local HTTP generates SDIO traffic that can combine with SOAP/NOTIFY.
    if (last_network_end_ms > 0) {
        unsigned long elapsed = millis() - last_network_end_ms;
        if (elapsed < SDIO_GENERAL_COOLDOWN_MS) {
            vTaskDelay(pdMS_TO_TICKS(SDIO_GENERAL_COOLDOWN_MS - elapsed));
        }
    }
    if (_aborted(abort_flag1, abort_flag2)) return false;

    // ── 2. HTTPS residue cooldown (opt-in: SDIO_WAIT_HTTPS_COOLDOWN) ─────────────
    // mbedTLS DMA buffers (~71KB) take ~3s to fully release after TLS session
    // teardown. Only needed when the caller itself does HTTPS — applying it
    // unconditionally to plain HTTP callers (sendSOAP, local Sonos getaa) caused
    // 3s of SDIO idle → P4 SDIO DMA clock-gated → pkt_rxbuff :928 overflow.
    if ((flags & SDIO_WAIT_HTTPS_COOLDOWN) && last_https_end_ms > 0) {
        unsigned long elapsed = millis() - last_https_end_ms;
        if (elapsed < SDIO_HTTPS_COOLDOWN_MS) {
            Serial.printf("[%s] HTTPS cooldown: waiting %lums\n",
                          tag, SDIO_HTTPS_COOLDOWN_MS - elapsed);
            vTaskDelay(pdMS_TO_TICKS(SDIO_HTTPS_COOLDOWN_MS - elapsed));
        }
    }
    if (_aborted(abort_flag1, abort_flag2)) return false;

    // ── 3. Queue poll residue cooldown ────────────────────────────────────────
    // updateQueue() returns ~20KB Browse response. Combined with a concurrent
    // download it exhausts the SDIO RX buffer pool → pkt_rxbuff :928 crash.
    if ((flags & SDIO_WAIT_QUEUE_POLL) && last_queue_fetch_time > 0) {
        unsigned long elapsed = millis() - last_queue_fetch_time;
        if (elapsed < SDIO_QUEUE_POLL_COOLDOWN_MS) {
            Serial.printf("[%s] Queue poll cooldown: waiting %lums\n",
                          tag, SDIO_QUEUE_POLL_COOLDOWN_MS - elapsed);
            vTaskDelay(pdMS_TO_TICKS(SDIO_QUEUE_POLL_COOLDOWN_MS - elapsed));
        }
    }
    if (_aborted(abort_flag1, abort_flag2)) return false;

    // ── 4. Inter-download cooldown ────────────────────────────────────────────
    // Back-to-back large HTTP downloads exhaust pkt_rxbuff: previous download's
    // TCP FIN-ACK + new download's SYN-ACK arrive simultaneously → overflow.
    // last_art_download_end_ms is set by art task AND clockBgTask after large downloads.
    //
    // HTTPS callers (SDIO_WAIT_HTTPS_COOLDOWN set) need SDIO_HTTPS_COOLDOWN_MS (3s):
    // TLS handshake generates a burst of 2-4 incoming TCP segments (ServerHello +
    // Certificate). If art's HTTP TCP residue hasn't fully drained, this burst
    // overflows pkt_rxbuff → :928. 3s matches the system-wide TLS drain standard.
    // HTTP callers (art task) need only SDIO_INTER_DOWNLOAD_MS (1s): HTTP SYN-ACK
    // is a single segment; 1s is sufficient for prior FIN-ACK to drain.
    if (last_art_download_end_ms > 0) {
        unsigned long threshold = (flags & SDIO_WAIT_HTTPS_COOLDOWN)
                                  ? SDIO_HTTPS_COOLDOWN_MS
                                  : SDIO_INTER_DOWNLOAD_MS;
        unsigned long elapsed = millis() - last_art_download_end_ms;
        if (elapsed < threshold) {
            Serial.printf("[%s] Inter-download cooldown: waiting %lums\n",
                          tag, threshold - elapsed);
            vTaskDelay(pdMS_TO_TICKS(threshold - elapsed));
        }
    }
    if (_aborted(abort_flag1, abort_flag2)) return false;

    // ── 5. Queue poll re-check ────────────────────────────────────────────────
    // The polling task runs freely during the inter-download wait above and may
    // fire updateQueue() at any point during that sleep.  Check #3 ran before
    // the wait and cannot see a queue fetch that occurs during it.  Re-check
    // here so we don't exit sdioPreWait with fresh 20KB SOAP residue in
    // pkt_rxbuff that will combine with the art download burst → :928 crash.
    if ((flags & SDIO_WAIT_QUEUE_POLL) && last_queue_fetch_time > 0) {
        unsigned long elapsed = millis() - last_queue_fetch_time;
        if (elapsed < SDIO_QUEUE_POLL_COOLDOWN_MS) {
            Serial.printf("[%s] Queue poll re-check: waiting %lums\n",
                          tag, SDIO_QUEUE_POLL_COOLDOWN_MS - elapsed);
            vTaskDelay(pdMS_TO_TICKS(SDIO_QUEUE_POLL_COOLDOWN_MS - elapsed));
        }
    }

    return !_aborted(abort_flag1, abort_flag2);
}
