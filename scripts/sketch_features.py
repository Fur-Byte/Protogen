"""Read and configure Protogen sketch feature declarations."""
import re

SENSOR_FEATURES = ("VisorSensor", "BoopSensor", "Microphone")
ALL_FEATURES = SENSOR_FEATURES + ("WirelessPaw", "VisorConnector")


def feature_declaration(source, name):
    pattern = re.compile(
        r"^([ \t]*)(//[ \t]*)?#[ \t]*define[ \t]+" + name + r"(?=[ \t]|$)([^\r\n]*)$",
        re.MULTILINE,
    )
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one declaration for {name}; found {len(matches)}")
    return pattern, matches[0]


def enabled_features(source):
    """Validate and identify all sensor and extension flags in the built snapshot."""
    return [name for name in ALL_FEATURES if feature_declaration(source, name)[1][2] is None]


def configure_features(source, enabled, names=SENSOR_FEATURES):
    """Set both enabled AND disabled flags; reject ambiguous/missing declarations."""
    if set(enabled) - set(names) or set(names) - set(ALL_FEATURES):
        raise ValueError("Unknown feature")
    for name in names:
        pattern, _ = feature_declaration(source, name)
        prefix = "#define " if name in enabled else "// #define "
        source = pattern.sub(lambda match: match[1] + prefix + name + match[3], source)
    return source
