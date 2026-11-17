"""The canonical P6 schema validator and deterministic asset compiler."""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import struct
import sys

MAX_BYTES = 32768
MAX_EVENTS = 32
VERSION = 1
REGIONS = ("left", "right", "mouth", "full")
CHANNELS = {
    "x": (-2, 2), "y": (-1, 1), "lid": (0, 8), "squint": (0, 4),
    "sprite": (0, 5), "aperture": (0, 3),
}
EASING = ("linear", "in", "out", "in-out", "settle")
HEADER = struct.Struct("<HBBHIBBBB2x")
EVENT = struct.Struct("<HHBBBBbb")


class TimelineError(ValueError):
    def __init__(self, path, message):
        self.path = path
        self.message = message
        super().__init__(f"{path}: {message}")

    def diagnostic(self):
        return {"path": self.path, "message": self.message}


def load_json(data):
    """Bound depth before decoding and reject duplicate keys and nonintegers."""
    if len(data) > MAX_BYTES:
        raise TimelineError("$", "document exceeds 32768 bytes")
    depth = 0
    quoted = escaped = False
    for char in data:
        if quoted:
            if escaped:
                escaped = False
            elif char == 92:
                escaped = True
            elif char == 34:
                quoted = False
        elif char == 34:
            quoted = True
        elif char in (91, 123):
            depth += 1
            if depth > 8:
                raise TimelineError("$", "JSON nesting exceeds eight levels")
        elif char in (93, 125):
            depth -= 1

    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise TimelineError("$", f"duplicate field {key!r}")
            result[key] = value
        return result

    def reject(_):
        raise TimelineError("$", "only finite integers are supported")

    def parse_int(token):
        if len(token) > 11:
            raise TimelineError("$", "integer exceeds supported width")
        return int(token)

    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=pairs, parse_int=parse_int,
                          parse_constant=reject, parse_float=reject)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise TimelineError("$", f"invalid JSON: {error}") from error


def fields(value, required, path, optional=()):
    if type(value) is not dict:
        raise TimelineError(path, "expected an object")
    missing = set(required) - value.keys()
    extra = value.keys() - set(required) - set(optional)
    if missing or extra:
        raise TimelineError(path, f"missing fields {sorted(missing)}, unexpected fields {sorted(extra)}")


def integer(value, lo, hi, path):
    if type(value) is not int or not lo <= value <= hi:
        raise TimelineError(path, f"expected integer {lo}..{hi}")
    return value


def choice(value, choices, path):
    if type(value) is not str or value not in choices:
        raise TimelineError(path, f"expected one of {', '.join(choices)}")
    return value


def boolean(value, path):
    if type(value) is not bool:
        raise TimelineError(path, "expected a boolean")
    return value


@dataclass(frozen=True)
class Compiled:
    document: dict
    data: bytes

    def source(self):
        return json.dumps(self.document, indent=2, sort_keys=True, ensure_ascii=True) + "\n"

    def header(self):
        # All generated tokens come from byte values, never user strings.
        lines = [", ".join(str(b) for b in self.data[i:i+16]) for i in range(0, len(self.data), 16)]
        return (
            '#pragma once\n#include "src/BehaviorCore/Timeline.h"\n'
            '#if !PROTOGEN_ANIMATION_TIMELINES\n#error "This asset requires timeline support."\n#endif\n'
            'namespace ProtogenAsset {\ninline constexpr uint8_t clip[] = {\n    '
            + ',\n    '.join(lines) + '\n};\n'
            'static_assert(Behavior::Timeline::validAsset(clip, sizeof(clip)), "Invalid timeline asset");\n'
            '}\n'
        )


