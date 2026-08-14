/*
 * Copyright (c) 2026 INOMO Technologies AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT quectel_eg915n_gnss

#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gnss/gnss_publish.h>
#include <zephyr/kernel.h>
#include <zephyr/modem/chat.h>
#include <zephyr/modem/pipelink.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>

#include "gnss_nmea0183_match.h"
#include "gnss_quectel_eg915n_parse.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gnss_quectel_eg915n, CONFIG_GNSS_LOG_LEVEL);

#define CHAT_RECEIVE_BUF_SIZE 256
#define CHAT_ARGV_SIZE	      32

#define PIPE_CLOSE_TIMEOUT K_SECONDS(5)
#define SCRIPT_TIMEOUT_S   5

#define STATE_PIPE_CONNECTED 0
#define STATE_PIPE_OPEN 1
#define STATE_RESUMED 2
#define STATE_GNSS_STARTED 3

/* A poll script already in flight when suspending has to finish first. */
#define STOP_RETRY_DELAY_MS 500
#define STOP_RETRIES	    ((SCRIPT_TIMEOUT_S * MSEC_PER_SEC) / STOP_RETRY_DELAY_MS + 1)

struct quectel_eg915n_gnss_config {
	struct modem_pipelink *pipelink;
	uint32_t fix_interval_ms;
};

struct quectel_eg915n_gnss_data {
	/* Must stay first: the shared match callbacks cast chat user data to this. */
	struct gnss_nmea0183_match_data match_data;
#if CONFIG_GNSS_SATELLITES
	struct gnss_satellite satellites[CONFIG_GNSS_QUECTEL_EG915N_SATELLITES_COUNT];
#endif

	struct modem_chat chat;
	uint8_t chat_receive_buf[CHAT_RECEIVE_BUF_SIZE];
	uint8_t *chat_argv[CHAT_ARGV_SIZE];

	struct k_work open_pipe_work;
	struct k_work_delayable poll_work;

	uint32_t fix_interval_ms;
	atomic_t state;
};

static int quectel_eg915n_gnss_pm_action(const struct device *dev, enum pm_device_action action);
static void quectel_eg915n_gnss_on_qgpsloc(struct modem_chat *chat, char **argv, uint16_t argc,
					   void *user_data);
static void quectel_eg915n_gnss_start_script_callback(struct modem_chat *chat,
						      enum modem_chat_script_result result,
						      void *user_data);
static void quectel_eg915n_gnss_poll_script_callback(struct modem_chat *chat,
						     enum modem_chat_script_result result,
						     void *user_data);
#if CONFIG_GNSS_SATELLITES
static void quectel_eg915n_gnss_on_gsv(struct modem_chat *chat, char **argv, uint16_t argc,
				       void *user_data);
#endif

MODEM_CHAT_MATCH_DEFINE(ok_match, "OK", "", NULL);

/* AT+QGPS=1 errors when GNSS already runs, AT+QGPSLOC gives +CME ERROR 516 before a fix. */
MODEM_CHAT_MATCHES_DEFINE(allow_matches,
			  MODEM_CHAT_MATCH("OK", "", NULL),
			  MODEM_CHAT_MATCH("ERROR", "", NULL),
			  MODEM_CHAT_MATCH("+CME ERROR", "", NULL));

MODEM_CHAT_MATCHES_DEFINE(abort_matches,
			  MODEM_CHAT_MATCH("ERROR", "", NULL),
			  MODEM_CHAT_MATCH("+CME ERROR", "", NULL));

/* Responses arrive before the final OK, and GSV spans several lines. */
MODEM_CHAT_MATCHES_DEFINE(unsol_matches,
	MODEM_CHAT_MATCH("+QGPSLOC: ", ",", quectel_eg915n_gnss_on_qgpsloc),
#if CONFIG_GNSS_SATELLITES
	MODEM_CHAT_MATCH_WILDCARD(GNSS_QUECTEL_EG915N_QGPSGNMEA_PREFIX "$??GSV,", ",*",
				  quectel_eg915n_gnss_on_gsv),
#endif
);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_eg915n_gnss_start_chat_script_cmds,
#if CONFIG_GNSS_SATELLITES
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+QGPSCFG=\"nmeasrc\",1", ok_match),
#endif
			      MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QGPS=1", allow_matches));

MODEM_CHAT_SCRIPT_DEFINE(quectel_eg915n_gnss_start_chat_script,
			 quectel_eg915n_gnss_start_chat_script_cmds, abort_matches,
			 quectel_eg915n_gnss_start_script_callback, SCRIPT_TIMEOUT_S);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_eg915n_gnss_poll_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QGPSLOC=1", allow_matches),
