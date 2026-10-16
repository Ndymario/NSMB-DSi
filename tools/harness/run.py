#!/usr/bin/env python3
"""Run the headless harness with BIOS/NAND paths taken from melonDS's settings.

  tools/harness/run.py --console dsi                  boot to the title screen
  tools/harness/run.py --console dsi --scenario 1-1   play into World 1-1
  tools/harness/run.py --console both --scenario 1-1 --out build/harness
  tools/harness/run.py --check                         assert the module's invariants

Screenshots are written as PNG plus one contact sheet per console. The DSi
NAND is copied to a temporary file first, so your dump is never written to.
Paths can be overridden with --bios9 etc. (see --help), and the settings
file with $MELONDS_CONFIG.
"""

import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / ".deps/harness-build/harness"
# Where melonDS keeps its settings: $MELONDS_CONFIG, else the per-platform default.
MELONDS_TOML_CANDIDATES = [
    pathlib.Path.home() / "Library/Preferences/melonDS/melonDS.toml",       # macOS
    pathlib.Path.home() / ".config/melonDS/melonDS.toml",                   # Linux
    pathlib.Path(os.environ.get("APPDATA", "")) / "melonDS/melonDS.toml",   # Windows
]

# frame -> keys. Timings are for the US ROM with direct boot.
SCENARIOS = {
    "title": {"frames": 600, "press": [], "shots": [600]},
    # Into 1-1 and stand still: a steady, repeatable frame for CPU load figures.
    "bench": {
        "frames": 4300,
        "press": ["620:a", "760:a", "3200:right", "3450:a"],
        "shots": [4300],
    },
    # Title -> Mario Game -> new file -> intro -> world map -> 1-1, run right
    # into the first Goomba, lose a life and return to the map. Covers every
    # scene change of a first session, including a level being torn down.
    "1-1": {
        "frames": 4600,
        "press": ["620:a", "760:a", "3200:right", "3450:a", "3950+400:right"],
        "shots": [3300, 3600, 4000, 4100, 4200, 4300, 4450, 4600],
    },
}


def melonds_paths() -> dict:
    candidates = [pathlib.Path(os.environ["MELONDS_CONFIG"])] if "MELONDS_CONFIG" in os.environ else MELONDS_TOML_CANDIDATES
    toml = next((p for p in candidates if p.is_file()), None)
    if toml is None:
        return {}
    cfg = tomllib.loads(toml.read_text())
    ds, dsi = cfg.get("DS", {}), cfg.get("DSi", {})
    return {
        "bios9": ds.get("BIOS9Path"), "bios7": ds.get("BIOS7Path"), "firmware": ds.get("FirmwarePath"),
        "dsi_bios9": dsi.get("BIOS9Path"), "dsi_bios7": dsi.get("BIOS7Path"),
        "dsi_firmware": dsi.get("FirmwarePath"), "nand": dsi.get("NANDPath"),
    }


def ppm_to_png(ppm: pathlib.Path) -> pathlib.Path:
    png = ppm.with_suffix(".png")
    try:
        from PIL import Image
        Image.open(ppm).save(png)
    except ImportError:
        subprocess.run(["sips", "-s", "format", "png", str(ppm), "--out", str(png)], check=True, capture_output=True)
    ppm.unlink()
    return png


