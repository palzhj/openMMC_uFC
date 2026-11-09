"""Protocol-level I2C emulators for the uFC-v2 FRU chain.

Models:
  * TCA9548A   - 8-channel I2C switch, control register at 0x70..0x77.
  * 24AA025E48 - 2 Kbit (256 B) I2C EEPROM with EUI-48, 0x50..0x57.

These are ordinary Python objects that implement the I2C transactions the
STM32 firmware issues (bus writes/reads, word address, page write wrap,
sequential read wrap, write protection).  They are not Renode peripherals;
Renode 1.17 has no I2C-capable Python peripheral and this host has no .NET SDK
for a C# model, so they are used for host-side verification of the bus
sequences and as a reference for a future Renode model.
"""


class I2CBus:
    """Tiny I2C bus: routes 7-bit addresses to devices.

    If a mux is set, a device attached with ``mux_channel`` is only reachable
    while that channel is enabled on the mux.
    """

    def __init__(self):
        self._devices = {}
        self._mux_channels = {}
        self._mux = None

    def set_mux(self, mux):
        self._mux = mux
        self._devices.setdefault(mux.address, mux)

    def attach(self, device, address=None, mux_channel=None):
        addr = device.address if address is None else (address & 0x7F)
        self._devices[addr] = device
        if mux_channel is not None:
            self._mux_channels[addr] = mux_channel

    def _route(self, address):
        address &= 0x7F
        if self._mux is not None and address != self._mux.address:
            channel = self._mux_channels.get(address)
            if channel is not None and not self._mux.channel_enabled(channel):
                return None
        return self._devices.get(address)

    def write(self, address, data):
        dev = self._route(address)
        if dev is None:
            return False
        return dev.write(bytes(data))

    def read(self, address, count):
        dev = self._route(address)
        if dev is None:
            return None
        return dev.read(count)


class TCA9548A:
    """TI TCA9548A 8-channel I2C switch.

    Control register bits: 1 = channel enabled, 0 = disabled.
    Power-on reset: all channels off (0x00).
    """

    def __init__(self, address=0x70):
        self.address = address & 0x7F
        self.control = 0

    def reset(self):
        self.control = 0

    def write(self, data):
        if len(data) < 1:
            return False
        self.control = data[0] & 0xFF
        return True

    def read(self, count):
        # The device returns the control register; repeated reads return it too.
        return bytes([self.control]) * count

    def channel_enabled(self, channel):
        return bool(self.control & (1 << channel))


class EEPROM24AA025E48:
    """Microchip 24AA025E48: 2 Kbit EEPROM with a pre-programmed EUI-48.

    * 256 bytes, 16-byte pages, one-byte word address.
    * Lower 128 bytes are writable; the upper 128 bytes are factory
      programmed / write-protected (the EUI-48 lives at 0xFA..0xFF).
    * Page writes wrap within the page; reads wrap at 0xFF.
    """

    SIZE = 256
    PAGE_SIZE = 16
    WRITABLE_SIZE = 128
    EUI48_OFFSET = 0xFA
    WRITE_CYCLE_MS = 5

    def __init__(self, address=0x50, eui48=None, writable_size=WRITABLE_SIZE):
        self.address = address & 0x7F
        self.memory = bytearray(self.SIZE)
        self.pointer = 0
        self.writable_size = writable_size
        self.busy = False
        if eui48 is not None:
            self.set_eui48(eui48)

    def reset(self):
        self.pointer = 0
        self.busy = False

    def set_eui48(self, eui48):
        data = bytes(eui48)
        if len(data) != 6:
            raise ValueError("EUI-48 must be 6 bytes")
        self.memory[self.EUI48_OFFSET:self.EUI48_OFFSET + 6] = data

    def write(self, data):
        if len(data) < 1:
            return False

        # First byte is the word address; the rest are data (page write).
        self.pointer = data[0] & 0xFF
        page_base = self.pointer & ~(self.PAGE_SIZE - 1)
        page_offset = self.pointer & (self.PAGE_SIZE - 1)

        for value in data[1:]:
            address = page_base + page_offset
            if address < self.writable_size:
                self.memory[address] = value
            page_offset = (page_offset + 1) & (self.PAGE_SIZE - 1)

        self.pointer = page_base + page_offset
        return True

    def read(self, count):
        out = bytearray()
        for _ in range(count):
            out.append(self.memory[self.pointer])
            self.pointer = (self.pointer + 1) & 0xFF
        return bytes(out)
