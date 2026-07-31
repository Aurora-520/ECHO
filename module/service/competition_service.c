#include "competition_service.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "ball_balance_service.h"
#include "bsp_time.h"
#include "chassis_actuator.h"
#include "h_mission_service.h"
#include "imu_service.h"
#include "parameter_service.h"
#include "task.h"
#include "vehicle_bringup_config.h"
#include "zdt_stepper.h"

#define COMPETITION_SAVE_DELAY_MS       750U
#define COMPETITION_DISTANCE_MAX_MM    5000
#define COMPETITION_DISTANCE_MIN_MM      20
#define COMPETITION_SPEED_MIN_DECI_RPM  100U
#define COMPETITION_SPEED_MAX_DECI_RPM  650U
#define COMPETITION_ANGLE_MAX_DECI_DEG 3600
#define COMPETITION_TURN_MIN_DECI_RPM   150U
#define COMPETITION_TURN_MAX_DECI_RPM   350U
#define COMPETITION_TEST_FIELD_COUNT     7U
#define COMPETITION_HEALTH_CHECK_MS    1500U
#if ECHO_COMPETITION_ENABLE_BUTTON_ABORT
#define COMPETITION_PHYSICAL_STOP_DEBOUNCE_MS 25U
#endif
#define COMPETITION_PHYSICAL_BUTTON_MASK      0x1FU

volatile competition_service_snapshot_t g_competition_service;

static competition_mission_t s_missions[COMPETITION_TASK_SLOT_COUNT];
static uint32_t s_countdown_start_ms;
static uint32_t s_settings_changed_ms;
static uint32_t s_parameter_transaction_id;
static bool s_launch_test;
static bool s_wait_start_release;
static uint32_t s_health_check_start_ms;
static uint32_t s_input_guard_until_ms;
#if ECHO_COMPETITION_ENABLE_BUTTON_ABORT
static uint32_t s_stop_button_candidate_since_ms;
static uint8_t s_stop_button_candidate_mask;
#endif

static int32_t CompetitionService_Abs(int32_t value)
{
    return (value < 0) ? -value : value;
}

static void CompetitionService_SetState(competition_state_t state)
{
    if (g_competition_service.state != (uint8_t) state) {
        g_competition_service.state = (uint8_t) state;
        g_competition_service.transition_count++;
    }
}

static void CompetitionService_DefaultSettings(
    competition_settings_t *settings)
{
    control_tuning_parameters_t tuning;

    memset(settings, 0, sizeof(*settings));
    ParameterService_GetSnapshot(&tuning);
    settings->task_slot = 0U;
    settings->test_action = (uint8_t) COMPETITION_TEST_DISTANCE;
    settings->start_delay_s = 3U;
    settings->distance_mm = 1000;
    settings->speed_deci_rpm = 400U;
    settings->angle_deci_deg = 900;
    settings->turn_speed_deci_rpm = 350U;
    settings->pid_kp = tuning.kp;
    settings->pid_ki = tuning.ki;
    settings->pid_kd = tuning.kd;
    settings->pid_target = tuning.target;
}

static bool CompetitionService_SettingsValid(
    const competition_settings_t *settings)
{
    int32_t distance = CompetitionService_Abs(settings->distance_mm);
    int32_t angle = CompetitionService_Abs(settings->angle_deci_deg);
    const float values[PARAMETER_COUNT] = {
        settings->pid_kp,
        settings->pid_ki,
        settings->pid_kd,
        settings->pid_target
    };
    uint8_t index;

    for (index = 0U; index < PARAMETER_COUNT; index++) {
        const parameter_metadata_t *metadata =
            ParameterService_GetMetadataByIndex(index);

        if (metadata == NULL || values[index] != values[index] ||
            values[index] < metadata->minimum_value ||
            values[index] > metadata->maximum_value) {
            return false;
        }
    }

    return settings->task_slot < COMPETITION_TASK_SLOT_COUNT &&
        settings->test_action <= (uint8_t) COMPETITION_TEST_BALL_CENTER &&
        settings->start_delay_s <= 9U &&
        distance >= COMPETITION_DISTANCE_MIN_MM &&
        distance <= COMPETITION_DISTANCE_MAX_MM &&
        settings->speed_deci_rpm >= COMPETITION_SPEED_MIN_DECI_RPM &&
        settings->speed_deci_rpm <= COMPETITION_SPEED_MAX_DECI_RPM &&
        angle >= 10 && angle <= COMPETITION_ANGLE_MAX_DECI_DEG &&
        settings->turn_speed_deci_rpm >=
            COMPETITION_TURN_MIN_DECI_RPM &&
        settings->turn_speed_deci_rpm <=
            COMPETITION_TURN_MAX_DECI_RPM;
}

