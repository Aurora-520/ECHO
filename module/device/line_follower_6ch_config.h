#ifndef ECHO_LINE_FOLLOWER_6CH_CONFIG_H
#define ECHO_LINE_FOLLOWER_6CH_CONFIG_H

#include "line_follower_6ch.h"

/*
 * Default to the documented register order so the drop-in reflectance backend
 * builds without an extra Keil define. If the sensor is mounted in the opposite
 * direction, change this one macro; no control-layer code needs to move.
 */
#ifndef LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER
#define LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER \
    LINE_FOLLOWER6_CHANNEL_ORDER_1_TO_6
#endif

#endif