#if CONFIG_GNSS_SATELLITES
			      MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QGPSGNMEA=\"GSV\"",
							      allow_matches),
#endif
);

MODEM_CHAT_SCRIPT_NO_ABORT_DEFINE(quectel_eg915n_gnss_poll_chat_script,
				  quectel_eg915n_gnss_poll_chat_script_cmds,
				  quectel_eg915n_gnss_poll_script_callback, SCRIPT_TIMEOUT_S);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_eg915n_gnss_stop_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QGPSEND", allow_matches));

MODEM_CHAT_SCRIPT_NO_ABORT_DEFINE(quectel_eg915n_gnss_stop_chat_script,
				  quectel_eg915n_gnss_stop_chat_script_cmds, NULL,
				  SCRIPT_TIMEOUT_S);

static void quectel_eg915n_gnss_on_qgpsloc(struct modem_chat *chat, char **argv, uint16_t argc,
					   void *user_data)
{
	struct quectel_eg915n_gnss_data *data = user_data;
	struct gnss_data parsed;

	ARG_UNUSED(chat);

	if (gnss_quectel_eg915n_parse_qgpsloc((const char *const *)argv, argc, &parsed) < 0) {
		LOG_WRN("Failed to parse +QGPSLOC response");
		return;
	}

	gnss_publish_data(data->match_data.gnss, &parsed);
}

#if CONFIG_GNSS_SATELLITES
/* The chat resets argv after this returns, so moving the pointer is safe. */
static void quectel_eg915n_gnss_on_gsv(struct modem_chat *chat, char **argv, uint16_t argc,
				       void *user_data)
{
	if (gnss_quectel_eg915n_strip_qgpsgnmea_prefix(argv, argc) < 0) {
		return;
	}

	gnss_nmea0183_match_gsv_callback(chat, argv, argc, user_data);
}
#endif

static void quectel_eg915n_gnss_schedule_poll(struct quectel_eg915n_gnss_data *data,
					      k_timeout_t delay)
{
	if (!atomic_test_bit(&data->state, STATE_PIPE_OPEN) ||
	    !atomic_test_bit(&data->state, STATE_RESUMED)) {
		return;
	}

	k_work_reschedule(&data->poll_work, delay);
}

static void quectel_eg915n_gnss_start_script_callback(struct modem_chat *chat,
						      enum modem_chat_script_result result,
						      void *user_data)
{
	struct quectel_eg915n_gnss_data *data = user_data;

	ARG_UNUSED(chat);

	if (result != MODEM_CHAT_SCRIPT_RESULT_SUCCESS) {
		LOG_WRN("Failed to start GNSS, retrying");
		quectel_eg915n_gnss_schedule_poll(data, K_MSEC(data->fix_interval_ms));
		return;
	}

	atomic_set_bit(&data->state, STATE_GNSS_STARTED);
	quectel_eg915n_gnss_schedule_poll(data, K_NO_WAIT);
}

static void quectel_eg915n_gnss_poll_script_callback(struct modem_chat *chat,
						     enum modem_chat_script_result result,
						     void *user_data)
{
	struct quectel_eg915n_gnss_data *data = user_data;

	ARG_UNUSED(chat);
	ARG_UNUSED(result);

	quectel_eg915n_gnss_schedule_poll(data, K_MSEC(data->fix_interval_ms));
}

static void quectel_eg915n_gnss_poll_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct quectel_eg915n_gnss_data *data =
		CONTAINER_OF(dwork, struct quectel_eg915n_gnss_data, poll_work);
	int ret;

	const struct modem_chat_script *script;

	if (!atomic_test_bit(&data->state, STATE_PIPE_OPEN) ||
	    !atomic_test_bit(&data->state, STATE_RESUMED)) {
		return;
	}

	script = atomic_test_bit(&data->state, STATE_GNSS_STARTED)
			 ? &quectel_eg915n_gnss_poll_chat_script
			 : &quectel_eg915n_gnss_start_chat_script;

	ret = modem_chat_run_script_async(&data->chat, script);
	if (ret < 0) {
		LOG_WRN("Failed to run GNSS script (%d)", ret);
		quectel_eg915n_gnss_schedule_poll(data, K_MSEC(data->fix_interval_ms));
	}
}

