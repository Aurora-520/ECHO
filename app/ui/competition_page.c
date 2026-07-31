#include "competition_page.h"

#include <stddef.h>

#include "h_mission_service.h"
#include "imu_service.h"
#include "ssd1306.h"

#define COMPETITION_UI_LINE_CHARS 21U

typedef struct {
    char text[COMPETITION_UI_LINE_CHARS + 1U];
    uint8_t length;
} competition_ui_line_t;

static void Line_Clear(competition_ui_line_t *line)
{
    line->length = 0U;
    line->text[0] = '\0';
}

static void Line_Char(competition_ui_line_t *line, char value)
{
    if (line->length < COMPETITION_UI_LINE_CHARS) {
        line->text[line->length++] = value;
        line->text[line->length] = '\0';
    }
}

static void Line_Text(competition_ui_line_t *line, const char *text)
{
    while (text != NULL && *text != '\0' &&
        line->length < COMPETITION_UI_LINE_CHARS) {
        Line_Char(line, *text++);
    }
}

static void Line_U32(competition_ui_line_t *line, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;

    do {
        digits[count++] = (char) ('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) {
        Line_Char(line, digits[--count]);
    }
}

static void Line_Hex8(competition_ui_line_t *line, uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    Line_Char(line, digits[(value >> 4U) & 0x0FU]);
    Line_Char(line, digits[value & 0x0FU]);
}

static void Line_I32(competition_ui_line_t *line, int32_t value)
{
    uint32_t magnitude;

    if (value < 0) {
        Line_Char(line, '-');
        magnitude = 0U - (uint32_t) value;
    } else {
        magnitude = (uint32_t) value;
    }
    Line_U32(line, magnitude);
}

static void Line_Fixed1(competition_ui_line_t *line, float value)
{
    int32_t deci = (int32_t) (value * 10.0f +
        ((value >= 0.0f) ? 0.5f : -0.5f));
    uint32_t magnitude;

    if (deci < 0) {
        Line_Char(line, '-');
        magnitude = 0U - (uint32_t) deci;
    } else {
        magnitude = (uint32_t) deci;
    }
    Line_U32(line, magnitude / 10U);
    Line_Char(line, '.');
    Line_Char(line, (char) ('0' + magnitude % 10U));
}

static void Line_Voltage(competition_ui_line_t *line, uint32_t mv)
{
    Line_U32(line, mv / 1000U);
    Line_Char(line, '.');
    Line_Char(line, (char) ('0' + (mv / 100U) % 10U));
    Line_Char(line, (char) ('0' + (mv / 10U) % 10U));
    Line_Char(line, (char) ('0' + mv % 10U));
    Line_Char(line, 'V');
}

static void Line_SignedFixed1(competition_ui_line_t *line, float value)
{
    if (value >= 0.0f) {
        Line_Char(line, '+');
    }
    Line_Fixed1(line, value);
}

static void Draw(uint8_t row, const competition_ui_line_t *line)
{
    Ssd1306_DrawText(0U, row, line->text);
}

static void DrawHeader(const char *name,
    const competition_page_data_t *data, const char *badge)
{
    competition_ui_line_t line;

    Line_Clear(&line);
    Line_Text(&line, name);
    Line_Text(&line, "  ");
    Line_Text(&line, CompetitionService_StateName(
        (competition_state_t) data->competition.state));
    if (data->competition.save_pending != 0U) {
        Line_Text(&line, " S");
    }
    if (badge != NULL && *badge != '\0') {
        Line_Char(&line, ' ');
        Line_Text(&line, badge);
    }
    while (line.length < 18U) {
        Line_Char(&line, ' ');
    }
    Line_U32(&line, (uint32_t) data->competition.page + 1U);
    Line_Text(&line, "/4");
    Draw(0U, &line);
}

static void DrawSelectPrefix(competition_ui_line_t *line,
    const competition_page_data_t *data, uint8_t field)
{
    Line_Char(line, data->competition.cursor == field ? '>' : ' ');
    Line_Char(line, data->competition.cursor == field &&
        data->competition.editing != 0U ? '*' : ' ');
}

static void AppendStatus(competition_ui_line_t *line, uint8_t ready)
{
    Line_Text(line, ready != 0U ? "OK" : "--");
}

static void AppendAvailableStatus(competition_ui_line_t *line,
    uint8_t available, uint8_t online)
{
    if (available == 0U) {
        Line_Text(line, "NA");
    } else {
        AppendStatus(line, online);
    }
}

static void FormatRunTime(uint32_t elapsed_ms, char text[6])
{
    uint32_t seconds;
    uint32_t hundredths;

    if (elapsed_ms > 99999U) {
        elapsed_ms = 99999U;
    }
    seconds = elapsed_ms / 1000U;
    hundredths = (elapsed_ms % 1000U) / 10U;
    text[0] = (char) ('0' + (seconds / 10U) % 10U);
    text[1] = (char) ('0' + seconds % 10U);
    text[2] = '.';
    text[3] = (char) ('0' + hundredths / 10U);
    text[4] = (char) ('0' + hundredths % 10U);
    text[5] = '\0';
}

static void RenderOfficialTimer(const competition_page_data_t *data)
{
    competition_ui_line_t line;
    char elapsed[6];
    const uint8_t slot = data->competition.settings.task_slot;

    Line_Clear(&line);
    Line_Text(&line, HMissionService_Code(slot));
    Line_Char(&line, ' ');
    Line_Text(&line, CompetitionService_StateName(
        (competition_state_t) data->competition.state));
    Ssd1306_DrawText(0U, 0U, line.text);
    FormatRunTime(data->competition.run_elapsed_ms, elapsed);
    Ssd1306_DrawTextScaled(4U, 20U, elapsed, 4U);
    if (slot == (uint8_t) H_MISSION_AB_CENTER) {
        Line_Clear(&line);
        Line_Text(&line, "BALL:");
        if (data->ball_valid != 0U) {
            Line_SignedFixed1(&line, data->ball_position_mm);
            Line_Text(&line, "mm");
        } else {
            Line_Text(&line, "--");
        }
        Draw(7U, &line);
    }
}

static void RenderLineCalibration(const competition_page_data_t *data)
{
    competition_ui_line_t line;
    uint8_t state = data->line_runtime_calibration_state;

    DrawHeader("LINE CAL", data,
        data->reflectance_backend == 2U ? "I6" : "A8");
    Line_Clear(&line);
    Line_Text(&line, "K1 WHITE:");
    Line_Text(&line, data->line_runtime_white_captured != 0U ?
        "OK" : (state == (uint8_t) H_LINE_RUNTIME_CAL_CAPTURE_WHITE ?
        "CAP" : "--"));
    Draw(1U, &line);
    Line_Clear(&line);
    Line_Text(&line, "K2 BLACK:");
    Line_Text(&line, data->line_runtime_black_captured != 0U ?
        "OK" : (state == (uint8_t) H_LINE_RUNTIME_CAL_CAPTURE_BLACK ?
        "CAP" : "--"));
    Draw(2U, &line);
    Line_Clear(&line);
    Line_Text(&line, "STATE:");
    if (state == (uint8_t) H_LINE_RUNTIME_CAL_CAPTURE_WHITE) {
        Line_Text(&line, "CAP WHITE");
    } else if (state == (uint8_t) H_LINE_RUNTIME_CAL_CAPTURE_BLACK) {
        Line_Text(&line, "CAP BLACK");
    } else if (state == (uint8_t) H_LINE_RUNTIME_CAL_COMPLETE) {
        Line_Text(&line, "OK");
    } else if (state == (uint8_t) H_LINE_RUNTIME_CAL_FAILED) {
        Line_Text(&line, "FAIL");
    } else {
        Line_Text(&line, "READY");
    }
    Draw(3U, &line);
    Line_Clear(&line);
    Line_Text(&line, "SAMPLES:");
    Line_U32(&line, data->line_runtime_calibration_samples);
    Draw(4U, &line);
    Line_Clear(&line);
    Line_Text(&line, "MASK:");
    Line_Hex8(&line, data->line_runtime_calibration_mask);
    Line_Text(&line, " RAM ONLY");
    Draw(5U, &line);
    Line_Clear(&line);
    Line_Text(&line, data->line_runtime_calibration_applied != 0U ?
        "ACTIVE:RAM PROFILE" : "ACTIVE:FLASH BASE");
    Draw(6U, &line);
    Line_Clear(&line);
    Line_Text(&line, "K5 EXIT");
    Draw(7U, &line);
}

static void RenderMain(const competition_page_data_t *data)
{
    competition_ui_line_t line;
    uint8_t ball_test = data->competition.run_is_test != 0U &&
        data->competition.settings.test_action ==
            (uint8_t) COMPETITION_TEST_BALL_CENTER;

    if (data->line_runtime_calibration_state !=
            (uint8_t) H_LINE_RUNTIME_CAL_IDLE) {
        RenderLineCalibration(data);
        return;
    }

    DrawHeader("MAIN", data, NULL);
    Line_Clear(&line);
    Line_Char(&line, '>');
    Line_Text(&line, HMissionService_Code(
        data->competition.settings.task_slot));
    Line_Char(&line, ' ');
    Line_Text(&line, HMissionService_Name(
        data->competition.settings.task_slot));
    Draw(1U, &line);

    Line_Clear(&line);
    if (data->competition.state ==
            (uint8_t) COMPETITION_STATE_COUNTDOWN) {
        Line_Text(&line, "START:");
        Line_U32(&line, (data->competition.countdown_remaining_ms +
            999U) / 1000U);
        Line_Text(&line, "s");
    } else {
        Line_Text(&line, "TIME:");
        Line_U32(&line, data->competition.run_elapsed_ms / 1000U);
        Line_Char(&line, '.');
        Line_Char(&line, (char) ('0' +
            (data->competition.run_elapsed_ms / 100U) % 10U));
        Line_Text(&line, "s RESULT:");
        Line_Text(&line, CompetitionService_ResultName(
            (competition_result_t) data->competition.result));
    }
    Draw(2U, &line);

    Line_Clear(&line);
    if (ball_test != 0U) {
        Line_Text(&line, "BALL:");
        if (data->ball_valid != 0U) {
            Line_SignedFixed1(&line, data->ball_position_mm);
            Line_Text(&line, "mm V:");
            Line_SignedFixed1(&line, data->ball_velocity_mm_s);
        } else {
            Line_Text(&line, "--");
        }
    } else {
        Line_Text(&line, "DIST:");
        Line_Fixed1(&line, data->distance_progress_mm);
        Line_Text(&line, "mm");
    }
    Draw(3U, &line);

    Line_Clear(&line);
    if (ball_test != 0U) {
        Line_Text(&line, "TGT:");
        Line_SignedFixed1(&line, data->ball_target_mm);
        Line_Text(&line, " ERR:");
        Line_SignedFixed1(&line, data->ball_error_mm);
    } else {
        Line_Text(&line, "RPM L:");
        Line_Fixed1(&line, data->left_rpm);
        Line_Text(&line, " R:");
        Line_Fixed1(&line, data->right_rpm);
    }
    Draw(4U, &line);

    Line_Clear(&line);
    Line_Text(&line, "YAW:");
    if (data->imu_ready != 0U) {
        Line_SignedFixed1(&line, data->yaw_deg);
    } else {
        Line_Text(&line, "--");
    }
    Line_Text(&line, " OUT:");
    Line_Text(&line, data->health.actuator_output_permitted != 0U ?
        "ON" : "LOCK");
    Draw(5U, &line);

    Line_Clear(&line);
    Line_Text(&line, "BAT:");
    if (data->supply_valid != 0U) {
        Line_Voltage(&line, data->battery_mv);
    } else {
        Line_Text(&line, "--");
    }
    Line_Text(&line, " T:");
    if (data->imu_ready != 0U) {
        Line_Fixed1(&line, data->temperature_c);
        Line_Char(&line, 'C');
    } else {
        Line_Text(&line, "--");
    }
    Draw(6U, &line);

    Line_Clear(&line);
    Line_Text(&line, "BALL:");
    if (data->ball_valid != 0U) {
        Line_SignedFixed1(&line, data->ball_position_mm);
        Line_Text(&line, "mm");
    } else {
        Line_Text(&line, "--");
    }
    Line_Text(&line, " L5:CAL");
    Draw(7U, &line);

}

static void RenderTest(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    if (data->competition.advanced_mode != 0U) {
        DrawHeader("TEST ADV", data, NULL);
        Line_Clear(&line);
        Line_Text(&line, "PARAM:");
        Line_Text(&line, data->advanced_metadata != NULL ?
            data->advanced_metadata->name : "--");
        Draw(1U, &line);
        Line_Clear(&line);
        Line_Text(&line, data->competition.editing ? "VALUE*>" : "VALUE:");
        Line_Fixed1(&line, data->competition.editing ?
            data->competition.advanced_draft_value : data->advanced_value);
        Draw(2U, &line);
        Line_Clear(&line);
        Line_Text(&line, "INDEX:");
        Line_U32(&line, (uint32_t) data->competition.advanced_parameter_index +
            1U);
        Line_Text(&line, "/4");
        Draw(3U, &line);
        if (data->advanced_metadata != NULL) {
            Line_Clear(&line);
            Line_Text(&line, "MIN:");
            Line_Fixed1(&line, data->advanced_metadata->minimum_value);
            Draw(4U, &line);
            Line_Clear(&line);
            Line_Text(&line, "MAX:");
            Line_Fixed1(&line, data->advanced_metadata->maximum_value);
            Draw(5U, &line);
            Line_Clear(&line);
            Line_Text(&line, "STEP:");
            Line_Fixed1(&line, data->advanced_metadata->step);
            Draw(6U, &line);
        }
        Line_Clear(&line);
        Line_Text(&line, "FLASH:");
        Line_Text(&line, data->competition.save_pending != 0U ?
            "PENDING" : "SAVED");
        Draw(7U, &line);
        return;
    }

    DrawHeader("TEST", data, NULL);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 0U);
    Line_Text(&line, "TASK:T");
    Line_U32(&line, (uint32_t) data->competition.settings.task_slot + 1U);
    Draw(1U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 1U);
    Line_Text(&line, "MODE:");
    if (data->competition.settings.test_action ==
            (uint8_t) COMPETITION_TEST_DISTANCE) {
        Line_Text(&line, "DIST");
    } else if (data->competition.settings.test_action ==
            (uint8_t) COMPETITION_TEST_HEADING) {
        Line_Text(&line, "TURN");
    } else {
        Line_Text(&line, "BALL");
    }
    Draw(2U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 2U);
    Line_Text(&line, "DIST:");
    Line_I32(&line, data->competition.settings.distance_mm);
    Line_Text(&line, "mm");
    Draw(3U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 3U);
    Line_Text(&line, "SPD:");
    Line_Fixed1(&line, (float) data->competition.settings.speed_deci_rpm *
        0.1f);
    Line_Text(&line, "rpm");
    Draw(4U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 4U);
    Line_Text(&line, "ANG:");
    Line_Fixed1(&line, (float) data->competition.settings.angle_deci_deg *
        0.1f);
    Line_Text(&line, "deg");
    Draw(5U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 5U);
    Line_Text(&line, "TRN:");
    Line_Fixed1(&line, (float) data->competition.settings.turn_speed_deci_rpm *
        0.1f);
    Line_Text(&line, "rpm");
    Draw(6U, &line);
    Line_Clear(&line);
    DrawSelectPrefix(&line, data, 6U);
    Line_Text(&line, "DELAY:");
    Line_U32(&line, data->competition.settings.start_delay_s);
    Line_Text(&line, "s");
    Draw(7U, &line);
}

