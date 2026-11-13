/*
 * Zigbee2MQTT external converter for the independently authored GL-SD-301P
 * End Device firmware (Basic modelId "GL-SD-301P-ED", manufacturerName
 * "analienx").
 *
 * Why this exists: after the custom image boots, any re-interview (including
 * re-pairing after a factory-new boot) reports the custom identity, which no
 * upstream definition matches. A generated definition is not guaranteed to
 * enable OTA, and OTA is the only wireless path back to the vendor image.
 * This definition keeps `ota: true` so the recovery request in the release
 * plan stays deliverable.
 *
 * Install: copy to <z2m data>/external_converters/ and restart Zigbee2MQTT.
 * CommonJS on purpose, matching the converters already proven on this
 * installation (tuya-zigbee-switch/zigbee2mqtt/converters).
 *
 * Firmware facts mirrored here (firmware/glsd301p-ed/glsd301p_telink_target.c):
 *   - endpoint 11, HA dimmable light, OnOff + Level + Identify + Groups;
 *   - minLevel 2 / maxLevel 254;
 *   - ED-009 supports writable StartUpOnOff and restores committed brightness;
 *     StartUpCurrentLevel is read-only 255 (previous level);
 *   - Identify Trigger Effect is rejected, so no effect control is exposed;
 *   - Basic:0xFF10 is a read-only 48-byte health snapshot (decode with
 *     tools/glsd301p_health_decode.py); it is never reported.
 */
const {light, identify} = require("zigbee-herdsman-converters/lib/modernExtend");

const supportsStartup = (device) => /^GLSD-ED-(?:009|0[1-9][0-9]|[1-9][0-9]{2})$/.test(device?.softwareBuildID || "");
const lightExtension = light({configureReporting: true, effect: false, powerOnBehavior: true});
const startupExpose = lightExtension.exposes.find(e => e.name === "power_on_behavior");
lightExtension.exposes = [
    ...lightExtension.exposes.filter(e => e.name !== "power_on_behavior"),
    (device) => supportsStartup(device) ? [startupExpose] : [],
];
lightExtension.toZigbee = lightExtension.toZigbee.map(converter => {
    if (!converter.key.includes("power_on_behavior")) return converter;
    const guarded = {...converter};
    for (const method of ["convertSet", "convertGet"]) {
        if (!converter[method]) continue;
        guarded[method] = async (entity, ...args) => {
            const meta = args[args.length - 1];
            const device = meta?.device || entity.getDevice?.();
            if (!supportsStartup(device)) throw new Error("power_on_behavior requires a refreshed GLSD-ED-009-or-later build identity");
            return converter[method](entity, ...args);
        };
    }
    return guarded;
});

const definition = {
    zigbeeModel: ["GL-SD-301P-ED"],
    model: "GL-SD-301P-ED",
    vendor: "analienx",
    description: "GL-SD-301P dimmer running independent RX-on-when-idle End Device firmware",
    extend: [lightExtension, identify()],
    ota: true,
};

module.exports = definition;