static void quectel_eg915n_gnss_pipe_callback(struct modem_pipe *pipe, enum modem_pipe_event event,
					      void *user_data)
{
	struct quectel_eg915n_gnss_data *data = user_data;

	ARG_UNUSED(pipe);

	switch (event) {
	case MODEM_PIPE_EVENT_OPENED:
		atomic_clear_bit(&data->state, STATE_GNSS_STARTED);
		atomic_set_bit(&data->state, STATE_PIPE_OPEN);
		modem_chat_attach(&data->chat, pipe);
		quectel_eg915n_gnss_schedule_poll(data, K_NO_WAIT);
		break;

	case MODEM_PIPE_EVENT_CLOSED:
		atomic_clear_bit(&data->state, STATE_PIPE_OPEN);
		atomic_clear_bit(&data->state, STATE_GNSS_STARTED);
		k_work_cancel_delayable(&data->poll_work);
		modem_chat_release(&data->chat);
		break;

	default:
		break;
	}
}

static void quectel_eg915n_gnss_open_pipe_handler(struct k_work *work)
{
	struct quectel_eg915n_gnss_data *data =
		CONTAINER_OF(work, struct quectel_eg915n_gnss_data, open_pipe_work);
	const struct device *dev = data->match_data.gnss;
	const struct quectel_eg915n_gnss_config *config = dev->config;
	struct modem_pipe *pipe = modem_pipelink_get_pipe(config->pipelink);

	if (!atomic_test_bit(&data->state, STATE_RESUMED) ||
	    !atomic_test_bit(&data->state, STATE_PIPE_CONNECTED)) {
		return;
	}

	modem_pipe_attach(pipe, quectel_eg915n_gnss_pipe_callback, data);
	modem_pipe_open_async(pipe);
}

static void quectel_eg915n_gnss_pipelink_callback(struct modem_pipelink *link,
						  enum modem_pipelink_event event, void *user_data)
{
	struct quectel_eg915n_gnss_data *data = user_data;

	ARG_UNUSED(link);

	switch (event) {
	case MODEM_PIPELINK_EVENT_CONNECTED:
		atomic_set_bit(&data->state, STATE_PIPE_CONNECTED);
		k_work_submit(&data->open_pipe_work);
		break;

	case MODEM_PIPELINK_EVENT_DISCONNECTED:
		atomic_clear_bit(&data->state, STATE_PIPE_CONNECTED);
		atomic_clear_bit(&data->state, STATE_PIPE_OPEN);
		atomic_clear_bit(&data->state, STATE_GNSS_STARTED);
		k_work_cancel_delayable(&data->poll_work);
		modem_chat_release(&data->chat);
		break;

	default:
		break;
	}
}

static int quectel_eg915n_gnss_resume(const struct device *dev)
{
	struct quectel_eg915n_gnss_data *data = dev->data;

	atomic_set_bit(&data->state, STATE_RESUMED);

	if (atomic_test_bit(&data->state, STATE_PIPE_OPEN)) {
		quectel_eg915n_gnss_schedule_poll(data, K_NO_WAIT);
	} else if (atomic_test_bit(&data->state, STATE_PIPE_CONNECTED)) {
		k_work_submit(&data->open_pipe_work);
	}

	return 0;
}

static int quectel_eg915n_gnss_suspend(const struct device *dev)
{
	const struct quectel_eg915n_gnss_config *config = dev->config;
	struct quectel_eg915n_gnss_data *data = dev->data;
	int ret = -EBUSY;
	int i;

	atomic_clear_bit(&data->state, STATE_RESUMED);
	k_work_cancel_delayable(&data->poll_work);
	atomic_clear_bit(&data->state, STATE_GNSS_STARTED);

	if (!atomic_test_bit(&data->state, STATE_PIPE_OPEN)) {
		return 0;
	}

	/* Cancelling the work does not stop a script the handler already started. */
	for (i = 0; (i < STOP_RETRIES) && (ret == -EBUSY); i++) {
		ret = modem_chat_run_script(&data->chat, &quectel_eg915n_gnss_stop_chat_script);
		if (ret == -EBUSY) {
			k_msleep(STOP_RETRY_DELAY_MS);
		}
	}

	if (ret < 0) {
		LOG_WRN("Failed to stop GNSS (%d)", ret);
	}

	modem_pipe_close(modem_pipelink_get_pipe(config->pipelink), PIPE_CLOSE_TIMEOUT);
	return 0;
}

static int quectel_eg915n_gnss_set_fix_rate(const struct device *dev, uint32_t fix_interval_ms)
{
	struct quectel_eg915n_gnss_data *data = dev->data;

	if (fix_interval_ms == 0) {
		return -EINVAL;
	}

	data->fix_interval_ms = fix_interval_ms;

	/* Apply the new interval to the pending poll instead of the one after it. */
	quectel_eg915n_gnss_schedule_poll(data, K_MSEC(fix_interval_ms));
	return 0;
}

static int quectel_eg915n_gnss_get_fix_rate(const struct device *dev, uint32_t *fix_interval_ms)
{
	struct quectel_eg915n_gnss_data *data = dev->data;

	*fix_interval_ms = data->fix_interval_ms;
	return 0;
}

