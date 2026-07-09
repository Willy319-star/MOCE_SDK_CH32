#!/usr/bin/env python3

import argparse
from pathlib import Path
import shutil
import subprocess


def run(cmd):
    print("[CMD]", " ".join(str(x) for x in cmd))
    subprocess.check_call(cmd)


def resolve_tool(root, name):
    if name == "minichlink":
        bundled = root / "tools" / "ch32fun" / "minichlink" / "minichlink.exe"
        if bundled.exists():
            return str(bundled)

    found = shutil.which(name)
    if found:
        return found

    if name == "minichlink":
        raise FileNotFoundError(
            "minichlink not found. Expected bundled tool at "
            f"{root / 'tools' / 'ch32fun' / 'minichlink' / 'minichlink.exe'} "
            "or minichlink in PATH."
        )

    raise FileNotFoundError(f"{name} not found in PATH")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", default="ch32v203g6u6")
    parser.add_argument("--app", default="led_blink")
    parser.add_argument("--tool", choices=["minichlink", "wchisp", "openocd"], default="minichlink")
    parser.add_argument("--interface", default="")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build_dir = root / "build" / args.board / args.app
    elf_candidates = [
        build_dir / "project" / args.app / f"{args.app}.elf",
        build_dir / "examples" / args.app / f"{args.app}.elf",
        build_dir / f"{args.app}.elf",
    ]
    elf = next((candidate for candidate in elf_candidates if candidate.exists()), elf_candidates[0])
    bin_file = elf.with_suffix(".bin")

    if not elf.exists():
        raise FileNotFoundError(f"ELF not found: {elf}")

    if args.tool == "minichlink":
        if not bin_file.exists():
            raise FileNotFoundError(f"BIN not found: {bin_file}")
        run([resolve_tool(root, "minichlink"), "-w", str(bin_file), "flash", "-b"])
    elif args.tool == "wchisp":
        if not bin_file.exists():
            raise FileNotFoundError(f"BIN not found: {bin_file}")
        run([resolve_tool(root, "wchisp"), "flash", str(bin_file)])
    else:
        if not args.interface:
            raise ValueError("--interface is required for openocd")
        run([
            resolve_tool(root, "openocd"),
            "-f", args.interface,
            "-c", f"program {elf} verify reset exit",
        ])


if __name__ == "__main__":
    main()