static void CompetitionService_MarkDirty(uint32_t now_ms)
{
    s_settings_changed_ms = now_ms;
    g_competition_service.save_pending = 1U;
}

static void CompetitionService_StartTimer(uint32_t now_ms, bool test)
{
    g_competition_service.run_start_ms = now_ms;
    g_competition_service.run_elapsed_ms = 0U;
    g_competition_service.run_count++;
    g_competition_service.run_is_test = test ? 1U : 0U;
    g_competition_service.run_has_started = 1U;
}

static void CompetitionService_UpdateTimer(uint32_t now_ms)
{
    if (g_competition_service.run_has_started != 0U &&
        g_competition_service.state == (uint8_t) COMPETITION_STATE_RUNNING) {
        g_competition_service.run_elapsed_ms =
            now_ms - g_competition_service.run_start_ms;
    }
}

static void CompetitionService_StopTimer(uint32_t now_ms)
{
    CompetitionService_UpdateTimer(now_ms);
}

static void CompetitionService_StopActive(uint32_t now_ms)
{
    uint8_t slot = g_competition_service.settings.task_slot;

    CompetitionService_StopTimer(now_ms);
    if (!s_launch_test && slot < COMPETITION_TASK_SLOT_COUNT &&
        s_missions[slot].stop != NULL) {
        s_missions[slot].stop(s_missions[slot].context);
    } else if (s_launch_test &&
        g_competition_service.settings.test_action ==
            (uint8_t) COMPETITION_TEST_BALL_CENTER) {
        BallBalanceService_RequestAbort();
    }
    ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_EMERGENCY);
    g_competition_service.emergency_stop_count++;
    g_competition_service.result = (uint8_t) COMPETITION_RESULT_ABORT;
    s_input_guard_until_ms = now_ms + 150U;
    CompetitionService_SetState(COMPETITION_STATE_ABORTED);
}

static void CompetitionService_BeginCountdown(bool test, uint32_t now_ms)
{
    s_launch_test = test;
    s_countdown_start_ms = now_ms;
    g_competition_service.countdown_remaining_ms =
        (uint32_t) g_competition_service.settings.start_delay_s * 1000U;
    g_competition_service.result = (uint8_t) COMPETITION_RESULT_NONE;
    g_competition_service.motion_applied = 0U;
    g_competition_service.run_has_started = 0U;
    g_competition_service.run_elapsed_ms = 0U;
    g_competition_service.run_is_test = test ? 1U : 0U;
    s_wait_start_release = true;
    g_competition_service.page = (uint8_t) COMPETITION_PAGE_MAIN;
    CompetitionService_SetState(COMPETITION_STATE_COUNTDOWN);
}

static void CompetitionService_StartTest(uint32_t now_ms)
{
    chassis_actuator_debug_request_t request;
    chassis_actuator_command_status_t status;

    if (g_competition_service.settings.test_action ==
            (uint8_t) COMPETITION_TEST_BALL_CENTER) {
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_NONE);
        if (BallBalanceService_CanStartPositionHold(BSP_Time_GetUs()) &&
            BallBalanceService_RequestStartPositionHold(0)) {
            g_competition_service.motion_applied = 1U;
            CompetitionService_StartTimer(now_ms, true);
            CompetitionService_SetState(COMPETITION_STATE_RUNNING);
        } else {
            g_competition_service.result =
                (uint8_t) COMPETITION_RESULT_REJECTED;
            CompetitionService_SetState(COMPETITION_STATE_FAULT);
        }
        return;
    }

    memset(&request, 0, sizeof(request));
    g_competition_service.request_sequence++;
    if (g_competition_service.request_sequence == 0U) {
        g_competition_service.request_sequence = 0xC0000001UL;
    }
    request.magic = CHASSIS_ACTUATOR_DEBUG_MAGIC;
    request.magic_inverse = CHASSIS_ACTUATOR_DEBUG_MAGIC_INVERSE;
    request.sequence = g_competition_service.request_sequence;
    if (g_competition_service.settings.test_action ==
            (uint8_t) COMPETITION_TEST_DISTANCE) {
        int16_t speed = (int16_t)
            g_competition_service.settings.speed_deci_rpm;

        if (g_competition_service.settings.distance_mm < 0) {
            speed = (int16_t) -speed;
        }
        request.left_electrical_permille = speed;
        request.right_electrical_permille = (int16_t)
            CompetitionService_Abs(
                g_competition_service.settings.distance_mm);
        request.duration_ms = CHASSIS_ACTUATOR_DISTANCE_MAX_DURATION_MS;
        request.reserved = (uint16_t) CHASSIS_ACTUATOR_MODE_DISTANCE;
    } else {
        (void) ChassisActuator_SetPivotMaximumRpm(
            (float) g_competition_service.settings.turn_speed_deci_rpm *
                0.1f);
        request.left_electrical_permille = 0;
        request.right_electrical_permille =
            g_competition_service.settings.angle_deci_deg;
        request.duration_ms = CHASSIS_ACTUATOR_HEADING_MAX_DURATION_MS;
        request.reserved = (uint16_t) CHASSIS_ACTUATOR_MODE_HEADING;
    }
    status = ChassisActuator_StageDebugRequest(&request);
    if (status == CHASSIS_ACTUATOR_COMMAND_STAGED) {
        CompetitionService_StartTimer(now_ms, true);
        CompetitionService_SetState(COMPETITION_STATE_RUNNING);
    } else {
        g_competition_service.result =
            (uint8_t) COMPETITION_RESULT_REJECTED;
        CompetitionService_SetState(COMPETITION_STATE_FAULT);
    }
}