static DEVICE_API(gnss, quectel_eg915n_gnss_api) = {
	.set_fix_rate = quectel_eg915n_gnss_set_fix_rate,
	.get_fix_rate = quectel_eg915n_gnss_get_fix_rate,
};

static int quectel_eg915n_gnss_init_chat(const struct device *dev)
{
	struct quectel_eg915n_gnss_data *data = dev->data;
	static const uint8_t delimiter[] = {'\r', '\n'};

	const struct modem_chat_config chat_config = {
		.user_data = data,
		.receive_buf = data->chat_receive_buf,
		.receive_buf_size = sizeof(data->chat_receive_buf),
		.delimiter = delimiter,
		.delimiter_size = ARRAY_SIZE(delimiter),
		.filter = NULL,
		.filter_size = 0,
		.argv = data->chat_argv,
		.argv_size = ARRAY_SIZE(data->chat_argv),
		.unsol_matches = unsol_matches,
		.unsol_matches_size = ARRAY_SIZE(unsol_matches),
	};

	return modem_chat_init(&data->chat, &chat_config);
}

static int quectel_eg915n_gnss_init(const struct device *dev)
{
	const struct quectel_eg915n_gnss_config *config = dev->config;
	struct quectel_eg915n_gnss_data *data = dev->data;
	int ret;

	const struct gnss_nmea0183_match_config match_config = {
		.gnss = dev,
#if CONFIG_GNSS_SATELLITES
		.satellites = data->satellites,
		.satellites_size = ARRAY_SIZE(data->satellites),
#endif
	};

	data->fix_interval_ms = config->fix_interval_ms;

	ret = gnss_nmea0183_match_init(&data->match_data, &match_config);
	if (ret < 0) {
		return ret;
	}

	ret = quectel_eg915n_gnss_init_chat(dev);
	if (ret < 0) {
		return ret;
	}

	k_work_init(&data->open_pipe_work, quectel_eg915n_gnss_open_pipe_handler);
	k_work_init_delayable(&data->poll_work, quectel_eg915n_gnss_poll_handler);

	modem_pipelink_attach(config->pipelink, quectel_eg915n_gnss_pipelink_callback, data);

	return pm_device_driver_init(dev, quectel_eg915n_gnss_pm_action);
}

static int quectel_eg915n_gnss_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		return quectel_eg915n_gnss_resume(dev);

	case PM_DEVICE_ACTION_SUSPEND:
		return quectel_eg915n_gnss_suspend(dev);

	default:
		return -ENOTSUP;
	}
}

BUILD_ASSERT(CONFIG_GNSS_QUECTEL_EG915N_INIT_PRIORITY > CONFIG_MODEM_CELLULAR_INIT_PRIORITY,
	     "The EG915N GNSS driver must initialize after the modem it attaches to");

#define QUECTEL_EG915N_GNSS_MODEM_NODE(inst) DT_PARENT(DT_DRV_INST(inst))
#define QUECTEL_EG915N_GNSS_PIPE_NAME(inst)  DT_INST_STRING_TOKEN(inst, modem_pipe_name)

#define QUECTEL_EG915N_GNSS_DEFINE(inst)                                                           \
	MODEM_PIPELINK_DT_DECLARE(QUECTEL_EG915N_GNSS_MODEM_NODE(inst),                            \
				  QUECTEL_EG915N_GNSS_PIPE_NAME(inst));                            \
                                                                                                   \
	static const struct quectel_eg915n_gnss_config quectel_eg915n_gnss_config_##inst = {       \
		.pipelink = MODEM_PIPELINK_DT_GET(QUECTEL_EG915N_GNSS_MODEM_NODE(inst),            \
						  QUECTEL_EG915N_GNSS_PIPE_NAME(inst)),            \
		.fix_interval_ms = DT_INST_PROP(inst, fix_rate),                                   \
	};                                                                                         \
                                                                                                   \
	static struct quectel_eg915n_gnss_data quectel_eg915n_gnss_data_##inst;                    \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, quectel_eg915n_gnss_pm_action);                             \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, quectel_eg915n_gnss_init, PM_DEVICE_DT_INST_GET(inst),         \
			      &quectel_eg915n_gnss_data_##inst,                                    \
			      &quectel_eg915n_gnss_config_##inst, POST_KERNEL,                     \
			      CONFIG_GNSS_QUECTEL_EG915N_INIT_PRIORITY,                            \
			      &quectel_eg915n_gnss_api);

DT_INST_FOREACH_STATUS_OKAY(QUECTEL_EG915N_GNSS_DEFINE)
