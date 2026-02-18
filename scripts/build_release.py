#!/usr/bin/env python3
"""
==============================================================================
                         PORKCHOP RELEASE BUILD SCRIPT
==============================================================================

    --[ 0x00 - What This Does

        Builds release binaries for M5Stack Core2 distribution:

        1. firmware_vX.X.X.bin           - App-only, flash at 0x10000 via esptool-js
                                           (preserves NVS, for upgrades)

        2. porkchop_vX.X.X_launcher.bin  - App-only, named for M5Launcher SD card
                                           (drop on SD, load via M5Launcher)

        3. porkchop_vX.X.X_m5burner.bin  - Full merged image, flash at 0x0
                                           (bootloader+partitions+app, nukes NVS)

        4. porkchop_vX.X.X_m5burner.zip  - M5Burner catalog package
                                           (m5burner.json + address-named bins)

        Output lands in m5porkchop_builds/ directory.


    --[ 0x01 - Usage

        python scripts/build_release.py

        That's it. No args. Version pulled from platformio.ini.
        Go make coffee, come back to fresh binaries.


    --[ 0x02 - Requirements

        * PlatformIO CLI (pio) in PATH
        * That's literally it


    --[ 0x03 - M5Launcher Notes

        M5Launcher loads app-only .bin files from the SD card via OTA.
        Just drop porkchop_vX.X.X_launcher.bin onto the SD card's
        root or a folder M5Launcher scans. The binary starts with
        ESP32 magic byte 0xE9 - M5Launcher recognizes this and
        installs it into the active OTA partition.

        M5Launcher also handles merged binaries (auto-crops from
        0x10000), but the app-only bin is cleaner and smaller.


==[EOF]==
"""

import os
import sys
import subprocess
import shutil
import re
import json
import zipfile
from pathlib import Path

# -- Config --
PIO_ENV = "m5core2"
CHIP = "esp32"
FLASH_SIZE = "16MB"
FLASH_MODE = "dio"
BOOTLOADER_OFFSET = "0x1000"   # ESP32 = 0x1000 (ESP32-S3 = 0x0)
PARTITIONS_OFFSET = "0x8000"
APP_OFFSET = "0x10000"

# Phrack-style banner
BANNER = r"""
 ██▓███   ▒█████   ██▀███   ██ ▄█▀ ▄████▄   ██░ ██  ▒█████   ██▓███
▓██░  ██▒▒██▒  ██▒▓██ ▒ ██▒ ██▄█▒ ▒██▀ ▀█  ▓██░ ██▒▒██▒  ██▒▓██░  ██▒
▓██░ ██▓▒▒██░  ██▒▓██ ░▄█ ▒▓███▄░ ▒▓█    ▄ ▒██▀▀██░▒██░  ██▒▓██░ ██▓▒
▒██▄█▓▒ ▒▒██   ██░▒██▀▀█▄  ▓██ █▄ ▒▓▓▄ ▄██▒░▓█ ░██ ▒██   ██░▒██▄█▓▒ ▒
▒██▒ ░  ░░ ████▓▒░░██▓ ▒██▒▒██▒ █▄▒ ▓███▀ ░░▓█▒░██▓░ ████▓▒░▒██▒ ░  ░
▒▓▒░ ░  ░░ ▒░▒░▒░ ░ ▒▓ ░▒▓░▒ ▒▒ ▓▒░ ░▒ ▒  ░ ▒ ░░▒░▒░ ▒░▒░▒░ ▒▓▒░ ░  ░
              CORE2 RELEASE BUILD SCRIPT - OINK!
"""


def log(msg, prefix="[*]"):
    print(f"{prefix} {msg}")


def log_ok(msg):
    log(msg, "[+]")


def log_err(msg):
    log(msg, "[!]")


def log_info(msg):
    log(msg, "[>]")