static void CompetitionService_StartMission(uint32_t now_ms)
{
    uint8_t slot = g_competition_service.settings.task_slot;

    if (slot >= COMPETITION_TASK_SLOT_COUNT ||
        s_missions[slot].start == NULL ||
        s_missions[slot].service == NULL) {
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_NONE);
        g_competition_service.result =
            (uint8_t) COMPETITION_RESULT_NO_TASK;
        CompetitionService_SetState(COMPETITION_STATE_RESULT);
    } else if (s_missions[slot].start(s_missions[slot].context)) {
        CompetitionService_StartTimer(now_ms, false);
        CompetitionService_SetState(COMPETITION_STATE_RUNNING);
    } else {
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
        g_competition_service.result =
            (uint8_t) COMPETITION_RESULT_REJECTED;
        CompetitionService_SetState(COMPETITION_STATE_FAULT);
    }
}

static void CompetitionService_AdjustSigned(int16_t *value, int16_t step,
    int16_t minimum, int16_t maximum, bool increase)
{
    int32_t next = *value + (increase ? step : -step);

    if (next > maximum) {
        next = maximum;
    } else if (next < minimum) {
        next = minimum;
    }
    if (next > -minimum && next < minimum) {
        next = increase ? minimum : -minimum;
    }
    *value = (int16_t) next;
}

static void CompetitionService_AdjustUnsigned(uint16_t *value,
    uint16_t step, uint16_t minimum, uint16_t maximum, bool increase)
{
    int32_t next = (int32_t) *value +
        (increase ? (int32_t) step : -(int32_t) step);

    if (next > maximum) {
        next = maximum;
    } else if (next < minimum) {
        next = minimum;
    }
    *value = (uint16_t) next;
}

static void CompetitionService_AdjustField(bool increase, uint32_t now_ms)
{
    competition_settings_t *settings =
        (competition_settings_t *) &g_competition_service.settings;
    uint8_t field = g_competition_service.cursor;

    if (field == 0U) {
        settings->task_slot = (uint8_t) ((settings->task_slot +
            (increase ? 1U : COMPETITION_TASK_SLOT_COUNT - 1U)) %
            COMPETITION_TASK_SLOT_COUNT);
    } else if (field == 1U) {
        settings->test_action = (uint8_t) (
            (settings->test_action + (increase ? 1U : 2U)) % 3U);
    } else if (field == 2U) {
        CompetitionService_AdjustSigned(&settings->distance_mm, 100,
            COMPETITION_DISTANCE_MIN_MM, COMPETITION_DISTANCE_MAX_MM,
            increase);
    } else if (field == 3U) {
        CompetitionService_AdjustUnsigned(&settings->speed_deci_rpm, 50U,
            COMPETITION_SPEED_MIN_DECI_RPM,
            COMPETITION_SPEED_MAX_DECI_RPM, increase);
    } else if (field == 4U) {
        CompetitionService_AdjustSigned(&settings->angle_deci_deg, 150,
            10, COMPETITION_ANGLE_MAX_DECI_DEG, increase);
    } else if (field == 5U) {
        CompetitionService_AdjustUnsigned(&settings->turn_speed_deci_rpm,
            50U, COMPETITION_TURN_MIN_DECI_RPM,
            COMPETITION_TURN_MAX_DECI_RPM, increase);
    } else if (field == 6U) {
        int32_t delay = settings->start_delay_s + (increase ? 1 : -1);

        if (delay < 0) {
            delay = 0;
        } else if (delay > 9) {
            delay = 9;
        }
        settings->start_delay_s = (uint8_t) delay;
    }
    CompetitionService_MarkDirty(now_ms);
}

