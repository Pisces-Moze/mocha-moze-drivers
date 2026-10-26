#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-only
"""Opt-in Tegra CPU policy and RAM-only swap; never edits voltage/OPP tables."""
import json
import logging
import os
from pathlib import Path
import signal
import subprocess
import time

LOG = logging.getLogger('mocha-runtime')
SYS = Path('/sys')
DEFAULTS = {'cpu_max_mhz': 1200, 'swappiness': 80}
running = True


def settings(path):
    result = DEFAULTS.copy()
    try:
        with path.open() as source:
            value = json.loads(source.read(4097))
        # Only bounded numeric preferences can affect privileged sysfs writes.
        if isinstance(value, dict):
            for key, low, high in (('cpu_max_mhz', 600, 1200), ('swappiness', 10, 100)):
                item = value.get(key)
                if type(item) is int and low <= item <= high:
                    result[key] = item
    except (OSError, ValueError):
        pass
    return result


def stock_ceiling(frequencies, limit):
    eligible = [value for value in frequencies if value <= limit]
    if not eligible:
        raise ValueError('No stock OPP within the requested CPU limit')
    return max(eligible)


def thermal_latch(hot, temperatures):
    # Missing sensors fail to the conservative ceiling, not full performance.
    if not temperatures or max(temperatures) >= 80000:
        return True
    return hot and max(temperatures) > 72000


