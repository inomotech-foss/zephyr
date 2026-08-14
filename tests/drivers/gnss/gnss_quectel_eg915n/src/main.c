/*
 * Copyright (c) 2026 INOMO Technologies AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/modem/chat.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include <modem_backend_mock.h>

#include "gnss_nmea0183.h"
#include "gnss_quectel_eg915n_parse.h"

/* Allow one microdegree of rounding from the ddmm.mmmm conversion. */
#define MAX_NDEG_ERROR 1000

#define ASSERT_NDEG(actual, expected)                                                              \
	zassert_within(actual, (int64_t)(expected), MAX_NDEG_ERROR, "Expected %lld, got %lld",     \
		       (long long)(expected), (long long)(actual))

/* Speed 36.0 km/h is exactly 10000 mm/s. */
static const char *const qgpsloc_moving[] = {
	"+QGPSLOC: ", "123519.00", "4807.038000", "N", "01131.000000", "E",
	"1.2",	      "545.4",	   "3",		  "84.40", "36.0",	  "19.4",
	"230394",     "10",
};

/* A 3D fix while stationary, where the module reports no course over ground. */
static const char *const qgpsloc_stationary[] = {
	"+QGPSLOC: ", "084440.0", "2238.694234", "N",  "11402.164478", "E",
	"0.6",	      "29.4",	  "3",		 "",   "0.0",	       "0.0",
	"010924",     "08",
};

/* A 3D fix in the southern and western hemispheres. */
static const char *const qgpsloc_south_west[] = {
	"+QGPSLOC: ", "123519.00", "4807.038000", "S", "01131.000000", "W",
	"1.2",	      "545.4",	   "3",		  "84.40", "36.0",	  "19.4",
	"230394",     "10",
};

/* The module answered, but reports positioning mode 1, meaning no position. */
static const char *const qgpsloc_no_fix[] = {
	"+QGPSLOC: ", "084440.0", "0000.000000", "N",  "00000.000000", "E",
	"0.0",	      "0.0",	  "1",		 "",   "0.0",	       "0.0",
	"010924",     "00",
};

/* A response cut short, an invalid hemisphere, and a malformed date. */
static const char *const qgpsloc_truncated[] = {"+QGPSLOC: ", "123519.00", "4807.038000", "N"};

static const char *const qgpsloc_bad_hemisphere[] = {
	"+QGPSLOC: ", "123519.00", "4807.038000", "X", "01131.000000", "E",
	"1.2",	      "545.4",	   "3",		  "84.40", "36.0",	  "19.4",
	"230394",     "10",
};

static const char *const qgpsloc_bad_date[] = {
	"+QGPSLOC: ", "123519.00", "4807.038000", "N", "01131.000000", "E",
	"1.2",	      "545.4",	   "3",		  "84.40", "36.0",	  "19.4",
	"99",	      "10",
};

ZTEST(gnss_quectel_eg915n, test_parse_qgpsloc_moving)
{
	struct gnss_data data;

	zassert_ok(gnss_quectel_eg915n_parse_qgpsloc(qgpsloc_moving,
						     ARRAY_SIZE(qgpsloc_moving), &data));

	zassert_equal(data.info.fix_status, GNSS_FIX_STATUS_GNSS_FIX);
	zassert_equal(data.info.fix_quality, GNSS_FIX_QUALITY_GNSS_SPS);

	/* 48 degrees 07.038000 minutes north, so 48.1173 degrees */
	ASSERT_NDEG(data.nav_data.latitude, 48117300000);
	/* 11 degrees 31.000000 minutes east */
	ASSERT_NDEG(data.nav_data.longitude, 11516666667);

	zassert_equal(data.nav_data.altitude, 545400, "Altitude should be in millimetres");
	zassert_equal(data.nav_data.bearing, 84400, "Bearing should be in millidegrees");
	zassert_equal(data.nav_data.speed, 10000, "36.0 km/h should be 10000 mm/s");

	zassert_equal(data.info.hdop, 1200, "HDOP should be scaled by 1000");
	zassert_equal(data.info.satellites_cnt, 10);

	zassert_equal(data.utc.hour, 12);
	zassert_equal(data.utc.minute, 35);
	zassert_equal(data.utc.millisecond, 19000);
	zassert_equal(data.utc.month_day, 23);
	zassert_equal(data.utc.month, 3);
	zassert_equal(data.utc.century_year, 94);
}

