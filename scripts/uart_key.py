"""Firmware-only UART key checks. Public fixtures remain usable by host tests.

These checks catch provisioning mistakes; they cannot prove entropy or uniqueness.
Only literal key headers are accepted so preprocessing cannot change the checked key.
Errors deliberately omit header contents and key values.
"""
from pathlib import Path
import os
import re
import shlex


def validate_uart_key_directory(directory):
    private = Path(directory).resolve()
    header = private / "ProtogenUartKey.h"
    if not header.is_file():
        raise RuntimeError("Missing per-helmet UART key header")
    try:
        text = header.read_text(encoding="ascii")
    except (OSError, UnicodeError):
        raise RuntimeError("Cannot read per-helmet UART key header") from None
    # C/C++ splices continued lines before parsing comments. Forbid splices and
    # their trigraph spelling so checked bytes cannot disappear in a comment.
    if "\\" in text or "??/" in text:
        raise RuntimeError("UART key headers cannot contain line continuations or trigraphs")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.DOTALL)
    match = re.fullmatch(
        r"\s*#pragma\s+once\s*#include\s*<stdint\.h>\s*"
        r"static\s+const\s+uint8_t\s+PROTOGEN_UART_AUTH_KEY\s*\[\s*32\s*\]"
        r"\s*=\s*\{([^{}]*)\}\s*;\s*", text)
    if not match:
        raise RuntimeError("UART key header must contain only the generated literal 32-byte key declaration")
    values = match[1].strip().removesuffix(",").split(",")
    literal = r"(?:0[xX][0-9a-fA-F]+|0|[1-9][0-9]*)(?:[uU])?"
    if len(values) != 32 or any(not re.fullmatch(literal, value.strip()) for value in values):
        raise RuntimeError("UART key must contain exactly 32 explicit byte literals")
    numbers = [int(value.strip().rstrip("uU"), 16 if value.strip().lower().startswith("0x") else 10)
               for value in values]
    if any(value > 255 for value in numbers):
        raise RuntimeError("UART key literals must be bytes")
    key = bytes(numbers)
    # Includes the public {13} fixture expanded to 32 bytes, uniform/sparse keys,
    # repeated patterns and the common sequential-byte examples.
    if (len(set(key)) < 8 or max(key.count(value) for value in set(key)) >= 16 or
            any(key == key[:period] * (32 // period) for period in (1, 2, 4, 8, 16)) or
            key == bytes((key[0] + offset) % 256 for offset in range(32)) or
            key == bytes((key[0] - offset) % 256 for offset in range(32))):
        raise RuntimeError("Public test or obviously weak UART key is forbidden in firmware builds")
    return private


def compiler_options(flags, directory=None):
    """Read macro presence and include paths from joined or split compiler flags."""
    tokens = shlex.split(flags) if isinstance(flags, str) else list(flags)
    macros, includes = set(), []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        option = next((prefix for prefix in ("-isystem", "-iquote", "-I", "-D", "-U")
                       if token.startswith(prefix)), None)
        if option:
            value = token[len(option):]
            if not value:
                index += 1
                if index == len(tokens):
                    raise RuntimeError("Missing compiler option argument")
                value = tokens[index]
            if option == "-D":
                macros.add(value.partition("=")[0])
            elif option == "-U":
                macros.discard(value)
            else:
                path = Path(value)
                includes.append(((Path(directory) / path) if directory and not path.is_absolute() else path).resolve())
        index += 1
    return macros, includes


def validate_uart_key_flags(flags, extra_directories=()):
    _, includes = compiler_options(flags)
    environment = [Path(path or ".").resolve() for name in ("CPATH", "CPLUS_INCLUDE_PATH")
                   if name in os.environ for path in os.environ[name].split(os.pathsep)]
    candidates = list(dict.fromkeys([Path(path).resolve() for path in extra_directories] + includes + environment))
    headers = [path for path in candidates if (path / "ProtogenUartKey.h").is_file()]
    if not headers:
        raise RuntimeError("Authenticated UART firmware requires a private UART key include directory")
    for path in headers:
        validate_uart_key_directory(path)
    if len({(path / "ProtogenUartKey.h").resolve() for path in headers}) != 1:
        raise RuntimeError("Ambiguous UART key headers in firmware include paths")
    return headers[0]
