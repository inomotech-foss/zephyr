/*
 * Copyright (c) 2026 INOMO Technologies AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gnss.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <stdlib.h>

#include "gnss_dump.h"

static const struct {
	const char *name;
	gnss_systems_t system;
} gnss_shell_systems[] = {
	{"gps", GNSS_SYSTEM_GPS},         {"glonass", GNSS_SYSTEM_GLONASS},
	{"galileo", GNSS_SYSTEM_GALILEO}, {"beidou", GNSS_SYSTEM_BEIDOU},
	{"qzss", GNSS_SYSTEM_QZSS},       {"irnss", GNSS_SYSTEM_IRNSS},
	{"sbas", GNSS_SYSTEM_SBAS},       {"imes", GNSS_SYSTEM_IMES},
};

static const char *const gnss_shell_nav_modes[] = {"zero", "low", "balanced", "high"};

static bool gnss_shell_device_check(const struct device *dev)
{
	return DEVICE_API_IS(gnss, dev);
}

static void gnss_shell_device_name_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, gnss_shell_device_check);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_gnss_device_name, gnss_shell_device_name_get);

static const struct device *gnss_shell_device_get(const struct shell *sh, const char *name)
{
	const struct device *dev = shell_device_get_binding(name);

	if (dev == NULL) {
		shell_error(sh, "device %s not found", name);
		return NULL;
	}

	if (!gnss_shell_device_check(dev)) {
		shell_error(sh, "%s is not a GNSS device", name);
		return NULL;
	}

	return dev;
}

static int gnss_shell_report(const struct shell *sh, const char *what, int err)
{
	if (err == -ENOSYS) {
		shell_error(sh, "%s not supported by this device", what);
	} else if (err < 0) {
		shell_error(sh, "%s failed (%d)", what, err);
	}

	return err;
}

static int cmd_gnss_fixrate(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = gnss_shell_device_get(sh, argv[1]);
	uint32_t fix_interval_ms;
	int err;

	if (dev == NULL) {
		return -ENODEV;
	}

	if (argc == 2) {
		err = gnss_get_fix_rate(dev, &fix_interval_ms);
		if (err < 0) {
			return gnss_shell_report(sh, "get fix rate", err);
		}

		shell_print(sh, "fix rate: %u ms", fix_interval_ms);
		return 0;
	}

	fix_interval_ms = (uint32_t)strtoul(argv[2], NULL, 0);

	err = gnss_set_fix_rate(dev, fix_interval_ms);
	return gnss_shell_report(sh, "set fix rate", err);
}

static int cmd_gnss_navmode(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = gnss_shell_device_get(sh, argv[1]);
	enum gnss_navigation_mode mode;
	int err;

	if (dev == NULL) {
		return -ENODEV;
	}

	if (argc == 2) {
		err = gnss_get_navigation_mode(dev, &mode);
		if (err < 0) {
			return gnss_shell_report(sh, "get navigation mode", err);
		}

		shell_print(sh, "navigation mode: %s", gnss_shell_nav_modes[mode]);
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(gnss_shell_nav_modes); i++) {
		if (strcmp(argv[2], gnss_shell_nav_modes[i]) == 0) {
			err = gnss_set_navigation_mode(dev, (enum gnss_navigation_mode)i);
			return gnss_shell_report(sh, "set navigation mode", err);
		}
	}

	shell_error(sh, "unknown navigation mode %s", argv[2]);
	return -EINVAL;
}

static void gnss_shell_print_systems(const struct shell *sh, const char *what,
				     gnss_systems_t systems)
{
	shell_fprintf(sh, SHELL_NORMAL, "%s:", what);

	for (size_t i = 0; i < ARRAY_SIZE(gnss_shell_systems); i++) {
		if (systems & gnss_shell_systems[i].system) {
			shell_fprintf(sh, SHELL_NORMAL, " %s", gnss_shell_systems[i].name);
		}
	}

	shell_fprintf(sh, SHELL_NORMAL, "\n");
}

static int cmd_gnss_systems(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = gnss_shell_device_get(sh, argv[1]);
	gnss_systems_t systems = 0;
	int err;

	if (dev == NULL) {
		return -ENODEV;
	}

	if (argc == 2) {
		err = gnss_get_enabled_systems(dev, &systems);
		if (err < 0) {
			return gnss_shell_report(sh, "get enabled systems", err);
		}

		gnss_shell_print_systems(sh, "enabled", systems);
		return 0;
	}

	for (size_t arg = 2; arg < argc; arg++) {
		bool found = false;

		for (size_t i = 0; i < ARRAY_SIZE(gnss_shell_systems); i++) {
			if (strcmp(argv[arg], gnss_shell_systems[i].name) == 0) {
				systems |= gnss_shell_systems[i].system;
				found = true;
				break;
			}
		}

		if (!found) {
			shell_error(sh, "unknown system %s", argv[arg]);
			return -EINVAL;
		}
	}

	err = gnss_set_enabled_systems(dev, systems);
	return gnss_shell_report(sh, "set enabled systems", err);
}

static int cmd_gnss_supported(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = gnss_shell_device_get(sh, argv[1]);
	gnss_systems_t systems;
	int err;

	ARG_UNUSED(argc);

	if (dev == NULL) {
		return -ENODEV;
	}

	err = gnss_get_supported_systems(dev, &systems);
	if (err < 0) {
		return gnss_shell_report(sh, "get supported systems", err);
	}

	gnss_shell_print_systems(sh, "supported", systems);
	return 0;
}

static int cmd_gnss_timepulse(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = gnss_shell_device_get(sh, argv[1]);
	k_ticks_t timestamp;
	int err;

	ARG_UNUSED(argc);

	if (dev == NULL) {
		return -ENODEV;
	}

	err = gnss_get_latest_timepulse(dev, &timestamp);
	if (err < 0) {
		return gnss_shell_report(sh, "get latest timepulse", err);
	}

	shell_print(sh, "latest timepulse: %lld ms uptime",
		    k_ticks_to_ms_floor64((uint64_t)timestamp));
	return 0;
}

#if CONFIG_GNSS_SHELL_MONITOR

/*
 * gnss_publish_data() holds a semaphore across every callback and runs from the
 * driver's context, so the callbacks only copy and hand the printing to a work
 * item. A sample arriving while the previous one is still queued is dropped.
 */
