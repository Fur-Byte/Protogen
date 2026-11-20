#!/usr/bin/env python3
"""Check that the production polling delay spans at least one scheduler tick."""
from pathlib import Path
import unittest
import re
ROOT=Path(__file__).resolve().parents[2]
class SchedulerConfiguration(unittest.TestCase):
    def test_five_ms_poll_is_representable_by_supported_sdk_config(self):
        values=dict(line.split('=',1) for line in (ROOT/'firmware/esp32/sdkconfig.defaults').read_text().splitlines() if '=' in line and not line.startswith('#'))
        # The SDK default of 100 Hz would truncate the production poll to zero
        # ticks. Check both the configured tick rate and the converted delay.
        hz=int(values.get('CONFIG_FREERTOS_HZ','100'))
        self.assertEqual(hz,1000)
        config=(ROOT/'firmware/esp32/src/WirelessConfig.h').read_text()
        poll=int(re.search(r'ServicePollMs = (\d+)',config)[1])
        self.assertGreaterEqual(poll*hz//1000,1)
        self.assertLessEqual(poll,2)
if __name__=='__main__': unittest.main()
