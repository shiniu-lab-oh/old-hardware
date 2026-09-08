#!/usr/bin/env python3
"""Build PB Runtime for every supported release Profile."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


DEFAULT_PROFILES = ("LP-001", "LP-003")


def expected_config(defaults: Path) -> list[str]:
    return [
        line.strip()
        for line in defaults.read_text(encoding="utf-8").splitlines()
        if line.strip().startswith("CONFIG_")
    ]


def verify_config(sdkconfig: Path, defaults: Path) -> None:
    generated = set(sdkconfig.read_text(encoding="utf-8").splitlines())
    missing = [line for line in expected_config(defaults) if line not in generated]
    if missing:
        raise RuntimeError(
            f"{sdkconfig} does not contain Profile defaults: {', '.join(missing)}"
        )


def build_profile(
    idf_py: Path,
    app_dir: Path,
    profiles_dir: Path,
    profile: str,
    target: str,
) -> None:
    defaults = profiles_dir / profile / "pb-runtime.sdkconfig.defaults"
    if not defaults.is_file():
        raise FileNotFoundError(f"Profile defaults not found: {defaults}")

    profile_slug = profile.lower().replace("-", "")
    build_dir = app_dir / f"build-matrix-{profile_slug}"
    sdkconfig = build_dir / "sdkconfig"
    command = [
        sys.executable,
        str(idf_py),
        "-B",
        str(build_dir),
        "-D",
        f"SDKCONFIG={sdkconfig}",
        "-D",
        f"PB_RUNTIME_PROFILE_DEFAULTS={defaults}",
        "-D",
        f"IDF_TARGET={target}",
        "build",
    ]

    print(f"\n==> Building PB Runtime for {profile}", flush=True)
    subprocess.run(command, cwd=app_dir, check=True)
    verify_config(sdkconfig, defaults)

    binary = build_dir / "pb_runtime.bin"
    print(
        f"==> {profile} PASS: {binary} ({binary.stat().st_size} bytes)",
        flush=True,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "profiles",
        nargs="*",
        default=DEFAULT_PROFILES,
        help="Profile IDs to build (default: LP-001 LP-003)",
    )
    parser.add_argument("--target", default="esp32")
    args = parser.parse_args()

    idf_command = shutil.which("idf.py")
    if idf_command is None:
        parser.error("idf.py was not found; run this in an ESP-IDF terminal")

    repo_root = Path(__file__).resolve().parents[1]
    app_dir = repo_root / "apps" / "pb-runtime"
    profiles_dir = repo_root / "profiles"
    for profile in args.profiles:
        build_profile(
            Path(idf_command),
            app_dir,
            profiles_dir,
            profile.upper(),
            args.target,
        )


if __name__ == "__main__":
    main()
