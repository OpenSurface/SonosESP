#ifndef ANALYTICS_H
#define ANALYTICS_H

// ============================================================================
// Anonymous install counter
// ----------------------------------------------------------------------------
// One HTTPS GET, once per boot, so the project can answer "how many panels are
// actually out there". Release download counts measure installs; this measures
// devices that are switched on.
//
// WHAT IS SENT, in full — this is the entire payload:
//
//     GET https://<site>.goatcounter.com/count?p=/boot/<version>/<4|7>in
//     User-Agent: SonosESP/<version> (esp32-p4)
//
// The firmware version and the panel size. Nothing else. Specifically NOT sent:
//
//   * No MAC address. A MAC is a permanent hardware identifier and counts as
//     personal data under GDPR, and it buys nothing a counter needs.
//   * No UUID or install id. GoatCounter deduplicates with its own rotating
//     session hash, so daily-active devices work with no identifier at all.
//   * No location. GoatCounter derives country from the request IP server-side
//     and does not store the IP, so sending coordinates would be a worse
//     version of data that already arrives for free.
//   * No room names, speaker names, network details, or anything about what is
//     playing.
//
// Off is a first-class state: ANALYTICS_ENABLED below is the build default and
// Settings > General carries a switch that persists to NVS. When it is off this
// code makes no network call at all.
//
// The call is deliberately unremarkable: it goes through sdioPreWait() and
// network_mutex like every other network path in this firmware (see
// sdio-defence), it runs once, it never retries, and every failure is silent.
// It must never be a reason the panel misbehaves.
// ============================================================================

// Build default for the toggle. The user's stored NVS preference wins once set.
#ifndef ANALYTICS_ENABLED
#define ANALYTICS_ENABLED 1
#endif

// Where the ping goes. Host only, no scheme and no path.
#define ANALYTICS_HOST "pizza.goatcounter.com"

// How long after boot to send it. Well clear of BOOT_REPORT_DELAY_MS so it
// cannot land in the CDC burst (#164), and long enough that WiFi, SDIO and the
// first artwork download have all settled.
#define ANALYTICS_BOOT_DELAY_MS 90000

// Retry budget for transient skips (radio busy, DMA under the gate). Sized as a
// window, not a burst: a panel playing music sits below ART_MIN_DMA_PRE_BURST
// for most of a session, so a four-minute window would miss exactly the devices
// worth counting. 5 min x 12 covers the first hour. The DMA-gate retry costs
// nothing — that check runs before anything blocks — and the mutex path uses a
// deliberately short timeout, so widening the window is close to free.
#define ANALYTICS_MAX_TRIES     12
#define ANALYTICS_RETRY_MS      300000UL

// The ping is the least urgent network user on the device. It waits a fraction
// of NETWORK_MUTEX_TIMEOUT_MS and gives up, because this runs on mainAppTask
// and every millisecond it blocks is a millisecond lv_timer_handler() does not
// run and the touchscreen is dead.
#define ANALYTICS_MUTEX_WAIT_MS 500

// Called once per mainAppTask iteration. Cheap no-op until it has fired.
void analyticsTick();

#endif // ANALYTICS_H
