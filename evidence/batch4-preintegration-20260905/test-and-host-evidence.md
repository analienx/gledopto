# Batch 4 raw evidence — 2026-09-05 (head fa8869dde2cd06ab3f2b3e9314f795c99a86e442)

## Windows Python 3.14.4 — tools/tests/test_wireless_dump.py -v (exit 0)

Ran 12 tests in 0.276s — OK (skipped=2: 'gcc not available' x2)

## Windows Python 3.14.4 — tools/tests/test_wireless_dump_guard.py -v (exit 0)

Ran 11 tests in 0.713s — OK

## WSL2 Ubuntu 26.04, gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, python3 3.14.4

tools/tests/test_wireless_dump.py -v: Ran 12 tests — OK (NO SKIPS; native
stager-core and dispatcher/cross-language fixtures compiled and passed)
tools/tests/test_wireless_dump_guard.py -v: Ran 11 tests — OK

## ASan + UBSan (-fsanitize=address,undefined -fno-omit-frame-pointer -g, gcc 15.2.0)

stager_core_test: PASS
stager_dispatch_test: PASS
INFO=01050926204433221156341200000008000000000000000400004e4c544b4e4c540000040000000000f00000000f358d1b0100000000f0000000ff00
DATA=44332211640000000000000030a5a4013001265d02004e4c54a9a8abaab5b44f121614b3b2f0000000b9b8bbba85848786818083828d8c8f8e89888b8ab518c5c800
ASAN_UBSAN_ALL_PASS (no sanitizer diagnostics on any binary)

## GitHub Actions runs for head fa8869dd (workflow 'Wireless dump offline regression')

push         run 33973002693 -> SUCCESS (jobs: Python 3.14 + native GCC; Python 3.11 + native GCC)
pull_request run 33973005618 -> SUCCESS (same matrix)

## HA host inventory (read-only SSH)

HAOS aarch64 (a0d7b954-ssh, kernel 6.18.34-haos-raspi)
Z2M addon: app_45df7312_zigbee2mqtt, image ghcr.io/zigbee2mqtt/zigbee2mqtt-aarch64:2.14.0-1
node v24.18.1; zigbee2mqtt 2.14.0; zigbee-herdsman 10.9.1 (pnpm:
/app/node_modules/.pnpm/zigbee-herdsman@10.9.1/...); zigbee-herdsman-converters 26.103.0
serial: /dev/serial/by-id/usb-SONOFF_SONOFF_Dongle_Max_MG24_...-port0, adapter: ember, 115200
coordinator: EmberZNet 9.1.1 [GA] ({"meta":{"build":0,"ezsp":19,"major":9,"minor":1,"patch":1},"type":"EmberZNet"})
MQTT: EMQX addon (app_a0d7b954_emqx); Z2M server mqtt://homeassistant:1883 (credentials redacted)
database.db: 184463 bytes, sha256 384829665f1f3d4e9b6637ef1b70e30fed21bbaab608362e077f849406337830
configuration.yaml: sha256 daf2b0e9b7f24ddc0fe1a7108cc10e8caf6370fe12ba8244aeac2badb6532482
existing backups/snapshots: /config/zigbee2mqtt/backup-production-20260904, bseed_ota, bseed_probe

## Key installed-source excerpts

zigbee-herdsman dist/controller/model/endpoint.d.ts:136:
  commandResponse<Cl extends number|string, Co extends number|string,
    Custom extends TCustomCluster|undefined = undefined>(
    clusterKey: Cl, commandKey: Co, payload: ClusterOrRawPayload<Cl,Co,Custom>,
    options?: Options, transactionSequenceNumber?: number): Promise<void>;

endpoint.js:648: async command(clusterKey, commandKey, payload, options) {
  const frame = await this.zclCommand(clusterKey, commandKey, payload, options, undefined, false, Zcl.FrameType.SPECIFIC);
  if (frame) return frame.payload; }
