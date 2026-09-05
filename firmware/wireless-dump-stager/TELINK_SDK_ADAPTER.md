# Telink SDK adapter pin — wireless dump stager

Status: **design/source pin only; no live OTA authorization**

Pinned upstream SDK:

```text
repo:   telink-semi/telink_zigbee_sdk
commit: 09fa2c3483b3aa2f0a6f2e2cc7e267cd6f1f9277
MCU:    TLSR8258 / B85 path
```

This file records the exact SDK integration points for the next implementation step. The production stager remains blocked behind the target-acceptance and transactional-rollback gates in `WIRELESS_EXTRACTION_TRANSFER.md`.

## 1. Flash read primitive

Pinned source:

```text
tl_zigbee_sdk/proj/drivers/drv_flash.c
```

The SDK wrapper is:

```c
void flash_read(u32 addr, u32 len, u8 *buf)
{
    flash_read_page(addr, len, buf);
}
```

The extraction adapter must expose **read only** to `glsd_stager_core_t`:

```c
static int telink_flash_read(void *user, uint32_t address, uint8_t *dst, uint32_t length)
{
    (void)user;
    flash_read_page(address, length, dst);
    return 0;
}
```

No `flash_write_page`, `flash_erase_sector`, `flash_erase`, boot-marker write, NV write, or factory write belongs in extraction build v1.

## 2. Raw cluster-specific receive path

Pinned source:

```text
tl_zigbee_sdk/stack/zigbee/zcl/zcl.h
```

The SDK defines:

```c
typedef status_t (*cluster_cmdHdlr_t)(zclIncoming_t *pInHdlrMsg);
```

and `zclIncoming_t` exposes the raw command payload through:

```c
pInMsg->pData
pInMsg->dataLen
pInMsg->hdr.cmd
pInMsg->addrInfo
```

This is the correct integration level for our existing pure dispatcher. Do not invent a second parser in the radio adapter. The handler should validate transport/addressing metadata, then call:

```c
glsd_stager_dispatch(..., pInMsg->hdr.cmd, pInMsg->pData, pInMsg->dataLen, ...)
```

## 3. Cluster registration

Pinned API:

```c
status_t zcl_registerCluster(
    u8 endpoint,
    u16 clusterId,
    u16 manuCode,
    u8 attrNum,
    const zclAttrInfo_t *pAttrTbl,
    cluster_cmdHdlr_t cmdHdlrFn,
    cluster_forAppCb_t cb
);
```

The private dump cluster remains:

```text
cluster ID: 0xFC00
endpoint:   11, unless the final stager descriptor deliberately allocates a separate endpoint
attributes: none required for extraction v1
```

Registration is not sufficient by itself: the final stager simple descriptor must also advertise the chosen input cluster consistently.

## 4. Response primitive

Pinned API:

```c
status_t zcl_sendCmd(
    u8 srcEp,
    epInfo_t *pDstEpInfo,
    u16 clusterId,
    u8 cmd,
    u8 specific,
    u8 direction,
    u8 disableDefaultRsp,
    u16 manuCode,
    u8 seqNo,
    u16 cmdPldLen,
    u8 *cmdPld
);
```

Telink OTA code provides the relevant response-address precedent: construct `epInfo_t` from the incoming source short address, source endpoint, and profile ID, and enable APS ACK for the unicast reply.

The adapter must send the bytes produced by `glsd_stager_dispatch()` unchanged. It must not reinterpret or rebuild INFO/DATA payloads.

## 5. Hard addressing gate before the real adapter is merged

Extraction v1 is intended to answer **unicast only**. Group/broadcast requests must not trigger flash-data responses.

Telink's ZCL layer has an `UNICAST_MSG(...)` helper at the APS-indication level, but the exact group/broadcast discriminator available inside the custom `cluster_cmdHdlr_t(zclIncoming_t *)` path has **not yet been pinned strongly enough** from source.

Therefore the real SDK adapter must remain unimplemented/disabled until one of these is proven from the pinned SDK:

1. `zclIncoming_t` / `zclIncomingAddrInfo_t` directly retains destination address mode/address sufficient to reject group/broadcast; or
2. the endpoint/AF receive hook can perform the unicast test before passing the frame into ZCL; or
3. a narrowly scoped wrapper around `zcl_rx_handler` can preserve that APS indication bit without changing unrelated ZCL behavior.

Do not guess this check and do not rely only on `session_id` as the addressing boundary.

## 6. Other adapter invariants

The SDK-facing adapter must preserve all existing extraction-core invariants:

```text
- 512 KiB flash only
- app banks 0x00000 / 0x40000 only
- executing stager bank marker must be 0x544C4E4B
- opposite old bank marker must be 0x544C4E00
- old Telink header must contain 5D 02
- declared size must be >=0x20 and <0x34000
- virtual +0x08 reconstruction must pass exact Telink xcrc32
- READ is relative to old app only
- READ data length is 1..48 bytes
- no reads from MAC/NV/factory/calibration regions
- no flash write/erase callback in extraction build
- no rollback commands in extraction build
```

## 7. Next offline implementation gate

Before adding Telink headers to the stager build:

1. pin the unicast/group distinction from the SDK source;
2. add a mock of that addressing metadata to host-native tests;
3. prove group/broadcast requests are rejected before `glsd_stager_dispatch()`;
4. prove the unicast response uses the incoming source endpoint/profile and APS ACK;
5. only then add the thin TLSR8258 adapter behind an explicit build flag.

None of the above requires touching `LivingRoomCircleLightDimmer`.
