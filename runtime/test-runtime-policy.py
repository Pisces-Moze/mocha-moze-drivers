#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-only
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('runtime_policy',Path(__file__).with_name('mocha-runtime-policy.py'))
policy=importlib.util.module_from_spec(spec);spec.loader.exec_module(policy)


class RuntimePolicy(unittest.TestCase):
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