def contact_sheet(pngs: list, out: pathlib.Path) -> None:
    try:
        from PIL import Image
    except ImportError:
        return
    cols = min(4, len(pngs))
    rows = (len(pngs) + cols - 1) // cols
    sheet = Image.new("RGB", (256 * cols, 384 * rows))
    for i, p in enumerate(pngs):
        sheet.paste(Image.open(p), ((i % cols) * 256, (i // cols) * 384))
    sheet.save(out)


def run(console: str, args, paths: dict, out: pathlib.Path) -> dict:
    scenario = SCENARIOS[args.scenario]
    cmd = [str(HARNESS), "--console", console, "--rom", str(args.rom), "--frames", str(scenario["frames"]), "--report"]
    for p in scenario["press"]:
        cmd += ["--press", p]
    shots = []
    for f in scenario["shots"]:
        ppm = out / f"{console}_{f:05d}.ppm"
        shots.append(ppm)
        cmd += ["--shot", f"{f}:{ppm}"]

    flags = ["bios9", "bios7"] + (["firmware"] if console == "ds" else ["dsi_bios9", "dsi_bios7", "dsi_firmware"])
    for key in flags:
        if paths.get(key):
            cmd += ["--" + key.replace("_", "-"), paths[key]]

    with tempfile.TemporaryDirectory() as tmp:
        if console == "dsi":
            nand = pathlib.Path(tmp) / "nand.bin"
            shutil.copyfile(paths["nand"], nand)
            cmd += ["--nand", str(nand)]
        result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(f"harness failed for {console}")
    pngs = [ppm_to_png(s) for s in shots]
    contact_sheet(pngs, out / f"{console}_sheet.png")
    report = json.loads(result.stdout[result.stdout.index("{"):])
    (out / f"{console}_report.json").write_text(json.dumps(report, indent=2))
    return report


def check(console: str, report: dict) -> list:
    """What must hold for every build of the module, per console."""
    arm9, arm7 = report.get("arm9"), report["arm7"]
    if arm9 is None:
        return ["no ARM9 report (is the Report component enabled?)"]
    problems = []
    expect = lambda ok, what: problems.append(what) if not ok else None
    expect(arm7["magic_ok"], "ARM7 never filled in the shared block")
    expect("A2D" in arm9["ntr_header"], "ROM header missing at 0x027FFE00")
    if console == "ds":
        expect(not arm9["twl"] and arm7["flags"] == 0, "DS run detected DSi mode")
        expect(arm9["mpu"][1] == "0x0200002B", "DS run changed MPU region 1")
        expect(arm9["root_heap"]["heap"] < "0x02400000", "DS run moved the root heap")
        for heap in ["extended_heap", "low_heap", "nwram_heap"]:
            expect(arm9[heap]["heap"] == "0x00000000", f"DS run created {heap}")
    else:
        expect(arm9["twl"] and arm7["flags"] == 7, "DSi run not detected as DSi mode")
        expect(arm9["console"], "DSi run not detected as DSi hardware")
        expect(arm9["root_heap"]["heap"] == "0x02800000", "root heap not in upper DSi RAM")
        expect(arm9["root_heap"]["size"] > 8_000_000, "root heap smaller than expected")
        expect(arm9["extended_heap"]["size"] > 4_000_000, "extended heap missing")
        expect(arm9["low_heap"]["size"] > 2_000_000, "low heap missing")
        expect(arm9["nwram_heap"]["size"] > 780_000, "NWRAM heap missing")
        expect(arm9["fast_clock"], "ARM9 not at 134MHz")
    expect(arm9["frames"] > 0, "main loop never ran")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rom", type=pathlib.Path, default=ROOT / "build/nsmb-dsi.nds")
    parser.add_argument("--console", choices=["ds", "dsi", "both"], default="both")
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), default="title")
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "build/harness")
    parser.add_argument("--check", action="store_true", help="run 1-1 on both consoles and assert the invariants")
    for key in ["bios9", "bios7", "firmware", "dsi_bios9", "dsi_bios7", "dsi_firmware", "nand"]:
        parser.add_argument("--" + key.replace("_", "-"), dest=key)
    args = parser.parse_args()

    if not HARNESS.exists():
        raise SystemExit(f"{HARNESS} is missing; build it with tools/bootstrap.sh --nitro <dir> --harness")
    paths = melonds_paths()
    for key in paths:
        if getattr(args, key):
            paths[key] = getattr(args, key)
    args.out.mkdir(parents=True, exist_ok=True)

    if args.check:
        args.console, args.scenario = "both", "1-1"
    consoles = ["ds", "dsi"] if args.console == "both" else [args.console]
    failed = False
    for console in consoles:
        report = run(console, args, paths, args.out)
        arm9 = report.get("arm9") or {}
        root = arm9.get("root_heap", {})
        print(f"{console}: twl={arm9.get('twl')} fast_clock={arm9.get('fast_clock')} "
              f"root_heap={root.get('heap')} ({root.get('size', 0) / 2**20:.1f}MB, "
              f"min free {root.get('free_min', 0) / 2**20:.1f}MB) -> {args.out / (console + '_sheet.png')}")
        if args.check:
            for problem in check(console, report):
                print(f"  FAIL: {problem}")
                failed = True
    if args.check:
        print("FAILED" if failed else "all checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
