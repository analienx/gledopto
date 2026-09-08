# Interoperability / clean implementation policy

This project develops an **independently implemented interoperability client** for GLEDOPTO hardware. The repository may also retain lawfully obtained third-party firmware and reverse-engineering evidence as reference material, but that material is kept in a clearly separated reference zone and is **not** implementation source code.

This is an engineering/process policy, not legal advice. Contributors remain responsible for applicable law, licence terms, contractual restrictions and authorization in their jurisdiction.

## Legal/engineering purpose

Reverse engineering is performed to determine the information necessary for an independently written implementation to interoperate with hardware that the project lawfully operates.

For EU/Czech work, the process is designed around the software-program rules reflected in Directive 2009/24/EC Articles 5 and 6 and Czech Act No. 121/2000 Coll. §§65–66: lawful users may in defined circumstances make necessary backups, observe/study/test functionality, and reproduce/translate code where indispensable to obtain otherwise-unavailable interoperability information. Those provisions have conditions and do not by themselves turn third-party firmware into open-source material or grant an unrestricted redistribution licence.

## Repository zones

### 1. Third-party reference zone

Canonical location:

`devices/<device>/vendor-firmware/`

This zone may contain:

- original vendor `.ota` / `.bin` images that were lawfully obtained;
- hashes, source/provenance records and vendor correspondence metadata;
- curated reverse-engineering notes, symbol maps, disassembly excerpts and derived evidence needed to reproduce interoperability findings;
- tools that analyse a supplied reference image.

Rules:

1. Third-party firmware remains copyrighted by its respective rightholder.
2. The repository/project licence does **not** relicense those files.
3. Every retained firmware image must have provenance + cryptographic hashes recorded in a manifest/metadata sidecar.
4. Do not modify an original reference image. Derived/patched images use a different path and explicit metadata.
5. Public redistribution is a separate question from lawful possession, backup, study or interoperability analysis; do not claim the interoperability exception is a blanket redistribution licence.
6. Credentials, Zigbee network keys, private keys and user-private data never belong in this zone.

Canonical originals belong under:

`devices/<device>/vendor-firmware/originals/`

Curated analysis belongs under:

`devices/<device>/vendor-firmware/reference-analysis/`

Temporary chunked/base64 transport under `.incoming/` is not canonical storage and should be removed once the hash-verified original has been reconstructed.

### 2. Interoperability specification zone

Canonical location:

`devices/<device>/interoperability/`

This is the narrow interface between reverse engineering and independent implementation. It contains functional/interface facts such as:

- electrical/digital interface type;
- pin assignments necessary for communication;
- baud rate, parity, stop bits and payload size;
- externally observable frame fields and behavioural meaning;
- timing, ranges and error behaviour required for compatibility;
- device identity and Zigbee behaviour needed for interoperability;
- black-box or independently derived test vectors.

Facts must be marked by confidence (`CONFIRMED`, `HIGH`, `INFERRED`, `UNKNOWN`). Unknown fields remain fail-closed.

### 3. Independent implementation zone

Implementation code lives outside `vendor-firmware/` and consumes the interoperability specification, public standards and public SDK documentation.

Implementation commits must not be produced by copying or mechanically translating vendor implementation expression. Compatible external behaviour is the objective; similarity of internal expression is not.

The same repository may contain both reference and implementation zones. The boundary is **provenance and dependency direction**, not destruction of useful evidence:

```text
third-party reference / RE evidence
              |
              v
interoperability facts + tests
              |
              v
independently written firmware
```

Implementation code must never include proprietary firmware blobs as linked source/object material unless a separate licence explicitly permits that use.

## GL-SD-301P canonical interface

`devices/gl-sd-301p/interoperability/INTERFACE.md`

Implementation may rely only on fields whose confidence is sufficient for the use being made of them. A guessed checksum, reserved byte, brightness mapping, safety timing or power-stage command must not become a flashable implementation merely to make a test pass.

## Hardware safety boundary

The installed production GL-SD-301P is not a development canary. Runtime electrical validation is performed only under the project's separate sacrificial-spare safety gate. Repository changes never authorize mains-side probing or flashing by themselves.

## Contribution declaration

A PR changing interoperability implementation should state:

- which public specification or documented interoperability fact it implements;
- how the implementation was independently authored;
- what tests validate external compatibility;
- which interface fields remain unknown;
- whether third-party reference material was consulted, and if so, which sanitized interface facts were carried into implementation.

A PR adding vendor firmware should state:

- acquisition/source provenance;
- original filename/version;
- SHA-256 (and preferably SHA-512);
- whether the file is unchanged from the supplied/downloaded original;
- known vendor licence/redistribution terms, if any;
- that the project licence does not apply to the third-party binary.
