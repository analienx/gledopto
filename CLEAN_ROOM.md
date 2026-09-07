# Clean-room interoperability policy

This repository contains an **independently implemented interoperability client** for GLEDOPTO hardware. It is not a repository for vendor firmware, decompiled vendor source, translated machine code, or reconstruction of the vendor implementation.

This document is an engineering/process rule, not legal advice. Contributors remain responsible for applicable law, licence terms, and authorization in their jurisdiction.

## Purpose limitation

Reverse engineering is used only to determine the information necessary for an independently written implementation to interoperate with hardware that the project lawfully operates.

The public implementation repository may contain only the minimum interface facts needed for that purpose, for example:

- electrical/digital interface type;
- pin assignment needed to communicate with the attached controller;
- baud rate, parity, stop bits and payload size;
- externally observable frame fields and their behavioural meaning;
- timing/range/error behaviour required for compatibility;
- device identity and Zigbee behaviour needed for interoperability;
- black-box test vectors expressed as inputs and externally observable outputs.

It must not contain vendor implementation expression merely because it was visible during analysis.

## Two-role boundary

### Analysis role

The analysis role may, in a private workspace and where lawfully permitted, observe, study, test and reverse engineer a device or firmware for the interoperability purpose above.

The analysis role may publish into this repository only a **sanitized interoperability specification**. That specification must:

1. state facts in interface/behavioural terms rather than translated vendor code;
2. distinguish `OBSERVED`, `INFERRED`, and `UNKNOWN` information;
3. include only details necessary to implement or validate interoperability;
4. avoid decompiled source, disassembly listings, copied control flow, vendor tables, bulk constants, or reconstructable firmware material;
5. retain provenance/confidence sufficient to audit the conclusion without retaining proprietary expression.

### Implementation role

Implementation work uses only:

- the sanitized interoperability specification in this repository;
- public standards and public manufacturer/SDK documentation that may lawfully be used;
- independently generated test cases and measurements from project-owned hardware.

Implementation commits must not be written by translating decompiled/disassembled vendor code. Similarity to a vendor implementation is not a design objective; only compatible external behaviour is.

## Prohibited repository content

Do not commit or attach:

- vendor `.ota`, `.bin`, `.hex`, `.elf` or flash images;
- base64/chunked/compressed/encoded forms of those images;
- full disassembly or decompiler output;
- source reconstructed from vendor machine code;
- raw flash dumps;
- private keys, Zigbee network keys, credentials or unsanitized logs;
- large vendor-derived lookup tables, constants or packet corpora that are not necessary interface facts.

GitHub Actions must not download, reconstruct, publish, or retain proprietary firmware as workflow artifacts for implementation work.

## Interoperability specification rule

The canonical GL-SD-301P interface contract is:

`devices/gl-sd-301p/interoperability/INTERFACE.md`

Implementation code may depend on a field only when that field is marked `CONFIRMED`. Unknown fields remain fail-closed. A guessed checksum, reserved byte, brightness mapping, safety timing or power-stage command must never be promoted into flashable firmware merely to make a test pass.

## Hardware safety boundary

The installed production GL-SD-301P is not a development canary. Runtime electrical validation is performed only on a sacrificial spare under the project's separate hardware-safety gate. Mains-side investigation requires appropriate isolation and instrumentation; repository changes never authorize physical mains work by themselves.

## Contribution declaration

A PR that changes the interoperability implementation should state:

- which public specification/standard or sanitized interface fact it implements;
- how it was independently implemented;
- what black-box tests validate it;
- whether any interface field remains unknown;
- that no vendor firmware, decompiled source or disassembly was used as implementation source material.

## Legal context

The process is designed around interoperability-focused limitations reflected, among other places, in Article 5(3) and Article 6 of Directive 2009/24/EC and the corresponding Czech software-program provisions in Act No. 121/2000 Coll. The repository does not claim that those provisions automatically authorize every act in every circumstance.