static void CompetitionService_HandleAdvanced(ui_input_event_t event,
    uint32_t now_ms)
{
    const parameter_metadata_t *metadata;

    if (event.kind == UI_EVENT_LONG_PRESS && event.key == UI_KEY_OK &&
        g_competition_service.editing == 0U) {
        g_competition_service.advanced_mode = 0U;
        return;
    }
    metadata = ParameterService_GetMetadataByIndex(
        g_competition_service.advanced_parameter_index);
    if (metadata == NULL) {
        return;
    }
    if (g_competition_service.editing != 0U) {
        if ((event.kind == UI_EVENT_PRESS ||
             event.kind == UI_EVENT_REPEAT) &&
            (event.key == UI_KEY_LEFT || event.key == UI_KEY_RIGHT)) {
            float delta = (event.key == UI_KEY_RIGHT) ?
                metadata->step : -metadata->step;

            g_competition_service.advanced_draft_value += delta;
            if (g_competition_service.advanced_draft_value <
                    metadata->minimum_value) {
                g_competition_service.advanced_draft_value =
                    metadata->minimum_value;
            } else if (g_competition_service.advanced_draft_value >
                    metadata->maximum_value) {
                g_competition_service.advanced_draft_value =
                    metadata->maximum_value;
            }
        } else if (event.kind == UI_EVENT_PRESS &&
            event.key == UI_KEY_OK) {
            s_parameter_transaction_id++;
            if (ParameterService_StageValue(s_parameter_transaction_id,
                    metadata->id,
                    g_competition_service.advanced_draft_value,
                    PARAMETER_ORIGIN_OLED) == PARAMETER_STATUS_STAGED) {
                float value = g_competition_service.advanced_draft_value;

                switch (metadata->id) {
                    case PARAMETER_ID_KP:
                        g_competition_service.settings.pid_kp = value;
                        break;
                    case PARAMETER_ID_KI:
                        g_competition_service.settings.pid_ki = value;
                        break;
                    case PARAMETER_ID_KD:
                        g_competition_service.settings.pid_kd = value;
                        break;
                    case PARAMETER_ID_TARGET:
                        g_competition_service.settings.pid_target = value;
                        break;
                    default:
                        break;
                }
                CompetitionService_MarkDirty(now_ms);
                g_competition_service.editing = 0U;
            }
        }
        return;
    }
    if (event.kind != UI_EVENT_PRESS && event.kind != UI_EVENT_REPEAT) {
        return;
    }
    if (event.key == UI_KEY_UP) {
        if (g_competition_service.advanced_parameter_index == 0U) {
            g_competition_service.advanced_parameter_index =
                PARAMETER_COUNT - 1U;
        } else {
            g_competition_service.advanced_parameter_index--;
        }
    } else if (event.key == UI_KEY_DOWN) {
        g_competition_service.advanced_parameter_index = (uint8_t) (
            (g_competition_service.advanced_parameter_index + 1U) %
            PARAMETER_COUNT);
    } else if (event.key == UI_KEY_OK &&
        ParameterService_GetValue(metadata->id,
            (float *) &g_competition_service.advanced_draft_value)) {
        g_competition_service.editing = 1U;
    }
}

void CompetitionService_Init(void)
{
    competition_settings_t settings;

    memset((void *) &g_competition_service, 0,
        sizeof(g_competition_service));
    memset(s_missions, 0, sizeof(s_missions));
    memset((void *) &g_competition_storage_diag, 0,
        sizeof(g_competition_storage_diag));
    CompetitionService_DefaultSettings(&settings);
    if (!CompetitionStorage_Load(&settings) ||
        !CompetitionService_SettingsValid(&settings)) {
        CompetitionService_DefaultSettings(&settings);
    }
    CompetitionStorage_SetSettingsSnapshot(&settings);
    g_competition_service.settings = settings;
    g_control_tuning_params.kp = settings.pid_kp;
    g_control_tuning_params.ki = settings.pid_ki;
    g_control_tuning_params.kd = settings.pid_kd;
    g_control_tuning_params.target = settings.pid_target;
    g_competition_service.state = (uint8_t) COMPETITION_STATE_READY;
    g_competition_service.page = (uint8_t) COMPETITION_PAGE_MAIN;
    g_competition_service.diag_view = 0U;
    g_competition_service.run_is_test = 0U;
    g_competition_service.run_has_started = 0U;
    g_competition_service.request_sequence = 0xC0000000UL;
    s_parameter_transaction_id = 0xF2000000UL;
    s_input_guard_until_ms = 0U;
#if ECHO_COMPETITION_ENABLE_BUTTON_ABORT
    s_stop_button_candidate_since_ms = 0U;
    s_stop_button_candidate_mask = 0U;
#endif
    (void) ChassisActuator_SetPivotMaximumRpm(
        (float) settings.turn_speed_deci_rpm * 0.1f);
}

