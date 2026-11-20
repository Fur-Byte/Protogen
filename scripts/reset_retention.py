"""Host model of a bounded, advisory journal for warm-reset P3 diagnostics.

An image is a byte array. The caller supplies the reset-domain decision. No
function writes hardware, changes command authority, or requests a reset.
"""

from dataclasses import dataclass
import struct
import zlib


MAGIC = b"P8RR"
VERSION = 1
COMMITTED = b"DONE"
HEADER = struct.Struct("<4sHHIII32s")
RECORD = struct.Struct("<IIIHH")
CHECKSUM = struct.Struct("<I")
CAPACITY = 4
MAX_ATTEMPTS = 3
UINT32_MAX = (1 << 32) - 1
BODY_BYTES = HEADER.size + CAPACITY * RECORD.size
MARKER_OFFSET = BODY_BYTES + CHECKSUM.size
SLOT_BYTES = MARKER_OFFSET + len(COMMITTED)
IMAGE_BYTES = 2 * SLOT_BYTES
# P3 Boot, AudioDrops and MainGap carry only local diagnostic metadata.
ALLOWED_EVENTS = frozenset((1, 10, 12))


@dataclass(frozen=True)
class Snapshot:
    publication: int
    boot_generation: int
    attempts: int
    records: tuple[tuple[int, int, int, int, int], ...]


def _check_revision(revision):
    if not isinstance(revision, bytes) or len(revision) != 32:
        raise ValueError("expected a 32-byte firmware identity")


def _advance(value):
    return (value + 1) & UINT32_MAX


def _follows(current, previous):
    return (current.publication == _advance(previous.publication) and
            current.boot_generation in (previous.boot_generation, _advance(previous.boot_generation)))


def _valid_records(records):
    return (all(event in ALLOWED_EVENTS for _, _, _, event, _ in records) and
            all(0 < ((current[1] - previous[1]) & UINT32_MAX) < (1 << 31)
                for previous, current in zip(records, records[1:])))


def _decode(slot, revision):
    if slot[MARKER_OFFSET:] != COMMITTED:
        return None
    if CHECKSUM.unpack_from(slot, BODY_BYTES)[0] != zlib.crc32(slot[:BODY_BYTES]):
        return None
    magic, version, count, publication, generation, attempts, identity = HEADER.unpack_from(slot)
    if (magic != MAGIC or version != VERSION or identity != revision or
            count > CAPACITY or attempts > MAX_ATTEMPTS):
        return None
    end = HEADER.size + count * RECORD.size
    if any(slot[end:BODY_BYTES]):
        return None
    records = tuple(RECORD.iter_unpack(slot[HEADER.size:end]))
    return Snapshot(publication, generation, attempts, records) if _valid_records(records) else None


def _latest_slot(image, revision):
    first = _decode(image[:SLOT_BYTES], revision)
    second = _decode(image[SLOT_BYTES:], revision)
    if first is None:
        return 1, second
    if second is None:
        return 0, first
    if _follows(second, first):
        return 1, second
    if _follows(first, second):
        return 0, first
    raise ValueError("incompatible publication or boot generation order")


def recover(image, revision, *, trusted_reset):
    """Return the latest committed snapshot for an eligible warm reset."""
    _check_revision(revision)
    if trusted_reset is not True or len(image) != IMAGE_BYTES:
        return None
    try:
        _, latest = _latest_slot(image, revision)
    except ValueError:
        return None
    return latest


def start_boot(previous):
    """Advance the model's generation and start a fresh diagnostic window."""
    if previous is None:
        return Snapshot(0, 0, 1, ())
    return Snapshot(_advance(previous.publication), _advance(previous.boot_generation),
                    min(previous.attempts + 1, MAX_ATTEMPTS), ())


def plan_publish(image, revision, snapshot):
    """Return alternate-slot writes in application order, with the commit marker last."""
    _check_revision(revision)
    if len(image) != IMAGE_BYTES:
        raise ValueError("invalid retention image length")
    latest_slot, previous = _latest_slot(image, revision)
    ordered = ((snapshot.publication, snapshot.boot_generation) == (0, 0)
               if previous is None else _follows(snapshot, previous))
    records = snapshot.records
    if (not ordered or not 0 <= snapshot.attempts <= MAX_ATTEMPTS or
            len(records) > CAPACITY):
        raise ValueError("invalid snapshot bounds or order")
    try:
        header = HEADER.pack(MAGIC, VERSION, len(records), snapshot.publication,
                             snapshot.boot_generation, snapshot.attempts, revision)
        payload = b"".join(RECORD.pack(*record) for record in records)
    except struct.error as error:
        raise ValueError("invalid snapshot field") from error
    if not _valid_records(records):
        raise ValueError("invalid P3 event type or sequence order")
    body = header + payload.ljust(CAPACITY * RECORD.size, b"\x00")
    offset = (1 - latest_slot) * SLOT_BYTES
    marker = offset + MARKER_OFFSET
    return ((marker, bytes(len(COMMITTED))),
            (offset, body + CHECKSUM.pack(zlib.crc32(body))),
            (marker, COMMITTED))
