import assert from "node:assert/strict";

import {
    BRIDGE_PROTOCOL_VERSION,
    TARGET_IEEE,
    errorResponse,
    parseRequest,
    successResponse,
} from "./glsd_wireless_dump_contract.mjs";

const read = parseRequest({
    protocol_version: BRIDGE_PROTOCOL_VERSION,
    request_id: "r1",
    target: TARGET_IEEE,
    op: "read",
    payload_hex: "00".repeat(13),
    timeout_ms: 2500,
});
assert.equal(read.payload.length, 13);
assert.equal(read.command, "read");
assert.equal(read.response, "readRsp");

assert.throws(
    () => parseRequest({...JSON.parse(JSON.stringify({
        protocol_version: 1,
        request_id: "r2",
        target: "0x0000000000000001",
        op: "info",
        payload_hex: "",
    }))}),
    /target must be exactly/,
);
assert.throws(
    () => parseRequest({
        protocol_version: 1,
        request_id: "r3",
        target: TARGET_IEEE,
        op: "read",
        payload_hex: "00".repeat(12),
    }),
    /exactly 13 bytes/,
);
assert.throws(
    () => parseRequest({
        protocol_version: 1,
        request_id: "r4",
        target: TARGET_IEEE,
        op: "write",
        payload_hex: "",
    }),
    /unsupported operation/,
);

const ok = successResponse(read, Buffer.from("aabb", "hex"));
assert.deepEqual(ok, {
    protocol_version: 1,
    request_id: "r1",
    target: TARGET_IEEE,
    op: "read",
    status: "ok",
    payload_hex: "aabb",
});
const err = errorResponse("r1", "read", "boom");
assert.equal(err.status, "error");
assert.equal(err.target, TARGET_IEEE);

console.log("glsd_wireless_dump_contract: PASS");
