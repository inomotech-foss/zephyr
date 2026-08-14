.. zephyr:board:: s32k142_evb

Overview
********

`NXP S32K142EVB-Q100`_ is a low-cost evaluation and development board for general-purpose
industrial and automotive applications.
The S32K142EVB-Q100 is based on the 32-bit Arm Cortex-M4F `NXP S32K142`_ microcontroller.
The onboard OpenSDA serial and debug adapter, running a mass storage device (MSD) bootloader
and a collection of OpenSDA Applications, offers options for serial communication,
flash programming, and run-control debugging.
It is a bridge between a USB host and the embedded target processor.

Hardware
********

- NXP S32K142

  - Arm Cortex-M4F @ up to 112 MHz
  - 256 KB Flash
  - 32 KB SRAM
  - up to 88 I/Os
  - 2x FlexCAN, one with FD
  - eDMA, 12-bit ADC, MPU, ECC and more.

- Interfaces

  - CAN, LIN, UART/SCI
  - Potentiometer, user RGB LED and 2 buttons.

More information about the hardware and design resources can be found at
`NXP S32K142EVB-Q100`_ website.

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

This board has 5 GPIO ports named from ``gpioa`` to ``gpioe``.

Pin control can be further configured from your application overlay by adding
children nodes with the desired pinmux configuration to the singleton node
``pinctrl``. Supported properties are described in
:zephyr_file:`dts/bindings/pinctrl/nxp,port-pinctrl.yaml`.

LEDs
----

The NXP S32K142EVB-Q100 board has one user RGB LED that can be used either as a GPIO
LED or as a PWM LED.

.. table:: RGB LED as GPIO LED

    +-----------------+------------------+----------------+-------+
    | Devicetree node | Devicetree alias | Label          | Pin   |
    +=================+==================+================+=======+
    | led1_red        | led0             | LED1_RGB_RED   | PTD15 |
    +-----------------+------------------+----------------+-------+
    | led1_green      | led1             | LED1_RGB_GREEN | PTD16 |
    +-----------------+------------------+----------------+-------+
    | led1_blue       | led2             | LED1_RGB_BLUE  | PTD0  |
    +-----------------+------------------+----------------+-------+

.. table:: RGB LED as PWM LED

    +-----------------+--------------------------+--------------------+------------------+
    | Devicetree node | Devicetree alias         | Label              | Pin              |
    +=================+==========================+====================+==================+
    | led1_red_pwm    | pwm-led0 / red-pwm-led   | LED1_RGB_RED_PWM   | PTD15 / FTM0_CH0 |
    +-----------------+--------------------------+--------------------+------------------+
    | led1_green_pwm  | pwm-led1 / green-pwm-led | LED1_RGB_GREEN_PWM | PTD16 / FTM0_CH1 |
    +-----------------+--------------------------+--------------------+------------------+
    | led1_blue_pwm   | pwm-led2 / blue-pwm-led  | LED1_RGB_BLUE_PWM  | PTD0 / FTM0_CH2  |
    +-----------------+--------------------------+--------------------+------------------+

The user can control the LEDs in any way. An output of ``0`` illuminates the LED.

Buttons
-------

The NXP S32K142EVB-Q100 board has two user buttons:

+-----------------+-------+-------+
| Devicetree node | Label | Pin   |
+=================+=======+=======+
| sw0 / button_3  | SW3   | PTC12 |
+-----------------+-------+-------+
| sw1 / button_4  | SW4   | PTC13 |
+-----------------+-------+-------+

Serial Console
==============

The serial console is provided via ``lpuart1`` on the OpenSDA adapter.

+------+--------------+
| Pin  | Pin Function |
+======+==============+
| PTC7 | LPUART1_TX   |
+------+--------------+
| PTC6 | LPUART1_RX   |
+------+--------------+

System Clock
============

The Arm Cortex-M4F core is configured to run at 80 MHz (RUN mode).

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Applications for the ``s32k142_evb`` board can be built in the usual way as
documented in :ref:`build_an_application`.

This board configuration supports `SEGGER J-Link`_ West runner for flashing and
debugging applications. Follow the steps described in :ref:`jlink-debug-host-tools`,
to setup the flash and debug host tools for this runner.

Flashing
========

Run the ``west flash`` command to flash the application using SEGGER J-Link.

Debugging
=========

Run the ``west debug`` command to start a GDB session using SEGGER J-Link.

Configuring a Console
=====================

We will use OpenSDA as a USB-to-serial adapter for the serial console.

Use the following settings with your serial terminal of choice (minicom, putty, etc.):

- Speed: 115200
- Data: 8 bits
- Parity: None
- Stop bits: 1

.. include:: ../../common/board-footer.rst.inc

References
**********

.. target-notes::

.. _NXP S32K142EVB-Q100:
   https://www.nxp.com/design/design-center/development-boards-and-designs/automotive-development-platforms/s32k-mcu-platforms/s32k142-q100-evaluation-board-for-automotive-general-purpose:S32K142EVB

.. _NXP S32K142:
   https://www.nxp.com/products/processors-and-microcontrollers/s32-automotive-platform/s32k-auto-general-purpose-mcus/s32k1-microcontrollers-for-automotive-general-purpose:S32K1

.. _SEGGER J-Link:
   https://wiki.segger.com/S32Kxxx