static void RenderTune(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    DrawHeader("TUNE", data, data->tune_boost_active != 0U ? "BST" : NULL);
    Line_Clear(&line);
    Line_Text(&line, "       L      R");
    Draw(1U, &line);
    Line_Clear(&line);
    Line_Text(&line, "TGT  ");
    Line_Fixed1(&line, data->tune_left_target_rpm);
    Line_Char(&line, ' ');
    Line_Fixed1(&line, data->tune_right_target_rpm);
    Draw(2U, &line);
    Line_Clear(&line);
    Line_Text(&line, "RPM  ");
    Line_Fixed1(&line, data->tune_left_measured_rpm);
    Line_Char(&line, ' ');
    Line_Fixed1(&line, data->tune_right_measured_rpm);
    Draw(3U, &line);
    Line_Clear(&line);
    Line_Text(&line, "PWM  ");
    Line_Fixed1(&line, data->tune_left_pwm_percent);
    Line_Char(&line, '%');
    Line_Char(&line, ' ');
    Line_Fixed1(&line, data->tune_right_pwm_percent);
    Line_Char(&line, '%');
    if (data->tune_pwm_saturated != 0U) {
        Line_Text(&line, " SAT");
    }
    Draw(4U, &line);
    Line_Clear(&line);
    Line_Text(&line, "ERR  ");
    Line_SignedFixed1(&line, data->tune_left_error_rpm);
    Line_Char(&line, ' ');
    Line_SignedFixed1(&line, data->tune_right_error_rpm);
    Draw(5U, &line);
    Line_Clear(&line);
    Line_Text(&line, "YAW:");
    Line_SignedFixed1(&line, data->yaw_deg);
    Line_Text(&line, " HE:");
    Line_SignedFixed1(&line, data->tune_heading_error_deg);
    Draw(6U, &line);
    Line_Clear(&line);
    Line_Text(&line, "BAT:");
    if (data->supply_valid != 0U) {
        Line_Voltage(&line, data->battery_mv);
    } else {
        Line_Text(&line, "--");
    }
    Line_Text(&line, " C:");
    Line_SignedFixed1(&line, data->tune_heading_correction_rpm);
    Draw(7U, &line);
}

