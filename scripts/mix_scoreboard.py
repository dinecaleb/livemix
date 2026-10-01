#!/usr/bin/env python3
"""How far TUNE MIX is from an engineer's own mix, on every reference session, against a committed baseline.

    scripts/mix_scoreboard.py                  # run every reference, fail if TUNE moved further from the hand mixes
    scripts/mix_scoreboard.py --update         # make this run the baseline (after a change that is meant to move it)
    scripts/mix_scoreboard.py --only praise-worship

A reference is a session an engineer finished by hand, listed in scripts/mix-scoreboard.txt with the windows to
listen to. For each window build/app/dine_mix_compare renders the session's own audio through the kept mix and
through a fresh TUNE MIX and prints a SCORE line (see app/Tools/MixCompare.cpp):

    channels  RMS distance of every channel against the lead, TUNE vs HAND (dB)
    groups    the same for the groups (dB)
    lead      the lead group against the rest of the mix, TUNE minus HAND (dB); below zero TUNE buries the lead
    tone      the master's tonal balance, loudness taken out (dB)

Every figure is a distance, so smaller is better and the verdict is per figure: one that is more than --tolerance
dB worse than the baseline on any window fails the run, and the mean over all windows is printed so a change that
helps one recording and hurts another shows as what it is. The audio lives on this Mac, not in the repo, so this is
a local check like the QUEENSVIEW windows, not a CI job; a reference whose session or audio is missing is skipped
and said, never counted as a pass.
"""
import argparse
import datetime
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
MANIFEST = HERE / "mix-scoreboard.txt"
BASELINE = HERE / "mix-scoreboard-baseline.txt"
TOOL = ROOT / "build" / "app" / "dine_mix_compare"
FIGURES = ("channels", "groups", "lead", "tone")
SCORE = re.compile(r"^SCORE window=(\S+) (.*)$")


def references():
    out = []
    for line in MANIFEST.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, session, windows, *rest = [p.strip() for p in line.split("|")]
        out.append({"name": name, "session": Path(os.path.expanduser(session)),
                    "windows": [w.strip() for w in windows.split(",")],
                    "flags": (rest[0].split() if rest else [])})
    return out


def run(ref, window, seconds):
    proc = subprocess.run([str(TOOL), str(ref["session"]), window, str(seconds), *ref["flags"]],
                          capture_output=True, text=True)
    missing = [l.strip() for l in proc.stdout.splitlines() if l.strip().startswith("missing:")]
    for line in proc.stdout.splitlines():
        m = SCORE.match(line)
        if m:
            figures = dict(kv.split("=") for kv in m.group(2).split())
            return {k: float(v) for k, v in figures.items()}, missing
    raise RuntimeError(f"{ref['name']} @ {window}: no SCORE line\n{proc.stdout[-2000:]}{proc.stderr[-2000:]}")


def distance(figure, value):
    return abs(value) if figure == "lead" else value


def read_baseline():
    rows = {}
    if BASELINE.exists():
        for line in BASELINE.read_text().splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            name, window, *figures = line.split()
            rows[(name, window)] = {k: float(v) for k, v in (f.split("=") for f in figures)}
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--update", action="store_true", help="write this run as the baseline")
    ap.add_argument("--tolerance", type=float, default=0.5, help="allowed worsening per figure in dB (default 0.5)")
    ap.add_argument("--seconds", type=float, default=40.0, help="length of each window (default 40)")
    ap.add_argument("--only", help="run one reference by name")
    args = ap.parse_args()

    if not TOOL.exists():
        sys.exit(f"{TOOL} is not built: cmake --build build --target dine_mix_compare --parallel 4")

    baseline = read_baseline()
    results, skipped, worse = {}, [], []
    print(f"{'reference':<18} {'window':>6}  " + "  ".join(f"{f:>15}" for f in FIGURES))
    for ref in references():
        if args.only and ref["name"] != args.only:
            continue
        if not ref["session"].exists():
            skipped.append(f"{ref['name']}: no session at {ref['session']}")
            continue
        for window in ref["windows"]:
            figures, missing = run(ref, window, args.seconds)
            if missing:
                skipped.append(f"{ref['name']} @ {window}: audio missing ({missing[0]} ...)")
                continue
            results[(ref["name"], window)] = figures
            was = baseline.get((ref["name"], window))
            cells = []
            for f in FIGURES:
                cell = f"{figures[f]:6.2f}"
                if was and f in was:
                    delta = distance(f, figures[f]) - distance(f, was[f])
                    cell += f" ({delta:+5.2f})"
                    if delta > args.tolerance:
                        worse.append(f"{ref['name']} @ {window}: {f} {was[f]:.2f} -> {figures[f]:.2f}")
                else:
                    cell += "  (new)"
                cells.append(f"{cell:>15}")
            print(f"{ref['name']:<18} {window:>6}  " + "  ".join(cells))

    if results:
        means = {f: sum(distance(f, r[f]) for r in results.values()) / len(results) for f in FIGURES}
        print(f"{'mean distance':<25}  " + "  ".join(f"{means[f]:>15.2f}" for f in FIGURES))
    for s in skipped:
        print(f"SKIPPED  {s}")

    if args.update:
        if skipped:
            sys.exit("not updating the baseline: a reference was skipped")
        head = git_head()
        lines = [f"# scripts/mix_scoreboard.py --update, {datetime.date.today()}, at {head}",
                 "# reference window figures (dB; distances from the engineer's own mix, smaller is closer)"]
        lines += [f"{n} {w} " + " ".join(f"{f}={r[f]:.2f}" for f in FIGURES) for (n, w), r in results.items()]
        BASELINE.write_text("\n".join(lines) + "\n")
        print(f"baseline written: {BASELINE.relative_to(ROOT)}")
        return 0
    if worse:
        print("\nFURTHER FROM THE HAND MIX than the baseline:")
        for w in worse:
            print(f"  {w}")
        return 1
    return 0


def git_head():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return "?"


if __name__ == "__main__":
    sys.exit(main())
