#pragma once
/**
 * ui_network_guard.h — Centralised SDIO crash-defence pre-download guard.
 *
 * sdioPreWait() consolidates all SDIO cooldown boilerplate that previously
 * lived duplicated across ui_album_art.cpp, lyrics.cpp, sonos_controller.cpp,
 * and ui_clock_screen.cpp.
 *
 * Call OUTSIDE the network mutex, BEFORE acquiring it.
 * Inside-mutex re-checks remain inline in each caller (one-shot, correct).
 *
 * Always enforces:
 *   1. SDIO_GENERAL_COOLDOWN_MS  — min gap since last network op (any protocol)
 *
 * Optional checks (select via flags):
 *   SDIO_WAIT_QUEUE_POLL     — updateQueue() residue (SDIO_QUEUE_POLL_COOLDOWN_MS)
 *   SDIO_WAIT_HTTPS_COOLDOWN — TLS teardown residue after last HTTPS session
 *                              (SDIO_HTTPS_COOLDOWN_MS = 3s).
 *                              Use only when the caller itself does HTTPS (lyrics, HTTPS art,
 *                              clock weather). Do NOT use for plain HTTP callers (sendSOAP,
 *                              local Sonos art) — it created 3s SDIO idle → pkt_rxbuff :928.
 *
 * Returns: false if abort_flag1 or abort_flag2 became true during any wait.
 *          true  if all cooldowns passed without abort.
 *
 * Callers with no abort flags can ignore the return value —
 * with nullptr flags it always returns true.
 */

#include <stdint.h>

// Flags for optional check groups
// Note: 0x01 and 0x02 are intentionally vacant — they used to host
// SDIO_WAIT_TRACK_CHANGE / SDIO_WAIT_STORM_GATE which both caused C6 idle
// → DMA clock-gate crashes and were removed (no callers left).
#define SDIO_WAIT_QUEUE_POLL     0x04u   // Gate on last_queue_fetch_time
#define SDIO_WAIT_HTTPS_COOLDOWN 0x08u   // Gate on last_https_end_ms (3s TLS teardown wait)

bool sdioPreWait(const char*    tag,
                 uint32_t       flags       = 0,
                 volatile bool* abort_flag1 = nullptr,
                 volatile bool* abort_flag2 = nullptr);