static void DrawAngle(uint8_t row, const char *name, float value,
    uint8_t ready)
{
    competition_ui_line_t line;

    Line_Clear(&line);
    Line_Text(&line, name);
    Line_Char(&line, ':');
    if (ready != 0U) {
        Line_SignedFixed1(&line, value);
        Line_Text(&line, "deg");
    } else {
        Line_Text(&line, "--");
    }
    Draw(row, &line);
}

static void RenderDiagImu(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    DrawHeader("DIAG", data, "IMU");
    Line_Clear(&line);
    Line_Text(&line, "TEMP:");
    if (data->imu_ready != 0U) {
        Line_Fixed1(&line, data->temperature_c);
        Line_Char(&line, 'C');
    } else {
        Line_Text(&line, "--");
    }
    Line_Text(&line, " IMU:");
    AppendAvailableStatus(&line, data->imu_available, data->imu_ready);
    Draw(1U, &line);
    DrawAngle(2U, "ROLL", data->roll_deg, data->imu_ready);
    DrawAngle(3U, "PITCH", data->pitch_deg, data->imu_ready);
    DrawAngle(4U, "YAW", data->yaw_deg, data->imu_ready);
    Line_Clear(&line);
    Line_Text(&line, "STATE:");
    if (data->imu_available == 0U) {
        Line_Text(&line, "NA");
    } else if (data->imu_state == (uint8_t) IMU_SERVICE_STATE_SETTLING) {
        Line_Text(&line, "SETTLE");
    } else if (data->imu_state ==
            (uint8_t) IMU_SERVICE_STATE_CALIBRATING) {
        Line_Text(&line, "CAL");
    } else if (data->imu_ready != 0U) {
        Line_Text(&line, "READY");
    } else {
        Line_Text(&line, "OFF");
    }
    Draw(5U, &line);
    Line_Clear(&line);
    Line_Text(&line, "CAL:");
    Line_U32(&line, data->imu_calibration_samples);
    Line_Char(&line, '/');
    Line_U32(&line, data->imu_calibration_target_samples);
    Draw(6U, &line);
    Line_Clear(&line);
    Line_Text(&line, data->imu_available != 0U && data->imu_ready != 0U ?
        ">IMU RESET" : "IMU BUSY");
    Draw(7U, &line);
}

