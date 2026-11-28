"""Experimental native ESP-IDF backend; no PlatformIO executable or web server."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

from build_support import file_sha256, git_identity, source_identity, write_json
from esp_source_integrity import package_source_digest
from esp_tool_integrity import PIN_PATH, verify_esp_tools
import official_esp
from uart_key import validate_uart_key_directory, validate_uart_key_flags

ROOT = Path(__file__).resolve().parents[1]
LOCKS = ROOT / "scripts/requirements"
SUFFIX = "-linux-x86_64-py313.txt"
TOOLS = ("toolchain-riscv32-esp", "tool-cmake", "tool-ninja", "tool-esp-rom-elfs")
FRAMEWORK = "packages/framework-espidf"
ROLES = ("helmet", "paw")


def locked_installer():
    spec = importlib.util.spec_from_file_location(
        "locked_esp_installer", ROOT / "scripts/install-locked-esp.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_host() -> None:
    if (platform.system(), platform.machine(), sys.implementation.name, sys.version_info[:2]) != (
        "Linux",
        "x86_64",
        "cpython",
        (3, 13),
    ):
        raise RuntimeError("Native candidate is currently qualified for Linux x86-64 CPython 3.13 only")


def sdk_version_marker(core: Path, version: str, *, initialize: bool = False) -> None:
    """An extracted SDK must not inherit the enclosing project's Git version."""
    path = core / FRAMEWORK / "version.txt"
    expected = version.encode()
    if initialize and not path.exists() and not path.is_symlink():
        with path.open("xb") as stream:
            stream.write(expected)
    if path.is_symlink() or not path.is_file() or path.read_bytes() != expected:
        raise RuntimeError("Native ESP-IDF version marker differs from lock")


def verify_sdk(core: Path, profile: str = "reference") -> dict:
    if profile == "official":
        return official_esp.verify(core)
    lock = json.loads((ROOT / "toolchain.lock.json").read_text())
    expected = lock["esp32_source_integrity"][FRAMEWORK]["tree_sha256"]
    # Reuse the reference SDK only if its known generated PlatformIO files are
    # exact. Fresh native provisioning writes only the SDK version marker.
    generated = {
        "version.txt": lock["esp32"]["esp_idf"].encode(),
        "components/__pio_env/CMakeLists.txt": (
            b"# Warning! Do not delete this auto-generated file.\n"
            b"file(GLOB component_sources *.c* *.S)\n"
            b"idf_component_register(SRCS ${component_sources})\n"
        ),
        **{f"components/__pio_env/__dummy{ext}": b"" for ext in (".c", ".cpp", ".S")},
    }
    actual = package_source_digest(core / FRAMEWORK, generated)
    if actual != expected:
        raise RuntimeError("Native ESP-IDF source differs from pin")
    sdk_version_marker(core, lock["esp32"]["esp_idf"])
    pins = json.loads(PIN_PATH.read_text())
    tools = verify_esp_tools(core, {name: pins[name] for name in TOOLS})
    if tools["state"] != "verified_archive_trees" or set(tools["trees"]) != set(TOOLS):
        raise RuntimeError("Native tool package verification incomplete")
    return {"framework_tree_sha256": actual, "tools": tools["trees"]}


def native_environment(core: Path, python: Path) -> dict[str, str]:
    # Do not inherit caller-supplied CMake/compiler/Python injection flags.
    env = {
        key: value
        for key, value in os.environ.items()
        if key
        in {
            "HOME",
            "USER",
            "LOGNAME",
            "TMPDIR",
            "TEMP",
            "TMP",
            "LANG",
            "TZ",
            "PATH",
            "HTTP_PROXY",
            "HTTPS_PROXY",
            "ALL_PROXY",
            "NO_PROXY",
            "http_proxy",
            "https_proxy",
            "all_proxy",
            "no_proxy",
        }
        or key.startswith("LC_")
    }
    packages = core / "packages"
    paths = [
        python.parent,
        packages / "toolchain-riscv32-esp/bin",
        packages / "tool-cmake/bin",
        packages / "tool-ninja",
    ]
    env.update(
        PATH=os.pathsep.join([*(str(path) for path in paths), os.defpath]),
        IDF_PATH=str(core / FRAMEWORK),
        IDF_PYTHON_ENV_PATH=str(python.parent.parent),
        IDF_COMPONENT_MANAGER="0",
        ESP_ROM_ELF_DIR=str(packages / "tool-esp-rom-elfs"),
        PYTHONNOUSERSITE="1",
        PIP_CONFIG_FILE=os.devnull,
        PIP_NO_INDEX="1",
        PIP_REQUIRE_HASHES="1",
    )
    return env