def get_version():
    """Extract version from platformio.ini"""
    ini_path = Path(__file__).parent.parent / "platformio.ini"

    if not ini_path.exists():
        log_err("platformio.ini not found - are you in the right directory?")
        sys.exit(1)

    with open(ini_path, "r") as f:
        content = f.read()

    match = re.search(r'custom_version\s*=\s*([\w.\-]+)', content)
    if match:
        return match.group(1)

    log_err("Could not find custom_version in platformio.ini")
    sys.exit(1)


def run_cmd(cmd, description):
    """Run a command and handle errors"""
    log_info(f"{description}...")

    try:
        result = subprocess.run(
            cmd,
            shell=True,
            capture_output=True,
            text=True
        )

        if result.returncode != 0:
            log_err(f"Command failed: {cmd}")
            log_err(result.stderr)
            sys.exit(1)

        return result.stdout
    except Exception as e:
        log_err(f"Exception running command: {e}")
        sys.exit(1)


def create_m5burner_package(builds_dir, version, bootloader_src, partitions_src, firmware_src):
    """Create M5Burner-compatible ZIP package with manifest and address-named bins"""
    log_info("Creating M5Burner catalog package...")

    pkg_dir = builds_dir / f"porkchop_v{version}_m5burner_pkg"
    fw_dir = pkg_dir / "firmware"

    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)
    fw_dir.mkdir(parents=True)

    # Copy bins with M5Burner address-naming convention
    shutil.copy2(bootloader_src, fw_dir / f"bootloader_{BOOTLOADER_OFFSET}.bin")
    shutil.copy2(partitions_src, fw_dir / f"partitions_{PARTITIONS_OFFSET}.bin")
    shutil.copy2(firmware_src, fw_dir / f"porkchop_{APP_OFFSET}.bin")

    # Generate m5burner.json manifest
    manifest = {
        "name": "PORKCHOP",
        "version": version,
        "description": "WiFi pentesting & wardriving tool for M5Stack Core2",
        "keywords": "wifi,wardriving,pentesting,handshake,spectrum,deauth",
        "author": "0ct0",
        "repository": "https://github.com/0ct0-porkchop/porkchop",
        "framework": "arduino",
        "firmware_category": {
            "PORKCHOP": {
                "path": "firmware",
                "device": ["M5Core2"],
                "default_baud": 921600
            }
        }
    }

    manifest_path = pkg_dir / "m5burner.json"
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    # Create ZIP
    zip_path = builds_dir / f"porkchop_v{version}_m5burner.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for file in pkg_dir.rglob("*"):
            if file.is_file():
                arcname = file.relative_to(pkg_dir)
                zf.write(file, arcname)

    shutil.rmtree(pkg_dir)

    size_kb = zip_path.stat().st_size / 1024
    log_ok(f"Created: {zip_path.name} ({size_kb:.1f} KB)")

    return zip_path


