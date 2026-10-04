#!/usr/bin/env python3
"""Apply pinned narrow GL-SD-301P patches to a hosted Telink SDK copy.

Each patch pins the exact original file hash, applies anchor-based edits
(each anchor must occur exactly once), and verifies the exact patched hash.
Re-running over an already-patched copy verifies the patched hash instead of
editing again, so repeated builds in one workspace stay deterministic.

Provenance (original/patched hashes, anchors) is emitted as JSON for the
build manifest. Only the listed files are touched.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

PATCHES = [
    {
        "id": "P1-timebase-hook",
        "relpath": "proj/os/ev_timer.c",
        "original_sha256": (
            "56bba6c25b1ed157c89c644d9d8793ccc9feb0fa37926cbd0824c23757bc9a67"
        ),
        "patched_sha256": (
            "239f1a38ac2cb1ab7a9241f329c492ff914c2245ca92159f0c18869c28fa01eb"
        ),
        "marker": "glsd301p_timebase_advance",
        "edits": [
            {
                "anchor": '#include "ev_timer.h"\n',
                "replacement": (
                    '#include "ev_timer.h"\n'
                    '#include "glsd301p_timebase.h"\n'
                ),
            },
            {
                "anchor": "    ev_rtc_update(updateTime);\n",
                "replacement": (
                    "    ev_rtc_update(updateTime);\n"
                    "    glsd301p_timebase_advance(updateTime);\n"
                ),
            },
        ],
    },
    {
        "id": "P2a-level-parse",
        "relpath": "zigbee/zcl/general/zcl_level.c",
        "original_sha256": (
            "dcb208c6fb5d368cd6ba4b4a57b0732a15b59d122f403a77351bd36a82b02dde"
        ),
        "patched_sha256": "9d410e021e4c8c7924c0b8a161c44910bc3f8edd4dfb81c69f466ea4131ac8c0",
        "marker": "pInMsg->dataLen != 3",
        "edits": [
            {
                "anchor": (
                    "    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:\n"
                    "    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF:\n"
                    "        cmdPayload.moveToLevel.level = *pData++;\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:\n"
                    "    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF:\n"
                    "        if ((pInMsg->dataLen != 3) && (pInMsg->dataLen != 5)) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        cmdPayload.moveToLevel.level = *pData++;\n"
                ),
            },
            {
                "anchor": (
                    "    case ZCL_CMD_LEVEL_MOVE:\n"
                    "    case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF:\n"
                    "        cmdPayload.move.moveMode = *pData++;\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_LEVEL_MOVE:\n"
                    "    case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF:\n"
                    "        if ((pInMsg->dataLen != 2) && (pInMsg->dataLen != 4)) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        cmdPayload.move.moveMode = *pData++;\n"
                ),
            },
            {
                "anchor": (
                    "    case ZCL_CMD_LEVEL_STEP:\n"
                    "    case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF:\n"
                    "        cmdPayload.step.stepMode = *pData++;\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_LEVEL_STEP:\n"
                    "    case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF:\n"
                    "        if ((pInMsg->dataLen != 4) && (pInMsg->dataLen != 6)) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        cmdPayload.step.stepMode = *pData++;\n"
                ),
            },
            {
                "anchor": (
                    "    case ZCL_CMD_LEVEL_STOP:\n"
                    "    case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:\n"
                    "        if (pInMsg->dataLen >= 2) {\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_LEVEL_STOP:\n"
                    "    case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:\n"
                    "        if ((pInMsg->dataLen != 0) && (pInMsg->dataLen != 2)) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        if (pInMsg->dataLen == 2) {\n"
                ),
            },
            {
                "anchor": (
                    "            pInMsg->clusterAppCb(&(pInMsg->addrInfo),"
                    " pInMsg->hdr.cmd, &cmdPayload);\n"
                ),
                "replacement": (
                    "            status = pInMsg->clusterAppCb("
                    "&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmdPayload);\n"
                ),
            },
        ],
    },
    {
        "id": "P2b-onoff-parse",
        "relpath": "zigbee/zcl/general/zcl_onoff.c",
        "original_sha256": (
            "53f14e401c00323313572095cb7f95254557d094ca2878ddb74ddadf2c688ae6"
        ),
        "patched_sha256": "8431f19fb70c05314e0e7bf1c105cba88e070197e6b3d852e5cd052ddb94b32f",
        "marker": "pInMsg->dataLen != 0",
        "edits": [
            {
                "anchor": (
                    "    case ZCL_CMD_ONOFF_OFF:\n"
                    "    case ZCL_CMD_ONOFF_ON:\n"
                    "    case ZCL_CMD_ONOFF_TOGGLE:\n"
                    "    case ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE:\n"
                    "        break;\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_ONOFF_OFF:\n"
                    "    case ZCL_CMD_ONOFF_ON:\n"
                    "    case ZCL_CMD_ONOFF_TOGGLE:\n"
                    "    case ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE:\n"
                    "        if (pInMsg->dataLen != 0) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        break;\n"
                ),
            },
            {
                "anchor": (
                    "    case ZCL_CMD_OFF_WITH_EFFECT:\n"
                    "        cmdPayload.offWithEffect.effectId = pData[0];\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_OFF_WITH_EFFECT:\n"
                    "        if (pInMsg->dataLen != 2) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        cmdPayload.offWithEffect.effectId = pData[0];\n"
                ),
            },
            {
                "anchor": (
                    "    case ZCL_CMD_ON_WITH_TIMED_OFF:\n"
                    "        cmdPayload.onWithTimeOff.onOffCtrl.onOffCtrl"
                    " = *pData++;\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_ON_WITH_TIMED_OFF:\n"
                    "        if (pInMsg->dataLen != 5) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        cmdPayload.onWithTimeOff.onOffCtrl.onOffCtrl"
                    " = *pData++;\n"
                ),
            },
            {
                "anchor": (
                    "            pInMsg->clusterAppCb(&(pInMsg->addrInfo),"
                    " pInMsg->hdr.cmd, &cmdPayload);\n"
                ),
                "replacement": (
                    "            status = pInMsg->clusterAppCb("
                    "&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmdPayload);\n"
                ),
            },
        ],
    },
    {
        "id": "P3-identify-parse",
        "relpath": "zigbee/zcl/general/zcl_identify.c",
        "original_sha256": (
            "75ac2aaa2f2fdeaf1661306b0e6d63eb8c7f2f0a1ac48de7ea394bda71ce1127"
        ),
        "patched_sha256": "32a9b56a79fc6bbfb78595ecf03459cb3628cc8952e4a28bf272d5e90b2e10fe",
        "marker": "pInMsg->dataLen != 2",
        "edits": [
            {
                "anchor": (
                    "    case ZCL_CMD_IDENTIFY:\n"
                    "        //status = zcl_identifyPrc(pInMsg);\n"
                    "        cmdPayload.identify.identifyTime = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_IDENTIFY:\n"
                    "        if (pInMsg->dataLen != 2) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        //status = zcl_identifyPrc(pInMsg);\n"
                    "        cmdPayload.identify.identifyTime = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
            },
            {
                "anchor": (
                    "    case ZCL_CMD_TRIGGER_EFFECT:\n"
                    "        //status = zcl_identifyTriggerEffectPrc(pInMsg);\n"
                    "        cmdPayload.triggerEffect.effectId = pInMsg->pData[0];\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_TRIGGER_EFFECT:\n"
                    "        if (pInMsg->dataLen != 2) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        //status = zcl_identifyTriggerEffectPrc(pInMsg);\n"
                    "        cmdPayload.triggerEffect.effectId = pInMsg->pData[0];\n"
                ),
            },
            {
                "anchor": (
                    "    case ZCL_CMD_IDENTIFY_QUERY:\n"
                    "        status = zcl_identifyQueryPrc(pInMsg);\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_IDENTIFY_QUERY:\n"
                    "        if (pInMsg->dataLen != 0) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        status = zcl_identifyQueryPrc(pInMsg);\n"
                ),
            },
            {
                "anchor": (
                    "    case ZCL_CMD_IDENTIFY_QUERY_RSP:\n"
                    "        //status = zcl_identifyQueryRspPrc(pInMsg);\n"
                    "        cmdPayload.identifyRsp.timeout = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
                "replacement": (
                    "    case ZCL_CMD_IDENTIFY_QUERY_RSP:\n"
                    "        if (pInMsg->dataLen != 2) {\n"
                    "            status = ZCL_STA_MALFORMED_COMMAND;\n"
                    "            break;\n"
                    "        }\n"
                    "        //status = zcl_identifyQueryRspPrc(pInMsg);\n"
                    "        cmdPayload.identifyRsp.timeout = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
            },
        ],
    },
    {
        "id": "P4-group-parse",
        "relpath": "zigbee/zcl/general/zcl_group.c",
        "original_sha256": (
            "82ec54ff0ecee3f1e876371f085cb10bf73ed0dfb2647399ad7481adc8732166"
        ),
        "patched_sha256": "6272e38bcb598f23c55b832e8c16d2c93f51795ba42762f892a9760c9e0530c1",
        "marker": "glsd301p_gcnt",
        "edits": [
            {
                "anchor": (
                    "    aps_add_group_req_t addGroup;\n"
                    "\n"
                    "    addGroup.group_addr = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
                "replacement": (
                    "    if (pInMsg->dataLen < 3) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u8 glsd301p_nlen = pInMsg->pData[2];\n"
                    "        if (glsd301p_nlen > 15) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "        if (pInMsg->dataLen != (u16)3 + (u16)glsd301p_nlen) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                    "    aps_add_group_req_t addGroup;\n"
                    "\n"
                    "    addGroup.group_addr = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
            },
            {
                "anchor": (
                    "    removeGroup.group_addr = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
                "replacement": (
                    "    if (pInMsg->dataLen != 2) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    removeGroup.group_addr = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
            },
            {
                "anchor": (
                    "        zcl_viewGroupRsp_t viewGroupRsp;\n"
                    "\n"
                    "        viewGroupRsp.groupId = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
                "replacement": (
                    "        if (pInMsg->dataLen != 2) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "        zcl_viewGroupRsp_t viewGroupRsp;\n"
                    "\n"
                    "        viewGroupRsp.groupId = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);\n"
                ),
            },
            {
                "anchor": (
                    "        u8 *pBuf = pInMsg->pData;\n"
                    "        u8 groupCnt = *pBuf++;\n"
                ),
                "replacement": (
                    "        {\n"
                    "            u8 glsd301p_gcnt = (pInMsg->dataLen >= 1) ? pInMsg->pData[0] : 0;\n"
                    "            if (glsd301p_gcnt > APS_GROUP_TABLE_NUM) {\n"
                    "                return ZCL_STA_INSUFFICIENT_SPACE;\n"
                    "            }\n"
                    "            if (pInMsg->dataLen != (u16)1 + (u16)glsd301p_gcnt * (u16)2) {\n"
                    "                return ZCL_STA_MALFORMED_COMMAND;\n"
                    "            }\n"
                    "        }\n"
                    "        u8 *pBuf = pInMsg->pData;\n"
                    "        u8 groupCnt = *pBuf++;\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_removeAllGroupPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    u8 status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_removeAllGroupPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    u8 status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen != 0) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_addGroupIfIdentifyPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    u8 status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_addGroupIfIdentifyPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    u8 status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 3) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u8 glsd301p_nlen = pInMsg->pData[2];\n"
                    "        if (glsd301p_nlen > 15) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "        if (pInMsg->dataLen != (u16)3 + (u16)glsd301p_nlen) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
        ],
    },
    {
        "id": "P5-foundation-parse",
        "relpath": "zigbee/zcl/zcl.c",
        "original_sha256": (
            "5f11eb33626af3821fc830b64bcbce79fb48633ea1ca00890b722144c17b2f2d"
        ),
        "patched_sha256": "75567f2b24766f4dab940d53ebd66c955618efd5975643ee4367e845e405f274",
        "marker": "glsd301p_attrRecValid",
        "edits": [
            {
                "anchor": (
                    "/***************************************************************************\n"
                    " **************************** Read *****************************************\n"
                    " ***************************************************************************/\n"
                    "#ifdef ZCL_READ\n"
                ),
                "replacement": (
                    "/***************************************************************************\n"
                    " **************************** Read *****************************************\n"
                    " ***************************************************************************/\n"
                    "/* GLSD301P P5: bounded pre-parse validators. Each validator walks the\n"
                    " * exact record layout the matching build pass consumes and rejects\n"
                    " * truncated streams before any allocation, callback, or table use.\n"
                    " * Record counts are capped at 255 so the parsers' u8 numAttr cannot\n"
                    " * wrap on over-long inputs. */\n"
                    "static u8 glsd301p_attrValLen(u8 dataType, u8 *p, u16 rem, u16 *pLen)\n"
                    "{\n"
                    "    u16 n;\n"
                    "    if ((dataType == ZCL_DATA_TYPE_LONG_CHAR_STR) || (dataType == ZCL_DATA_TYPE_LONG_OCTET_STR)) {\n"
                    "        if (rem < 2) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        n = BUILD_U16(p[0], p[1]);\n"
                    "        if (n > rem - 2) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        *pLen = n + 2;\n"
                    "        return 1;\n"
                    "    }\n"
                    "    if ((dataType == ZCL_DATA_TYPE_CHAR_STR) || (dataType == ZCL_DATA_TYPE_OCTET_STR)) {\n"
                    "        if (rem < 1) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        n = p[0];\n"
                    "        if (n > rem - 1) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        *pLen = n + 1;\n"
                    "        return 1;\n"
                    "    }\n"
                    "    if (dataType == ZCL_DATA_TYPE_STRUCT) {\n"
                    "        u8 itemNum;\n"
                    "        u16 pos = 2;\n"
                    "        u8 i;\n"
                    "        if (rem < 2) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        itemNum = p[0];\n"
                    "        for (i = 0; i < itemNum; i++) {\n"
                    "            u16 l;\n"
                    "            if (pos >= rem) {\n"
                    "                return 0;\n"
                    "            }\n"
                    "            l = zcl_getDataTypeLen(p[pos]);\n"
                    "            pos += 1;\n"
                    "            if (l > rem - pos) {\n"
                    "                return 0;\n"
                    "            }\n"
                    "            pos += l;\n"
                    "        }\n"
                    "        *pLen = pos;\n"
                    "        return 1;\n"
                    "    }\n"
                    "    n = zcl_getDataTypeLen(dataType);\n"
                    "    if (n > rem) {\n"
                    "        return 0;\n"
                    "    }\n"
                    "    *pLen = n;\n"
                    "    return 1;\n"
                    "}\n"
                    "\n"
                    "/* attrID + dataType + value records (write, report). */\n"
                    "static u8 glsd301p_attrRecValid(zclIncoming_t *pCmd)\n"
                    "{\n"
                    "    u8 *p = pCmd->pData;\n"
                    "    u16 rem = pCmd->dataLen;\n"
                    "    u16 n = 0;\n"
                    "    while (rem > 0) {\n"
                    "        u16 vl = 0;\n"
                    "        if (rem < 3) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        if (!glsd301p_attrValLen(p[2], p + 3, rem - 3, &vl)) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        p += 3 + vl;\n"
                    "        rem -= 3 + vl;\n"
                    "        n++;\n"
                    "        if (n > 255) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "    }\n"
                    "    return 1;\n"
                    "}\n"
                    "\n"
                    "/* attrID + status + [dataType + value] records (read response). */\n"
                    "static u8 glsd301p_readRspValid(zclIncoming_t *pCmd)\n"
                    "{\n"
                    "    u8 *p = pCmd->pData;\n"
                    "    u16 rem = pCmd->dataLen;\n"
                    "    u16 n = 0;\n"
                    "    while (rem > 0) {\n"
                    "        if (rem < 3) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        if (p[2] == ZCL_STA_SUCCESS) {\n"
                    "            u16 vl = 0;\n"
                    "            if (rem < 4) {\n"
                    "                return 0;\n"
                    "            }\n"
                    "            if (!glsd301p_attrValLen(p[3], p + 4, rem - 4, &vl)) {\n"
                    "                return 0;\n"
                    "            }\n"
                    "            p += 4 + vl;\n"
                    "            rem -= 4 + vl;\n"
                    "        } else {\n"
                    "            p += 3;\n"
                    "            rem -= 3;\n"
                    "        }\n"
                    "        n++;\n"
                    "        if (n > 255) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "    }\n"
                    "    return 1;\n"
                    "}\n"
                    "\n"
                    "/* configure-reporting records, matching the build-pass layout. */\n"
                    "static u8 glsd301p_cfgValid(zclIncoming_t *pCmd)\n"
                    "{\n"
                    "    u8 *p = pCmd->pData;\n"
                    "    u16 rem = pCmd->dataLen;\n"
                    "    u16 n = 0;\n"
                    "    while (rem > 0) {\n"
                    "        if (rem < 3) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        if (p[0] == ZCL_SEND_ATTR_REPORTS) {\n"
                    "            u16 l;\n"
                    "            if (rem < 8) {\n"
                    "                return 0;\n"
                    "            }\n"
                    "            if (zcl_analogDataType(p[3])) {\n"
                    "                l = zcl_getDataTypeLen(p[3]);\n"
                    "                if (l > rem - 8) {\n"
                    "                    return 0;\n"
                    "                }\n"
                    "                p += 8 + l;\n"
                    "                rem -= 8 + l;\n"
                    "            } else {\n"
                    "                p += 8;\n"
                    "                rem -= 8;\n"
                    "            }\n"
                    "        } else {\n"
                    "            if (rem < 5) {\n"
                    "                return 0;\n"
                    "            }\n"
                    "            p += 5;\n"
                    "            rem -= 5;\n"
                    "        }\n"
                    "        n++;\n"
                    "        if (n > 255) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "    }\n"
                    "    return 1;\n"
                    "}\n"
                    "\n"
                    "/* read-reporting-configuration response records. */\n"
                    "static u8 glsd301p_readCfgRspValid(zclIncoming_t *pCmd)\n"
                    "{\n"
                    "    u8 *p = pCmd->pData;\n"
                    "    u16 rem = pCmd->dataLen;\n"
                    "    u16 n = 0;\n"
                    "    while (rem > 0) {\n"
                    "        if (rem < 4) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "        if (p[0] == ZCL_STA_SUCCESS) {\n"
                    "            if (p[1] == ZCL_SEND_ATTR_REPORTS) {\n"
                    "                u16 l;\n"
                    "                if (rem < 9) {\n"
                    "                    return 0;\n"
                    "                }\n"
                    "                if (zcl_analogDataType(p[4])) {\n"
                    "                    l = zcl_getDataTypeLen(p[4]);\n"
                    "                    if (l > rem - 9) {\n"
                    "                        return 0;\n"
                    "                    }\n"
                    "                    p += 9 + l;\n"
                    "                    rem -= 9 + l;\n"
                    "                } else {\n"
                    "                    p += 9;\n"
                    "                    rem -= 9;\n"
                    "                }\n"
                    "            } else {\n"
                    "                if (rem < 6) {\n"
                    "                    return 0;\n"
                    "                }\n"
                    "                p += 6;\n"
                    "                rem -= 6;\n"
                    "            }\n"
                    "        } else {\n"
                    "            p += 4;\n"
                    "            rem -= 4;\n"
                    "        }\n"
                    "        n++;\n"
                    "        if (n > 255) {\n"
                    "            return 0;\n"
                    "        }\n"
                    "    }\n"
                    "    return 1;\n"
                    "}\n"
                    "\n"
                    "#ifdef ZCL_READ\n"
                ),
            },
            {
                "anchor": (
                    "    if (zcl_vars.hookFn && toAppFlg && inMsg.attrCmd) {\n"
                    "        zcl_vars.hookFn(&inMsg);\n"
                    "        ev_buf_free(inMsg.attrCmd);\n"
                    "    }\n"
                ),
                "replacement": (
                    "    if (zcl_vars.hookFn && toAppFlg && inMsg.attrCmd) {\n"
                    "        zcl_vars.hookFn(&inMsg);\n"
                    "    }\n"
                    "    if (inMsg.attrCmd) {\n"
                    "        ev_buf_free(inMsg.attrCmd);\n"
                    "        inMsg.attrCmd = NULL;\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "            status = zcl_foundationCmdHandler(&inMsg);\n"
                    "            if ((status != ZCL_STA_UNSUP_GENERAL_COMMAND) && (status != ZCL_STA_UNSUP_MANU_GENERAL_COMMAND) &&\n"
                    "                (status != ZCL_STA_SUCCESS) && (status != ZCL_STA_CMD_HAS_RESP)) {\n"
                    "                status = ZCL_STA_FAILURE;\n"
                    "            }\n"
                ),
                "replacement": (
                    "            status = zcl_foundationCmdHandler(&inMsg);\n"
                ),
            },
            {
                "anchor": (
                    "            if (zcl_analogDataType(dataType)) {\n"
                    "                reportChangeLen = zcl_getDataTypeLen(dataType);\n"
                    "                pBuf += reportChangeLen;\n"
                    "\n"
                    "                dataLen += reportChangeLen;\n"
                    "            } else {\n"
                    "                pBuf++;\n"
                    "            }\n"
                ),
                "replacement": (
                    "            if (zcl_analogDataType(dataType)) {\n"
                    "                reportChangeLen = zcl_getDataTypeLen(dataType);\n"
                    "                pBuf += reportChangeLen;\n"
                    "\n"
                    "                dataLen += reportChangeLen;\n"
                    "            }\n"
                ),
            },
            {
                "anchor": "    u8 len = sizeof(zclCfgReportCmd_t) + numAttr * sizeof(zclCfgReportRec_t);\n",
                "replacement": "    u16 len = sizeof(zclCfgReportCmd_t) + numAttr * sizeof(zclCfgReportRec_t);\n",
            },
            {
                "anchor": "    u8 len = sizeof(zclCfgReportRspCmd_t) + pCfgReportCmd->numAttr * sizeof(zclCfgReportStatus_t);\n",
                "replacement": "    u16 len = sizeof(zclCfgReportRspCmd_t) + pCfgReportCmd->numAttr * sizeof(zclCfgReportStatus_t);\n",
            },
            {
                "anchor": "    u8 len = sizeof(zclReadReportCfgRspCmd_t) + pReadReportCfgCmd->numAttr * sizeof(zclReportCfgRspRec_t);\n",
                "replacement": "    u16 len = sizeof(zclReadReportCfgRspCmd_t) + pReadReportCfgCmd->numAttr * sizeof(zclReportCfgRspRec_t);\n",
            },
            {
                "anchor": (
                    "    /* Parse In Read Response Command */\n"
                    "    pReadRspCmd = zcl_parseInReadRspCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if (!glsd301p_readRspValid(pCmd)) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Read Response Command */\n"
                    "    pReadRspCmd = zcl_parseInReadRspCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    bool rspSend = FALSE;\n"
                    "\n"
                    "    /* Parse In Write Command */\n"
                    "    zclWriteCmd_t *pWriteCmd = zcl_parseInWriteCmd(pCmd);\n"
                ),
                "replacement": (
                    "    bool rspSend = FALSE;\n"
                    "\n"
                    "    if (!glsd301p_attrRecValid(pCmd)) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Write Command */\n"
                    "    zclWriteCmd_t *pWriteCmd = zcl_parseInWriteCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    bool needWrite = TRUE;\n"
                    "\n"
                    "    /* Parse In Write Command */\n"
                    "    zclWriteCmd_t *pWriteCmd = zcl_parseInWriteCmd(pCmd);\n"
                ),
                "replacement": (
                    "    bool needWrite = TRUE;\n"
                    "\n"
                    "    if (!glsd301p_attrRecValid(pCmd)) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Write Command */\n"
                    "    zclWriteCmd_t *pWriteCmd = zcl_parseInWriteCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Configure Report Command */\n"
                    "    zclCfgReportCmd_t *pCfgReportCmd = zcl_parseInCfgReportCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if (!glsd301p_cfgValid(pCmd)) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Configure Report Command */\n"
                    "    zclCfgReportCmd_t *pCfgReportCmd = zcl_parseInCfgReportCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Report Command */\n"
                    "    pReportCmd = zcl_parseInReportCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if (!glsd301p_attrRecValid(pCmd)) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Report Command */\n"
                    "    pReportCmd = zcl_parseInReportCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Read Report Configure Response Command */\n"
                    "    pReadReportCfgRspCmd = zcl_parseInReadReportCfgRspCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if (!glsd301p_readCfgRspValid(pCmd)) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Read Report Configure Response Command */\n"
                    "    pReadReportCfgRspCmd = zcl_parseInReadReportCfgRspCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Default Response Command */\n"
                    "    zclDefaultRspCmd_t *pDfltRspCmd = zcl_parseInDftRspCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if (pCmd->dataLen != 2) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Default Response Command */\n"
                    "    zclDefaultRspCmd_t *pDfltRspCmd = zcl_parseInDftRspCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    bool status_disComplete = TRUE;\n"
                    "\n"
                    "    /* Parse In Discover Attributes Command */\n"
                    "    zclDiscoverAttrCmd_t *pDiscAttrCmd = zcl_parseInDiscAttrsCmd(pCmd);\n"
                ),
                "replacement": (
                    "    bool status_disComplete = TRUE;\n"
                    "\n"
                    "    if (pCmd->dataLen != 3) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Discover Attributes Command */\n"
                    "    zclDiscoverAttrCmd_t *pDiscAttrCmd = zcl_parseInDiscAttrsCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    bool status_discComplete = TRUE;\n"
                    "\n"
                    "    /* Parse In Discover Attributes Command */\n"
                    "    zclDiscoverAttrCmd_t *pDiscAttrCmd = zcl_parseInDiscAttrsCmd(pCmd);\n"
                ),
                "replacement": (
                    "    bool status_discComplete = TRUE;\n"
                    "\n"
                    "    if (pCmd->dataLen != 3) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Discover Attributes Command */\n"
                    "    zclDiscoverAttrCmd_t *pDiscAttrCmd = zcl_parseInDiscAttrsCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Discover Attributes Response Command */\n"
                    "    pDiscAttrRspCmd = zcl_parseInDiscAttrsRspCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if (pCmd->dataLen < 1) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Discover Attributes Response Command */\n"
                    "    pDiscAttrRspCmd = zcl_parseInDiscAttrsRspCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Discover Extended Attributes Response Command */\n"
                    "    pDiscAttrExtRspCmd = zcl_parseInDiscAttrsExtRspCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if (pCmd->dataLen < 1) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Discover Extended Attributes Response Command */\n"
                    "    pDiscAttrExtRspCmd = zcl_parseInDiscAttrsExtRspCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Read Command */\n"
                    "    zclReadCmd_t *pReadCmd = zcl_parseInReadCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if ((pCmd->dataLen == 0) || (pCmd->dataLen & 1)) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Read Command */\n"
                    "    zclReadCmd_t *pReadCmd = zcl_parseInReadCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Write Response Command */\n"
                    "    pWriteRspCmd = zcl_parseInWriteRspCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if ((pCmd->dataLen != 1) && ((pCmd->dataLen < 3) || (pCmd->dataLen % 3 != 0))) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Write Response Command */\n"
                    "    pWriteRspCmd = zcl_parseInWriteRspCmd(pCmd);\n"
                ),
            },
            {
                "anchor": (
                    "    /* Parse In Configure Report Response Command */\n"
                    "    pCfgReportRspCmd = zcl_parseInCfgReportRspCmd(pCmd);\n"
                ),
                "replacement": (
                    "    if ((pCmd->dataLen != 1) && ((pCmd->dataLen < 4) || (pCmd->dataLen % 4 != 0))) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    /* Parse In Configure Report Response Command */\n"
                    "    pCfgReportRspCmd = zcl_parseInCfgReportRspCmd(pCmd);\n"
                ),
            },
        ],
    },
    {
        "id": "P6-ota-parse",
        "relpath": "zigbee/zcl/ota_upgrading/zcl_ota.c",
        "original_sha256": (
            "d559b5db965fec064d20e5c0800d68974fd43aebd902879c94d86d4f30c87b94"
        ),
        "patched_sha256": "722ed279b7c7c9cccc1c9f24b7b0ba99a73660f5694c3b136f4a868ecefb45cc",
        "marker": "GLSD301P P6",
        "edits": [
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_queryNextImageReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    u8 status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "/* GLSD301P P6: exact OTA lengths before any payload read or callback. */\n"
                    "_CODE_ZCL_ static status_t zcl_ota_queryNextImageReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    u8 status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 9) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u8 fc = pInMsg->pData[0];\n"
                    "        u16 need = (fc & IMAGE_FC_BITMASK_HARDWARE_VERSION_PRESENT) ? 11 : 9;\n"
                    "        if (pInMsg->dataLen != need) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_imageBlockReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_imageBlockReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 14) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u8 fc = pInMsg->pData[0];\n"
                    "        u16 need = 14;\n"
                    "        if (fc & BLOCK_FC_BITMASK_NODE_IEEE_PRESENT) {\n"
                    "            need += EXT_ADDR_LEN;\n"
                    "        }\n"
                    "        if (fc & BLOCK_FC_BITMASK_MIN_PERIOD_PRESENT) {\n"
                    "            need += 2;\n"
                    "        }\n"
                    "        if (pInMsg->dataLen != need) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_imagePageReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_imagePageReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 18) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u8 fc = pInMsg->pData[0];\n"
                    "        u16 need = 18;\n"
                    "        if (fc & PAGE_FC_BITMASK_NODE_IEEE_PRESENT) {\n"
                    "            need += EXT_ADDR_LEN;\n"
                    "        }\n"
                    "        if (pInMsg->dataLen != need) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_upgradeEndReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_upgradeEndReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 1) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u16 need = (pInMsg->pData[0] == ZCL_STA_SUCCESS) ? 9 : 1;\n"
                    "        if (pInMsg->dataLen != need) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_queryDeviceSpecificFileReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_queryDeviceSpecificFileReqPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen != 18) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_imageNotifyPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_imageNotifyPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 2) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u8 pt = pInMsg->pData[0];\n"
                    "        u16 need;\n"
                    "        if (pt == IMAGE_NOTIFY_QUERY_JITTER) {\n"
                    "            need = 2;\n"
                    "        } else if (pt == IMAGE_NOTIFY_QUERT_JITTER_MFG) {\n"
                    "            need = 4;\n"
                    "        } else if (pt == IMAGE_NOTIFY_QUERY_JITTER_MFG_TYPE) {\n"
                    "            need = 6;\n"
                    "        } else if (pt == IMAGE_NOTIFY_QUERY_JITTER_MFG_TYPE_VER) {\n"
                    "            need = 10;\n"
                    "        } else {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "        if (pInMsg->dataLen != need) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_queryNextImageRspPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_queryNextImageRspPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 1) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u16 need = (pInMsg->pData[0] == ZCL_STA_SUCCESS) ? 13 : 1;\n"
                    "        if (pInMsg->dataLen != need) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_imageBlockRspPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_imageBlockRspPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 1) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u8 st = pInMsg->pData[0];\n"
                    "        u16 need;\n"
                    "        if (st == ZCL_STA_SUCCESS) {\n"
                    "            if (pInMsg->dataLen < 14) {\n"
                    "                return ZCL_STA_MALFORMED_COMMAND;\n"
                    "            }\n"
                    "            need = 14 + pInMsg->pData[13];\n"
                    "        } else if (st == ZCL_STA_WAIT_FOR_DATA) {\n"
                    "            need = 11;\n"
                    "        } else {\n"
                    "            need = 1;\n"
                    "        }\n"
                    "        if (pInMsg->dataLen != need) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_upgradeEndRspPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_upgradeEndRspPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen != 16) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                ),
            },
            {
                "anchor": (
                    "_CODE_ZCL_ static status_t zcl_ota_queryDeviceSpecificFileRspPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                ),
                "replacement": (
                    "_CODE_ZCL_ static status_t zcl_ota_queryDeviceSpecificFileRspPrc(zclIncoming_t *pInMsg)\n"
                    "{\n"
                    "    status_t status = ZCL_STA_SUCCESS;\n"
                    "    if (pInMsg->dataLen < 1) {\n"
                    "        return ZCL_STA_MALFORMED_COMMAND;\n"
                    "    }\n"
                    "    {\n"
                    "        u16 need = (pInMsg->pData[0] == ZCL_STA_SUCCESS) ? 13 : 1;\n"
                    "        if (pInMsg->dataLen != need) {\n"
                    "            return ZCL_STA_MALFORMED_COMMAND;\n"
                    "        }\n"
                    "    }\n"
                ),
            },
        ],
    },
]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def apply_patch(sdk_root: Path, patch: dict, derive: bool = False) -> dict:
    relpath = patch["relpath"]
    path = sdk_root / relpath
    if not path.is_file():
        raise SystemExit(f"ERROR: SDK file missing for {patch['id']}: {relpath}")
    raw = path.read_bytes()
    original_hash = sha256_bytes(raw)
    marker = patch["marker"].encode("ascii")

    if marker in raw:
        if original_hash != patch["patched_sha256"]:
            raise SystemExit(
                f"ERROR: {patch['id']} marker present but {relpath} hash "
                f"{original_hash} != pinned patched {patch['patched_sha256']}"
            )
        return {
            "id": patch["id"],
            "file": relpath,
            "original_sha256": patch["original_sha256"],
            "patched_sha256": patch["patched_sha256"],
            "applied": False,
            "already_patched": True,
        }

    if original_hash != patch["original_sha256"]:
        raise SystemExit(
            f"ERROR: {patch['id']} {relpath} hash {original_hash} != pinned "
            f"original {patch['original_sha256']}"
        )

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SystemExit(f"ERROR: {patch['id']} {relpath} not UTF-8: {exc}")

    crlf = text.count("\r\n")
    lf = text.count("\n") - crlf
    style = "crlf" if crlf > 0 and lf == 0 else ("lf" if crlf == 0 else "mixed")
    if style == "mixed":
        raise SystemExit(f"ERROR: {patch['id']} {relpath} mixed newlines")
    work = text.replace("\r\n", "\n")
    newline = "\r\n" if style == "crlf" else "\n"

    anchors = []
    for edit in patch["edits"]:
        anchor = edit["anchor"]
        occurrences = work.count(anchor)
        if occurrences != 1:
            raise SystemExit(
                f"ERROR: {patch['id']} anchor {anchor!r} occurs "
                f"{occurrences}x in {relpath}, need exactly 1"
            )
        line_no = work[: work.find(anchor)].count("\n") + 1
        anchors.append({"anchor": anchor, "line": line_no})
        work = work.replace(anchor, edit["replacement"])

    # Single newline restoration at the end; replacements stay LF until here.
    patched_text = work.replace("\n", newline) if style == "crlf" else work
    patched_raw = patched_text.encode("utf-8")
    patched_hash = sha256_bytes(patched_raw)
    if derive:
        path.write_bytes(patched_raw)
        print(f"DERIVED {patch['id']} {relpath} sha256={patched_hash}")
        return {
            "id": patch["id"],
            "file": relpath,
            "original_sha256": patch["original_sha256"],
            "patched_sha256": patched_hash,
            "applied": True,
            "derived": True,
        }
    if patched_hash != patch["patched_sha256"]:
        raise SystemExit(
            f"ERROR: {patch['id']} {relpath} patched hash {patched_hash} != "
            f"pinned {patch['patched_sha256']}"
        )
    path.write_bytes(patched_raw)
    return {
        "id": patch["id"],
        "file": relpath,
        "original_sha256": patch["original_sha256"],
        "patched_sha256": patch["patched_sha256"],
        "applied": True,
        "already_patched": False,
        "newline_style": style,
        "anchors": anchors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--provenance-out", type=Path)
    parser.add_argument("--derive", action="store_true")
    args = parser.parse_args(argv)

    report = {
        "schema": 1,
        "sdk_root": str(args.sdk_root),
        "patches": [apply_patch(args.sdk_root, p, derive=args.derive) for p in PATCHES],
    }
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.provenance_out:
        args.provenance_out.write_text(text, encoding="utf-8")
    print(text, end="")
    print("GLSD301P_SDK_PATCHES_APPLIED=" + str(len(report["patches"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
