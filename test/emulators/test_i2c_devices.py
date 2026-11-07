"""Unit tests for the TCA9548A and 24AA025E48 protocol emulators."""

import unittest

from i2c_emulators import EEPROM24AA025E48, I2CBus, TCA9548A


class TestTCA9548A(unittest.TestCase):
    def setUp(self):
        self.mux = TCA9548A()

    def test_power_on_reset(self):
        self.assertEqual(self.mux.read(1), b"\x00")
        for channel in range(8):
            self.assertFalse(self.mux.channel_enabled(channel))

    def test_channel_select_and_readback(self):
        self.assertTrue(self.mux.write([0x01]))
        self.assertTrue(self.mux.channel_enabled(0))
        self.assertFalse(self.mux.channel_enabled(1))
        self.assertEqual(self.mux.read(1), b"\x01")

    def test_multiple_channels(self):
        self.mux.write([0x81])
        self.assertTrue(self.mux.channel_enabled(0))
        self.assertTrue(self.mux.channel_enabled(7))

    def test_reset(self):
        self.mux.write([0xFF])
        self.mux.reset()
        self.assertEqual(self.mux.read(1), b"\x00")


class TestEEPROM24AA025E48(unittest.TestCase):
    def setUp(self):
        self.eeprom = EEPROM24AA025E48()

    def _read_at(self, address, count):
        self.eeprom.write([address])
        return self.eeprom.read(count)

    def test_byte_write_read(self):
        self.assertTrue(self.eeprom.write([0x00, 0xA5]))
        self.assertEqual(self._read_at(0x00, 1), b"\xA5")

    def test_page_write(self):
        data = bytes(range(16))
        self.eeprom.write([0x00] + list(data))
        self.assertEqual(self._read_at(0x00, 16), data)

    def test_page_write_wraps_within_page(self):
        self.eeprom.write([0x0F, 0xAA, 0xBB])
        self.assertEqual(self.eeprom.memory[0x0F], 0xAA)
        self.assertEqual(self.eeprom.memory[0x00], 0xBB)

    def test_sequential_read_wraps_at_0xff(self):
        self.eeprom.memory[0xF8:0x100] = bytes(range(8))
        self.eeprom.memory[0x00:0x08] = bytes(range(0x10, 0x18))
        self.assertEqual(
            self._read_at(0xF8, 16),
            bytes(range(8)) + bytes(range(0x10, 0x18)),
        )

    def test_upper_half_write_protected(self):
        original = bytes(self.eeprom.memory[0x80:0x90])
        self.eeprom.write([0x80] + list(range(16)))
        self.assertEqual(bytes(self.eeprom.memory[0x80:0x90]), original)

    def test_eui48(self):
        eui = b"\x00\x11\x22\x33\x44\x55"
        self.eeprom.set_eui48(eui)
        self.assertEqual(self._read_at(0xFA, 6), eui)


class TestMuxedBus(unittest.TestCase):
    def setUp(self):
        self.bus = I2CBus()
        self.mux = TCA9548A(0x70)
        self.eeprom = EEPROM24AA025E48(0x50)
        self.bus.set_mux(self.mux)
        self.bus.attach(self.eeprom, mux_channel=0)

    def test_eeprom_unreachable_when_channel_off(self):
        self.assertFalse(self.bus.write(0x50, [0x00, 0xAA]))

    def test_eeprom_reachable_after_channel_on(self):
        self.assertTrue(self.bus.write(0x70, [0x01]))
        self.assertTrue(self.bus.write(0x50, [0x00, 0xAA]))
        self.assertTrue(self.bus.write(0x50, [0x00]))
        self.assertEqual(self.bus.read(0x50, 1), b"\xAA")


class TestFRUImageThroughMux(unittest.TestCase):
    """Mirror the firmware's FRU write/read sequence (8-byte chunks)."""

    def test_122_byte_fru_image(self):
        bus = I2CBus()
        mux = TCA9548A(0x70)
        eeprom = EEPROM24AA025E48(0x50)
        bus.set_mux(mux)
        bus.attach(eeprom, mux_channel=0)

        image = bytes((i ^ 0x5A) & 0xFF for i in range(122))

        self.assertTrue(bus.write(0x70, [0x01]))          # select channel 0
        for offset in range(0, len(image), 8):            # 8-byte page writes
            chunk = image[offset:offset + 8]
            self.assertTrue(bus.write(0x50, [offset] + list(chunk)))

        self.assertTrue(bus.write(0x50, [0x00]))          # set word address
        self.assertEqual(bus.read(0x50, len(image)), image)


if __name__ == "__main__":
    unittest.main()
