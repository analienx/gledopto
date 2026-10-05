#!/usr/bin/env python3
"""Generate the R25 slow-loop mutant of src/glsd301p_identify.c.

The mutant preserves every end state of the O(1) production tick but
reintroduces input-dependent per-second catch-up work WITHOUT writing
tick_steps_max. A work oracle that only reads that counter passes on
the mutant (SURVIVED); an oracle observing actual work fails it
(KILLED). The mutant is written to an ephemeral hosted path only;
production sources are never modified.

Usage: glsd301p_make_tick_mutant.py --src <identify.c> --out <path>
"""

import argparse
import sys

ANCHOR = """    whole = age / 1000u;
    st->second_mark_ms += whole * 1000u;
    if (whole >= st->countdown) {
        st->countdown = 0u;
    } else {
        st->countdown -= (uint16_t)whole;
    }
"""

MUTANT = """    whole = age / 1000u;
    {
        /* R25 mutant: per-second catch-up loop; end states match the
         * O(1) path exactly, but work is input-dependent and the
         * diagnostic counter is never written. */
        uint32_t r25_mut = whole;
        static int r25_mut_marked = 0;
        if (!r25_mut_marked) {
            r25_mut_marked = 1;
            printf("R25_MUTANT_LOOP_ACTIVE\\n");
            fflush(stdout);
        }
        while ((r25_mut > 0u) && (st->countdown > 0u)) {
            st->second_mark_ms += 1000u;
            st->countdown--;
            r25_mut--;
        }
        if (r25_mut > 0u) {
            st->second_mark_ms += r25_mut * 1000u;
        }
    }
"""

STDIO_ANCHOR = '#include <stddef.h>\n'
STDIO_REPLACEMENT = '#include <stddef.h>\n#include <stdio.h>\n'


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    with open(args.src, "r", encoding="utf-8") as fh:
        text = fh.read()

    if text.count(ANCHOR) != 1:
        print("mutant anchor not unique/found", file=sys.stderr)
        return 1
    if text.count(STDIO_ANCHOR) != 1:
        print("stdio anchor not unique/found", file=sys.stderr)
        return 1

    text = text.replace(ANCHOR, MUTANT).replace(
        STDIO_ANCHOR, STDIO_REPLACEMENT
    )

    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("R25_MUTANT_WRITTEN %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
