/*
 * Copyright (c) 2026 Victor Miranda
 * Copyright (c) 2026 INOMO Technologies AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/modem/modem_cellular.h>

#define DT_DRV_COMPAT quectel_eg915n

/* The SIM is not readable for several seconds after the modem reports RDY. */
#define SIM_STARTUP_WAIT_MS 5500

MODEM_CELLULAR_COMMON_CHAT_MATCHES();

MODEM_CHAT_MATCHES_DEFINE(quectel_eg915n_unsol, MODEM_CELLULAR_COMMON_UNSOL_MATCHES,
			  MODEM_CHAT_MATCH("RDY", "", modem_cellular_chat_on_modem_ready));

MODEM_CHAT_MATCHES_DEFINE(quectel_eg915n_abort_matches,
			  MODEM_CHAT_MATCH("ERROR", "", NULL),
			  MODEM_CHAT_MATCH("+CME ERROR", "", NULL),
			  MODEM_CHAT_MATCH("+CMS ERROR", "", NULL),
			  MODEM_CHAT_MATCH("POWERED DOWN", "", NULL));

/*
 * A timeout here counts as success, because a modem already running at the saved
 * baud rate does not answer at the old one. Nothing in this script is guaranteed
 * to run, so anything that must be applied belongs in the init script too.
 */
MODEM_CHAT_SCRIPT_CMDS_DEFINE(
	quectel_eg915n_set_baudrate_chat_script_cmds,
	MODEM_CHAT_SCRIPT_CMD_RESP("AT", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("ATE0", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+IPR=" STRINGIFY(CONFIG_MODEM_CELLULAR_NEW_BAUDRATE) ";&W",
				   ok_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_eg915n_set_baudrate_chat_script,
			 quectel_eg915n_set_baudrate_chat_script_cmds, quectel_eg915n_abort_matches,
			 modem_cellular_chat_callback_handler, 10);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(
	quectel_eg915n_init_chat_script_cmds,
	MODEM_CHAT_SCRIPT_CMD_RESP_NONE("", SIM_STARTUP_WAIT_MS),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("ATE0", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+IFC=2,2", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CPIN?", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CMEE=1", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG=1", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CREG?", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG?", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGSN", imei_match), MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGMM", cgmm_match), MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGMI", cgmi_match), MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGMR", cgmr_match), MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CIMI", cimi_match), MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CMUX=0,0,5,127", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_eg915n_init_chat_script, quectel_eg915n_init_chat_script_cmds,
			 quectel_eg915n_abort_matches, modem_cellular_chat_callback_handler,
			 10 + (SIM_STARTUP_WAIT_MS / 1000));

MODEM_CHAT_SCRIPT_CMDS_DEFINE(
	quectel_eg915n_network_chat_script_cmds,
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+QCFG=\"cmux/urcport\",1", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+CGACT=0,1", allow_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CFUN=1", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_eg915n_network_chat_script,
			 quectel_eg915n_network_chat_script_cmds, quectel_eg915n_abort_matches,
			 modem_cellular_chat_callback_handler, 60);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_eg915n_dial_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("ATD*99***1#", connect_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_eg915n_dial_chat_script, quectel_eg915n_dial_chat_script_cmds,
			 dial_abort_matches, modem_cellular_chat_callback_handler, 10);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_eg915n_periodic_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG?", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CSQ", csq_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_eg915n_periodic_chat_script,
			 quectel_eg915n_periodic_chat_script_cmds, quectel_eg915n_abort_matches,
			 modem_cellular_chat_callback_handler, 4);

static const struct modem_cellular_vendor_config quectel_eg915n_vendor = {
	/* clang-format off */
	.scripts = {
		.set_baudrate = &quectel_eg915n_set_baudrate_chat_script,
		.init = &quectel_eg915n_init_chat_script,
		.network = &quectel_eg915n_network_chat_script,
		.dial = &quectel_eg915n_dial_chat_script,
		.periodic = &quectel_eg915n_periodic_chat_script,
	},
	.unsol_matches = {
		.matches = quectel_eg915n_unsol,
		.size = ARRAY_SIZE(quectel_eg915n_unsol),
	},
	/* clang-format on */
	.chat_delimiter = "\r",
	.chat_filter = "\n",
	.power_pulse_duration_ms = 2000,
	.reset_pulse_duration_ms = 500,
	.startup_time_ms = 15000,
	.shutdown_time_ms = 3000,
};

#define MODEM_CELLULAR_DEVICE_QUECTEL_EG915N(inst)                                                 \
	MODEM_DT_INST_PPP_DEFINE(inst, MODEM_CELLULAR_INST_NAME(ppp, inst), NULL, 1500, 64);       \
                                                                                                   \
	static struct modem_cellular_data MODEM_CELLULAR_INST_NAME(data, inst);                    \
                                                                                                   \
	MODEM_CELLULAR_DEFINE_AND_INIT_USER_PIPES(inst, (user_pipe_0, 3), (gnss_pipe, 4))          \
                                                                                                   \
	MODEM_CELLULAR_DEFINE_INSTANCE(inst, &quectel_eg915n_vendor)

DT_INST_FOREACH_STATUS_OKAY(MODEM_CELLULAR_DEVICE_QUECTEL_EG915N)
