#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-only
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys
from unittest.mock import patch
from types import SimpleNamespace

spec=importlib.util.spec_from_file_location('runtime_policy',Path(__file__).with_name('mocha-runtime-policy.py'))
policy=importlib.util.module_from_spec(spec);spec.loader.exec_module(policy)


class RuntimePolicy(unittest.TestCase):
    def test_vde_guard_accepts_only_the_qualified_exact_release(self):
        self.assertTrue(policy.supports_kernel('6.12.111-moze.1-native-runtime'))
        self.assertTrue(policy.supports_kernel('6.12.111-moze.1-native-vde2'))
        for release in ['6.12.111-moze.1-native', '6.12.111-moze.1-native-vde1',
                        '6.12.111-moze.1-native-vde3', '6.12.112-moze.1-native-vde2']:
            with self.subTest(release=release):
                self.assertFalse(policy.supports_kernel(release))

    def test_startup_never_briefly_exceeds_user_thermal_or_screen_cap(self):
        for user_cap,screen_on,temp,expected in ((600,True,65000,510000),
                                                (1020,False,65000,510000),
                                                (1020,True,81000,714000),
                                                (1020,True,65000,1020000)):
            with self.subTest(user_cap=user_cap,screen_on=screen_on,temp=temp),tempfile.TemporaryDirectory() as folder:
                root=Path(folder)
                node=root/'devices/system/cpu/cpufreq/policy0';node.mkdir(parents=True)
                for name,value in {'scaling_governor':'powersave','scaling_max_freq':'612000',
                                   'scaling_available_frequencies':'204000 306000 408000 510000 612000 714000 816000 918000 1020000 1122000',
                                   'scaling_available_governors':'powersave schedutil'}.items():
                    (node/name).write_text(value)
                dpms=root/'class/drm/card1-DSI-1/dpms';dpms.parent.mkdir(parents=True);dpms.write_text('On' if screen_on else 'Off')
                config=root/'.config/mocha/runtime.json';config.parent.mkdir(parents=True)
                config.write_text(json.dumps({'cpu_max_mhz':user_cap}))
                swappiness=root/'swappiness';swappiness.write_text('60')
                writes=[]
                original_write=Path.write_text
                def write(path,data,*args,**kwargs):
                    if path==node/'scaling_max_freq':writes.append(int(data))
                    return original_write(path,data,*args,**kwargs)
                def sleep(_):policy.running=False
                policy.running=True
                with patch.object(policy,'SYS',root),patch.dict(sys.modules,{'pwd':SimpleNamespace(getpwnam=lambda _:SimpleNamespace(pw_dir=str(root)))}),\
                     patch.object(policy.os,'geteuid',return_value=0,create=True),patch.object(policy.os,'uname',return_value=SimpleNamespace(release='fixture-native-runtime'),create=True),\
                     patch.object(policy,'temperatures',return_value=[temp]),patch.object(policy,'verify_hardware_rate'),\
                     patch.object(policy,'start_zram'),patch.object(policy.time,'sleep',side_effect=sleep),\
                     patch.object(Path,'write_text',autospec=True,side_effect=write),\
                     patch.object(policy,'Path',side_effect=lambda value:swappiness if value=='/proc/sys/vm/swappiness' else Path(value)):
                    policy.main()
                self.assertEqual(writes[0],expected)
                self.assertTrue(all(value<=expected for value in writes[:-1]))
                self.assertEqual(writes[-1],612000)
                self.assertEqual((node/'scaling_governor').read_text(),'powersave')
                self.assertEqual(swappiness.read_text(),'60')

    def test_cached_request_is_not_hardware_acceptance(self):
        self.assertFalse(policy.rate_matches(204000000,1874250000))
        self.assertTrue(policy.rate_matches(204000000,204000000))
        self.assertTrue(policy.rate_matches(204000000,216750000))
        self.assertFalse(policy.rate_matches(0,0))

    def test_mismatched_dfll_refuses_before_policy_application(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            monitor=root/'kernel/debug/tegra_dfll_fcpu/rate'
            monitor.parent.mkdir(parents=True)
            monitor.write_text('1874250000')
            node=root/'policy0';node.mkdir()
            (node/'scaling_cur_freq').write_text('204000')
            with patch.object(policy,'SYS',root),patch.object(policy.time,'sleep'):
                with self.assertRaisesRegex(ValueError,'does not follow CPUFreq'):
                    policy.verify_hardware_rate(node)

    def test_no_frequency_is_invented_or_rounded_up(self):
        stock=[204000,510000,696000,1020000,1224000]
        self.assertEqual(policy.stock_ceiling(stock,1200000),1020000)
        self.assertEqual(policy.stock_ceiling(stock,792000),696000)
        with self.assertRaises(ValueError):policy.stock_ceiling(stock,100000)

    def test_transient_samples_need_two_consecutive_matches(self):
        root=Path('/fixture')
        monitor=root/'kernel/debug/tegra_dfll_fcpu/rate'
        node=root/'policy0'
        values=iter(['900000000']*3+['221000000']*2)
        samples=[]
        def read(path, *args, **kwargs):
            if path==monitor:
                value=next(values);samples.append(value);return value
            return '204000'
        with patch.object(policy,'SYS',root),patch.object(Path,'read_text',autospec=True,side_effect=read),patch.object(policy.time,'sleep'):
            policy.verify_hardware_rate(node)
        self.assertEqual(len(samples),5)

    def test_isolated_matching_sample_does_not_hide_failure(self):
        root=Path('/fixture')
        monitor=root/'kernel/debug/tegra_dfll_fcpu/rate'
        values=iter(['221000000','1874250000']*10)
        def read(path, *args, **kwargs):
            return next(values) if path==monitor else '204000'
        with patch.object(policy,'SYS',root),patch.object(Path,'read_text',autospec=True,side_effect=read),patch.object(policy.time,'sleep'):
            with self.assertRaisesRegex(ValueError,'does not follow CPUFreq'):
                policy.verify_hardware_rate(root/'policy0')

    def test_temperature_hysteresis_and_missing_sensor(self):
        self.assertTrue(policy.thermal_latch(False,[80000]))
        self.assertTrue(policy.thermal_latch(True,[75000]))
        self.assertFalse(policy.thermal_latch(True,[72000]))
        self.assertTrue(policy.thermal_latch(False,[]))

    def test_user_values_cannot_expand_privileged_limits(self):
        with tempfile.TemporaryDirectory() as folder:
            config=Path(folder)/'runtime.json'
            for data in ({'cpu_max_mhz':2300,'swappiness':-1},
                         {'cpu_max_mhz':True,'swappiness':'80'},[],
                         {'cpu_max_mhz':'1200; rm -rf /'}):
                config.write_text(json.dumps(data))
                self.assertEqual(policy.settings(config),policy.DEFAULTS)
            config.write_text('{truncated')
            self.assertEqual(policy.settings(config),policy.DEFAULTS)
            config.write_text('{"cpu_max_mhz":792,"swappiness":60}')
            self.assertEqual(policy.settings(config),{'cpu_max_mhz':792,'swappiness':60})

    def test_existing_zram_is_never_reset_or_reformatted(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);node=root/'block/zram0';node.mkdir(parents=True)
            node.joinpath('disksize').write_text('268435456')
            with patch.object(policy,'SYS',root),patch.object(policy.subprocess,'run') as command:
                policy.start_zram()
                command.assert_not_called()
                self.assertEqual(node.joinpath('disksize').read_text(),'268435456')


if __name__=='__main__':unittest.main()