void CompetitionService_Service(uint32_t now_ms)
{
    CompetitionService_UpdateTimer(now_ms);

    if (g_competition_service.health_check_state ==
            (uint8_t) COMPETITION_HEALTH_CHECK_RUNNING &&
        (uint32_t) (now_ms - s_health_check_start_ms) >=
            COMPETITION_HEALTH_CHECK_MS) {
        ZdtStepper_StopDiagnosticScan();
        g_competition_service.health_check_state =
            (uint8_t) COMPETITION_HEALTH_CHECK_COMPLETE;
    }

    if (g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_COUNTDOWN) {
        uint32_t elapsed = now_ms - s_countdown_start_ms;
        uint32_t total = (uint32_t)
            g_competition_service.settings.start_delay_s * 1000U;

        if (elapsed >= total) {
            g_competition_service.countdown_remaining_ms = 0U;
            if (s_launch_test) {
                CompetitionService_StartTest(now_ms);
            } else {
                CompetitionService_StartMission(now_ms);
            }
        } else {
            g_competition_service.countdown_remaining_ms = total - elapsed;
        }
    } else if (g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_RUNNING) {
        if (s_launch_test) {
            if (g_competition_service.settings.test_action ==
                    (uint8_t) COMPETITION_TEST_BALL_CENTER) {
                ball_balance_mission_status_t status =
                    BallBalanceService_GetMissionStatus();

                if (status == BALL_BALANCE_MISSION_FAULT) {
                    CompetitionService_StopTimer(now_ms);
                    g_competition_service.result =
                        (uint8_t) COMPETITION_RESULT_MOTION_FAULT;
                    CompetitionService_SetState(COMPETITION_STATE_FAULT);
                } else if (BallBalanceService_IsTargetSettled()) {
                    CompetitionService_StopTimer(now_ms);
                    g_competition_service.result =
                        (uint8_t) COMPETITION_RESULT_OK;
                    CompetitionService_SetState(COMPETITION_STATE_RESULT);
                }
            } else if (g_competition_service.motion_applied == 0U &&
                g_chassis_actuator_diag.last_request_sequence ==
                    g_competition_service.request_sequence) {
                g_competition_service.motion_applied = 1U;
            } else if (g_competition_service.motion_applied != 0U &&
                g_chassis_actuator_diag.output_permitted == 0U) {
                if (g_chassis_actuator_diag.last_stop_reason ==
                        (uint8_t) CHASSIS_ACTUATOR_STOP_COMPLETE) {
                    CompetitionService_StopTimer(now_ms);
                    g_competition_service.result =
                        (uint8_t) COMPETITION_RESULT_OK;
                    CompetitionService_SetState(COMPETITION_STATE_RESULT);
                } else {
                    CompetitionService_StopTimer(now_ms);
                    g_competition_service.result =
                        (uint8_t) COMPETITION_RESULT_MOTION_FAULT;
                    CompetitionService_SetState(COMPETITION_STATE_FAULT);
                }
            }
        } else {
            uint8_t slot = g_competition_service.settings.task_slot;
            competition_mission_status_t status =
                s_missions[slot].service(s_missions[slot].context, now_ms);

            if (status != COMPETITION_MISSION_RUNNING) {
                CompetitionService_StopTimer(now_ms);
                ChassisActuator_ForceSafe(status ==
                    COMPETITION_MISSION_COMPLETE ?
                    CHASSIS_ACTUATOR_STOP_COMPLETE :
                    CHASSIS_ACTUATOR_STOP_REJECTED);
                g_competition_service.result = (uint8_t) (status ==
                    COMPETITION_MISSION_COMPLETE ? COMPETITION_RESULT_OK :
                    COMPETITION_RESULT_MOTION_FAULT);
                CompetitionService_SetState(status ==
                    COMPETITION_MISSION_COMPLETE ?
                    COMPETITION_STATE_RESULT : COMPETITION_STATE_FAULT);
            }
        }
    }

    if (g_competition_service.save_pending != 0U &&
        g_competition_service.editing == 0U &&
        g_chassis_actuator_diag.output_permitted == 0U &&
        (uint32_t) (now_ms - s_settings_changed_ms) >=
            COMPETITION_SAVE_DELAY_MS) {
        bool saved = CompetitionStorage_Save(
            (const competition_settings_t *)
                &g_competition_service.settings);

        g_competition_service.save_pending = 0U;
        if (!saved) {
            g_competition_service.result =
                (uint8_t) COMPETITION_RESULT_STORAGE_FAULT;
        }
    }
}

