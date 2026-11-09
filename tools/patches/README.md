# Patches for external tools

## ipmitool-hpm-bridge-block.patch

`ipmitool hpm upgrade` sizes its upload blocks from the LAN interface's
maximum request data size (256 B). Through a NAT-MCH double bridge the
request is carried in a 32-byte IPMB frame, so the first block never
reaches the target and the upgrade fails. ipmitool only shrinks the block
after the target answers with a "length" error - which a bridge drops.

The patch caps the block at 20 bytes whenever a target/transit address is
set (a bridged request):

```sh
git clone --branch IPMITOOL_1_8_19 https://github.com/ipmitool/ipmitool.git
cd ipmitool && git apply /path/to/ipmitool-hpm-bridge-block.patch
./bootstrap && ./configure --enable-intf-lan --disable-intf-lanplus ... && make
```

With the patched binary and the firmware's HPM handlers accepting the
HPM.1 IANA prefix:

```sh
ipmitool -I lan -H <mch> -A none -T 0x82 -B 0 -t 0x88 -b 0 \
         hpm upgrade image.hpm 1 force activate
```

The toolbox's OTA tab ("Upload (IPMB, .bin)") does the same thing without
patching ipmitool.
