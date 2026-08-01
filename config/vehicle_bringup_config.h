#ifndef ECHO_VEHICLE_BRINGUP_CONFIG_H
#define ECHO_VEHICLE_BRINGUP_CONFIG_H

/* Formal installed-hardware baseline for the assembled 513X chassis. */
#define ECHO_ENABLE_REFLECTANCE 1U
#define ECHO_REFLECTANCE_BACKEND_ADC8 1U
#define ECHO_REFLECTANCE_BACKEND_I2C6 2U
/* Change only this selector when swapping the physical line sensor. */
#define ECHO_REFLECTANCE_BACKEND ECHO_REFLECTANCE_BACKEND_ADC8
#if (ECHO_REFLECTANCE_BACKEND != ECHO_REFLECTANCE_BACKEND_ADC8) && \
    (ECHO_REFLECTANCE_BACKEND != ECHO_REFLECTANCE_BACKEND_I2C6)
#error "Select a supported reflectance backend"
#endif
#define ECHO_ENABLE_TFMINI      0U
#define ECHO_ENABLE_BALL_VISION 1U
/* MaixCAM uses UART1 PA9/RX; UART2 remains owned by the ESP link. */
#define ECHO_BALL_VISION_USE_UART2 0U
#define ECHO_ENABLE_ESP_LINK    1U
#define ECHO_ENABLE_OLED        1U
#define ECHO_ENABLE_IMU         1U
#define ECHO_IMU_DIAGNOSTIC_CAPTURE 0U

/*
 * Debug firmware aborts a running/countdown mission on any physical key.
 * Set this to 0U for a locked competition build.
 */
#ifndef ECHO_COMPETITION_ENABLE_BUTTON_ABORT
#define ECHO_COMPETITION_ENABLE_BUTTON_ABORT 1U
#endif
#if (ECHO_COMPETITION_ENABLE_BUTTON_ABORT != 0U) && \
    (ECHO_COMPETITION_ENABLE_BUTTON_ABORT != 1U)
#error "ECHO_COMPETITION_ENABLE_BUTTON_ABORT must be 0U or 1U"
#endif

/* Installed MPU6050 six-face calibration at approximately 30.1--30.5 C. */
#define ECHO_IMU_ACCEL_BIAS_X_G  0.00825236f
#define ECHO_IMU_ACCEL_BIAS_Y_G -0.01317945f
#define ECHO_IMU_ACCEL_BIAS_Z_G  0.07864387f
#define ECHO_IMU_ACCEL_SCALE_X   1.00036323f
#define ECHO_IMU_ACCEL_SCALE_Y   0.99782687f
#define ECHO_IMU_ACCEL_SCALE_Z   0.99201797f

#endif
