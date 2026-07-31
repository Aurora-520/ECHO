#ifndef ECHO_COMPETITION_PAGE_H
#define ECHO_COMPETITION_PAGE_H

#include <stdint.h>

#include "competition_service.h"
#include "parameter_service.h"
#include "system_health.h"

typedef struct {
    competition_service_snapshot_t competition;
    system_health_snapshot_t health;
    const parameter_metadata_t *advanced_metadata;
    float advanced_value;
    uint32_t battery_mv;
    uint32_t uptime_s;
    uint32_t esp_rtt_us;
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    float temperature_c;
    float left_rpm;
    float right_rpm;
    float distance_progress_mm;
    float tune_left_target_rpm;
    float tune_right_target_rpm;
    float tune_left_measured_rpm;
    float tune_right_measured_rpm;
    float tune_left_pwm_percent;
    float tune_right_pwm_percent;
    float tune_left_error_rpm;
    float tune_right_error_rpm;
    float tune_heading_error_deg;
    float tune_heading_correction_rpm;
    float ball_position_mm;
    float ball_target_mm;
    float ball_error_mm;
    float ball_velocity_mm_s;
    uint8_t tune_pwm_saturated;
    uint8_t tune_boost_active;
    uint8_t tune_output_permitted;
    uint8_t supply_valid;
    uint8_t imu_available;
    uint8_t imu_ready;
    uint8_t imu_state;
    uint16_t imu_calibration_samples;
    uint16_t imu_calibration_target_samples;
    uint8_t encoders_ready;
    uint8_t oled_available;
    uint8_t reflectance_available;
    uint8_t reflectance_mask;
    uint8_t esp_available;
    uint8_t esp_ready;
    uint8_t lidar_available;
    uint8_t lidar_online;
    uint8_t vision_available;
    uint8_t vision_online;
    uint8_t vision_valid;
    uint8_t zdt_gen1_online;
    uint8_t zdt_gen2_online;
    uint8_t zdt_gen1_available;
    uint8_t zdt_gen2_available;
    uint8_t health_check_pass;
    uint8_t ball_valid;
    uint8_t line_runtime_calibration_state;
    uint8_t line_runtime_calibration_mask;
    uint16_t line_runtime_calibration_samples;
    uint8_t line_runtime_calibration_applied;
    uint8_t line_runtime_white_captured;
    uint8_t line_runtime_black_captured;
    uint8_t reflectance_backend;
} competition_page_data_t;

void CompetitionPage_Render(const competition_page_data_t *data);

#endif
