#ifndef ECHO_BSP_ENCODER_H
#define ECHO_BSP_ENCODER_H

#include <stdint.h>

/*
 * D153B chassis wiring contract:
 * - Left wheel uses Motor A: motor + -> AOUT1 (AO1), motor - -> AOUT2
 *   (AO2), encoder phase A -> E1A, encoder phase B -> E1B.
 * - Right wheel uses Motor B: motor + -> BOUT1 (BO1), motor - -> BOUT2
 *   (BO2), encoder phase A -> E2A, encoder phase B -> E2B.
 * Phase wiring stays fixed; installation direction is normalized above BSP.
 */
typedef struct {
    int64_t total_counts;
    int32_t delta_counts;
    uint32_t error_count;
    uint32_t edge_count;
    uint16_t raw_counter;
    uint8_t phase_a;
    uint8_t phase_b;
    uint8_t initialized;
    uint8_t running;
} bsp_encoder_sample_t;

typedef struct {
    struct {
        int64_t total_counts;
        int32_t last_delta_counts;
        uint32_t sample_count;
        uint32_t max_abs_delta_counts;
        uint16_t raw_counter;
        uint8_t initialized;
        uint8_t running;
    } left;
    struct {
        int64_t total_counts;
        int32_t last_delta_counts;
        uint32_t sample_count;
        uint32_t max_abs_delta_counts;
        uint32_t edge_irq_count;
        uint32_t late_irq_count;
        uint32_t max_edges_per_sample;
        uint8_t phase_a;
        uint8_t phase_b;
        uint8_t initialized;
        uint8_t running;
    } right;
    uint32_t left_qei_error_count;
} bsp_encoder_diagnostics_t;

/* Watch/debug readers must treat this as read-only. */
extern volatile bsp_encoder_diagnostics_t g_bsp_encoder_diag;

void BSP_Encoder_Init(void);
void BSP_Encoder_SampleLeft(bsp_encoder_sample_t *sample);
void BSP_Encoder_SampleRight(bsp_encoder_sample_t *sample);

#endif
