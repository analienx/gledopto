# Telink SDK adapter pin — wireless dump stager

Status: **thin adapter implemented from pinned source contracts; target TC32 build still blocked; no live OTA authorization**.

Pinned upstream SDK:

```text
repo:   telink-semi/telink_zigbee_sdk
commit: 09fa2c3483b3aa2f0a6f2e2cc7e267cd6f1f9277
MCU:    TLSR8258 / B85 path
```

Implemented files:

```text
glsd_transport_adapter.h/.c      stack-independent fail-closed boundary
glsd_telink_sdk_adapter.h/.c     thin Telink binding behind GLSD_TELINK_SDK
```

The host/native build compiles a deliberate fail-closed Telink stub. Only a real
TC32 target build with `GLSD_TELINK_SDK` defined may compile the SDK-facing
handler.

## 1. Flash read primitive

Pinned source:

```text
tl_zigbee_sdk/proj/drivers/drv_flash.c
```

SDK wrapper:

```c
void flash_read(u32 addr, u32 len, u8 *buf)
{
    flash_read_page(addr, len, buf);
}
```

The extraction image must expose **read only** to `glsd_stager_core_t`:

```c
static int telink_flash_read(void *user, uint32_t address, uint8_t *dst, uint32_t length)
{
    (void)user;
    flash_read_page(address, length, dst);
    return 0;
}
```

No `flash_write_page`, `flash_erase_sector`, `flash_erase`, boot-marker write,
NV write or factory write belongs in extraction build v1.

## 2. Exact raw cluster receive path

Pinned source:

```text
tl_zigbee_sdk/stack/zigbee/zcl/zcl.h
```

The SDK defines:

```c
typedef status_t (*cluster_cmdHdlr_t)(zclIncoming_t *pInHdlrMsg);
```

and `zclIncoming_t` retains both raw payload and APS/ZCL metadata:

```text
msg              apsdeDataInd_t *
pData            raw command payload
dataLen           raw payload length
addrInfo.profileId
addrInfo.srcAddr
addrInfo.dstAddr
addrInfo.srcEp
addrInfo.dstEp
addrInfo.seqNum
addrInfo.dirCluster
addrInfo.apsSec
hdr.cmd
```

The adapter therefore does not invent another wire parser. It normalizes those
fields into `glsd_transport_request_t`, then the pure transport layer invokes the
existing `glsd_stager_dispatch()`.

## 3. Unicast/group/broadcast gate is now source-proven

The pinned SDK provides this exact helper in `zcl.h`:

```c
#define UNICAST_MSG(msg) \
    (((msg)->indInfo.dst_addr < NWK_BROADCAST_ROUTER_COORDINATOR) && \
     (((msg)->indInfo.dst_addr_mode) != APS_SHORT_GROUPADDR_NOEP))
```

Because `zclIncoming_t.msg` is the original `apsdeDataInd_t *`, the custom
cluster handler can call `UNICAST_MSG(incoming->msg)` directly.

`glsd_transport_handle()` then independently requires:

```text
is_unicast == true
destination_endpoint == 11
client_to_server == true
```

before calling the dispatcher. Group/broadcast traffic therefore cannot reach a
READ response path.

The native regression proves non-unicast, wrong-endpoint and wrong-direction
requests do not invoke the response callback.

## 4. Response route and APS security are pinned

`zclIncomingAddrInfo_t` supplies the exact requester short address, source
endpoint, profile ID, ZCL sequence and whether APS security was present.

Pinned SDK precedents construct `epInfo_t` as:

```text
dstAddrMode = APS_SHORT_DSTADDR_WITHEP
dstAddr.shortAddr = incoming source short address
dstEp = incoming source endpoint
profileId = incoming profile ID
txOptions includes APS_TX_OPT_ACK_TX
txOptions includes APS_TX_OPT_SECURITY_ENABLED when incoming apsSec is set
```

The implemented adapter follows that pattern and calls:

```c
zcl_sendCmd(
    11,
    &dst,
    0xFC00,
    response_command,
    TRUE,
    ZCL_FRAME_SERVER_CLIENT_DIR,
    TRUE,
    0,
    incoming_zcl_sequence,
    payload_length,
    payload
);
```

The payload is exactly what `glsd_stager_dispatch()` produced; the SDK adapter
does not rebuild INFO/DATA fields.

## 5. Cluster registration

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

The thin adapter registers:

```text
endpoint:     11
cluster ID:   0xFC00
manufacturer: 0
attributes:   none
```

Registration is only one part of the final target application. Its simple
descriptor must also advertise `0xFC00` as an input cluster. The final target
project must keep its total registered cluster set within the SDK's configured
`ZCL_CLUSTER_NUM_MAX` rather than inheriting an oversized sample profile.

## 6. Adapter behavior on errors

`glsd_telink_sdk_adapter.c` returns `ZCL_STA_CMD_HAS_RESP` only when the pure
transport layer successfully sent the explicit cluster response.

Dropped or invalid requests return normal success to ZCL but produce no private
payload. The companion host always sets `disableDefaultResponse=true`; this
avoids accidentally turning malformed READs into a data-bearing alternate path.

If target cluster registration fails, the adapter clears its context and returns
failure. It never falls back to a different endpoint or cluster.

## 7. Other extraction invariants retained

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
- unicast only, endpoint 11 only, client-to-server only
- APS ACK on replies; preserve incoming APS security where present
```

## 8. What remains before target compilation

The radio/addressing design is no longer the blocker. Remaining target-build
gates are:

1. obtain a reproducible TC32 compiler/toolchain with acceptable provenance;
2. obtain/link the required TLSR8258 low-level SDK support objects/headers;
3. establish the exact production-module silicon/flash/board assumptions needed
   for the target project;
4. choose a minimal endpoint/simple-descriptor/cluster set that includes the
   private dump cluster and the recovery/OTA path without guessing board logic;
5. compile with `GLSD_TELINK_SDK`, then inspect the map, symbols, flash addresses
   and forbidden write/erase references before generating any OTA container.

None of those steps authorizes loading the extension or serving an image to
`LivingRoomCircleLightDimmer`.
