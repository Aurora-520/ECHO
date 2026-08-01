#ifndef ECHO_H_BALL_CONTROL_CONFIG_H
#define ECHO_H_BALL_CONTROL_CONFIG_H

/*
 * Provisional linkage defaults. H_BALL_MOTOR_POLARITY is the first item to
 * verify on the assembled mechanism: a positive controller output must
 * accelerate the ball toward the camera's positive coordinate direction.
 */
#define H_BALL_MOTOR_POLARITY                         1
#define H_BALL_VISION_POLARITY                       -1
#define H_BALL_POSITION_VELOCITY_GAIN_PER_S          2.2f
#define H_BALL_NEGATIVE_POSITION_VELOCITY_GAIN_PER_S 2.6f
#define H_BALL_MAXIMUM_VELOCITY_MM_S                80.0f
#define H_BALL_SEARCH_GAIN_MDEG_PER_MM_S            700.0f
#define H_BALL_MAXIMUM_SEARCH_RATE_MDEG_PER_S     35000.0f
#define H_BALL_VELOCITY_NOISE_DEADBAND_MM_S          12.0f
#define H_BALL_MOTION_DETECTION_VELOCITY_MM_S          8.0f
#define H_BALL_MOTION_DETECTION_DISPLACEMENT_MM         1.0f
#define H_BALL_SEARCH_VELOCITY_DAMPING_MDEG_PER_MM_S 120.0f
#define H_BALL_VELOCITY_FEEDBACK_MDEG_PER_MM_S       180.0f
#define H_BALL_REVERSAL_BRAKING_MDEG_PER_MM_S        260.0f
#define H_BALL_MOVING_BIAS_DECAY_MDEG_PER_S        60000.0f
#define H_BALL_HOLD_BIAS_DECAY_MDEG_PER_S             500.0f
#define H_BALL_STALL_REACQUIRE_ERROR_MM                7.0f
#define H_BALL_STALL_REACQUIRE_SAMPLES                  12U
#define H_BALL_INTEGRAL_LIMIT_MILLIDEGREES       40000.0f
#define H_BALL_MAXIMUM_OUTPUT_MILLIDEGREES       40000.0f
#define H_BALL_H5_MAXIMUM_OUTPUT_MILLIDEGREES    40000.0f
#define H_BALL_H5_INTEGRAL_LIMIT_MILLIDEGREES    40000.0f
#define H_BALL_MAXIMUM_SLEW_MILLIDEGREES_PER_S  100000.0f
#define H_BALL_MOTOR_SPEED_RPM                         25U
#define H_BALL_MOTOR_ACCELERATION_RPM_S               150U
#define H_BALL_COMMAND_PERIOD_US                    20000U
#define H_BALL_VISION_HOLD_US                      250000U
#define H_BALL_DRIVE_VISION_HOLD_US                500000U
#define H_BALL_STARTUP_TIMEOUT_US                 1500000U
#define H_BALL_H3_TIMEOUT_US                            0U
#define H_BALL_H3_POSITIVE_TARGET_DECIMM              500
#define H_BALL_H3_NEGATIVE_TARGET_DECIMM             -500
#define H_BALL_H3_POSITIVE_TOLERANCE_DECIMM             70
#define H_BALL_H3_POSITIVE_BRAKE_DECIMM                390
#define H_BALL_H3_POSITIVE_BRAKE_ENTRY_VELOCITY_MM_S     20.0f
#define H_BALL_H3_POSITIVE_BRAKE_GAIN_MDEG_PER_MM_S     300.0f
#define H_BALL_H3_POSITIVE_VELOCITY_MM_S                 20
#define H_BALL_H3_FINAL_TOLERANCE_DECIMM               100
#define H_BALL_H3_FINAL_VELOCITY_MM_S                   15
#define H_BALL_H3_FINAL_SETTLE_US                   150000U
#define H_BALL_H3_START_VELOCITY_LIMIT_MM_S              60
#define H_BALL_HOLD_TARGET_LIMIT_DECIMM                1000
#define H_BALL_HOLD_TOLERANCE_DECIMM                    100
#define H_BALL_HOLD_VELOCITY_MM_S                        15
#define H_BALL_HOLD_SETTLE_US                        150000U

#endif
