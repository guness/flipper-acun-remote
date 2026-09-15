#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sequence_core.h"

#define RADIO_TX_REPEATS 6
#define RADIO_TX_TIMEOUT_MS 3000

typedef enum {
    RadioEventNone,
    RadioEventPress, /* a press confirmed by RADIO_PRESS_CONFIRM_FRAMES identical frames; see *press */
    RadioEventOverflow, /* pulse queue overflowed, capture is unreliable */
    RadioEventTxDone,
    RadioEventTxTimeout, /* RADIO_TX_TIMEOUT_MS without completion */
} RadioEvent;

typedef struct Radio Radio;

Radio* radio_alloc(void);
void radio_free(Radio* radio);
void radio_rx_start(Radio* radio);
bool radio_tx_start(Radio* radio, const SeqProfile* profile);
void radio_stop(Radio* radio);
bool radio_is_busy(const Radio* radio);
/* Poll from the GUI thread every ~10 ms. Repeats of the last confirmed press are dropped. */
RadioEvent radio_tick(Radio* radio, SeqFrame* press);
/* Live receive signal strength in dBm. Only meaningful while RX is running. */
float radio_rssi(const Radio* radio);