def verify_python(python: Path, env: dict[str, str]) -> dict:
    report = json.loads((python.parent.parent.parent / "verified-install.json").read_text())
    locks = {group + SUFFIX: file_sha256(LOCKS / (group + SUFFIX)) for group in ("idf", "idf-build")}
    if report["state"] != "success" or any(report["locks"].get(k) != v for k, v in locks.items()):
        raise RuntimeError("Native Python installation does not match current locks")
    subprocess.run([str(python), "-m", "pip", "check"], env=env, check=True)
    installed = json.loads(
        subprocess.check_output(
            [
                str(python),
                "-c",
                "import importlib.metadata as m,json; print(json.dumps({d.metadata['Name']:d.version for d in m.distributions()}))",
            ],
            text=True,
            env=env,
        )
    )
    normalize = lambda value: re.sub(r"[-_.]+", "-", value).lower()
    actual = {normalize(k): v for k, v in installed.items()}
    expected = {}
    parse = locked_installer().parse_locked_requirement
    for filename in locks:
        for line in (LOCKS / filename).read_text().splitlines():
            if line.strip() and not line.lstrip().startswith("#"):
                name, version, _ = parse(line)
                expected[normalize(name)] = version
    if {k: v for k, v in actual.items() if k != "pip"} != expected:
        raise RuntimeError("Installed native Python distribution versions differ from locks")
    return {"locks": locks, "versions": actual}


def setup(args: argparse.Namespace) -> None:
    require_host()
    installer = locked_installer()
    core = args.sdk_root.absolute()
    output = args.python_root.absolute()
    if any(path.exists() or path.is_symlink() for path in (core, output)):
        raise RuntimeError(
            "Native setup requires fresh SDK and Python directories; existing installations are untouched"
        )
    # A completed setup record is published only after verified provisioning.
    # Failed directories are never silently reused or promoted.
    wheelhouse = (args.wheelhouse or ROOT / "build/cache/native-wheels").resolve()
    archives = (args.archives or ROOT / "build/cache/sha256").resolve()
    if args.wheelhouse is None:
        installer.download_python(wheelhouse, ("idf",))
    lock = json.loads((ROOT / "toolchain.lock.json").read_text())
    if args.sdk_profile == "official":
        official_esp.install(core, archives, installer, lock, args.archives is None)
    else:
        installer.install_packages(
            core,
            archives,
            lock,
            locations={FRAMEWORK, *("packages/" + name for name in TOOLS)},
            content_addressed=args.archives is None,
        )
        sdk_version_marker(core, lock["esp32"]["esp_idf"], initialize=True)
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/install-locked-python.py"),
            "--native-only",
            "--wheelhouse",
            str(wheelhouse),
            "--output",
            str(output),
        ],
        check=True,
    )
    python = output / "idf/bin/python"
    env = native_environment(core, python)
    write_json(
        output / "native-setup.json",
        {
            "state": "success",
            "sdk": verify_sdk(core, args.sdk_profile),
            "sdk_profile": args.sdk_profile,
            "python": verify_python(python, env),
            "sdk_root": str(core),
            "production_qualified": False,
        },
    )


def compare_configuration(reference: Path, native: Path) -> dict:
    previous = json.loads((reference / "config/sdkconfig.json").read_text())
    current = json.loads((native / "config/sdkconfig.json").read_text())
    differences = {
        key: [previous.get(key), current.get(key)]
        for key in sorted(previous.keys() | current.keys())
        if key not in previous or key not in current or previous[key] != current[key]
    }
    partition = (reference / "partitions.bin").read_bytes() == (
        native / "partition_table/partition-table.bin"
    ).read_bytes()
    return {
        "configuration_differences": differences,
        "partition_table_identical": partition,
        "equivalent_configuration": not differences and partition,
    }


def sbom_command(python: Path, output: Path, env: dict[str, str]) -> tuple[list[str], dict[str, str]]:
    """Generate an inventory without downloading or trusting cached CVE exclusions."""
    exclusions = output / "sbom-external-exclusions.json"
    exclusions.write_text("{}\n")
    return [
        str(python.parent / "esp-idf-sbom"),
        "--no-color",
        "create",
        "--format",
        "spdx-json-ld@3.0.1",
        "--rem-unused",
        "--no-guess",
        "--no-sync-excluded-cves",
        "--vex-format",
        "none",
        "-o",
        str(output / "sbom.spdx.json"),
        str(output / "project_description.json"),
    ], dict(env, SBOM_EXCLUDED_CVES_FILE=str(exclusions))


