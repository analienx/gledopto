import {Zcl} from "zigbee-herdsman";

import {
    BRIDGE_PROTOCOL_VERSION,
    CLUSTER_ID,
    CLUSTER_NAME,
    REQUEST_TOPIC,
    RESPONSE_TOPIC,
    STATE_TOPIC,
    TARGET_ENDPOINT,
    TARGET_IEEE,
    errorResponse,
    fullTopic,
    parseRequest,
    successResponse,
} from "./glsd_wireless_dump_contract.mjs";

// Host-only definition. Registering it teaches herdsman how to serialize and
// parse cluster 0xFC00; it sends no Zigbee command and changes no device state.
const raw = [{name: "data", type: Zcl.BuffaloZclDataType.BUFFER}];
const GLSD_CLUSTER = {
    name: CLUSTER_NAME,
    ID: CLUSTER_ID,
    attributes: {},
    commands: {
        ping: {name: "ping", ID: 0x00, response: 0x80, parameters: raw},
        info: {name: "info", ID: 0x01, response: 0x81, parameters: raw},
        read: {name: "read", ID: 0x02, response: 0x82, parameters: raw},
        abort: {name: "abort", ID: 0x03, response: 0x83, parameters: raw},
    },
    commandsResponse: {
        pingRsp: {name: "pingRsp", ID: 0x80, parameters: raw},
        infoRsp: {name: "infoRsp", ID: 0x81, parameters: raw},
        readRsp: {name: "readRsp", ID: 0x82, parameters: raw},
        abortRsp: {name: "abortRsp", ID: 0x83, parameters: raw},
    },
};

export default class GlsdWirelessDumpExtension {
    constructor(zigbee, mqtt, _state, _publishEntityState, eventBus, _enableDisable, _restart, _addExtension, settings, logger) {
        this.zigbee = zigbee;
        this.mqtt = mqtt;
        this.eventBus = eventBus;
        this.settings = settings;
        this.logger = logger;
        this.inFlight = false;
        this.boundOnMQTTMessage = this.onMQTTMessage.bind(this);
        this.requestTopic = undefined;
    }

    async start() {
        const baseTopic = this.settings.get().mqtt.base_topic;
        this.requestTopic = fullTopic(baseTopic, REQUEST_TOPIC);

        const device = this.zigbee.resolveEntity(TARGET_IEEE);
        if (!device?.zh || device.zh.ieeeAddr.toLowerCase() !== TARGET_IEEE) {
            throw new Error(`GL-SD dump target ${TARGET_IEEE} is not present in Zigbee2MQTT`);
        }
        if (!device.zh.getEndpoint(TARGET_ENDPOINT)) {
            throw new Error(`GL-SD dump target has no endpoint ${TARGET_ENDPOINT}`);
        }

        device.zh.addCustomCluster(CLUSTER_NAME, GLSD_CLUSTER);
        this.eventBus.onMQTTMessage(this, this.boundOnMQTTMessage);

        await this.mqtt.publish(
            STATE_TOPIC,
            JSON.stringify({
                protocol_version: BRIDGE_PROTOCOL_VERSION,
                state: "ready",
                target: TARGET_IEEE,
                endpoint: TARGET_ENDPOINT,
                cluster: `0x${CLUSTER_ID.toString(16)}`,
                read_only: true,
            }),
        );
        this.logger.info(`GL-SD wireless dump transport ready for exact target ${TARGET_IEEE}`);
    }

    async stop() {
        this.eventBus.removeListeners(this);
        await this.mqtt.publish(
            STATE_TOPIC,
            JSON.stringify({protocol_version: BRIDGE_PROTOCOL_VERSION, state: "stopped", target: TARGET_IEEE}),
        );
    }

    async publishResponse(value) {
        await this.mqtt.publish(RESPONSE_TOPIC, JSON.stringify(value));
    }

    async onMQTTMessage(data) {
        if (data.topic !== this.requestTopic) return;

        let decoded;
        let request;
        try {
            decoded = JSON.parse(data.message);
            request = parseRequest(decoded);
        } catch (error) {
            await this.publishResponse(errorResponse(decoded?.request_id, decoded?.op, error.message));
            return;
        }

        if (this.inFlight) {
            await this.publishResponse(errorResponse(request.requestId, request.op, "another dump request is already in flight"));
            return;
        }

        this.inFlight = true;
        try {
            // Resolve by the compile-time IEEE on every call. A friendly-name
            // rename or caller-provided id can never redirect this transport.
            const device = this.zigbee.resolveEntity(TARGET_IEEE);
            if (!device?.zh || device.zh.ieeeAddr.toLowerCase() !== TARGET_IEEE) {
                throw new Error("target device disappeared from Zigbee2MQTT");
            }
            const endpoint = device.zh.getEndpoint(TARGET_ENDPOINT);
            if (!endpoint) throw new Error(`target endpoint ${TARGET_ENDPOINT} is unavailable`);

            // `command.response` in the custom cluster lets herdsman match the
            // server response by address/endpoint/cluster/command/TSN. The v1
            // payload independently carries session_id/seq/offset/length, which
            // the guarded Python host validates again before persisting bytes.
            const response = await endpoint.command(
                CLUSTER_NAME,
                request.command,
                {data: request.payload},
                {
                    timeout: request.timeoutMs,
                    disableDefaultResponse: true,
                    disableRecovery: false,
                    sendPolicy: "immediate",
                },
            );
            if (!response || !Buffer.isBuffer(response.data)) {
                throw new Error(`missing or malformed ${request.response} payload`);
            }

            await this.publishResponse(successResponse(request, response.data));
        } catch (error) {
            await this.publishResponse(errorResponse(request.requestId, request.op, error.message));
        } finally {
            this.inFlight = false;
        }
    }
}
