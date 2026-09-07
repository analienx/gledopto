# GL-SD-301P End Device build revalidation — 2026-09-07

Purpose: trigger a fresh isolated CI build of the existing RX-on-when-idle End Device product after GLEDOPTO supplied an exact-model stock OTA reference.

Vendor reference supplied directly by GLEDOPTO for GL-SD-301P:

- filename: `GL-SD-301P_V20851203_20251230.ota`
- size: `208946` bytes
- SHA-256: `16595a38ab9783d3afc4eb58ab4ec32625249bd569c1fa6dbaf468bddc76dd72`
- manufacturerCode: `0x124F`
- imageType: `0x1416`
- fileVersion: `0x28013001`
- OTA header string: `Telink OTA Sample Usage`
- boot lineage: classic Telink TC32/B85 marker at raw-image offset `0x08`

This commit does not authorize serving or flashing any image. The CI objective is build-only validation of the client/End Device product:

- Zigbee role = End Device
- `ZB_MAC_RX_ON_WHEN_IDLE=1`
- `PM_ENABLE=0`
- stack archive = `libzb_ed.a`
- endpoint = 11

Production deployment remains blocked until the actual GL-SD-301P power-stage driver is implemented and validated.