def build(args: argparse.Namespace) -> None:
    require_host()
    core = args.sdk_root.resolve()
    # Resolve the venv directory, not its Python symlink (which points at the system interpreter).
    python = args.python_root.resolve() / "idf/bin/python"
    env = native_environment(core, python)
    sdk = verify_sdk(core, args.sdk_profile)
    python_record = verify_python(python, env)
    source = source_identity(ROOT)
    git = git_identity(ROOT)
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    role = args.role
    project = ROOT / "firmware/esp32"
    cmake = core / "packages/tool-cmake/bin/cmake"
    ninja = core / "packages/tool-ninja/ninja"
    nm = core / "packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-nm"
    manifest = {
        "schema": 1,
        "state": "running",
        "backend": "native-esp-idf-candidate",
        "role": role,
        "sdk_profile": args.sdk_profile,
        "sdk": sdk,
        "python": python_record,
        "source": source,
        **git,
        "uart_auth": bool(args.uart_key_dir),
        "production_qualified": False,
    }
    target = output / "manifest.json"
    write_json(target, manifest)
    try:
        command = [
            str(cmake),
            "-S",
            str(project),
            "-B",
            str(output),
            "-G",
            "Ninja",
            "-DIDF_TARGET=esp32c3",
            f"-DPROTOGEN_NATIVE_ROLE={role}",
            f"-DSDKCONFIG={output / 'sdkconfig'}",
            f"-DPYTHON={python}",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            "-DPYTHON_DEPS_CHECKED=1",
        ]
        if args.uart_key_dir:
            if role != "helmet":
                raise RuntimeError("UART key is valid only for the helmet role")
            private = validate_uart_key_directory(args.uart_key_dir)
            validate_uart_key_flags([], [private, project / "src", project / "include"])
            command.append(f"-DPROTOGEN_NATIVE_UART_DIR={private}")
        defaults = []
        if args.sdk_profile == "official":
            # Preserve the existing PSK-only transport's certificate settings.
            defaults += [
                "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=n",
                "CONFIG_MBEDTLS_X509_TRUSTED_CERT_CALLBACK=n",
            ]
        if args.reproducible:
            if git["working_tree_dirty"] or not git["git_head"]:
                raise RuntimeError("Reproducibility requires a clean committed source")
            defaults.append("CONFIG_APP_REPRODUCIBLE_BUILD=y")
            env["SOURCE_DATE_EPOCH"] = subprocess.check_output(
                ["git", "show", "-s", "--format=%ct", git["git_head"]], cwd=ROOT, text=True
            ).strip()
        if defaults:
            extra = output / (
                "candidate.defaults" if args.sdk_profile == "official" else "reproducible.defaults"
            )
            extra.write_text("\n".join(defaults) + "\n")
            command.append(f"-DPROTOGEN_NATIVE_EXTRA_DEFAULTS={extra}")
        for label, invocation in (
            ("configure", command),
            ("compile", [str(cmake), "--build", str(output), "-j", str(args.jobs)]),
        ):
            with (output / (label + ".log")).open("w") as log:
                subprocess.run(invocation, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        description = json.loads((output / "project_description.json").read_text())
        version = json.loads((ROOT / "toolchain.lock.json").read_text())["esp32"]["esp_idf"]
        if args.sdk_profile == "official":
            version = official_esp.pins()["release"]
        if description.get("git_revision") != version:
            raise RuntimeError("Native SDK project description does not identify the pinned release")
        sdkconfig = json.loads((output / "config/sdkconfig.json").read_text())
        # Production is never inferred from a successful development build.
        required = {
            "FREERTOS_HZ": 1000,
            "ESPTOOLPY_FLASHSIZE": "4MB",
            "PARTITION_TABLE_SINGLE_APP_LARGE": True,
            "MBEDTLS_PSK_MODES": True,
            "MBEDTLS_KEY_EXCHANGE_PSK": True,
            "MBEDTLS_SSL_PROTO_DTLS": True,
            "MBEDTLS_SSL_PROTO_TLS1_2": True,
            "MBEDTLS_SSL_PROTO_TLS1_3": False,
            "MBEDTLS_SSL_COOKIE_C": True,
            "MBEDTLS_SSL_RENEGOTIATION": False,
            "MBEDTLS_CLIENT_SSL_SESSION_TICKETS": False,
            "MBEDTLS_SERVER_SSL_SESSION_TICKETS": False,
            "MBEDTLS_ASYMMETRIC_CONTENT_LEN": False,
            "MBEDTLS_SSL_MAX_CONTENT_LEN": 1024,
            "ESP_WIFI_SOFTAP_SUPPORT": False,
            "MBEDTLS_GCM_C": True,
            "MBEDTLS_ECDH_C": True,
            "MBEDTLS_ECP_DP_SECP256R1_ENABLED": True,
            "NVS_ENCRYPTION": False,
            "SECURE_BOOT": False,
            "SECURE_FLASH_ENC_ENABLED": False,
            "PROTOGEN_PRODUCTION": False,
            "ESP_CONSOLE_UART_DEFAULT" if role == "helmet" else "ESP_CONSOLE_USB_SERIAL_JTAG": True,
        }
        if args.sdk_profile == "official":
            required.update(
                MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=False, MBEDTLS_X509_TRUSTED_CERT_CALLBACK=False
            )
        if any(sdkconfig.get(key) != value for key, value in required.items()):
            raise RuntimeError("Resolved native SDK security or role configuration differs")
        symbols = subprocess.check_output(
            [str(nm), "--defined-only", "-C", str(output / "protogen_wireless.elf")], text=True, env=env
        )
        if " app_main" not in symbols or any("::uartMac(" in line for line in symbols.splitlines()) != bool(
            args.uart_key_dir
        ):
            raise RuntimeError("Native linked role or UART authentication differs")
        artifacts = (
            "protogen_wireless.bin",
            "protogen_wireless.elf",
            "bootloader/bootloader.bin",
            "partition_table/partition-table.bin",
            "config/sdkconfig.json",
        )
        manifest["artifacts"] = {name: file_sha256(output / name) for name in artifacts}
        manifest["binary_bytes"] = (output / "protogen_wireless.bin").stat().st_size
        size = output / "size.json"
        with size.open("w") as stream:
            subprocess.run(
                [
                    str(python),
                    "-m",
                    "esp_idf_size",
                    "--format",
                    "json2",
                    str(output / "protogen_wireless.map"),
                ],
                env=env,
                stdout=stream,
                check=True,
            )
        manifest["linker_memory"] = json.loads(size.read_text())
        manifest["artifacts"]["size.json"] = file_sha256(size)

        if args.reference:
            manifest["reference"] = compare_configuration(args.reference.resolve(), output)
        manifest["reproducible_requested"] = args.reproducible
        manifest["warnings"] = len(re.findall(r"warning:", (output / "compile.log").read_text(), re.I))
        if verify_sdk(core, args.sdk_profile) != sdk or verify_python(python, env) != python_record:
            raise RuntimeError("Native dependencies changed during build")
        if source_identity(ROOT) != source or git_identity(ROOT) != git:
            raise RuntimeError("Source changed during build")
        # Generate SPDX 3.0.1 through the pinned vendor SBOM tool.
        sbom = output / "sbom.spdx.json"
        command, sbom_env = sbom_command(python, output, env)
        with (output / "sbom.log").open("w") as log:
            subprocess.run(command, env=sbom_env, stdout=log, stderr=subprocess.STDOUT, check=True)
        json.loads(sbom.read_text())
        manifest["sbom_limitations"] = [
            line for line in (output / "sbom.log").read_text().splitlines() if line.startswith("WARNING:")
        ]
        manifest["configuration_warnings"] = len(
            re.findall(r"(?m)^CMake Warning", (output / "configure.log").read_text())
        )
        manifest["artifacts"]["sbom.spdx.json"] = file_sha256(sbom)
        manifest["artifacts"]["sbom-external-exclusions.json"] = file_sha256(
            output / "sbom-external-exclusions.json"
        )
        manifest["sbom_vulnerability_assessment"] = "not_performed"
        manifest["state"] = (
            "success"
            if not manifest["warnings"]
            and (not args.reference or manifest["reference"]["equivalent_configuration"])
            else "comparison_failed"
        )
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        manifest.update(state="failed", error=str(error))
        raise
    finally:
        write_json(target, manifest)
    if manifest["state"] != "success":
        raise RuntimeError("Native candidate did not meet comparison/warning gates; see manifest")
    print(json.dumps({"role": role, "binary_bytes": manifest["binary_bytes"], "state": manifest["state"]}))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, default=ROOT / "build/native-sdk")
    parser.add_argument("--python-root", type=Path, default=ROOT / "build/native-python")
    parser.add_argument("--sdk-profile", choices=("reference", "official"), default="reference")
    sub = parser.add_subparsers(dest="command", required=True)
    provision = sub.add_parser("setup")
    provision.add_argument("--wheelhouse", type=Path)
    provision.add_argument("--archives", type=Path)
    target = sub.add_parser("build")
    target.add_argument("role", choices=ROLES)
    target.add_argument("--output", type=Path, required=True)
    target.add_argument("--reference", type=Path)
    target.add_argument("--uart-key-dir", type=Path)
    target.add_argument("--reproducible", action="store_true")
    target.add_argument("--jobs", type=int, choices=range(1, 33), default=4)
    args = parser.parse_args()
    try:
        (setup if args.command == "setup" else build)(args)
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Native ESP candidate: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
