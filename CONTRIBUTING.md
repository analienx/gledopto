# Contributing device research

Thanks for helping investigate additional GLEDOPTO/Telink devices.

This project is intentionally **evidence-first**. A device should not be treated as compatible merely because it has the same brand, enclosure, Zigbee clusters, or MCU family as another supported target.

## Best kind of contribution

If you want a device investigated as a possible always-awake Zigbee End Device/leaf target, open an issue and include as much of the following as you can obtain safely.

### 1. Exact device identity

```text
model:
manufacturer:
hardware revision / hwVersion:
softwareBuildID / swBuildId:
dateCode:
applicationVersion:
stackVersion:
```

Photos of product labels and PCB markings are useful when you already have safe physical access to the device. Do not open mains-powered equipment merely to satisfy an issue template.

### 2. Zigbee identity and topology

Please include, where available:

```text
current Zigbee role: Router / End Device / unknown
endpoint(s):
input clusters:
output clusters:
manufacturerCode:
imageType:
fileVersion:
```

A Zigbee2MQTT or ZHA device interview/descriptor dump is much more useful than screenshots of a dashboard.

### 3. Why you want the router role removed

Examples:

- children or other devices repeatedly attach to it and become unreliable;
- route failures correlate with this device;
- the device is frequently power-switched and therefore makes a poor router;
- you already have sufficient dedicated routers and want lighting devices to remain leaves;
- you are researching whether a family of devices can use a common End Device firmware architecture.

Please describe observations rather than assuming the device is at fault.

### 4. Normal behavior that must be preserved

List the functions you actually use, for example:

- On/Off
- brightness/level
- color temperature
- RGB/color
- physical switch/PUSH input
- local buttons
- direct binding
- groups/scenes
- reporting
- startup behavior
- OTA update/recovery

A port is not considered useful merely because it joins the network as an End Device. Product behavior has to remain correct too.

### 5. Logs and protocol evidence

Useful evidence includes:

- Zigbee2MQTT/ZHA interview output;
- relevant debug logs around normal supported operations;
- read-only Basic/OnOff/Level/Color attribute reads;
- OTA **check/query metadata** without starting an update;
- public firmware/documentation links;
- checksums and provenance for firmware you are legally permitted to possess/analyse.

Please sanitize unrelated network identifiers when appropriate.

## Firmware and copyright boundary

Do not casually attach copyrighted vendor firmware to a public issue. If a binary is legally shareable, document its source and provenance. Otherwise provide hashes, metadata, public links, or derived interoperability observations.

Independent implementation code must not be represented as vendor code, and vendor/reference material must not be represented as being relicensed by this project.

## Safety

Do not perform mains probing, open energized equipment, flash production devices, alter unknown attributes, or trigger OTA updates simply to collect information for an issue.

Read-only Zigbee descriptors, attributes, normal device operation, logs, and public documentation are preferred first.

## What happens next

A device can progress roughly through these stages:

```text
identity / evidence
        ↓
hardware + protocol classification
        ↓
independent interface specification
        ↓
host tests + target implementation
        ↓
reproducible quarantined build
        ↓
explicit hardware-canary gate
        ↓
validated support
```

Any stage may stop if the evidence is insufficient or the risk is not justified.

## Before opening an issue

Search existing issues for your exact model and revision first. If it is new, use the **Device research request** issue template and include raw text/logs wherever possible rather than only screenshots.