ZTEST(gnss_quectel_eg915n, test_parse_qgpsloc_stationary)
{
	struct gnss_data data;

	/* An empty course over ground must not discard the rest of the fix. */
	zassert_ok(gnss_quectel_eg915n_parse_qgpsloc(qgpsloc_stationary,
						     ARRAY_SIZE(qgpsloc_stationary), &data));

	zassert_equal(data.info.fix_status, GNSS_FIX_STATUS_GNSS_FIX);
	zassert_equal(data.nav_data.bearing, 0, "Bearing should default to zero");
	zassert_equal(data.nav_data.speed, 0);

	/* 22 degrees 38.694234 minutes north */
	ASSERT_NDEG(data.nav_data.latitude, 22644903900);
	/* 114 degrees 2.164478 minutes east */
	ASSERT_NDEG(data.nav_data.longitude, 114036074633);

	zassert_equal(data.nav_data.altitude, 29400);
	zassert_equal(data.info.hdop, 600);
	zassert_equal(data.info.satellites_cnt, 8, "Leading zero should be accepted");
}

ZTEST(gnss_quectel_eg915n, test_parse_qgpsloc_south_west)
{
	struct gnss_data data;

	zassert_ok(gnss_quectel_eg915n_parse_qgpsloc(qgpsloc_south_west,
						     ARRAY_SIZE(qgpsloc_south_west), &data));

	ASSERT_NDEG(data.nav_data.latitude, -48117300000);
	ASSERT_NDEG(data.nav_data.longitude, -11516666667);
}

ZTEST(gnss_quectel_eg915n, test_parse_qgpsloc_no_fix)
{
	struct gnss_data data;

	zassert_ok(gnss_quectel_eg915n_parse_qgpsloc(qgpsloc_no_fix,
						     ARRAY_SIZE(qgpsloc_no_fix), &data));

	zassert_equal(data.info.fix_status, GNSS_FIX_STATUS_NO_FIX);
	zassert_equal(data.info.fix_quality, GNSS_FIX_QUALITY_INVALID);
	zassert_equal(data.nav_data.latitude, 0, "Position must not be reported without a fix");
	zassert_equal(data.nav_data.longitude, 0, "Position must not be reported without a fix");
}

ZTEST(gnss_quectel_eg915n, test_parse_qgpsloc_rejects_bad_input)
{
	struct gnss_data data;

	zassert_equal(gnss_quectel_eg915n_parse_qgpsloc(qgpsloc_truncated,
							ARRAY_SIZE(qgpsloc_truncated), &data),
		      -EINVAL, "A truncated response should be rejected");

	zassert_equal(gnss_quectel_eg915n_parse_qgpsloc(qgpsloc_bad_hemisphere,
							ARRAY_SIZE(qgpsloc_bad_hemisphere), &data),
		      -EINVAL, "An invalid hemisphere should be rejected");

	zassert_equal(gnss_quectel_eg915n_parse_qgpsloc(qgpsloc_bad_date,
							ARRAY_SIZE(qgpsloc_bad_date), &data),
		      -EINVAL, "A malformed date should be rejected");
}

/* Check modem_chat splits a real +QGPSLOC line the way the parser expects. */
static struct modem_chat chat;
static uint8_t chat_delimiter[] = {'\r', '\n'};
static uint8_t chat_receive_buf[256];
static uint8_t *chat_argv[32];

static struct modem_backend_mock mock;
static uint8_t mock_rx_buf[256];
static uint8_t mock_tx_buf[256];
static struct modem_pipe *mock_pipe;

static uint16_t captured_argc;
static char captured_argv[GNSS_QUECTEL_EG915N_QGPSLOC_ARGC][32];
static const char *captured_argv_ptr[GNSS_QUECTEL_EG915N_QGPSLOC_ARGC];

static const char **captured_argv_ptrs(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(captured_argv_ptr); i++) {
		captured_argv_ptr[i] = captured_argv[i];
	}

	return captured_argv_ptr;
}

static void on_qgpsloc(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	captured_argc = argc;

	for (uint16_t i = 0; (i < argc) && (i < ARRAY_SIZE(captured_argv)); i++) {
		strncpy(captured_argv[i], argv[i], sizeof(captured_argv[0]) - 1);
		captured_argv[i][sizeof(captured_argv[0]) - 1] = '\0';
	}
}

static uint16_t gsv_calls;
static int gsv_strip_ret;
static int gsv_header_ret;
static struct gnss_nmea0183_gsv_header gsv_header;
static struct gnss_satellite gsv_satellites[16];
static uint16_t gsv_satellites_len;