endpoint.js:654-661: async commandResponse(...) {
  ... const cluster = this.getCluster(clusterKey, device, options?.manufacturerCode);
  const command = Zcl.Utils.getClusterCommandResponse(cluster, commandKey);
  transactionSequenceNumber = transactionSequenceNumber ?? zclTransactionSequenceNumber_1.default.next();
  ... Zcl.Frame.create(..., device.customClusters, optionsWithDefaults.reservedBits); }
endpoint.d.ts:27: disableDefaultResponse?: boolean;
controller/helpers/ota.js:372: this.dataSettings.requestTimeout = 150000; // ensures never zero

z2m dist/extension/otaUpdate.js:73: topicRegex .../bridge/request/device/ota_update/(update|check|schedule|unschedule)/?(downgrade|abort)?
otaUpdate.js:80: setOtaConfiguration(data.getPath(), settings.get().ota.zigbee_ota_override_index_location);
otaUpdate.js:111: if (data.device.zh.scheduledOta?.url !== undefined || data.device.definition.ota)
otaUpdate.js:119-120: image_block_request_timeout ?? 150000; image_block_response_delay ?? 250;
mqtt.js:150-152: // Republish retained messages in case MQTT broker does not persist them.

herdsman controller.js:605-606: device.emit(event, ...args); this.emit(event, ...args);

## Pinned Telink SDK (telink-semi/telink_zigbee_sdk @ 09fa2c3483b3aa2f0a6f2e2cc7e267cd6f1f9277)

ota.h:30  #define OTA_IMAGE_MAX_DATA_SIZE 48
ota.h:42  #define OTA_MAX_IMAGE_BLOCK_RSP_WAIT_TIME 5
ota.h:48  #define OTA_MAX_IMAGE_BLOCK_RETRIES 10
ota.c:35  #define TL_IMAGE_START_FLAG 0x4b
ota.c:36  #define TL_START_UP_FLAG_WHOLE 0x544c4e4b
ota.c:139-159 mcuBootAddrGet(): flag@0x0+8 == 0x544C4E4B -> boot 0x0; else flag@0x40000+8 -> boot 0x40000; else invalid
ota.c:161-204 ota_newImageValid(): size<=FLASH_OTA_IMAGE_MAX_SIZE; (flag32 & 0xffffff00)==0x544c4e00;
          xcrc32 over fw_size-4 with byte+8 forced to 0x4B; compare tail
ota.c:206-254 ota_mcuReboot(): validate new bank; if invalid return (nothing written);
          else writeWithCheck(new+8,0x4B); flash_write(base+8,0x00); SYSTEM_RESET()
ota.c:792 ota_fileIdCmp(): manuCode/imageType/fileVersion equal-or-0xFFFF-wildcard
ota.c:812 ota_saveUpdateInfo2NV(): nv_flashWriteNew(NV_MODULE_OTA, NV_ITEM_OTA_CODE, ...) crash resume
proj/drivers/drv_flash.h: flash_read/flash_write/flash_writeWithCheck/flash_erase/flash_lock/flash_unlock
stack/zigbee/af/zb_af.h:475 bool af_endpointRegister(u8 ep, af_simple_descriptor_t*, af_endpoint_cb_t rx_cb, af_dataCnf_cb_t cnfCb)
stack/zigbee/af/zb_af.h:498 u8 af_dataSend(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 cmdPldLen, u8 *cmdPld, u8 *apsCnt)
stack/zigbee/af/zb_af.h:322 void af_nodeDescRxOnWhenIdleUpdate(bool enable)
proj/common/utility.c: xcrc32 (reflected 0xEDB88320 table, init-only, no final XOR)

TC32 toolchain on Windows host: NOT FOUND (tc32-elf-gcc / tc32-elf-objcopy)
chip_8258 low-level driver sources (clock/flash-reg/watchdog/trng): NOT in public ref
(platform/boot/8258 boot_8258.link + cstartup only; board headers only) — library-only in Telink IDE
