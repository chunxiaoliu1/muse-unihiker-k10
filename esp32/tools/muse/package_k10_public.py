"""Build an allowlisted, credential-audited K10 USB firmware release directory."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TOKEN = re.compile(rb"mgst_[A-Za-z0-9_-]{43}|sk_car_[A-Za-z0-9_-]{16,}")
FLASH_FILES = {"0x0": "bootloader/bootloader.bin", "0x10000": "partition_table/partition-table.bin",
               "0x1d000": "ota_data_initial.bin", "0x20000": "muse-gadget.bin"}


def package(build: Path):
    build = build.resolve()
    config = (build / "sdkconfig").read_text(encoding="utf-8")
    if not re.search(r"^CONFIG_GADGET_SDK_RUNTIME_ONLY=y$", config, re.M):
        raise ValueError("not a runtime-only public build")
    if not re.search(r"^CONFIG_MUSE_BOARD_DFROBOT_UNIHIKER_K10=y$", config, re.M):
        raise ValueError("not a UNIHIKER K10 build")
    for key in ("GADGET_SDK_TOKEN", "HOMEHUB_WIFI_SSID", "HOMEHUB_WIFI_PASSWORD", "MUSE_CARTESIA_API_KEY"):
        match = re.search(rf'^CONFIG_{key}="(.*)"$', config, re.M)
        if match and match.group(1):
            raise ValueError(f"non-empty private setting: {key}")
    args = json.loads((build / "flasher_args.json").read_text())
    if args["flash_files"] != FLASH_FILES:
        raise ValueError("unexpected K10 flash layout; refusing to publish")
    spec = importlib.util.spec_from_file_location('usb_config_bundle', ROOT / 'tools/muse/build_usb_config.py')
    bundler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundler)
    bundler.build_page()
    release = build / "release"
    release.mkdir(exist_ok=True)
    # Stage in a fresh subdirectory; never mix files from an earlier release.
    import tempfile
    with tempfile.TemporaryDirectory(prefix="k10-public-", dir=build) as temporary:
        stage = Path(temporary)
        (stage / "firmware").mkdir()
        manifest = {"board": "UNIHIKER K10", "chip": "esp32s3", "flash_size": "16MB", "files": []}
        for offset, relative in args["flash_files"].items():
            target = stage / "firmware" / Path(relative).name
            shutil.copyfile(build / relative, target)
            manifest["files"].append({"offset": offset, "file": f"firmware/{target.name}"})
        shutil.copytree(ROOT / "tools/muse/usb-config", stage / "usb-config")
        shutil.copyfile(ROOT / "tools/muse/K10_PUBLIC_RELEASE.md", stage / "README.md")
        shutil.copyfile(ROOT / "tools/muse/flash_k10_public.ps1", stage / "flash.ps1")
        (stage / "licenses").mkdir()
        shutil.copyfile(ROOT.parent / "LICENSE", stage / "licenses/Muse-SDK-LICENSE")
        # Preserve bundled component copyright/license notices with their names.
        sources = [ROOT / "components", ROOT / "managed_components"]
        if os.environ.get("IDF_PATH"):
            sources.append(Path(os.environ["IDF_PATH"]) / "components")
            license_path = Path(os.environ["IDF_PATH"]) / "LICENSE"
            if license_path.is_file(): shutil.copyfile(license_path, stage / "licenses/ESP-IDF-LICENSE")
        for source in sources:
            for path in source.rglob("*"):
                if path.is_file() and path.name.upper().startswith(("LICENSE", "COPYING", "NOTICE")):
                    relative = path.relative_to(source)
                    target = stage / "licenses" / source.parent.name / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, target)
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        checksums = []
        for path in sorted(stage.rglob("*")):
            if not path.is_file(): continue
            data = path.read_bytes()
            if TOKEN.search(data):
                raise ValueError(f"credential audit rejected {path.relative_to(stage)}")
            checksums.append(f"{hashlib.sha256(data).hexdigest()}  {path.relative_to(stage).as_posix()}")
        (stage / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
        archive = release / "unihiker-k10-muse-public.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for path in sorted(stage.rglob("*")):
                if path.is_file(): output.write(path, path.relative_to(stage))
        # Files remain available for inspection and local page hosting.
        for path in stage.iterdir():
            target = release / path.name
            if path.is_dir():
                # Known artifact directories only, under the resolved build/release.
                # Remove stale contents so public loose files match the audited ZIP.
                if target.exists():
                    if target.is_symlink() or not target.resolve().is_relative_to(release.resolve()):
                        raise ValueError("unsafe release artifact directory")
                    shutil.rmtree(target)
                shutil.copytree(path, target)
            else: shutil.copyfile(path, target)
    print(f"Public package and credential audit complete: {release}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path)
    package(parser.parse_args().build)
