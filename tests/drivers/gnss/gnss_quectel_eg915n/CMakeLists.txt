# Copyright (c) 2026 INOMO Technologies AG
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(gnss_quectel_eg915n)

target_sources(app PRIVATE
  src/main.c
  ${ZEPHYR_BASE}/drivers/gnss/gnss_quectel_eg915n_parse.c
  ${ZEPHYR_BASE}/tests/subsys/modem/mock/modem_backend_mock.c
)

target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/drivers/gnss
  ${ZEPHYR_BASE}/tests/subsys/modem/mock
)