static void on_gsv(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	int ret;

	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	gsv_calls++;

	gsv_strip_ret = gnss_quectel_eg915n_strip_qgpsgnmea_prefix(argv, argc);
	if (gsv_strip_ret < 0) {
		return;
	}

	gsv_header_ret = gnss_nmea0183_parse_gsv_header((const char **)argv, argc, &gsv_header);
	if (gsv_header_ret < 0) {
		return;
	}

	ret = gnss_nmea0183_parse_gsv_svs((const char **)argv, argc,
					  &gsv_satellites[gsv_satellites_len],
					  ARRAY_SIZE(gsv_satellites) - gsv_satellites_len);
	if (ret < 0) {
		return;
	}

	gsv_satellites_len += (uint16_t)ret;
}

/* Same match specification as the one the driver registers. */
MODEM_CHAT_MATCHES_DEFINE(test_unsol_matches,
			  MODEM_CHAT_MATCH("+QGPSLOC: ", ",", on_qgpsloc),
			  MODEM_CHAT_MATCH_WILDCARD(GNSS_QUECTEL_EG915N_QGPSGNMEA_PREFIX "$??GSV,",
						    ",*", on_gsv));

ZTEST(gnss_quectel_eg915n, test_chat_splits_qgpsloc_response)
{
	static const char response[] =
		"\r\n+QGPSLOC: 123519.00,4807.038000,N,01131.000000,E,"
		"1.2,545.4,3,84.40,36.0,19.4,230394,10\r\n";
	struct gnss_data data;

	captured_argc = 0;
	modem_backend_mock_put(&mock, (const uint8_t *)response, sizeof(response) - 1);
	k_msleep(100);

	zassert_equal(captured_argc, GNSS_QUECTEL_EG915N_QGPSLOC_ARGC,
		      "Expected %d arguments, got %u", GNSS_QUECTEL_EG915N_QGPSLOC_ARGC,
		      captured_argc);

	/* The hemisphere must land in its own argument, not glued to the coordinate. */
	zassert_str_equal(captured_argv[2], "4807.038000");
	zassert_str_equal(captured_argv[3], "N");
	zassert_str_equal(captured_argv[4], "01131.000000");
	zassert_str_equal(captured_argv[5], "E");
	zassert_str_equal(captured_argv[13], "10");

	/* What the chat produced must be what the parser accepts. */
	zassert_ok(gnss_quectel_eg915n_parse_qgpsloc(captured_argv_ptrs(), captured_argc, &data));
}

ZTEST(gnss_quectel_eg915n, test_chat_splits_qgpsloc_response_without_cog)
{
	static const char response[] =
		"\r\n+QGPSLOC: 084440.0,2238.694234,N,11402.164478,E,"
		"0.6,29.4,3,,0.0,0.0,010924,08\r\n";

	captured_argc = 0;
	modem_backend_mock_put(&mock, (const uint8_t *)response, sizeof(response) - 1);
	k_msleep(100);

	zassert_equal(captured_argc, GNSS_QUECTEL_EG915N_QGPSLOC_ARGC,
		      "An empty course over ground must still yield %d arguments",
		      GNSS_QUECTEL_EG915N_QGPSLOC_ARGC);
	zassert_str_equal(captured_argv[9], "", "Course over ground should be empty");
	zassert_str_equal(captured_argv[10], "0.0");
}

/* Real GSV burst: NMEA 4.1 signal id after the last satellite, empty SNR when untracked. */
static const char gsv_response[] =
	"\r\n+QGPSGNMEA: $GPGSV,3,1,12,05,21,309,33,06,23,202,21,07,69,091,27,"
	"21,58,301,26,0*61\r\n"
	"\r\n+QGPSGNMEA: $GPGSV,3,2,12,11,,,22,16,,,16,30,,,27,33,31,210,,0*50\r\n"
	"\r\n+QGPSGNMEA: $GPGSV,3,3,12,37,34,161,,39,33,156,,40,19,124,,"
	"38,31,210,,0*6A\r\n";

ZTEST(gnss_quectel_eg915n, test_strip_qgpsgnmea_prefix)
{
	char sentence[] = "+QGPSGNMEA: $GPGSV,3,1,12";
	char other[] = "+QGPSLOC: 123519.00";
	char *argv[1];

	argv[0] = sentence;
	zassert_ok(gnss_quectel_eg915n_strip_qgpsgnmea_prefix(argv, ARRAY_SIZE(argv)));
	zassert_str_equal(argv[0], "$GPGSV,3,1,12");

	argv[0] = other;
	zassert_equal(gnss_quectel_eg915n_strip_qgpsgnmea_prefix(argv, ARRAY_SIZE(argv)), -EINVAL);
	zassert_equal(gnss_quectel_eg915n_strip_qgpsgnmea_prefix(argv, 0), -EINVAL);
}