void CompetitionService_ServicePhysicalButtons(uint8_t pressed_mask,
    uint32_t now_ms)
{
    uint8_t normalized_mask =
        pressed_mask & COMPETITION_PHYSICAL_BUTTON_MASK;
#if ECHO_COMPETITION_ENABLE_BUTTON_ABORT
    bool stop_enabled =
        g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_RUNNING ||
        g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_COUNTDOWN;
#endif

    if (s_wait_start_release) {
#if ECHO_COMPETITION_ENABLE_BUTTON_ABORT
        s_stop_button_candidate_mask = 0U;
#endif
        if (normalized_mask == 0U) {
            s_wait_start_release = false;
        }
        return;
    }
#if ECHO_COMPETITION_ENABLE_BUTTON_ABORT
    if (!stop_enabled || normalized_mask == 0U) {
        s_stop_button_candidate_mask = 0U;
        return;
    }
    if (normalized_mask != s_stop_button_candidate_mask) {
        s_stop_button_candidate_mask = normalized_mask;
        s_stop_button_candidate_since_ms = now_ms;
        return;
    }
    if ((uint32_t) (now_ms - s_stop_button_candidate_since_ms) >=
            COMPETITION_PHYSICAL_STOP_DEBOUNCE_MS) {
        s_stop_button_candidate_mask = 0U;
        CompetitionService_StopActive(now_ms);
    }
#else
    (void) normalized_mask;
    (void) now_ms;
#endif
}

