#ifndef ECHO_COMPETITION_STORAGE_H
#define ECHO_COMPETITION_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#define COMPETITION_SETTINGS_VERSION 4U
#define COMPETITION_REFLECTANCE_CHANNEL_COUNT 8U

typedef enum {
    COMPETITION_TEST_DISTANCE = 0U,
    COMPETITION_TEST_HEADING = 1U,
    COMPETITION_TEST_BALL_CENTER = 2U
} competition_test_action_t;

typedef struct {
    uint8_t task_slot;
    uint8_t test_action;
    uint8_t start_delay_s;
    uint8_t reserved;
    int16_t distance_mm;
    uint16_t speed_deci_rpm;
    int16_t angle_deci_deg;
    uint16_t turn_speed_deci_rpm;
    float pid_kp;
    float pid_ki;
    float pid_kd;
    float pid_target;
} competition_settings_t;

typedef struct {
    uint16_t black[COMPETITION_REFLECTANCE_CHANNEL_COUNT];
    uint16_t white[COMPETITION_REFLECTANCE_CHANNEL_COUNT];
    uint8_t valid_mask;
    uint8_t reserved[3];
} competition_reflectance_calibration_t;

typedef struct {
    uint32_t load_count;
    uint32_t save_count;
    uint32_t save_failure_count;
    uint32_t generation;
    uint8_t loaded_valid;
    uint8_t last_save_ok;
    uint8_t reserved[2];
} competition_storage_diagnostics_t;

extern volatile competition_storage_diagnostics_t
    g_competition_storage_diag;

uint32_t CompetitionStorage_Crc32(const void *data, uint32_t length);
bool CompetitionStorage_Load(competition_settings_t *settings);
void CompetitionStorage_SetSettingsSnapshot(
    const competition_settings_t *settings);
bool CompetitionStorage_Save(const competition_settings_t *settings);
bool CompetitionStorage_LoadReflectanceCalibration(
    competition_reflectance_calibration_t *calibration);
bool CompetitionStorage_SaveReflectanceCalibration(
    const competition_reflectance_calibration_t *calibration);

#endif
