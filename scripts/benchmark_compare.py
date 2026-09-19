#!/usr/bin/env python3
"""Compares a run of build/tests/livemix_benchmark against the committed baseline.

    build/tests/livemix_benchmark | tee build/benchmark.txt
    scripts/benchmark_compare.py build/benchmark.txt                 # fail if > 15 % slower
    scripts/benchmark_compare.py build/benchmark.txt --tolerance 25
    scripts/benchmark_compare.py build/benchmark.txt --update        # make this run the baseline

The benchmark prints one row per (instances, block) for the ChannelProcessor chain and for the
FX chain; the figure compared is the per-instance cost of one block (us/block/inst). A single
row on a shared runner is noisy, so the verdict is the geometric mean of current/baseline over
every row: a real regression slows every row, a noisy one moves a few. Rows that are
individually over the tolerance are printed as a warning. The baseline is machine-specific -
regenerate it (--update) on the machine class that will run the comparison, and commit it.
"""
import argparse
import datetime
import math
import platform
import subprocess
import re
import sys
from pathlib import Path

DEFAULT_BASELINE = Path(__file__).resolve().parent / "benchmark-baseline.txt"
ROW = re.compile(r"^\s*(\d+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s*$")


def machine():
    try:
        return subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True,
                              check=True).stdout.strip() or platform.processor()
    except Exception:
        return platform.processor() or platform.machine()


def parse(text):
    """{(section, instances, block): us_per_block_per_instance}"""
    rows = {}
    section = "?"
    for line in text.splitlines():
        if line.startswith("#"):
            continue
        if "ChannelProcessor benchmark" in line:
            section = "channel"
        elif "FxChain benchmark" in line:
            section = "fx"
        m = ROW.match(line)
        if m:
            rows[(section, int(m.group(1)), int(m.group(2)))] = float(m.group(3))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("current", help="the benchmark's output (a file, or - for stdin)")
    ap.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    ap.add_argument("--tolerance", type=float, default=15.0, help="allowed slowdown in percent (default 15)")
    ap.add_argument("--update", action="store_true", help="write the current run as the baseline and exit")
    args = ap.parse_args()

    text = sys.stdin.read() if args.current == "-" else Path(args.current).read_text()
    current = parse(text)
    if not current:
        print("benchmark_compare: no benchmark rows found in the input", file=sys.stderr)
        return 2

    if args.update:
        header = ("# livemix_benchmark baseline. Regenerate with:\n"
                  "#   build/tests/livemix_benchmark > /tmp/bench.txt && scripts/benchmark_compare.py /tmp/bench.txt --update\n"
                  "# Machine-specific: capture it on the class of machine that runs the comparison (CI: macos-latest).\n"
                  f"# measured on: {machine()} on {datetime.date.today().isoformat()}\n")
        args.baseline.write_text(header + text)
        print(f"benchmark_compare: wrote {len(current)} rows to {args.baseline}")
        return 0

    if not args.baseline.exists():
        print(f"benchmark_compare: no baseline at {args.baseline} (run with --update to create one)", file=sys.stderr)
        return 2
    baseline = parse(args.baseline.read_text())

    ratios = []
    slow_rows = []
    print(f"{'section':8} {'inst':>5} {'block':>6} {'baseline':>10} {'current':>10} {'change':>8}")
    for key in sorted(baseline):
        if key not in current:
            print(f"benchmark_compare: row {key} is in the baseline but not in this run", file=sys.stderr)
            return 2
        b, c = baseline[key], current[key]
        if b <= 0.0 or c <= 0.0:
            continue
        r = c / b
        ratios.append(r)
        flag = "  <-- slower than the tolerance" if r > 1.0 + args.tolerance / 100.0 else ""
        if flag:
            slow_rows.append(key)
        print(f"{key[0]:8} {key[1]:>5} {key[2]:>6} {b:>10.2f} {c:>10.2f} {100.0 * (r - 1.0):>+7.1f}%{flag}")

    if not ratios:
        print("benchmark_compare: nothing to compare", file=sys.stderr)
        return 2
    geomean = math.exp(sum(math.log(r) for r in ratios) / len(ratios))
    print(f"\ngeometric mean of current/baseline over {len(ratios)} rows: {geomean:.3f} "
          f"({100.0 * (geomean - 1.0):+.1f} %, tolerance {args.tolerance:.0f} %)")
    if slow_rows:
        print(f"{len(slow_rows)} row(s) individually over the tolerance (see above)")
    if geomean > 1.0 + args.tolerance / 100.0:
        print("benchmark_compare: REGRESSION - the benchmark is slower than the baseline by more than the tolerance")
        return 1
    print("benchmark_compare: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
