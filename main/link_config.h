#pragma once
#include "sdkconfig.h"

/* ---- identity -------------------------------------------------------- */
#if CONFIG_LINK_ROLE_A
  #define NODE_ID        0xA1
  #define PEER_ID        0xA2
  #define NODE_NAME      "A"
  #define NODE_INITIATOR 1
#else
  #define NODE_ID        0xA2
  #define PEER_ID        0xA1
  #define NODE_NAME      "B"
  #define NODE_INITIATOR 0
#endif

/* ---- timers (ms) ----------------------------------------------------- */
#define DH_RETRY_MS          500
#define CONFIRM_TIMEOUT_MS   1000
#define HANDSHAKE_TIMEOUT_MS 1000
#define LISTEN_TIMEOUT_MS    2000
#define IDLE_POLL_MS         200
#define ACK_TIMEOUT_MS       400
#define MAX_ACK_RETRIES      4

/* ---- sizing ---------------------------------------------------------- */
#define OUTBOX_DEPTH         6
#define EVENT_QUEUE_DEPTH    10

/* ---- task placement -------------------------------------------------- */
/* Core 0 (PRO_CPU) runs the Wi-Fi/ESP-NOW stack and our RX callback.
 * Core 1 (APP_CPU) runs everything of ours: crypto, state machine, I/O. */
#define CORE_APP             1
#define PROTO_TASK_PRIO      6
#define CONSOLE_TASK_PRIO    4
#define TELEMETRY_TASK_PRIO  3