static const struct shell *monitor_shell;
static const struct device *monitor_dev;
static struct k_spinlock monitor_lock;

static struct gnss_data monitor_data;
static bool monitor_data_pending;
static struct k_work monitor_data_work;

#if CONFIG_GNSS_SATELLITES
static struct gnss_satellite monitor_satellites[CONFIG_GNSS_SHELL_MONITOR_SATELLITES_COUNT];
static uint16_t monitor_satellites_len;
static bool monitor_satellites_pending;
static struct k_work monitor_satellites_work;
#endif

static void gnss_shell_monitor_data_handler(struct k_work *work)
{
	static char buf[CONFIG_GNSS_SHELL_MONITOR_BUF_SIZE];
	const struct shell *sh = monitor_shell;
	struct gnss_data data;
	k_spinlock_key_t key;

	ARG_UNUSED(work);

	key = k_spin_lock(&monitor_lock);
	data = monitor_data;
	monitor_data_pending = false;
	k_spin_unlock(&monitor_lock, key);

	if (sh == NULL) {
		return;
	}

	if (gnss_dump_info(buf, sizeof(buf), &data.info) == 0) {
		shell_print(sh, "%s", buf);
	}

	if (gnss_dump_nav_data(buf, sizeof(buf), &data.nav_data) == 0) {
		shell_print(sh, "%s", buf);
	}

	if (gnss_dump_time(buf, sizeof(buf), &data.utc) == 0) {
		shell_print(sh, "%s", buf);
	}
}

static void gnss_shell_on_data(const struct device *dev, const struct gnss_data *data)
{
	k_spinlock_key_t key = k_spin_lock(&monitor_lock);

	if ((dev != monitor_dev) || monitor_data_pending) {
		k_spin_unlock(&monitor_lock, key);
		return;
	}

	monitor_data = *data;
	monitor_data_pending = true;
	k_spin_unlock(&monitor_lock, key);

	k_work_submit(&monitor_data_work);
}

GNSS_DATA_CALLBACK_DEFINE(NULL, gnss_shell_on_data);

