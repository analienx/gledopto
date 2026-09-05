// Pure contract shared by the Zigbee2MQTT extension and offline tests.
// This module performs no Zigbee, MQTT, OTA, or filesystem I/O.

export const BRIDGE_PROTOCOL_VERSION = 1;
export const TARGET_IEEE = "0xa4c13850cfcdb3a4";
export const TARGET_ENDPOINT = 11;
export const CLUSTER_ID = 0xfc00;
export const CLUSTER_NAME = "glsdWirelessDump";
export const REQUEST_TOPIC = "bridge/request/glsd_wireless_dump";
export const RESPONSE_TOPIC = "bridge/response/glsd_wireless_dump";
export const STATE_TOPIC = "bridge/glsd_wireless_dump/state";

export const OPS = Object.freeze({
    ping: Object.freeze({command: "ping", response: "pingRsp", requestLength: 5}),
    info: Object.freeze({command: "info", response: "infoRsp", requestLength: 0}),
    read: Object.freeze({command: "read", response: "readRsp", requestLength: 13}),
    abort: Object.freeze({command: "abort", response: "abortRsp", requestLength: 0}),
});

const MIN_TIMEOUT_MS = 1000;
const MAX_TIMEOUT_MS = 30000;
const DEFAULT_TIMEOUT_MS = 10000;

function assertPlainObject(value, name) {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error(`${name} must be an object`);
    }
}

function parsePayloadHex(value) {
    if (typeof value !== "string") throw new Error("payload_hex must be a string");
    if (value.length % 2 !== 0 || !/^[0-9a-f]*$/i.test(value)) {
        throw new Error("payload_hex must contain complete hexadecimal bytes");
    }
    return Buffer.from(value, "hex");
}

export function parseRequest(input) {
    assertPlainObject(input, "request");
    if (input.protocol_version !== BRIDGE_PROTOCOL_VERSION) {
        throw new Error(`unsupported bridge protocol_version ${input.protocol_version}`);
    }
    if (typeof input.request_id !== "string" || input.request_id.length < 1 || input.request_id.length > 128) {
        throw new Error("request_id must be a non-empty string of at most 128 characters");
    }
    if (typeof input.target !== "string" || input.target.toLowerCase() !== TARGET_IEEE) {
        throw new Error(`target must be exactly ${TARGET_IEEE}`);
    }
    if (!Object.hasOwn(OPS, input.op)) throw new Error(`unsupported operation '${input.op}'`);

    const payload = parsePayloadHex(input.payload_hex ?? "");
    const spec = OPS[input.op];
    if (payload.length !== spec.requestLength) {
        throw new Error(`${input.op} payload must be exactly ${spec.requestLength} bytes`);
    }

    const timeoutMs = input.timeout_ms === undefined ? DEFAULT_TIMEOUT_MS : Number(input.timeout_ms);
    if (!Number.isInteger(timeoutMs) || timeoutMs < MIN_TIMEOUT_MS || timeoutMs > MAX_TIMEOUT_MS) {
        throw new Error(`timeout_ms must be an integer in ${MIN_TIMEOUT_MS}..${MAX_TIMEOUT_MS}`);
    }

    return Object.freeze({
        protocolVersion: BRIDGE_PROTOCOL_VERSION,
        requestId: input.request_id,
        target: TARGET_IEEE,
        op: input.op,
        payload,
        timeoutMs,
        command: spec.command,
        response: spec.response,
    });
}

export function successResponse(request, payload) {
    if (!Buffer.isBuffer(payload)) throw new Error("response payload must be a Buffer");
    return {
        protocol_version: BRIDGE_PROTOCOL_VERSION,
        request_id: request.requestId,
        target: TARGET_IEEE,
        op: request.op,
        status: "ok",
        payload_hex: payload.toString("hex"),
    };
}

export function errorResponse(requestId, op, message) {
    return {
        protocol_version: BRIDGE_PROTOCOL_VERSION,
        request_id: typeof requestId === "string" ? requestId : "invalid",
        target: TARGET_IEEE,
        op: typeof op === "string" ? op : "invalid",
        status: "error",
        error: String(message),
    };
}

export function fullTopic(baseTopic, relativeTopic) {
    if (typeof baseTopic !== "string" || !baseTopic) throw new Error("base topic is empty");
    return `${baseTopic}/${relativeTopic}`;
}
