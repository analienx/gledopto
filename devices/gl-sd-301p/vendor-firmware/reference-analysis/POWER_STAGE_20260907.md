# GL-SD-301P V20851203 — power-stage interoperability analysis

Reference purpose: preserve enough reproducible reverse-engineering evidence to audit the functional interface promoted into `../../interoperability/INTERFACE.md`.

This note is not implementation source code. The independently written implementation consumes the functional interface specification, not this vendor control flow.

## Reference image

See `../MANIFEST.json`.

Relevant identity:

- model: GL-SD-301P
- vendor build label: 20851203
- OTA file version: `0x28013001`
- manufacturer/image type: `0x124F / 0x1416`
- SHA-256: `16595a38ab9783d3afc4eb58ab4ec32625249bd569c1fa6dbaf468bddc76dd72`
- TC32/B85/TLSR8258 lineage

## Public-source anchors

Analysis used Telink's public Zigbee SDK as a structural/type oracle. Important public facts include:

- `drv_uart_init(u32 baudRate, u8 *rxBuf, u16 rxBufLen, uart_irq_callback)`;
- `drv_uart_tx_start(u8 *data, u32 len)`;
- TLSR8258 UART TX supports PB1; RX supports PA0;
- public sample-light `zcl_onOffAttr_t` layout places `onOff` after onTime/offWaitTime/startUpOnOff;
- public `zcl_levelAttr_t` places `curLevel` after `remainingTime`;
- public `sampleLight_onOffUpdate()` updates On/Off state and calls `light_refresh(LIGHT_STA_ON_OFF)`;
- public `light_applyUpdate()` performs the Level Control transition arithmetic.

## Resolved vendor functions / behavior

Addresses are offsets within the analyzed TC32 application image and are retained only to make the finding auditable.

| Address | Functional identification | Confidence |
|---:|---|---|
| `0x135ec` | Telink-style UART driver initialization | HIGH |
| `0x136d0` | Telink-style `drv_uart_tx_start(data,len)` | HIGH |
| `0x15290` | vendor normal lamp-output refresh to secondary controller | CONFIRMED/HIGH |
| `0x15304` | Telink sample-light-style level-update arithmetic | HIGH |
| `0x173dc` | vendor `sampleLight_onOffUpdate` equivalent | CONFIRMED/HIGH |
| `0x174c8` | restore/re-apply current On/Off state | HIGH |
| `0x154b4` | separate timed auxiliary operation/pattern engine | HIGH |

### UART initialization

The application initializes a 9600-baud UART with a 64-byte RX buffer and a callback whose resolved code target is a no-op return stub. The public Telink driver signature identifies the fourth argument as the receive callback.

Result: the stock driver enables RX, but no application-level RX processing has been found in the core lamp-control path.

### Control buffer

The shared initialized control buffer resolves to:

```text
A5 5A 02 03 04 AA
```

Identified control senders mutate only byte 2 (family) and byte 3 (value) before transmitting exactly six bytes.

Functional wire template:

```text
A5 5A CC VV 04 AA
```

### Standard On/Off state mapping

A RAM object at `0x842370` matches the public Telink `zcl_onOffAttr_t` layout and initialized state. In particular `+5` is `onOff` and `+6` is `globalSceneControl`.

The function at `0x173dc` mirrors the public On/Off command handling:

- command ON sets `onOff=1`;
- command OFF sets `onOff=0` and clears onTime;
- the third path toggles current state;
- all paths converge on the normal output refresh at `0x15290`.

This proves stock Zigbee OFF does **not** require family `0x02`.

### Standard Level state mapping

A RAM object at `0x8423e0` matches public `zcl_levelAttr_t`; `+2` is `curLevel`. Its initialized current level is `0xFE` (254).

At `0x15290`:

1. if standard `onOff` is false, the outgoing family-`0x01` value is zero;
2. if `onOff` is true, outgoing value begins as `curLevel`;
3. it is compared with configuration byte index 35 (default observed value `0x02`);
4. absent a separately controlled bypass flag, values below that threshold are clamped to the threshold;
5. the routine transmits six bytes through `0x136d0`.

Therefore normal stock frames are:

```text
OFF: A5 5A 01 00 04 AA
ON:  A5 5A 01 LL 04 AA
```

with `LL` normally the current ZCL level after minimum-output handling.

### Level transitions

The vendor function at `0x15304` strongly matches Telink's public `light_applyUpdate()` arithmetic, including fixed-point current-level progression and positive-step rounding. Vendor ZCL level-processing paths subsequently call `0x15290`.

Result: an independent implementation can use normal/public ZCL Level Control state progression and emit each resulting current level through family `0x01`.

### Startup state synchronization

The application initialization path calls a helper at `0x15284`, which calls the current-state re-application path at `0x174c8`; that in turn invokes `0x173dc`, and therefore `0x15290`.

There is no special UART transaction adjacent to `drv_uart_init`. The stock boot path re-applies restored standard lamp state through the same normal family-`0x01` output mechanism.

### Family `0x02`

`0x154b4` sends family `0x02` and manages a timed state engine with recurring intervals such as 200, 400 and 500 ms. Its callers include configuration/private-control and status-like paths. Normal On/Off/Level processing does not require it.

Observed mode values: `0,1,2,3,4,15`.

Conclusion: classify as auxiliary timed operation/pattern/configuration behavior until individual meanings are established. It remains relevant for full physical/feedback parity but is not a blocker for normal Zigbee dimming.

### Special half-level sender

`0x15178` sends family `0x01` with `curLevel >> 1`; its only identified caller is in a separate local state machine rather than the ordinary ZCL Level Control refresh.

Do not apply this transform globally. Its exact purpose remains unresolved.

## Engineering decision

Offline/static analysis is now sufficient to implement and host-test the core power-stage interface without a logic analyzer:

- TX-oriented 9600 8N1 serial transport;
- family-`0x01` OFF/ON/Level frames;
- minimum-level policy boundary;
- startup state re-sync.

Physical UART capture remains useful for corroboration and for completing family `0x02`, physical PUSH and vendor-specific configuration behavior, but it is no longer a prerequisite for basic client dimming.
