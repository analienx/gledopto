#!/usr/bin/env node
/*
 * Gate for devices/gl-sd-301p/zigbee2mqtt/glsd301p_ed.js.
 *
 * Loads the external converter through zigbee-herdsman-converters' own
 * definition pipeline and proves it still matches the firmware: the Basic
 * modelId/manufacturerName strings are parsed from the target source, OTA
 * must stay enabled (the only wireless recovery path), and no control may be
 * exposed that the firmware refuses (power-on behavior, effects).
 *
 * Usage: node tools/check_z2m_converter.js <repo-root>
 * Requires zigbee-herdsman-converters to be resolvable (CI installs a pinned
 * version into a throwaway prefix).
 */
"use strict";

const fs = require("fs");
const path = require("path");

const root = path.resolve(process.argv[2] || ".");
const zhc = require("zigbee-herdsman-converters");

function fail(msg) {
    console.error("ERROR: " + msg);
    process.exit(1);
}

function zclString(source, name) {
    const m = source.match(new RegExp(name + "\\[\\]\\s*=\\s*\\{(\\d+),([^}]*)\\}"));
    if (!m) fail("cannot parse " + name + " from target source");
    const chars = m[2].split(",").map((c) => c.trim().replace(/^'(.*)'$/, "$1"));
    if (chars.length !== Number(m[1])) fail(name + " length prefix does not match its bytes");
    return chars.join("");
}

(async () => {
    const target = fs.readFileSync(
        path.join(root, "firmware/glsd301p-ed/glsd301p_telink_target.c"), "utf8");
    const model = zclString(target, "g_basic_model_id");
    const vendor = zclString(target, "g_basic_mfr_name");

    const def = require(path.join(root, "devices/gl-sd-301p/zigbee2mqtt/glsd301p_ed.js"));
    if (!def.zigbeeModel || !def.zigbeeModel.includes(model)) {
        fail(`converter zigbeeModel ${JSON.stringify(def.zigbeeModel)} does not include firmware modelId ${model}`);
    }
    if (def.vendor !== vendor) fail(`converter vendor ${def.vendor} != firmware manufacturerName ${vendor}`);

    const prepared = zhc.prepareDefinition(def);
    if (prepared.ota !== true) fail("converter must keep ota: true (vendor recovery path)");
    if (typeof prepared.configure !== "function") fail("converter lost reporting configuration");

    const exposes = typeof prepared.exposes === "function" ? prepared.exposes({}, {}) : prepared.exposes;
    const names = [];
    for (const e of exposes) {
        names.push(e.name || e.type);
        for (const f of e.features || []) names.push(f.name);
    }
    for (const required of ["state", "brightness", "power_on_behavior"]) {
        if (!names.includes(required)) fail("missing expose: " + required);
    }
    for (const forbidden of ["effect"]) {
        if (names.includes(forbidden)) fail("exposes control the firmware refuses: " + forbidden);
    }

    zhc.addExternalDefinition(def);
    const found = await zhc.findByDevice({
        modelID: model,
        manufacturerName: vendor,
        endpoints: [],
        getEndpoint: () => undefined,
        type: "EndDevice",
    });
    if (!found || found.model !== def.model) fail("findByDevice does not resolve the firmware identity");

    console.log(`Z2M_CONVERTER=PASS model=${model} vendor=${vendor} ota=true exposes=${names.join(",")}`);
})().catch((e) => fail(e && e.stack ? e.stack : String(e)));
