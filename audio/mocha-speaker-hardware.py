#!/usr/bin/python3
"""Load the validated Mocha speaker path; never write amplifier MTP."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

KERNEL = '6.12.111-moze.1-native-vde2'
ROOT = Path('/usr/local/lib/mocha-audio') / KERNEL / '20261022'
FWROOT = Path('/lib/firmware/mocha')


def run(args):
    return subprocess.run(args, check=True, timeout=20, capture_output=True, text=True)


def control(name, value):
    run(['amixer', '-q', '-c', 'Speakers', 'cset', 'name=' + name, value])


def stop():
    # Do not unload modules while desktop applications can hold ALSA handles.
    if 'Mocha Speakers' not in Path('/proc/asound/cards').read_text():
        return
    for name, value in [('DAC1 Playback Switch', 'off,off'),
                        ('Int Left Spk Switch', 'off'),
                        ('Int Right Spk Switch', 'off')]:
        control(name, value)


def start():
    if os.uname().release != KERNEL:
        raise RuntimeError('audio modules do not match running kernel')
    compatible = Path('/proc/device-tree/compatible').read_bytes().split(b'\0')
    if b'nvidia,mocha' not in compatible:
        raise RuntimeError('this audio path is specific to Mocha')
    manifest = json.loads((ROOT / 'manifest.json').read_text())
    for group, folder in [('modules', ROOT), ('firmware', FWROOT)]:
        for item in manifest[group]:
            p = folder / item['file']
            if p.name != item['file'] or hashlib.sha256(p.read_bytes()).hexdigest() != item['sha256']:
                raise RuntimeError('audio asset mismatch: ' + item['file'])
    for item in manifest['modules']:
        p = ROOT / item['file']
        if run(['modinfo', '-F', 'vermagic', str(p)]).stdout.split()[0] != KERNEL:
            raise RuntimeError('module ABI mismatch: ' + p.name)
        if not (Path('/sys/module') / item['name']).exists():
            run(['insmod', str(p), *item.get('parameters', [])])
    if 'Mocha Speakers' not in Path('/proc/asound/cards').read_text():
        raise RuntimeError('speaker ALSA card not registered')
    # Keep the codec muted while selecting protected firmware and the route.
    control('DAC1 Playback Switch', 'off,off')
    for side in ['Left', 'Right']:
        control(side + ' Spk Digital Volume', '219')  # -18 dB hardware ceiling
        control(side + ' Spk Input Channel Mux', side)
        control(side + ' Spk Amplifier Channel Mux', 'DSP')
        control(side + ' Spk Input Interface Mux', 'Input1')
        names = [('Boot Patch', 'tfa9890_boot.patch'), ('ROM Patch', 'tfa9890_rom.patch'),
                 ('Speaker File', 'tfa9890_' + side.lower() + '.speaker'),
                 ('Config File', 'tfa9890.config'),
                 ('Preset File', 'tfa9890_' + side.lower() + '_music.preset'),
                 ('Equalizer File', 'tfa9890_' + side.lower() + '_music.eq')]
        for label, name in names:
            payload = ('mocha/' + name).encode().ljust(512, b'\0')
            control(side + ' Spk ' + label, ','.join(hex(v) for v in payload))
    for name, value in [('DAC1 L Mux', 'IF1 DAC'), ('DAC1 R Mux', 'IF1 DAC'),
                        ('DAC1 MIXL DAC1 Switch', 'on'), ('DAC1 MIXR DAC1 Switch', 'on'),
                        ('RxDP Mux', 'DAC1'), ('DSP DL Mux', 'Bypass'), ('DSP UL Mux', 'Bypass'),
                        ('IF2 ADC Mux', 'TxDC_DAC'), ('Int Left Spk Switch', 'on'),
                        ('Int Right Spk Switch', 'on'), ('DAC1 Playback Switch', 'on,on')]:
        control(name, value)
    print('Mocha protected speaker path configured; DSP starts with playback')


if __name__ == '__main__':
    try:
        if len(sys.argv) == 2 and sys.argv[1] == 'stop':
            stop()
        else:
            start()
    except Exception:
        try:
            stop()
        except Exception:
            pass
        raise
