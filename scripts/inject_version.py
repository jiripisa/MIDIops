"""PlatformIO pre-build hook: embed the current git short SHA and the build
timestamp into the firmware.

Defines ``MIDIOPS_VERSION`` (commit hash, falls back to ``"dev"``) and
``MIDIOPS_BUILD`` (ISO ``YYYY-MM-DD HH:MM``) as C string literals.
"""
import datetime
import subprocess

Import("env")  # type: ignore[name-defined]  # provided by PlatformIO

try:
    sha = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"],
        stderr=subprocess.DEVNULL,
    ).decode().strip()
except Exception:
    sha = "dev"

build_ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")

env.Append(CPPDEFINES=[
    ("MIDIOPS_VERSION", env.StringifyMacro(sha)),
    ("MIDIOPS_BUILD",   env.StringifyMacro(build_ts)),
])
print(f">>> MIDIops build {sha} @ {build_ts}")