void CompetitionService_HandleEvent(ui_input_event_t event,
    uint32_t now_ms)
{
    uint8_t field_count;

    if (event.kind == UI_EVENT_NONE || event.kind == UI_EVENT_TIMEOUT) {
        return;
    }
    if ((int32_t) (now_ms - s_input_guard_until_ms) < 0) {
        return;
    }
    if (g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_RUNNING ||
        g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_COUNTDOWN) {
#if ECHO_COMPETITION_ENABLE_BUTTON_ABORT
        CompetitionService_StopActive(now_ms);
#endif
        return;
    }
    /* After a task has stopped, any deliberate key event returns to the
     * READY main page. During RUNNING/COUNTDOWN the branch above still
     * prevents accidental button input from affecting the mission. */
    if ((g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_RESULT ||
         g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_ABORTED ||
         g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_FAULT) &&
        (event.kind == UI_EVENT_PRESS ||
         event.kind == UI_EVENT_REPEAT ||
         event.kind == UI_EVENT_LONG_PRESS)) {
        uint8_t slot = g_competition_service.settings.task_slot;

        if (g_competition_service.run_is_test != 0U &&
            g_competition_service.settings.test_action ==
                (uint8_t) COMPETITION_TEST_BALL_CENTER) {
            BallBalanceService_RequestAbort();
        } else if (g_competition_service.state ==
                (uint8_t) COMPETITION_STATE_RESULT &&
            slot < COMPETITION_TASK_SLOT_COUNT &&
            s_missions[slot].stop != NULL) {
            s_missions[slot].stop(s_missions[slot].context);
        }
        g_competition_service.result =
            (uint8_t) COMPETITION_RESULT_NONE;
        g_competition_service.run_has_started = 0U;
        g_competition_service.page =
            (uint8_t) COMPETITION_PAGE_MAIN;
        g_competition_service.cursor = 0U;
        g_competition_service.editing = 0U;
        g_competition_service.advanced_mode = 0U;
        CompetitionService_SetState(COMPETITION_STATE_READY);
        return;
    }
    if (HMissionService_RuntimeCalibrationActive()) {
        if (event.kind == UI_EVENT_PRESS && event.key == UI_KEY_UP) {
            (void) HMissionService_CaptureRuntimeWhite(now_ms);
        } else if (event.kind == UI_EVENT_PRESS &&
            event.key == UI_KEY_DOWN) {
            (void) HMissionService_CaptureRuntimeBlack(now_ms);
        } else if (event.kind == UI_EVENT_PRESS &&
            event.key == UI_KEY_OK) {
            HMissionService_AbortRuntimeCalibration();
        }
        return;
    }
    if (g_competition_service.advanced_mode != 0U) {
        CompetitionService_HandleAdvanced(event, now_ms);
        return;
    }
    if (g_competition_service.page ==
            (uint8_t) COMPETITION_PAGE_DIAG &&
        g_competition_service.diag_view == 0U &&
        event.key == UI_KEY_OK && event.kind == UI_EVENT_LONG_PRESS) {
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_NONE);
        (void) ImuService_RequestRecalibration();
        return;
    }
    if (g_competition_service.page ==
            (uint8_t) COMPETITION_PAGE_DIAG &&
        g_competition_service.diag_view != 0U &&
        event.key == UI_KEY_OK && event.kind == UI_EVENT_LONG_PRESS) {
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_NONE);
        if (ZdtStepper_StartDiagnosticScan()) {
            s_health_check_start_ms = now_ms;
            g_competition_service.health_check_count++;
            g_competition_service.health_check_state =
                (uint8_t) COMPETITION_HEALTH_CHECK_RUNNING;
        } else {
            g_competition_service.health_check_state =
                (uint8_t) COMPETITION_HEALTH_CHECK_FAILED;
        }
        return;
    }
    if (g_competition_service.page ==
            (uint8_t) COMPETITION_PAGE_MAIN) {
        if (g_competition_service.state ==
                (uint8_t) COMPETITION_STATE_READY &&
            event.key == UI_KEY_OK &&
            event.kind == UI_EVENT_LONG_PRESS) {
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_NONE);
            (void) HMissionService_BeginRuntimeCalibration();
            return;
        }
        if ((event.kind == UI_EVENT_PRESS ||
             event.kind == UI_EVENT_REPEAT) &&
            (event.key == UI_KEY_UP || event.key == UI_KEY_DOWN)) {
            uint8_t delta = (event.key == UI_KEY_UP) ? 1U :
                COMPETITION_TASK_SLOT_COUNT - 1U;

            g_competition_service.settings.task_slot = (uint8_t) (
                (g_competition_service.settings.task_slot + delta) %
                COMPETITION_TASK_SLOT_COUNT);
            CompetitionService_SetState(COMPETITION_STATE_READY);
            CompetitionService_MarkDirty(now_ms);
            return;
        }
        if (event.key == UI_KEY_OK && event.kind == UI_EVENT_PRESS) {
            if (g_competition_service.state ==
                    (uint8_t) COMPETITION_STATE_RESULT ||
                g_competition_service.state ==
                    (uint8_t) COMPETITION_STATE_ABORTED ||
                g_competition_service.state ==
                    (uint8_t) COMPETITION_STATE_FAULT) {
                uint8_t slot = g_competition_service.settings.task_slot;

                if (g_competition_service.run_is_test != 0U &&
                    g_competition_service.settings.test_action ==
                        (uint8_t) COMPETITION_TEST_BALL_CENTER) {
                    BallBalanceService_RequestAbort();
                } else if (g_competition_service.state ==
                        (uint8_t) COMPETITION_STATE_RESULT &&
                    slot < COMPETITION_TASK_SLOT_COUNT &&
                    s_missions[slot].stop != NULL) {
                    s_missions[slot].stop(s_missions[slot].context);
                }
                g_competition_service.result =
                    (uint8_t) COMPETITION_RESULT_NONE;
                g_competition_service.run_has_started = 0U;
                CompetitionService_SetState(COMPETITION_STATE_READY);
            } else if (g_competition_service.state ==
                    (uint8_t) COMPETITION_STATE_READY) {
                s_launch_test = false;
                s_wait_start_release = true;
                g_competition_service.result =
                    (uint8_t) COMPETITION_RESULT_NONE;
                g_competition_service.motion_applied = 0U;
                g_competition_service.page =
                    (uint8_t) COMPETITION_PAGE_MAIN;
                CompetitionService_StartMission(now_ms);
            }
            return;
        }
    } else if (g_competition_service.page ==
            (uint8_t) COMPETITION_PAGE_TEST) {
        field_count = COMPETITION_TEST_FIELD_COUNT;
        if (event.key == UI_KEY_OK &&
            event.kind == UI_EVENT_LONG_PRESS &&
            g_competition_service.editing == 0U) {
            if (g_competition_service.cursor == 0U) {
                g_competition_service.advanced_mode = 1U;
                g_competition_service.advanced_parameter_index = 0U;
            } else {
                CompetitionService_BeginCountdown(true, now_ms);
            }
            return;
        }
        if (event.key == UI_KEY_OK && event.kind == UI_EVENT_PRESS) {
            g_competition_service.editing =
                g_competition_service.editing ? 0U : 1U;
            return;
        }
        if (g_competition_service.editing != 0U &&
            (event.kind == UI_EVENT_PRESS ||
             event.kind == UI_EVENT_REPEAT) &&
            (event.key == UI_KEY_UP || event.key == UI_KEY_DOWN ||
             event.key == UI_KEY_LEFT || event.key == UI_KEY_RIGHT)) {
            CompetitionService_AdjustField(
                event.key == UI_KEY_UP || event.key == UI_KEY_RIGHT,
                now_ms);
            return;
        }
        if (g_competition_service.editing == 0U &&
            (event.kind == UI_EVENT_PRESS ||
             event.kind == UI_EVENT_REPEAT) &&
            (event.key == UI_KEY_UP || event.key == UI_KEY_DOWN)) {
            if (event.key == UI_KEY_UP) {
                g_competition_service.cursor =
                    (g_competition_service.cursor == 0U) ?
                    field_count - 1U :
                    g_competition_service.cursor - 1U;
            } else {
                g_competition_service.cursor = (uint8_t) (
                    (g_competition_service.cursor + 1U) % field_count);
            }
            return;
        }
    } else if (g_competition_service.page ==
            (uint8_t) COMPETITION_PAGE_DIAG &&
        g_competition_service.editing == 0U &&
        (event.kind == UI_EVENT_PRESS || event.kind == UI_EVENT_REPEAT) &&
        (event.key == UI_KEY_UP || event.key == UI_KEY_DOWN)) {
        g_competition_service.diag_view = (event.key == UI_KEY_DOWN) ?
            1U : 0U;
        return;
    }

    if (g_competition_service.editing == 0U &&
        event.kind == UI_EVENT_PRESS &&
        (event.key == UI_KEY_LEFT || event.key == UI_KEY_RIGHT)) {
        uint8_t page = g_competition_service.page;

        if (event.key == UI_KEY_RIGHT) {
            page = (uint8_t) ((page + 1U) % COMPETITION_PAGE_COUNT);
        } else {
            page = (page == 0U) ? COMPETITION_PAGE_COUNT - 1U : page - 1U;
        }
        g_competition_service.page = page;
        g_competition_service.cursor = 0U;
        if (g_competition_service.state ==
            (uint8_t) COMPETITION_STATE_ARMED) {
            CompetitionService_SetState(COMPETITION_STATE_READY);
        }
    }
}

