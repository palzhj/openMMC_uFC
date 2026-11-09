# STM32F1XX bootloader (newboot)

This bootloader is responsible for copying the new firmware image from the firmware update area to the application or bootloader (self-update) area. It shouldn't be confused with the STM32F1XX ROM bootloader (firmware programming via serial port).

## Flash layout

The flash memory is divided in three regions defined in the linker script for each target.

LPC1764:
* Bootloader      0x08000000 - 0x08001FFF (8 KiB);
* Application     0x08002000 - 0x08006FFF (28 KiB);
* Firmware update 0x08007000 - 0x0800FFFF (28 KiB);
* New firmware record address: 0x0800FF00.

```
New firmware record:

  +-----------------+-----------------+-----------------+---------------+------------+
  |  Major version  |  Minor version  |  Build version  | Firmware type | Magic word |
  | number (1 byte) | number (1 byte) | number (1 byte) |   (1 byte)    | (4 bytes)  |
  +-----------------+-----------------+-----------------+---------------+------------+

```

The bootloader checks if the magic word is equal to 0xAAAAAAAA (firmware update magic word), if it is, the new firmware will be copied from the firmware update region to the application or bootloader region depending on the firmware type (application or bootloader). After finishing the copying, the bootloader will erase the firmware update region.

The Firmware type byte indicates what to update, (0x01: application, 0x02: bootloader). All flash writing logic is executed from SRAM to allow self updating.

## Migrating from the older openMMC versions

newboot is not compatible with openMMC prior version 1.5.0, and the older bootloader doesn't support self update nor openMMC >= 1.5.0, so remote updates via HPM would require a special version of openMMC that updates the bootloader from the application side. This is not done yet, so the only way to safely update the bootloader and openMMC now is via the JTAG/SWD interface.
