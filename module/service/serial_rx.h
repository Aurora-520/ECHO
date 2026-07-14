#ifndef ECHO_SERIAL_RX_H
#define ECHO_SERIAL_RX_H

#include <stdbool.h>
#include <stdint.h>

#define SERIAL_RX_RING_CAPACITY_BYTES 256U

typedef struct {
    uint32_t received_byte_count;
    uint32_t overflow_count;
    uint32_t flush_count;
    uint16_t used_bytes;
    uint16_t high_water_bytes;
    uint8_t initialized;
    uint8_t reserved[3];
} serial_rx_diagnostics_t;

extern volatile serial_rx_diagnostics_t g_serial_rx_diag;

void SerialRx_Init(void);
bool SerialRx_TryRead(uint8_t *byte);
uint16_t SerialRx_Available(void);
uint32_t SerialRx_GetOverflowCount(void);
void SerialRx_Flush(void);

#endif
