"""PlatformIO hook: explicitly opt into the per-helmet UART authentication key."""
import os
from pathlib import Path
# SCons puts this hook's directory on sys.path; it does not define __file__.
from uart_key import compiler_options, validate_uart_key_directory, validate_uart_key_flags
Import("env")
directory=os.environ.get("PROTOGEN_UART_KEY_DIR")
project=Path(env.subst("$PROJECT_DIR"))
flag_sets=[env.GetProjectOption(option, []) for option in ("build_flags", "build_src_flags")]
flag_sets += [env.get(option, []) for option in ("BUILD_FLAGS", "SRC_BUILD_FLAGS")]
flags=" ".join(value if isinstance(value, str) else " ".join(value or []) for value in flag_sets)
macros,includes=compiler_options(env.subst(flags), project)
for definition in env.get("CPPDEFINES", []):
    macros.add(str(definition[0] if isinstance(definition, (tuple, list)) else definition).partition("=")[0])
for include in env.get("CPPPATH", []):
    path=Path(env.subst(str(include)))
    includes.append((project/path if not path.is_absolute() else path).resolve())
if directory:
    private=validate_uart_key_directory(directory)
    includes.append(private)
if directory or "PROTOGEN_UART_AUTH" in macros:
    for variable in ("PROJECT_SRC_DIR", "PROJECT_INCLUDE_DIR"):
        path=env.subst("$"+variable)
        if "$" not in path:includes.append(Path(path))
    validate_uart_key_flags([], includes+[project/"src", project/"include"])
if directory:
    env.Append(CPPDEFINES=[("PROTOGEN_UART_AUTH",1)],CPPPATH=[str(private)])
