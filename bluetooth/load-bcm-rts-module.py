#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-only
"""Explicit, fail-closed RTS startup fix for the installed VDE2 kernel."""
import hashlib
import ctypes
import os
from pathlib import Path
import stat
import socket
import struct
import subprocess
import time

RELEASE = '6.12.111-moze.1-native-vde2'
SHA256 = '082945624fb01ea2c98af98cb8df8606fe15a5155789971f6e11b5e506258851'
MODULE = Path('/usr/local/libexec/mocha-bt-modules') / SHA256[:16] / 'mocha_bcm_rts.ko'
DEVICE = Path('/sys/bus/serial/devices/serial0-0')
ORIGINAL = Path('/sys/bus/serial/drivers/hci_uart_bcm')
CANDIDATE = Path('/sys/bus/serial/drivers/mocha_bcm_rts')


def controller_ready():
    # Read-only Linux MGMT commands; avoid the interactive btmgmt shell.
    def request(channel, opcode, index):
        channel.send(struct.pack('<HHH', opcode, index, 0))
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            channel.settimeout(max(.01, deadline - time.monotonic()))
            data = channel.recv(1024)
            if len(data) < 9:
                continue
            event, response_index, length = struct.unpack_from('<HHH', data)
            command, status = struct.unpack_from('<HB', data, 6)
            if event == 1 and response_index == index and command == opcode:
                if status or len(data) != length + 6:
                    return None
                return data[9:]
        return None
    try:
        with socket.socket(socket.AF_BLUETOOTH, socket.SOCK_RAW, socket.BTPROTO_HCI) as channel:
            # CPython 3.13's HCI address converter cannot express channels.
            # sockaddr_hci uses three native-endian unsigned shorts on Linux.
            address = ctypes.create_string_buffer(struct.pack('=HHH', socket.AF_BLUETOOTH, 0xffff, 3))
            libc = ctypes.CDLL(None, use_errno=True)
            libc.bind.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint]
            libc.bind.restype = ctypes.c_int
            if libc.bind(channel.fileno(), address, 6) != 0:
                number = ctypes.get_errno()
                raise OSError(number, os.strerror(number))
            indices = request(channel, 3, 0xffff)  # MGMT_OP_READ_INDEX_LIST
            if not indices or len(indices) < 4:
                return False
            count = struct.unpack_from('<H', indices)[0]
            if len(indices) != 2 + count * 2:
                return False
            for offset in range(2, len(indices), 2):
                index = struct.unpack_from('<H', indices, offset)[0]
                info = request(channel, 4, index)  # MGMT_OP_READ_INFO
                if info and len(info) >= 7 and info[6] != 0:
                    return True
            return False
    except (OSError, TimeoutError):
        return False


def main():
    if os.geteuid() != 0 or os.uname().release != RELEASE:
        raise RuntimeError('Requires root and the exact installed VDE2 kernel')
    if b'nvidia,mocha' not in Path('/sys/firmware/devicetree/base/compatible').read_bytes().split(b'\0'):
        raise RuntimeError('Not a Mocha board')
    if b'brcm,bcm43540-bt' not in (DEVICE / 'of_node/compatible').read_bytes().split(b'\0'):
        raise RuntimeError('Unexpected Bluetooth controller')
    if MODULE.resolve(strict=True) != MODULE:
        raise RuntimeError('Module path must not contain symlinks')
    for path in [MODULE, *MODULE.parents]:
        info = path.stat()
        if info.st_uid != 0 or info.st_mode & 0o022:
            raise RuntimeError('Untrusted module path permissions')
    if not stat.S_ISREG(MODULE.stat().st_mode) or hashlib.sha256(MODULE.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError('Module content mismatch')
    vermagic = subprocess.check_output(['modinfo', '-F', 'vermagic', str(MODULE)], text=True)
    if vermagic.split()[0] != RELEASE:
        raise RuntimeError('Module release mismatch')
    # BlueZ's userspace HID backend and AVRCP need these input interfaces.
    # Load before the already-bound fast path as well as on a cold boot.
    for name in ('uhid', 'uinput'):
        subprocess.run(['modprobe', name], check=True, timeout=10)
    driver = (DEVICE / 'driver').resolve(strict=True)
    if driver == CANDIDATE and controller_ready():
        print('MOCHA_BLUETOOTH_RTS_READY')
        return
    if driver != ORIGINAL:
        raise RuntimeError('Unexpected current Bluetooth driver')
    if Path('/sys/module/mocha_bcm_rts').exists():
        raise RuntimeError('Unexpected preloaded candidate module')
    loaded = False
    try:
        subprocess.run(['insmod', str(MODULE)], check=True, timeout=10)
        loaded = True
        (ORIGINAL / 'unbind').write_text(DEVICE.name)
        (CANDIDATE / 'bind').write_text(DEVICE.name)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if controller_ready():
                print('MOCHA_BLUETOOTH_RTS_READY')
                return
            time.sleep(.5)
        raise RuntimeError('Bluetooth initialization did not complete')
    except Exception:
        if (DEVICE / 'driver').exists() and (DEVICE / 'driver').resolve() == CANDIDATE:
            (CANDIDATE / 'unbind').write_text(DEVICE.name)
        if not (DEVICE / 'driver').exists():
            (ORIGINAL / 'bind').write_text(DEVICE.name)
        if loaded:
            subprocess.run(['rmmod', 'mocha_bcm_rts'], check=True, timeout=10)
        raise


if __name__ == '__main__':
    main()
