#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build an isolated UARTC PIO diagnostic; never installs or binds it.

Derives from Linux 6.12.111 drivers/tty/serial/serial-tegra.c and preserves
its NVIDIA copyright and GPL-2.0 header. Only the Mocha UARTC can probe;
driver/TTY names are separate from the built-in driver.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--destination',type=Path,required=True)
    parser.add_argument('--raw-tty',action='store_true',help='Expose a private TTY instead of the Bluetooth serdev child')
    args=parser.parse_args()
    release=(args.output/'include/config/kernel.release').read_text().strip()
    if release!='6.12.111-moze.1-native-vde2':
        parser.error('Use the matching VDE2 build output')
    data=(args.source/'drivers/tty/serial/serial-tegra.c').read_bytes().replace(b'\r\n',b'\n')
    source_sha=hashlib.sha256(data).hexdigest()
    if source_sha!='6125dc3a3a68b7586cbe88fad4f5d2ac1c99cb0d971f64e3087ffe0efbbe23a7':
        parser.error('UART source differs from the pinned 6.12.111 reference')
    text=data.decode()
    replacements={
        '"tegra_hsuart"':'"mocha_uart_pio"',
        '"ttyTHS"':'"ttyMOCHA"',
        '"serial-tegra"':'"mocha-uart-pio"',
        '"platform:serial-tegra"':'"platform:mocha-uart-pio"',
        'index = of_property_match_string(np, "dma-names", "rx");':'index = -ENODEV; /* explicit RX PIO diagnostic */',
        'index = of_property_match_string(np, "dma-names", "tx");':'index = -ENODEV; /* explicit TX PIO diagnostic */',
    }
    for old,new in replacements.items():
        if text.count(old)!=1:raise ValueError('Unexpected UART source anchor: '+old)
        text=text.replace(old,new)
    anchor='\tcdata = of_device_get_match_data(&pdev->dev);'
    assert text.count(anchor)==1
    guard='''\tif (!of_machine_is_compatible("nvidia,mocha"))
\t\treturn -ENODEV;
\tresource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
\tif (!resource || resource->start != 0x70006200)
\t\treturn -ENODEV;

'''
    text=text.replace(anchor,guard+anchor)
    name='mocha_uart_pio'
    driver='mocha-uart-pio'
    if args.raw_tty:
        name='mocha_uart_raw';driver='mocha-uart-raw'
        text=text.replace('mocha_uart_pio',name).replace('mocha-uart-pio',driver)
        anchor='\tconst struct tegra_uart_chip_data *cdata;\n\n\tif (!of_machine_is_compatible'
        assert text.count(anchor)==1
        text=text.replace(anchor,'\tconst struct tegra_uart_chip_data *cdata;\n\tstruct fwnode_handle *saved_node;\n\n\tif (!of_machine_is_compatible')
        anchor='\tret = uart_add_one_port(&tegra_uart_driver, u);'
        assert text.count(anchor)==1
        # Suppress automatic serdev creation only during port registration.
        # The original firmware node is restored before probe returns.
        text=text.replace(anchor,'\tsaved_node = dev_fwnode(&pdev->dev);\n\tdevice_set_node(&pdev->dev, NULL);\n'+anchor+'\n\tdevice_set_node(&pdev->dev, saved_node);')
    args.destination.mkdir(parents=True,exist_ok=False)
    (args.destination/(name+'.c')).write_text(text)
    (args.destination/'Makefile').write_text('obj-m += '+name+'.o\n')
    subprocess.run(['make','-C',str(args.source.resolve()),'O='+str(args.output.resolve()),
                    'M='+str(args.destination.resolve()),'ARCH=arm','CROSS_COMPILE=arm-linux-gnueabihf-',
                    'LOCALVERSION=','-j1','modules'],check=True)
    module=args.destination/(name+'.ko')
    vermagic=subprocess.check_output(['modinfo','-F','vermagic',str(module)],text=True).strip()
    if vermagic.split()[0]!=release:raise ValueError('Built module release mismatch')
    record={'kernelrelease':release,'source_sha256':source_sha,
            'derived_source_sha256':hashlib.sha256(text.encode()).hexdigest(),
            'module_sha256':hashlib.sha256(module.read_bytes()).hexdigest(),
            'vermagic':vermagic,'driver':driver,'uart_address':'0x70006200',
            'mode':'raw TTY PIO' if args.raw_tty else 'RX and TX PIO','installed':False,'loaded':False}
    (args.destination/'build.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record))

if __name__=='__main__':main()
