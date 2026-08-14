/*
 * Copyright (c) 2026 INOMO Technologies AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_GNSS_GNSS_QUECTEL_EG915N_PARSE_H_
#define ZEPHYR_DRIVERS_GNSS_GNSS_QUECTEL_EG915N_PARSE_H_

#include <zephyr/drivers/gnss.h>
#include <zephyr/types.h>

/* Argument count of a +QGPSLOC response, including argv[0]. */
#define GNSS_QUECTEL_EG915N_QGPSLOC_ARGC 14

/**
 * @brief Parse the arguments of a +QGPSLOC response to AT+QGPSLOC=1
 *
 * A response reporting no position is parsed successfully, and yields a
 * position with fix status @ref GNSS_FIX_STATUS_NO_FIX.
 *
 * @param argv Response arguments, where argv[0] is the matched prefix
 * @param argc Number of response arguments
 * @param data Destination for the parsed position
 *
 * @retval 0 if the response was parsed
 * @retval -EINVAL if the response could not be parsed
 */
int gnss_quectel_eg915n_parse_qgpsloc(const char *const *argv, uint16_t argc,
				      struct gnss_data *data);

/* Prefix on every NMEA sentence the module reports over AT. */
#define GNSS_QUECTEL_EG915N_QGPSGNMEA_PREFIX "+QGPSGNMEA: "

/**
 * @brief Point argv[0] past the +QGPSGNMEA prefix
 *
 * The shared GSV parser reads the constellation from the third character of the
 * message id, so it must see "$GPGSV," rather than the AT response prefix.
 *
 * @param argv Response arguments, where argv[0] is the prefix and message id
 * @param argc Number of response arguments
 *
 * @retval 0 if the prefix was stripped
 * @retval -EINVAL if argv[0] does not start with the prefix
 */
int gnss_quectel_eg915n_strip_qgpsgnmea_prefix(char **argv, uint16_t argc);

#endif /* ZEPHYR_DRIVERS_GNSS_GNSS_QUECTEL_EG915N_PARSE_H_ */