ZTEST(gnss_quectel_eg915n, test_chat_splits_gsv_response)
{
	modem_backend_mock_put(&mock, (const uint8_t *)gsv_response, sizeof(gsv_response) - 1);
	k_msleep(100);

	zassert_equal(gsv_calls, 3, "Expected one callback per GSV message, got %u", gsv_calls);
	zassert_ok(gsv_strip_ret);
	zassert_ok(gsv_header_ret);
	zassert_equal(gsv_header.system, GNSS_SYSTEM_GPS);
	zassert_equal(gsv_header.number_of_messages, 3);
	zassert_equal(gsv_header.number_of_svs, 12);

	/* The trailing signal id must not be mistaken for a fifth satellite. */
	zassert_equal(gsv_satellites_len, 12, "Expected 12 satellites, got %u",
		      gsv_satellites_len);

	zassert_equal(gsv_satellites[0].prn, 5);
	zassert_equal(gsv_satellites[0].elevation, 21);
	zassert_equal(gsv_satellites[0].azimuth, 309);
	zassert_equal(gsv_satellites[0].snr, 33);
	zassert_true(gsv_satellites[0].is_tracked);
	zassert_equal(gsv_satellites[0].system, GNSS_SYSTEM_GPS);

	/* Only an SNR means the position is unknown. */
	zassert_equal(gsv_satellites[4].prn, 11);
	zassert_equal(gsv_satellites[4].elevation, 0);
	zassert_equal(gsv_satellites[4].azimuth, 0);
	zassert_equal(gsv_satellites[4].snr, 22);
	zassert_true(gsv_satellites[4].is_tracked);

	/* An empty signal to noise ratio means the satellite is not tracked. */
	zassert_false(gsv_satellites[7].is_tracked);
	zassert_equal(gsv_satellites[7].snr, 0);

	/* PRNs above the GPS range are reported as SBAS with an offset applied. */
	zassert_equal(gsv_satellites[7].prn, 33 + 87);
	zassert_equal(gsv_satellites[7].system, GNSS_SYSTEM_SBAS);
	zassert_equal(gsv_satellites[8].prn, 37 + 87);
	zassert_equal(gsv_satellites[8].elevation, 34);
	zassert_equal(gsv_satellites[8].azimuth, 161);
}

static void *gnss_quectel_eg915n_setup(void)
{
	const struct modem_chat_config chat_config = {
		.user_data = NULL,
		.receive_buf = chat_receive_buf,
		.receive_buf_size = sizeof(chat_receive_buf),
		.delimiter = chat_delimiter,
		.delimiter_size = ARRAY_SIZE(chat_delimiter),
		.filter = NULL,
		.filter_size = 0,
		.argv = chat_argv,
		.argv_size = ARRAY_SIZE(chat_argv),
		.unsol_matches = test_unsol_matches,
		.unsol_matches_size = ARRAY_SIZE(test_unsol_matches),
	};

	const struct modem_backend_mock_config mock_config = {
		.rx_buf = mock_rx_buf,
		.rx_buf_size = sizeof(mock_rx_buf),
		.tx_buf = mock_tx_buf,
		.tx_buf_size = sizeof(mock_tx_buf),
		.limit = sizeof(mock_rx_buf),
	};

	zassert_ok(modem_chat_init(&chat, &chat_config));

	mock_pipe = modem_backend_mock_init(&mock, &mock_config);
	zassert_ok(modem_pipe_open(mock_pipe, K_SECONDS(10)));
	zassert_ok(modem_chat_attach(&chat, mock_pipe));

	return NULL;
}

static void gnss_quectel_eg915n_before(void *f)
{
	ARG_UNUSED(f);

	captured_argc = 0;
	memset(captured_argv, 0, sizeof(captured_argv));

	gsv_calls = 0;
	gsv_strip_ret = 0;
	gsv_header_ret = 0;
	memset(&gsv_header, 0, sizeof(gsv_header));
	memset(gsv_satellites, 0, sizeof(gsv_satellites));
	gsv_satellites_len = 0;

	modem_backend_mock_reset(&mock);
}

ZTEST_SUITE(gnss_quectel_eg915n, NULL, gnss_quectel_eg915n_setup, gnss_quectel_eg915n_before, NULL,
	    NULL);