#if CONFIG_GNSS_SATELLITES
static void gnss_shell_monitor_satellites_handler(struct k_work *work)
{
	static char buf[CONFIG_GNSS_SHELL_MONITOR_BUF_SIZE];
	const struct shell *sh = monitor_shell;
	struct gnss_satellite satellites[ARRAY_SIZE(monitor_satellites)];
	uint16_t len;
	k_spinlock_key_t key;

	ARG_UNUSED(work);

	key = k_spin_lock(&monitor_lock);
	len = monitor_satellites_len;
	memcpy(satellites, monitor_satellites, len * sizeof(satellites[0]));
	monitor_satellites_pending = false;
	k_spin_unlock(&monitor_lock, key);

	if (sh == NULL) {
		return;
	}

	for (uint16_t i = 0; i < len; i++) {
		if (gnss_dump_satellite(buf, sizeof(buf), &satellites[i]) == 0) {
			shell_print(sh, "%s", buf);
		}
	}
}

static void gnss_shell_on_satellites(const struct device *dev,
				     const struct gnss_satellite *satellites, uint16_t size)
{
	k_spinlock_key_t key = k_spin_lock(&monitor_lock);

	if ((dev != monitor_dev) || monitor_satellites_pending) {
		k_spin_unlock(&monitor_lock, key);
		return;
	}

	monitor_satellites_len = MIN(size, ARRAY_SIZE(monitor_satellites));
	memcpy(monitor_satellites, satellites,
	       monitor_satellites_len * sizeof(monitor_satellites[0]));
	monitor_satellites_pending = true;
	k_spin_unlock(&monitor_lock, key);

	k_work_submit(&monitor_satellites_work);
}

GNSS_SATELLITES_CALLBACK_DEFINE(NULL, gnss_shell_on_satellites);
#endif /* CONFIG_GNSS_SATELLITES */

static int cmd_gnss_monitor(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = gnss_shell_device_get(sh, argv[1]);
	k_spinlock_key_t key;
	bool enable;

	if (dev == NULL) {
		return -ENODEV;
	}

	if (strcmp(argv[2], "on") == 0) {
		enable = true;
	} else if (strcmp(argv[2], "off") == 0) {
		enable = false;
	} else {
		shell_error(sh, "expected on or off");
		return -EINVAL;
	}

	key = k_spin_lock(&monitor_lock);
	monitor_shell = enable ? sh : NULL;
	monitor_dev = enable ? dev : NULL;
	k_spin_unlock(&monitor_lock, key);

	shell_print(sh, "monitor %s for %s", enable ? "on" : "off", dev->name);
	return 0;
}

static int gnss_shell_init(void)
{
	k_work_init(&monitor_data_work, gnss_shell_monitor_data_handler);
#if CONFIG_GNSS_SATELLITES
	k_work_init(&monitor_satellites_work, gnss_shell_monitor_satellites_handler);
#endif
	return 0;
}

SYS_INIT(gnss_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_GNSS_SHELL_MONITOR */

SHELL_STATIC_SUBCMD_SET_CREATE(
	gnss_sub_cmds,
	SHELL_CMD_ARG(fixrate, &dsub_gnss_device_name,
		      SHELL_HELP("Get or set the fix interval", "<device> [interval_ms]"),
		      cmd_gnss_fixrate, 2, 1),
	SHELL_CMD_ARG(navmode, &dsub_gnss_device_name,
		      SHELL_HELP("Get or set the navigation mode",
				 "<device> [zero|low|balanced|high]"),
		      cmd_gnss_navmode, 2, 1),
	SHELL_CMD_ARG(systems, &dsub_gnss_device_name,
		      SHELL_HELP("Get or set the enabled systems", "<device> [system ...]"),
		      cmd_gnss_systems, 2, ARRAY_SIZE(gnss_shell_systems)),
	SHELL_CMD_ARG(supported, &dsub_gnss_device_name,
		      SHELL_HELP("Show the supported systems", "<device>"), cmd_gnss_supported, 2,
		      0),
	SHELL_CMD_ARG(timepulse, &dsub_gnss_device_name,
		      SHELL_HELP("Show the latest timepulse", "<device>"), cmd_gnss_timepulse, 2,
		      0),
#if CONFIG_GNSS_SHELL_MONITOR
	SHELL_CMD_ARG(monitor, &dsub_gnss_device_name,
		      SHELL_HELP("Print published data for a device", "<device> <on|off>"),
		      cmd_gnss_monitor, 3, 0),
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(gnss, &gnss_sub_cmds, "GNSS commands", NULL);