def compile_document(doc):
    fields(doc, ("version", "id", "name", "duration_ms", "initial", "events"), "$",
           ("cancel", "completion"))
    integer(doc["version"], VERSION, VERSION, "version")
    identifier = doc["id"]
    if type(identifier) is not str or not re.fullmatch(r"[a-z][a-z0-9_-]{0,31}", identifier):
        raise TimelineError("id", "expected 1–32 lowercase ASCII identifier characters")
    name = doc["name"]
    if type(name) is not str or not 1 <= len(name) <= 80 or not name.isprintable() or not name.strip():
        raise TimelineError("name", "expected a printable name of 1–80 characters")
    duration = integer(doc["duration_ms"], 1, 60000, "duration_ms")
    cancel = choice(doc.get("cancel", "release"), ("release",), "cancel")
    completion = choice(doc.get("completion", "release"), ("release",), "completion")

    initial = doc["initial"]
    fields(initial, ("expression", "natural_blinks", "idle_motion", "cosmetic_seed", "audio"), "initial")
    expression = integer(initial["expression"], 0, 5, "initial.expression")
    seed = integer(initial["cosmetic_seed"], 0, 2**32-1, "initial.cosmetic_seed")
    audio = integer(initial["audio"], 0, 500, "initial.audio")
    idle = boolean(initial["idle_motion"], "initial.idle_motion")
    blink = boolean(initial["natural_blinks"], "initial.natural_blinks")

    if type(doc["events"]) is not list or len(doc["events"]) > MAX_EVENTS:
        raise TimelineError("events", "expected at most 32 events")
    normalized = []
    expanded = []
    names = list(CHANNELS)
    for index, item in enumerate(doc["events"]):
        path = f"events[{index}]"
        fields(item, ("at_ms", "duration_ms", "from", "to", "region", "channel", "priority", "easing"),
               path, ("repeat",))
        at = integer(item["at_ms"], 0, duration, path+".at_ms")
        length = integer(item["duration_ms"], 0, 5000, path+".duration_ms")
        region = choice(item["region"], REGIONS, path+".region")
        channel = choice(item["channel"], CHANNELS, path+".channel")
        if region != "full" and ((region == "mouth") != (channel == "aperture")):
            raise TimelineError(path+".channel", "property does not address this region")
        lo, hi = CHANNELS[channel]
        first = integer(item["from"], lo, hi, path+".from")
        last = integer(item["to"], lo, hi, path+".to")
        if channel == "sprite" and first != last:
            raise TimelineError(path+".to", "sprite events must hold one existing glyph")
        priority = integer(item["priority"], 0, 7, path+".priority")
        easing = choice(item["easing"], EASING, path+".easing")
        count, period = 1, 1
        if "repeat" in item:
            fields(item["repeat"], ("count", "period_ms"), path+".repeat")
            count = integer(item["repeat"]["count"], 2, 8, path+".repeat.count")
            period = integer(item["repeat"]["period_ms"], 1, 60000, path+".repeat.period_ms")
        if at + (count-1)*period + length > duration:
            raise TimelineError(path, "event extends past clip duration")
        if len(expanded) + count > MAX_EVENTS:
            raise TimelineError(path, "expanded event count exceeds 32")
        canonical = {k: item[k] for k in sorted(item)}
        if "repeat" in item:
            canonical["repeat"] = {"count": count, "period_ms": period}
        normalized.append(canonical)
        for n in range(count):
            expanded.append((at+n*period, at, index, length, REGIONS.index(region),
                             names.index(channel), priority, EASING.index(easing), first, last))
    # Equal expanded timestamps sort by canonical source order: original
    # base time, then original declaration index. This survives re-import.
    expanded.sort()
    end = []
    data = bytearray(HEADER.pack(VERSION, len(expanded), audio >> 8, duration, seed, expression,
                                 blink, idle, audio & 255))
    for at, _, index, length, region, channel, priority, easing, first, last in expanded:
        end = [t for t in end if t >= at]  # P5 includes the last millisecond.
        if len(end) == 8:
            raise TimelineError(f"events[{index}]", f"global capacity exceeded at {at} ms")
        end.append(at+length)
        data.extend(EVENT.pack(at, length, region, channel, priority, easing, first, last))
    data.extend(bytes((MAX_EVENTS-len(expanded))*EVENT.size))
    canonical = {"version": VERSION, "id": identifier, "name": name, "duration_ms": duration,
                 "initial": dict(initial), "events": sorted(normalized, key=lambda item: item["at_ms"]),
                 "cancel": cancel, "completion": completion}
    return Compiled(canonical, bytes(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--format", choices=("json", "header", "binary"), default="header")
    args = parser.parse_args()
    try:
        with args.input.open("rb") as stream:
            compiled = compile_document(load_json(stream.read(MAX_BYTES+1)))
        if args.format == "binary":
            sys.stdout.buffer.write(compiled.data)
        else:
            print(compiled.source() if args.format == "json" else compiled.header(), end="")
    except (TimelineError, OSError) as error:
        detail = error.diagnostic() if isinstance(error, TimelineError) else {"path": "$", "message": str(error)}
        print(json.dumps({"error": detail}), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
