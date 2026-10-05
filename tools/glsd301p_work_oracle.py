#!/usr/bin/env python3
"""R25 hosted work oracle: prove identify-tick work is gap-independent.

Builds tests/test_glsd301p_tick_work.c plus the shared production
src/glsd301p_identify.c / src/glsd301p_timebase.c with gcov coverage,
runs exactly one tick with a small gap and one with a large gap, and
compares the per-line execution counts of glsd301p_identify.c. The O(1)
tick must show IDENTICAL counts (same block sequence, no catch-up
loop); the slow-loop mutant shows differing counts (KILLED). End
states are asserted from the driver output. Deterministic and
bounded: two single-tick runs, no wall-clock thresholds.

Usage:
  glsd301p_work_oracle.py --root <repo> --work <dir> --identify <identify.c>
      --small-ms 2000 --large-ms 200000 --expect identical|differ
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

COUNT_RE = re.compile(r"^\s*([0-9]+|#####):\s*([0-9]+):")


def run(cmd, cwd):
    proc = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    return proc.returncode, proc.stdout


def parse_gcov(path):
    counts = {}
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = COUNT_RE.match(line)
            if not m:
                continue
            raw, lineno = m.group(1), int(m.group(2))
            counts[lineno] = 0 if raw == "#####" else int(raw)
    return counts


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--identify", required=True,
                    help="identify.c TU under test (production or mutant)")
    ap.add_argument("--small-ms", type=int, default=2000)
    ap.add_argument("--large-ms", type=int, default=200000)
    ap.add_argument("--expect", required=True,
                    choices=["identical", "differ"])
    args = ap.parse_args()

    work = args.work
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)

    # Absolute -o paths keep every .gcno/.gcda inside the work dir.
    units = [(args.identify, "identify.o"),
             (os.path.join(args.root, "src/glsd301p_timebase.c"),
              "timebase.o"),
             (os.path.join(args.root, "tests/test_glsd301p_tick_work.c"),
              "tick_work.o")]
    for src, obj in units:
        rc, out = run(["cc", "-std=gnu11",
                       "-I" + os.path.join(args.root, "src"),
                       "-Wall", "-Wextra", "-Werror", "--coverage", "-O0",
                       "-c", src, "-o", os.path.join(work, obj)], args.root)
        if rc != 0:
            print(out)
            print("R25_ORACLE=BUILD_FAIL %s" % src, file=sys.stderr)
            return 1
    rc, out = run(["cc", "--coverage", "-O0", "identify.o", "timebase.o",
                   "tick_work.o", "-o", "tick_work"], work)
    if rc != 0:
        print(out)
        print("R25_ORACLE=LINK_FAIL", file=sys.stderr)
        return 1

    results = {}
    for tag, gap in [("small", args.small_ms), ("large", args.large_ms)]:
        for name in os.listdir(work):
            if name.endswith(".gcda"):
                os.remove(os.path.join(work, name))
        rc, out = run(["./tick_work", str(gap)], work)
        m = re.search(r"R25_TICK gap=(\d+) store=(\d+)", out)
        if rc != 0 or not m:
            print(out)
            print("R25_ORACLE=RUN_FAIL %s" % tag, file=sys.stderr)
            return 1
        expect_store = 0xFFFF - (gap // 1000)
        if int(m.group(2)) != expect_store:
            print(out)
            print("R25_ORACLE=STORE_MISMATCH %s" % tag, file=sys.stderr)
            return 1
        rc, out = run(["gcov", "-m", "-o", work, args.identify], work)
        if rc != 0:
            print(out)
            print("R25_ORACLE=GCOV_FAIL %s" % tag, file=sys.stderr)
            return 1
        gcov_files = [n for n in os.listdir(work)
                      if n.startswith("identify.c") and n.endswith(".gcov")]
        if len(gcov_files) != 1:
            print("R25_ORACLE=GCOV_AMBIGUOUS %s" % gcov_files,
                  file=sys.stderr)
            return 1
        results[tag] = parse_gcov(os.path.join(work, gcov_files[0]))
        os.remove(os.path.join(work, gcov_files[0]))

    small, large = results["small"], results["large"]
    if set(small) != set(large):
        print("R25_ORACLE=LINESET_DIFFERS", file=sys.stderr)
        identical = False
    else:
        diffs = [(n, small[n], large[n]) for n in sorted(small)
                 if small[n] != large[n]]
        identical = not diffs
        for n, a, b in diffs[:10]:
            print("R25_COUNT_DIFF line=%d small=%d large=%d" % (n, a, b))

    print("R25_COUNTS small_lines=%d large_lines=%d identical=%s"
          % (len(small), len(large), identical))
    if args.expect == "identical":
        if not identical:
            print("R25_WORK_ORACLE=FAIL", file=sys.stderr)
            return 1
        print("R25_WORK_ORACLE=PASS")
        return 0
    if identical:
        print("R25_MUTANT=SURVIVED", file=sys.stderr)
        return 1
    print("R25_MUTANT=KILLED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
