#!/usr/bin/env python3
"""Exercise the P8 host-only reset-retention journal."""

from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import reset_retention as model


REVISION = b"\x21" * 32
OTHER_REVISION = b"\x43" * 32
EMPTY = bytes(248)
BOOT = (100, 0, 0, 1, 0)
GAP = (200, 2, 1, 12, 0)


def apply_writes(image, operations, limit=None):
    result = bytearray(image)
    for offset, data in operations:
        size = len(data) if limit is None else min(limit, len(data))
        result[offset:offset + size] = data[:size]
        if limit is not None:
            limit -= size
    return bytes(result)


def fixture(publication=0, generation=0, attempts=1, records=(BOOT,), revision=REVISION):
    """Build fixture bytes directly from the documented wire format."""
    header = struct.pack("<4sHHIII32s", b"P8RR", 1, len(records), publication, generation,
                         attempts, revision)
    payload = b"".join(struct.pack("<IIIHH", *record) for record in records)
    body = header + payload.ljust(64, b"\x00")
    return body + struct.pack("<I", zlib.crc32(body)) + b"DONE"


def checksum(slot):
    struct.pack_into("<I", slot, 116, zlib.crc32(slot[:116]))


class ResetRetentionTests(unittest.TestCase):
    def recover(self, image, revision=REVISION, trusted_reset=True):
        return model.recover(image, revision, trusted_reset=trusted_reset)

    def test_format_and_known_record_offsets(self):
        state = model.Snapshot(0, 0, 1, (BOOT, GAP))
        image = apply_writes(EMPTY, model.plan_publish(EMPTY, REVISION, state))
        self.assertEqual(image, fixture(records=(BOOT, GAP)) + bytes(124))
        self.assertEqual(self.recover(image), state)

    def test_boot_generation_and_attempts_saturate_until_stable(self):
        image = EMPTY
        for boot in range(9):
            prior = self.recover(image)
            state = model.start_boot(prior)
            self.assertEqual(state, model.Snapshot(boot, boot, min(boot + 1, 3), ()))
            image = apply_writes(image, model.plan_publish(image, REVISION, state))
        stable = model.Snapshot(9, 8, 0, (BOOT, GAP))
        image = apply_writes(image, model.plan_publish(image, REVISION, stable))
        self.assertEqual(self.recover(image), stable)
        self.assertEqual(model.start_boot(stable), model.Snapshot(10, 9, 1, ()))

    def test_rollover_of_publication_boot_generation_and_p3_sequence(self):
        image = fixture(0xFFFFFFFF, 0xFFFFFFFF) + fixture(0xFFFFFFFE, 0xFFFFFFFE)
        records = ((0xFFFFFFFB, 0xFFFFFFFF, 2, 12, 0), (8, 0, 1, 10, 0))
        first = model.start_boot(self.recover(image))
        self.assertEqual(first, model.Snapshot(0, 0, 2, ()))
        state = model.Snapshot(first.publication, first.boot_generation, first.attempts, records)
        image = apply_writes(image, model.plan_publish(image, REVISION, state))
        self.assertEqual(self.recover(image), state)

    def test_rejection_of_format_fields_even_when_checksum_matches(self):
        for name, offset, field in [
            ("magic", 0, b"bad!"), ("version", 4, b"\x02\x00"),
            ("count", 6, b"\x05\x00"), ("attempt count", 16, b"\x04\x00\x00\x00"),
            ("unknown event", 64, b"\x0d\x00"), ("dirty padding", 68, b"\x01"),
            ("marker", 120, b"donE"),
        ]:
            with self.subTest(field=name):
                invalid = bytearray(fixture())
                invalid[offset:offset + len(field)] = field
                checksum(invalid)
                self.assertIsNone(self.recover(invalid + bytes(124)))

    def test_bad_crc_rolls_back_to_previous_snapshot(self):
        prior_slot = fixture(7, 3)
        next_slot = bytearray(fixture(8, 3, records=(BOOT, GAP)))
        for offset in (8, 58, 79, 116, 119):
            with self.subTest(offset=offset):
                corrupt = next_slot.copy()
                corrupt[offset] ^= 1
                self.assertEqual(self.recover(prior_slot + corrupt), model.Snapshot(7, 3, 1, (BOOT,)))

    def test_every_partial_write_preserves_preceding_commit(self):
        for initial, previous in [
            (EMPTY, None),
            (fixture(0, 0) + bytes(124), model.Snapshot(0, 0, 1, (BOOT,))),
            (fixture(0, 0) + fixture(1, 1), model.Snapshot(1, 1, 1, (BOOT,))),
        ]:
            next_state = model.start_boot(previous)
            writes = model.plan_publish(initial, REVISION, next_state)
            full = sum(len(data) for _, data in writes)
            for prefix in range(full):
                with self.subTest(previous=previous, prefix=prefix):
                    self.assertEqual(self.recover(apply_writes(initial, writes, prefix)), previous)
            self.assertEqual(self.recover(apply_writes(initial, writes)), next_state)

    def test_reset_cause_revision_and_image_length_gates(self):
        image = fixture() + bytes(124)
        self.assertIsNone(self.recover(image, OTHER_REVISION))
        self.assertIsNone(self.recover(image, trusted_reset=False))
        for invalid in (image[:-1], image + b"\x00", b"\xff" * 248, EMPTY):
            self.assertIsNone(self.recover(invalid))
        with self.assertRaises(ValueError):
            self.recover(image, b"short")

    def test_conflicting_slots_reject_recovery_and_publication(self):
        for publication, generation in ((0, 1), (2, 1), (0x80000000, 1), (1, 4)):
            with self.subTest(publication=publication, generation=generation):
                image = fixture(0, 0) + fixture(publication, generation)
                self.assertIsNone(self.recover(image))
                with self.assertRaises(ValueError):
                    model.plan_publish(image, REVISION, model.start_boot(None))

    def test_rejected_write_shapes(self):
        for state in (
            model.Snapshot(1, 0, 1, ()),
            model.Snapshot(0, 2, 1, ()),
            model.Snapshot(0, 0, 4, ()),
            model.Snapshot(0, -1, 1, ()),
            model.Snapshot(0, 0, 1, (BOOT,) * 5),
            model.Snapshot(0, 0, 1, (BOOT, BOOT)),
            model.Snapshot(0, 0, 1, (GAP, BOOT)),
            model.Snapshot(0, 0, 1, ((1, 0, 9, 11, 0),)),
        ):
            with self.subTest(state=state), self.assertRaises(ValueError):
                model.plan_publish(EMPTY, REVISION, state)


if __name__ == "__main__":
    unittest.main()
