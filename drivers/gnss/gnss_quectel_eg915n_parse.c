/*
 * Copyright (c) 2026 INOMO Technologies AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "gnss_nmea0183.h"
#include "gnss_parse.h"
#include "gnss_quectel_eg915n_parse.h"

/* Argument positions within a +QGPSLOC response to AT+QGPSLOC=1. */
#define QGPSLOC_ARGV_UTC       1
#define QGPSLOC_ARGV_LATITUDE  2
#define QGPSLOC_ARGV_NS	       3
#define QGPSLOC_ARGV_LONGITUDE 4
#define QGPSLOC_ARGV_EW	       5
#define QGPSLOC_ARGV_HDOP      6
#define QGPSLOC_ARGV_ALTITUDE  7
#define QGPSLOC_ARGV_FIX       8
#define QGPSLOC_ARGV_COG       9
#define QGPSLOC_ARGV_SPKM      10
#define QGPSLOC_ARGV_DATE      12
#define QGPSLOC_ARGV_NSAT      13

/* +QGPSLOC <fix> values. Anything else means the module has no position. */
#define QGPSLOC_FIX_2D 2
#define QGPSLOC_FIX_3D 3

int gnss_quectel_eg915n_parse_qgpsloc(const char *const *argv, uint16_t argc,
				      struct gnss_data *data)
{
	int64_t milli;
	int64_t ndeg;
	int32_t i32;

	__ASSERT(argv != NULL, "argv argument must be provided");
	__ASSERT(data != NULL, "data argument must be provided");

	if (argc != GNSS_QUECTEL_EG915N_QGPSLOC_ARGC) {
		return -EINVAL;
	}

	memset(data, 0, sizeof(*data));

	if (gnss_parse_atoi(argv[QGPSLOC_ARGV_FIX], 10, &i32) < 0) {
		return -EINVAL;
	}

	if ((i32 != QGPSLOC_FIX_2D) && (i32 != QGPSLOC_FIX_3D)) {
		data->info.fix_status = GNSS_FIX_STATUS_NO_FIX;
		data->info.fix_quality = GNSS_FIX_QUALITY_INVALID;
		return 0;
	}

	/* The module reports positioning mode, not fix quality. */
	data->info.fix_status = GNSS_FIX_STATUS_GNSS_FIX;
	data->info.fix_quality = GNSS_FIX_QUALITY_GNSS_SPS;

	if ((gnss_nmea0183_parse_hhmmss(argv[QGPSLOC_ARGV_UTC], &data->utc) < 0) ||
	    (gnss_nmea0183_parse_ddmmyy(argv[QGPSLOC_ARGV_DATE], &data->utc) < 0)) {
		return -EINVAL;
	}

	if ((argv[QGPSLOC_ARGV_NS][0] != 'N') && (argv[QGPSLOC_ARGV_NS][0] != 'S')) {
		return -EINVAL;
	}

	if ((argv[QGPSLOC_ARGV_EW][0] != 'E') && (argv[QGPSLOC_ARGV_EW][0] != 'W')) {
		return -EINVAL;
	}

	if (gnss_nmea0183_ddmm_mmmm_to_ndeg(argv[QGPSLOC_ARGV_LATITUDE], &ndeg) < 0) {
		return -EINVAL;
	}

	data->nav_data.latitude = (argv[QGPSLOC_ARGV_NS][0] == 'S') ? -ndeg : ndeg;

	if (gnss_nmea0183_ddmm_mmmm_to_ndeg(argv[QGPSLOC_ARGV_LONGITUDE], &ndeg) < 0) {
		return -EINVAL;
	}

	data->nav_data.longitude = (argv[QGPSLOC_ARGV_EW][0] == 'W') ? -ndeg : ndeg;

	/* Altitude is in metres. */
	if (gnss_parse_dec_to_milli(argv[QGPSLOC_ARGV_ALTITUDE], &milli) == 0) {
		data->nav_data.altitude = (int32_t)milli;
	}

	/* Empty while the module cannot measure speed, including standing still. */
	if ((argv[QGPSLOC_ARGV_COG][0] != '\0') &&
	    (gnss_parse_dec_to_milli(argv[QGPSLOC_ARGV_COG], &milli) == 0) && (milli >= 0)) {
		data->nav_data.bearing = (uint32_t)milli;
	}

	/* km/h thousandths to mm/s. */
	if ((gnss_parse_dec_to_milli(argv[QGPSLOC_ARGV_SPKM], &milli) == 0) && (milli >= 0)) {
		data->nav_data.speed = (uint32_t)((milli * 1000) / 3600);
	}

	if ((gnss_parse_dec_to_milli(argv[QGPSLOC_ARGV_HDOP], &milli) == 0) && (milli >= 0)) {
		data->info.hdop = (uint32_t)milli;
	}

	if ((gnss_parse_atoi(argv[QGPSLOC_ARGV_NSAT], 10, &i32) == 0) && (i32 >= 0)) {
		data->info.satellites_cnt = (uint16_t)i32;
	}

	return 0;
}

int gnss_quectel_eg915n_strip_qgpsgnmea_prefix(char **argv, uint16_t argc)
{
	const size_t len = sizeof(GNSS_QUECTEL_EG915N_QGPSGNMEA_PREFIX) - 1;

	__ASSERT(argv != NULL, "argv argument must be provided");

	if (argc < 1) {
		return -EINVAL;
	}

	if (strncmp(argv[0], GNSS_QUECTEL_EG915N_QGPSGNMEA_PREFIX, len) != 0) {
		return -EINVAL;
	}

	argv[0] += len;
	return 0;
}
