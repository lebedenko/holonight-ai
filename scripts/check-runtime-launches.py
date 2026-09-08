#!/usr/bin/env python3
"""Observe real startup under four style selectors, with no desktop activation."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise AssertionError("Process did not terminate promptly")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--forbid-path", type=Path)
    parser.add_argument("--logs", type=Path, required=True)
    args = parser.parse_args()
    args.logs.mkdir(parents=True, exist_ok=True)
    executable, prefix = args.executable.resolve(), args.prefix.resolve()
    for mode in ("default", "environment", "command-line", "external-config"):
        with tempfile.TemporaryDirectory(prefix="uqc104-") as directory:
            root = Path(directory)
            env = {key: os.environ[key] for key in ("PATH", "LANG") if key in os.environ}
            env.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                       QT_QPA_PLATFORMTHEME="", QML_IMPORT_TRACE="1", QT_DEBUG_PLUGINS="1",
                       QT_FORCE_STDERR_LOGGING="1", QT_LOGGING_RULES="qt.qml.import.debug=true;qt.core.plugin.loader.debug=true",
                       LD_LIBRARY_PATH=str(prefix / "lib"), HOME=str(root))
            for key, name in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                              ("XDG_CACHE_HOME", "cache"), ("XDG_RUNTIME_DIR", "runtime"),
                              ("XDG_CONFIG_DIRS", "config-dirs"), ("XDG_DATA_DIRS", "data-dirs")):
                path = root / name
                path.mkdir(mode=0o700)
                env[key] = str(path)
            config = root / "config/holonight-ai/config.json"
            config.parent.mkdir()
            config.write_text(json.dumps({
                "provider_instances": {"schema_version": 1, "instances": [
                    {"id": name, "type": name, "name": name, "enabled": False, "settings": {}}
                    for name in ("ollama", "openai", "anthropic", "google")], "tombstones": []},
                "utility": {"chat_title_generation_enabled": False}}))
            bus_config = root / "bus.conf"
            bus_config.write_text('''<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>
<policy context="default"><allow send_destination="*"/><allow eavesdrop="true"/>
<allow own="*"/></policy></busconfig>''')
            bus = subprocess.Popen(["dbus-daemon", "--nofork", "--print-address=1",
                                    f"--config-file={bus_config}"], stdout=subprocess.PIPE, text=True, env=env)
            try:
                # A bounded read also catches daemon startup failure.
                import select
                assert select.select([bus.stdout], [], [], 5)[0], "Private bus startup timed out"
                address = bus.stdout.readline().strip()
                assert address.startswith("unix:"), "Private bus did not publish an address"
                env["DBUS_SESSION_BUS_ADDRESS"] = address
                command = [str(executable)]
                expected = "Holonight" if mode == "default" else "Fusion"
                if mode == "environment":
                    env["QT_QUICK_CONTROLS_STYLE"] = "Fusion"
                elif mode == "command-line":
                    env["QT_QUICK_CONTROLS_STYLE"] = "Holonight"
                    command += ["-style", "Fusion"]
                elif mode == "external-config":
                    external = root / "controls.conf"
                    external.write_text("[Controls]\nStyle=Fusion\n")
                    env["QT_QUICK_CONTROLS_CONF"] = str(external)
                log_path = args.logs / f"{mode}.log"
                with log_path.open("w") as log:
                    process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        deadline = time.monotonic() + 3
                        while time.monotonic() < deadline:
                            assert process.poll() is None, f"{mode}: premature exit {process.returncode}"
                            time.sleep(0.05)
                        maps = Path(f"/proc/{process.pid}/maps").read_text()
                        assert str(prefix / "lib/qt6/qml/Holonight/Core") in maps, f"{mode}: wrong Core plugin"
                        assert str(prefix / "lib/qt6/qml/Holonight/Controls") in maps, f"{mode}: wrong composite plugin"
                        if expected == "Holonight":
                            assert "libholonight_qml" in maps, f"{mode}: missing style plugin"
                        else:
                            assert "libqtquickcontrols2fusionstyleplugin" in maps, f"{mode}: missing Fusion plugin"
                        (args.logs / f"{mode}.maps").write_text(maps)
                    finally:
                        stop(process)
                evidence = log_path.read_text()
                assert re.search(rf"/{expected}/(?:Button|TextArea)\.qml", evidence), f"{mode}: no implementation evidence"
                diagnostics = re.findall(r"^.*(?:ReferenceError|TypeError|Binding loop|Cannot assign|Unable to assign|"
                                         r"is not a type|is not installed|Failed to create|Error loading|QML [A-Za-z]+:|Required property|"
                                         r"Skipped invalid|failed to load component).*$", evidence, re.M | re.I)
                assert not diagnostics, "\n".join(diagnostics)
                if args.forbid_path:
                    assert str(args.forbid_path.resolve()) not in evidence.replace(str(prefix), "<installed-prefix>"), f"{mode}: installed build-path discovery"
                    assert str(args.forbid_path.resolve()) not in maps.replace(str(prefix), "<installed-prefix>"), f"{mode}: installed build library loaded"
                assert (root / "data/holonight-ai/conversations.db").is_file(), "Missing disposable database"
                print(f"PASS {mode}: {expected} implementation and staged plugins; startup observed and reaped")
            finally:
                stop(bus)


if __name__ == "__main__":
    main()