bool CompetitionService_RegisterMission(uint8_t slot,
    const competition_mission_t *mission)
{
    if (slot >= COMPETITION_TASK_SLOT_COUNT || mission == NULL ||
        mission->start == NULL || mission->service == NULL) {
        return false;
    }
    s_missions[slot] = *mission;
    return true;
}

void CompetitionService_GetSnapshot(
    competition_service_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    taskENTER_CRITICAL();
    *snapshot = g_competition_service;
    taskEXIT_CRITICAL();
}

const char *CompetitionService_StateName(competition_state_t state)
{
    switch (state) {
        case COMPETITION_STATE_READY: return "READY";
        case COMPETITION_STATE_ARMED: return "ARMED";
        case COMPETITION_STATE_COUNTDOWN: return "COUNT";
        case COMPETITION_STATE_RUNNING: return "RUN";
        case COMPETITION_STATE_RESULT: return "DONE";
        case COMPETITION_STATE_ABORTED: return "STOP";
        case COMPETITION_STATE_FAULT: return "FAULT";
        default: return "?";
    }
}

const char *CompetitionService_ResultName(competition_result_t result)
{
    switch (result) {
        case COMPETITION_RESULT_NONE: return "NONE";
        case COMPETITION_RESULT_OK: return "OK";
        case COMPETITION_RESULT_ABORT: return "ABORT";
        case COMPETITION_RESULT_NO_TASK: return "EMPTY";
        case COMPETITION_RESULT_REJECTED: return "REJECT";
        case COMPETITION_RESULT_MOTION_FAULT: return "MOTION";
        case COMPETITION_RESULT_STORAGE_FAULT: return "FLASH";
        default: return "?";
    }
}
