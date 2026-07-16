#ifndef ECHO_MOTOR_PROFILE_CONFIG_H
#define ECHO_MOTOR_PROFILE_CONFIG_H

#define ECHO_MOTOR_PROFILE_MG370 1U
#define ECHO_MOTOR_PROFILE_513X  2U

/* Override this single macro from the compiler command line if required. */
#ifndef ECHO_MOTOR_PROFILE_SELECTION
#define ECHO_MOTOR_PROFILE_SELECTION ECHO_MOTOR_PROFILE_MG370
#endif

#endif