def rate_matches(requested, measured):
    # DFLL's monitor is quantized by a 51 MHz reference / four cycles.
    # CCF/scaling_cur_freq alone can report an unapplied software request.
    return requested > 0 and measured > 0 and abs(requested-measured) <= max(25500000,requested//10)


def verify_hardware_rate(node):
    monitor=SYS/'kernel/debug/tegra_dfll_fcpu/rate'
    matched = 0
    previous = None
    mismatch = None
    for _ in range(20):
        before=int(node.joinpath('scaling_cur_freq').read_text())*1000
        time.sleep(.02)
        measured=int(monitor.read_text())
        after=int(node.joinpath('scaling_cur_freq').read_text())*1000
        if before==after and rate_matches(after,measured):
            matched = matched + 1 if previous == after else 1
            previous = after
            if matched >= 2:
                return
        else:
            matched = 0
            previous = None
            if before == after:
                mismatch = (after, measured)
        # Switching governors can leave transitional monitor samples. Keep
        # the same error threshold and require two consecutive matches at
        # one request, within a bounded window; one lucky sample is not proof.
        time.sleep(.05)
    if mismatch is not None:
        requested, measured = mismatch
        raise ValueError(f'DFLL monitor {measured} Hz does not follow CPUFreq {requested} Hz; refuse runtime policy')
    raise ValueError('No stable CPU/DFLL sample; refuse runtime policy')


def temperatures():
    values=[]
    for node in SYS.glob('class/thermal/thermal_zone*'):
        try:
            if node.joinpath('type').read_text().strip() in ('cpu-thermal', 'gpu-thermal'):
                values.append(int(node.joinpath('temp').read_text()))
        except (OSError, ValueError):
            continue
    return values


def start_zram():
    node=SYS/'block/zram0'
    if not node.exists():
        LOG.info('zram unavailable in this kernel')
        return
    # An existing consumer owns its device; do not reset, resize or swapoff it.
    if int(node.joinpath('disksize').read_text()) != 0:
        LOG.info('existing zram configuration preserved')
        return
    algorithms=node.joinpath('comp_algorithm').read_text().replace('[','').replace(']','').split()
    if 'lz4' not in algorithms:
        raise ValueError('Candidate requires the LZ4 zram backend')
    node.joinpath('comp_algorithm').write_text('lz4')
    node.joinpath('disksize').write_text(str(384*1024*1024))
    subprocess.run(['mkswap','/dev/zram0'],check=True,capture_output=True,timeout=15)
    subprocess.run(['swapon','--priority','100','/dev/zram0'],check=True,timeout=15)
    LOG.info('LZ4 RAM swap enabled: 384 MiB logical, priority 100')


def stop(signum, frame):
    global running
    running=False


def supports_kernel(release):
    # VDE2 carries the same Palmas/DFLL alignment and RAM swap support. Do
    # not admit other experimental VDE builds by a broad suffix match.
    return release.endswith('-native-runtime') or release == '6.12.111-moze.1-native-vde2'


def main():
    import pwd
    if os.geteuid()!=0:
        raise PermissionError('Run the trial service as root')
    # This candidate has matching CPU DVFS, DFLL and zram support. Installing
    # the service on the accepted native kernel must not imply those exist.
    if not supports_kernel(os.uname().release):
        raise ValueError('Use the separately validated native-runtime or exact VDE2 kernel')
    policies=list(SYS.glob('devices/system/cpu/cpufreq/policy*'))
    if not policies:raise ValueError('No CPU DVFS policy exposed')
    # This gate runs before any frequency/governor/swap change. A mismatch is
    # a hardware-control failure, not something a governor ceiling repairs.
    for node in policies:verify_hardware_rate(node)
    config=Path(pwd.getpwnam('mocha').pw_dir) / '.config/mocha/runtime.json'
    originals=[]
    swappiness=Path('/proc/sys/vm/swappiness')
    previous_swappiness=swappiness.read_text()
    try:
        # Respect the user's lower cap, current display state and thermal
        # protection before schedutil can ramp; the first loop is too late.
        preference=settings(config)
        hot=thermal_latch(False,temperatures())
        display_on=any(node.read_text().strip()=='On' for node in SYS.glob('class/drm/card*-DSI-1/dpms'))
        initial_ceiling=min(preference['cpu_max_mhz']*1000,
                            792000 if hot else 1200000,
                            1200000 if display_on else 510000)
        for node in policies:
            frequencies=[int(value) for value in node.joinpath('scaling_available_frequencies').read_text().split()]
            governors=node.joinpath('scaling_available_governors').read_text().split()
            if 'schedutil' not in governors:raise ValueError('schedutil unavailable')
            originals.append((node, node.joinpath('scaling_governor').read_text(),
                              node.joinpath('scaling_max_freq').read_text(), frequencies))
            # Apply the cap before permitting the governor to ramp frequency.
            node.joinpath('scaling_max_freq').write_text(str(stock_ceiling(frequencies,initial_ceiling)))
            node.joinpath('scaling_governor').write_text('schedutil')
        for node, _, _, _ in originals:verify_hardware_rate(node)
        start_zram()
        last=None
        while running:
            preference=settings(config)
            hot=thermal_latch(hot,temperatures())
            display_on=any(node.read_text().strip()=='On' for node in SYS.glob('class/drm/card*-DSI-1/dpms'))
            ceiling=min(preference['cpu_max_mhz']*1000,792000 if hot else 1200000,
                        1200000 if display_on else 510000)
            caps=[]
            for node, _, _, frequencies in originals:
                cap=stock_ceiling(frequencies,ceiling)
                if int(node.joinpath('scaling_max_freq').read_text())!=cap:
                    node.joinpath('scaling_max_freq').write_text(str(cap))
                    verify_hardware_rate(node)
                caps.append(cap)
            if int(swappiness.read_text())!=preference['swappiness']:
                swappiness.write_text(str(preference['swappiness']))
            state=(tuple(caps),hot,display_on,preference['swappiness'])
            if state!=last:
                LOG.info('CPU caps_khz=%s hot=%s display_on=%s swappiness=%s',*state)
                last=state
            time.sleep(2)
    finally:
        for node, governor, ceiling, _ in originals:
            try:
                node.joinpath('scaling_governor').write_text('powersave')
                node.joinpath('scaling_max_freq').write_text(ceiling)
                node.joinpath('scaling_governor').write_text(governor)
            except OSError:LOG.exception('Could not restore CPU policy')
        swappiness.write_text(previous_swappiness)
        # Leave RAM swap online: swapoff can allocate hundreds of MiB at exit.


if __name__=='__main__':
    logging.basicConfig(level=logging.INFO,format='%(name)s %(message)s')
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    main()
