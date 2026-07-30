#ifndef ECHO_LINE_FOLLOWER_6CH_CONFIG_H
#define ECHO_LINE_FOLLOWER_6CH_CONFIG_H

#include "line_follower_6ch.h"

/*
 * Select only after moving each physical probe over the target line.
 * The replacement BSP backend rejects UNCONFIRMED at compile time.
 */
#ifndef LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER
#define LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER \
    LINE_FOLLOWER6_CHANNEL_ORDER_UNCONFIRMED
#endif

#endif