def main():
    print(BANNER)

    project_root = Path(__file__).parent.parent
    os.chdir(project_root)

    log_info(f"Working directory: {project_root}")

    version = get_version()
    log_ok(f"Building version: {version}")

    # Create output directory
    builds_dir = project_root / "m5porkchop_builds"
    builds_dir.mkdir(exist_ok=True)
    log_ok(f"Output directory: {builds_dir}")

    # Build paths
    build_dir = project_root / ".pio" / "build" / PIO_ENV
    firmware_src = build_dir / "firmware.bin"
    bootloader_src = build_dir / "bootloader.bin"
    partitions_src = build_dir / "partitions.bin"

    # Output filenames
    firmware_dst = builds_dir / f"firmware_v{version}.bin"
    launcher_dst = builds_dir / f"porkchop_v{version}_launcher.bin"
    m5burner_dst = builds_dir / f"porkchop_v{version}_m5burner.bin"

    # ── Step 1: Build ──
    log("")
    log("=" * 60)
    log("STEP 1: Building firmware with PlatformIO")
    log("=" * 60)

    run_cmd(f"pio run -t clean -e {PIO_ENV}", "Cleaning build artifacts")
    run_cmd(f"pio run -e {PIO_ENV}", "Compiling")
    log_ok("Firmware compiled successfully")

    for src, name in [(firmware_src, "firmware.bin"),
                      (bootloader_src, "bootloader.bin"),
                      (partitions_src, "partitions.bin")]:
        if not src.exists():
            log_err(f"{name} not found at {src}")
            sys.exit(1)

    fw_size = firmware_src.stat().st_size
    log_ok(f"Firmware size: {fw_size / 1024:.1f} KB")

    # Sanity check: firmware must fit in OTA partition (3MB = 0x300000)
    ota_limit = 0x300000
    if fw_size > ota_limit:
        log_err(f"Firmware ({fw_size} bytes) exceeds OTA partition limit ({ota_limit} bytes)!")
        log_err("M5Launcher won't be able to install this. Reduce firmware size.")
        sys.exit(1)

    # ── Step 2: App-only bin (esptool-js upgrades) ──
    log("")
    log("=" * 60)
    log("STEP 2: App-only binary for esptool-js upgrades")
    log("=" * 60)

    shutil.copy2(firmware_src, firmware_dst)
    size_kb = firmware_dst.stat().st_size / 1024
    log_ok(f"Created: {firmware_dst.name} ({size_kb:.1f} KB)")

    # ── Step 3: M5Launcher bin (app-only, SD card) ──
    log("")
    log("=" * 60)
    log("STEP 3: M5Launcher binary for SD card loading")
    log("=" * 60)

    shutil.copy2(firmware_src, launcher_dst)
    size_kb = launcher_dst.stat().st_size / 1024
    log_ok(f"Created: {launcher_dst.name} ({size_kb:.1f} KB)")
    log_info("Drop this on SD card for M5Launcher manual bin load")

    # ── Step 4: Merged binary for M5Burner (full flash) ──
    log("")
    log("=" * 60)
    log("STEP 4: Merged binary for M5 Burner / direct flash")
    log("=" * 60)

    merge_cmd = (
        f'pio pkg exec -p tool-esptoolpy -- esptool.py '
        f'--chip {CHIP} merge_bin '
        f'-o "{m5burner_dst}" '
        f'--flash_mode {FLASH_MODE} --flash_size {FLASH_SIZE} '
        f'{BOOTLOADER_OFFSET} "{bootloader_src}" '
        f'{PARTITIONS_OFFSET} "{partitions_src}" '
        f'{APP_OFFSET} "{firmware_src}"'
    )

    run_cmd(merge_cmd, "Merging binaries")

    size_kb = m5burner_dst.stat().st_size / 1024
    log_ok(f"Created: {m5burner_dst.name} ({size_kb:.1f} KB)")

    # ── Step 5: M5Burner catalog package ──
    log("")
    log("=" * 60)
    log("STEP 5: M5Burner catalog package")
    log("=" * 60)

    zip_path = create_m5burner_package(
        builds_dir, version, bootloader_src, partitions_src, firmware_src
    )

    # ── Summary ──
    log("")
    log("=" * 60)
    log("BUILD COMPLETE - OINK!")
    log("=" * 60)
    log("")
    log_ok(f"Release binaries for v{version}:")
    log("")
    log(f"    {firmware_dst.name}")
    log(f"        -> Flash at {APP_OFFSET} via esptool-js (preserves NVS)")
    log("")
    log(f"    {launcher_dst.name}")
    log(f"        -> Drop on SD card for M5Launcher manual bin load")
    log("")
    log(f"    {m5burner_dst.name}")
    log(f"        -> Flash at 0x0 via M5 Burner (full install)")
    log("")
    log(f"    {zip_path.name}")
    log(f"        -> M5Burner catalog package (m5burner.json + bins)")
    log("")
    log_info(f"Files are in: {builds_dir}")
    log("")

    return 0


if __name__ == "__main__":
    sys.exit(main())
