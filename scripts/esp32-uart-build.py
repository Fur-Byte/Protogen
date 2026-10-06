"""PlatformIO hook: explicitly opt into the per-helmet UART authentication key."""
import os
from pathlib import Path
Import("env")
directory=os.environ.get("PROTOGEN_UART_KEY_DIR")
if directory:
    private=Path(directory).resolve()
    if not (private/"ProtogenUartKey.h").is_file():
        raise RuntimeError("Missing per-helmet UART key header")
    env.Append(CPPDEFINES=[("PROTOGEN_UART_AUTH",1)],CPPPATH=[str(private)])
