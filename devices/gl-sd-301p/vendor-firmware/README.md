# GL-SD-301P third-party firmware reference

This directory is the canonical **reference/evidence zone** for original GLEDOPTO GL-SD-301P firmware used for interoperability research.

## Layout

- `originals/` — byte-identical vendor firmware references.
- `reference-analysis/` — curated reverse-engineering evidence needed to reproduce interoperability findings.
- `MANIFEST.json` — provenance, hashes, versions and licence status for canonical originals.

Third-party firmware is **not covered by the repository's source-code licence** unless an individual manifest entry explicitly says otherwise.

Originals are retained because they are useful for:

- reproducible reverse engineering;
- comparing revisions/builds;
- validating protocol conclusions;
- recovering from a mistaken interpretation;
- maintaining a known stock reference/backup;
- checking future GLEDOPTO updates for interoperability changes.

Do not modify originals in place. Temporary base64/chunk transport under `.incoming/` is not canonical and should disappear once a hash-verified original exists in `originals/`.

The independent client firmware does not link or embed these images. Functional information needed by implementation is documented separately under `../interoperability/`.
