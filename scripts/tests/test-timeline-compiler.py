import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from timelines.compiler import compile_document, TimelineError

def document(events=()):
    return {"version": 1, "id": "qualification", "name": "Qualification", "duration_ms": 60000,
            "initial": {"expression": 0, "natural_blinks": False, "idle_motion": False,
                        "cosmetic_seed": 19, "audio": 0}, "events": list(events)}

def event(at=0, duration=100, channel="x", region="left", first=-2, last=2):
    return {"at_ms": at, "duration_ms": duration, "region": region, "channel": channel,
            "from": first, "to": last, "easing": "linear", "priority": 3}

class CompilerTests(unittest.TestCase):
    def test_canonical_and_stable_ties(self):
        source = document([event(1000), event(0), event(0, channel="lid", first=0, last=8)])
        asset = compile_document(source)
        self.assertEqual([e["channel"] for e in asset.document["events"]], ["x", "lid", "x"])
        self.assertEqual(compile_document(asset.document).data, asset.data)
        self.assertEqual(asset.data[:8].hex(), "0100030060ea1300")
        self.assertEqual(asset.data[16:26].hex(), "0000640000000300fe02")

    def test_limits(self):
        compile_document(document([event(i * 101) for i in range(32)]))
        for events in ([event()] * 9, [event(i * 101) for i in range(33)],
                       [event(59999, 2)], [event(duration=5001)], [event(first=-3)],
                       [event(region="mouth")], [event(channel="sprite", first=0, last=1)]):
            with self.assertRaises(TimelineError):
                compile_document(document(events))

    def test_repetition(self):
        motion = event()
        motion["repeat"] = {"count": 3, "period_ms": 101}
        asset = compile_document(document([motion, event(101, channel="lid", first=0, last=8)]))
        self.assertEqual(asset.data[2], 4)
        self.assertEqual([int.from_bytes(asset.data[16+i*10:18+i*10], "little") for i in range(4)], [0,101,101,202])

if __name__ == "__main__":
    unittest.main()
