/*
 * Copyright (c) 2026 INOMO Technologies AG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gnss.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>
#include <string.h>

#define GNSS_NAME DEVICE_DT_NAME(DT_NODELABEL(gnss))

static const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(gnss));
static const struct shell *sh;

static const char *run(const char *cmd)
{
	size_t size;

	shell_backend_dummy_clear_output(sh);
	zassert_ok(shell_execute_cmd(sh, cmd), "%s failed", cmd);

	return shell_backend_dummy_get_output(sh, &size);
}

static void run_expect_err(const char *cmd)
{
	shell_backend_dummy_clear_output(sh);
	zassert_not_equal(shell_execute_cmd(sh, cmd), 0, "%s should have failed", cmd);
}

ZTEST(gnss_shell, test_fixrate_round_trip)
{
	uint32_t fix_interval_ms;

	run("gnss fixrate " GNSS_NAME " 500");

	zassert_ok(gnss_get_fix_rate(dev, &fix_interval_ms));
	zassert_equal(fix_interval_ms, 500);

	zassert_not_null(strstr(run("gnss fixrate " GNSS_NAME), "500"));
}

ZTEST(gnss_shell, test_navmode_round_trip)
{
	enum gnss_navigation_mode mode;

	run("gnss navmode " GNSS_NAME " balanced");

	zassert_ok(gnss_get_navigation_mode(dev, &mode));
	zassert_equal(mode, GNSS_NAVIGATION_MODE_BALANCED_DYNAMICS);

	zassert_not_null(strstr(run("gnss navmode " GNSS_NAME), "balanced"));
}

ZTEST(gnss_shell, test_systems_round_trip)
{
	gnss_systems_t systems;
	const char *out;

	run("gnss systems " GNSS_NAME " gps glonass");

	zassert_ok(gnss_get_enabled_systems(dev, &systems));
	zassert_equal(systems, GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS);

	out = run("gnss systems " GNSS_NAME);
	zassert_not_null(strstr(out, "gps"));
	zassert_not_null(strstr(out, "glonass"));
	zassert_is_null(strstr(out, "galileo"));
}

ZTEST(gnss_shell, test_supported_lists_a_system)
{
	gnss_systems_t systems;

	zassert_ok(gnss_get_supported_systems(dev, &systems));
	zassume_true(systems & GNSS_SYSTEM_GPS, "emul should support GPS");

	zassert_not_null(strstr(run("gnss supported " GNSS_NAME), "gps"));
}

ZTEST(gnss_shell, test_rejects_bad_input)
{
	run_expect_err("gnss fixrate no_such_device");
	run_expect_err("gnss navmode " GNSS_NAME " sideways");
	run_expect_err("gnss systems " GNSS_NAME " pulsar");
}

static void *setup(void)
{
	sh = shell_backend_dummy_get_ptr();

	/* z_shell_print() drops output until the backend has started. */
	WAIT_FOR(shell_ready(sh), USEC_PER_SEC, k_msleep(1));
	zassert_true(shell_ready(sh), "shell backend did not start");

	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(gnss_set_fix_rate(dev, 1000));
	zassert_ok(gnss_set_navigation_mode(dev, GNSS_NAVIGATION_MODE_ZERO_DYNAMICS));
	zassert_ok(gnss_set_enabled_systems(dev, GNSS_SYSTEM_GPS));
}

ZTEST_SUITE(gnss_shell, NULL, setup, before, NULL, NULL);
