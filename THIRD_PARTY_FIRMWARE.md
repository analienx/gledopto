# Third-party firmware policy

This repository may retain original firmware images from GLEDOPTO and other vendors as interoperability/reference material.

## Copyright and licensing

Third-party firmware remains copyrighted by its respective rightholder. Unless the vendor has expressly licensed a binary under compatible terms, **the repository's source-code licence does not apply to that binary**.

Retention of a firmware image for backup, study, testing or interoperability analysis is distinct from a claim that the project owns the firmware or may relicense it. Public redistribution can involve additional rights and conditions; provenance records must therefore preserve how each image was obtained and any vendor-provided terms.

## Required metadata

Every canonical original under `devices/*/vendor-firmware/originals/` must be represented in that device's `MANIFEST.json` with at least:

- vendor and device/model;
- exact stored filename and original/source filename;
- acquisition provenance;
- version/build/date information where available;
- SHA-256 (preferably SHA-512 too);
- whether the artifact is byte-identical to the obtained original;
- known source/licence/redistribution notes;
- `project_license_applies: false` unless a specific licence says otherwise.

## Integrity

Original references are immutable evidence. Do not edit them in place. A patched, converted or repackaged derivative receives a new filename, new hashes and explicit derivative metadata.

## Interoperability separation

Reverse-engineering findings needed by independently written firmware are promoted into `devices/<device>/interoperability/` as functional/interface facts and tests. Implementation code should depend on that documented interface rather than on proprietary implementation expression.

## Sensitive data

Never commit credentials, Zigbee network keys, private signing keys, user identifiers, private logs or other secrets with firmware evidence.

## EU/Czech context

The project is operated from the EU/Czech Republic. Directive 2009/24/EC Articles 5–6 and Czech Act No. 121/2000 Coll. §§65–66 contain specific rules concerning backup, observation/study/testing and decompilation for interoperability. This file records the project's engineering process and does not assert that those provisions automatically authorize every form of public redistribution.
