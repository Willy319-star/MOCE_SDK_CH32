#!/usr/bin/env python3

import argparse
import os
import subprocess
from pathlib import Path


def run(cmd):
    print("[CMD]", " ".join(str(x) for x in cmd))
    subprocess.check_call(cmd)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", default="ch32v203g6u6")
    parser.add_argument("--app", default="led_blink")
    parser.add_argument("--generator", default="Ninja")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build_dir = root / "build" / args.board / args.app
    tmp_dir = root / "build" / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("TMPDIR", str(tmp_dir))

    run([
        "cmake",
        "-S", root,
        "-B", build_dir,
        "-G", args.generator,
        f"-DCMAKE_TOOLCHAIN_FILE={root / 'toolchain' / 'riscv-none-elf-gcc.cmake'}",
        f"-DBOARD={args.board}",
        f"-DAPP={args.app}",
    ])

    run(["cmake", "--build", build_dir])


if __name__ == "__main__":
    main()