static uint16_t MinimumStackWords(const system_health_snapshot_t *health)
{
    uint16_t minimum = health->system_stack_free_words;

    if (health->service_stack_free_words < minimum) {
        minimum = health->service_stack_free_words;
    }
    if (health->telemetry_stack_free_words < minimum) {
        minimum = health->telemetry_stack_free_words;
    }
    if (health->display_stack_free_words < minimum) {
        minimum = health->display_stack_free_words;
    }
    if (health->idle_stack_free_words < minimum) {
        minimum = health->idle_stack_free_words;
    }
    if (health->timer_stack_free_words < minimum) {
        minimum = health->timer_stack_free_words;
    }
    return minimum;
}

static const char *HealthShort(const system_health_snapshot_t *health)
{
    if (health->active_issue_mask != 0U || health->sticky_issue_mask != 0U) {
        return "WARN";
    }
    if (health->level == (uint8_t) SYSTEM_HEALTH_OK) {
        return "OK";
    }
    if (health->level == (uint8_t) SYSTEM_HEALTH_FAULT) {
        return "FAULT";
    }
    return "UNK";
}

static void RenderDiagHardware(const competition_page_data_t *data)
{
    competition_ui_line_t line;

    DrawHeader("DIAG", data, "HW");
    Line_Clear(&line);
    Line_Text(&line, "UP:");
    Line_U32(&line, data->uptime_s);
    Line_Text(&line, "s STK:");
    Line_U32(&line, MinimumStackWords(&data->health));
    Draw(1U, &line);
    Line_Clear(&line);
    Line_Text(&line, "HEAP:");
    Line_U32(&line, data->health.heap_free_bytes);
    Line_Text(&line, " FLASH:");
    Line_Text(&line, data->competition.result ==
        (uint8_t) COMPETITION_RESULT_STORAGE_FAULT ? "ERR" : "OK");
    Draw(2U, &line);
    Line_Clear(&line);
    Line_Text(&line, "I2C:");
    Line_U32(&line, data->health.i2c_error_count);
    Line_Text(&line, " DL:");
    Line_U32(&line, data->health.system_deadline_miss_count);
    Draw(3U, &line);
    Line_Clear(&line);
    Line_Text(&line, "BAT:");
    AppendStatus(&line, data->supply_valid);
    Line_Text(&line, " IMU:");
    AppendAvailableStatus(&line, data->imu_available, data->imu_ready);
    Line_Text(&line, " ENC:");
    AppendStatus(&line, data->encoders_ready);
    Draw(4U, &line);
    Line_Clear(&line);
    Line_Text(&line, "OLED:");
    AppendAvailableStatus(&line, data->oled_available,
        data->health.oled_online);
    Line_Text(&line, " IR:");
    AppendAvailableStatus(&line, data->reflectance_available,
        data->reflectance_mask == 0xFFU);
    Line_Text(&line, " ESP:");
    AppendAvailableStatus(&line, data->esp_available, data->esp_ready);
    Draw(5U, &line);
    Line_Clear(&line);
    Line_Text(&line, "VIS:");
    AppendAvailableStatus(&line, data->vision_available,
        data->vision_online);
    Line_Text(&line, " Z1:");
    AppendAvailableStatus(&line, data->zdt_gen1_available,
        data->zdt_gen1_online);
    Line_Text(&line, " Z2:");
    AppendAvailableStatus(&line, data->zdt_gen2_available,
        data->zdt_gen2_online);
    Draw(6U, &line);
    Line_Clear(&line);
    Line_Text(&line, ">SCAN:");
    if (data->competition.health_check_state ==
            (uint8_t) COMPETITION_HEALTH_CHECK_RUNNING) {
        Line_Text(&line, "RUN");
    } else if (data->competition.health_check_state ==
            (uint8_t) COMPETITION_HEALTH_CHECK_COMPLETE) {
        Line_Text(&line, data->health_check_pass != 0U ? "PASS" : "FAIL");
    } else if (data->competition.health_check_state ==
            (uint8_t) COMPETITION_HEALTH_CHECK_FAILED) {
        Line_Text(&line, "BUSY");
    } else {
        Line_Text(&line, "READY");
    }
    Line_Text(&line, " H:");
    Line_Text(&line, HealthShort(&data->health));
    Draw(7U, &line);
}

void CompetitionPage_Render(const competition_page_data_t *data)
{
    if (data == NULL) {
        return;
    }
    Ssd1306_Clear();
    if (data->competition.run_is_test == 0U &&
        data->competition.run_has_started != 0U &&
        data->competition.state != (uint8_t) COMPETITION_STATE_READY) {
        RenderOfficialTimer(data);
        return;
    }
    switch ((competition_page_t) data->competition.page) {
        case COMPETITION_PAGE_MAIN: RenderMain(data); break;
        case COMPETITION_PAGE_TEST: RenderTest(data); break;
        case COMPETITION_PAGE_TUNE: RenderTune(data); break;
        case COMPETITION_PAGE_DIAG:
            if (data->competition.diag_view == 0U) {
                RenderDiagImu(data);
            } else {
                RenderDiagHardware(data);
            }
            break;
        default: RenderMain(data); break;
    }
}
