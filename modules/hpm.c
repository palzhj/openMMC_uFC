/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015-2016  Henrique Silva <henrique.silva@lnls.br>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

/* Project includes */
#include "ipmi.h"
#include "hpm.h"
#include "utils.h"
#include "string.h"
#include "led.h"
#include "payload.h"
#include "port.h"
#include "GitSHA1.h"

/* Local Variables */

/* Stores the code of the current or last completed long duration command */
static uint8_t cmd_in_progress;

/* Stores the completion code of the current or last completed long duration command */
static uint8_t last_cmd_cc;

/*Current component under upgrade */
static t_component* active_component = NULL;

/* Variable used to monitor HPM upload fw block command's block number */
static uint8_t expected_block_n;

/* IPMC Capabilities */
t_ipmc_capabilities ipmc_cap = {
    .flags = { .upgrade_undesirable = 0,
               .automatic_rollback_overridden = 0,
               .ipmc_degraded_in_update = 0,
               .deferred_activation = 1,
               .services_affected = 1,
               .manual_rollback = 1,
               .automatic_rollback = 1,
               .self_test = 1
    }
};

/* Components properties */
t_component hpm_components[HPM_MAX_COMPONENTS] = {
    [HPM_BOOTLOADER_COMPONENT_ID] = {
        .description = "Bootloader",
        .properties = {
            .flags = {
                .reserved = 0x00,
                .cold_reset_required = 1,
                .deferred_activation_supported = 0,
                .comparison_supported = 1,
                .preparation_support = 0,
                .rollback_backup_support = 0x01
            }
        },
        .hpm_prepare_comp_f = bootloader_hpm_prepare_comp,
        .hpm_upload_block_f = bootloader_hpm_upload_block,
        .hpm_finish_upload_f = bootloader_hpm_finish_upload,
        .hpm_get_upgrade_status_f = bootloader_hpm_get_upgrade_status,
        .hpm_activate_firmware_f = bootloader_hpm_activate_firmware
    },
    [HPM_IPMC_COMPONENT_ID] = {
        .description = "MMC",
        .properties = {
            .flags = {
                .reserved = 0x00,
                .cold_reset_required = 1,
                .deferred_activation_supported = 0,
                .comparison_supported = 1,
                .preparation_support = 0,
                .rollback_backup_support = 0x01
            }
        },
        .hpm_prepare_comp_f = ipmc_hpm_prepare_comp,
        .hpm_upload_block_f = ipmc_hpm_upload_block,
        .hpm_finish_upload_f = ipmc_hpm_finish_upload,
        .hpm_get_upgrade_status_f = ipmc_hpm_get_upgrade_status,
        .hpm_activate_firmware_f = ipmc_hpm_activate_firmware
    },
    [HPM_PAYLOAD_COMPONENT_ID] = {
        .description = "Payload",
        .properties = {
            .flags = {
                .reserved = 0x00,
                .cold_reset_required = 1,
                .deferred_activation_supported = 0,
                .comparison_supported = 1,
                .preparation_support = 1,
                .rollback_backup_support = 0x01
            }
        },
        .hpm_prepare_comp_f = payload_hpm_prepare_comp,
        .hpm_upload_block_f = payload_hpm_upload_block,
        .hpm_finish_upload_f = payload_hpm_finish_upload,
        .hpm_get_upgrade_status_f = payload_hpm_get_upgrade_status,
        .hpm_activate_firmware_f = payload_hpm_activate_firmware
    }
};

/*******************************************************/

void hpm_init( void )
{
    /* The component table (including the description strings) is fully
     * initialised at compile time; nothing to do here. */
}

/* HPM.1 requests carry the PICMG IANA (5A 31 00) before the command data;
 * some openMMC tools instead send a single group-extension byte. Return the
 * offset where the command arguments start for either form. */
static uint8_t hpm_arg_base(const ipmi_msg *req)
{
    if (req->data_len >= 3 && req->data[0] == 0x5A && req->data[1] == 0x31 && req->data[2] == 0x00) {
        return 3;
    }
    return 1;
}

IPMI_HANDLER(ipmi_picmg_get_upgrade_capabilities, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_GET_UPGRADE_CAPABILITIES, ipmi_msg *req, ipmi_msg* rsp)
{
    uint8_t len = rsp->data_len = 0;

    rsp->data[len++] = IPMI_PICMG_GRP_EXT;
    rsp->data[len++] = HPM_SUPPORTED_VERSION;
    rsp->data[len++] = ipmc_cap.byte;
    rsp->data[len++] = HPM_UPGRADE_TIMEOUT;
    rsp->data[len++] = HPM_SELF_TEST_TIMEOUT;
    rsp->data[len++] = HPM_ROLLBACK_TIMEOUT;
    rsp->data[len++] = HPM_INACCESSIBILITY_TIMEOUT;
    rsp->data[len++] = (1 << HPM_BOOTLOADER_COMPONENT_ID | 1 << HPM_IPMC_COMPONENT_ID | 1 << HPM_PAYLOAD_COMPONENT_ID);

    rsp->data_len = len;
    rsp->completion_code = IPMI_CC_OK;

    /* This is not a long-duration command, so we don't need to update neither cmd_in_progress nor last_cmd_cc variables */
}

/* The build stamps are baked in by CMake (modules/GitSHA1.c.in):
 *   g_GIT_TAG  = "v1.7.0-3-g155ac55"   g_GIT_SHA1 = "155ac5...f2d2"
 * HPM.1 encodes a firmware version in six bytes: major, minor, aux0..3.  Use
 * the tag's MAJOR.MINOR.PATCH for the first three and the first three SHA1
 * bytes as the build fingerprint, so an HPM tool can tell two builds apart. */
static uint8_t hpm_dec(const char **s)
{
    uint8_t value = 0;

    while (**s >= '0' && **s <= '9') {
        value = value * 10 + (**s - '0');
        (*s)++;
    }
    return value;
}

static uint8_t hpm_hex(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
}

static void hpm_fw_version(uint8_t comp_id, uint8_t rev[6])
{
    const char *tag = g_GIT_TAG;
    const char *sha = g_GIT_SHA1;
    uint8_t i;

    memset(rev, 0, 6);

    /* The MMC and the first-stage bootloader are flashed from the same build;
     * the payload's (FPGA) HPM version is not known to this controller. */
    if (comp_id == HPM_PAYLOAD_COMPONENT_ID) {
        return;
    }
    if (tag[0] != 'v') {
        return;                     /* no version information baked in */
    }

    tag++;
    rev[0] = hpm_dec(&tag);         /* major */
    if (*tag != '.') {
        memset(rev, 0, 6);
        return;
    }
    tag++;
    rev[1] = hpm_dec(&tag);         /* minor */
    if (*tag == '.') {
        tag++;
        rev[2] = hpm_dec(&tag);     /* patch -> aux0 */
    }

    for (i = 0; i < 3; i++) {       /* aux1..3: SHA1 prefix */
        rev[3 + i] = (uint8_t)((hpm_hex(sha[2 * i]) << 4) | hpm_hex(sha[2 * i + 1]));
    }
}

IPMI_HANDLER(ipmi_picmg_get_component_properties, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_GET_COMPONENT_PROPERTIES, ipmi_msg *req, ipmi_msg* rsp)
{
    uint8_t len = rsp->data_len = 0;

    uint8_t base = hpm_arg_base(req);
    uint8_t comp_id = req->data[base];
    uint8_t comp_properties_selector = req->data[base + 1];

    rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;

    if (comp_id > 7) {
        rsp->data[len++] = IPMI_PICMG_GRP_EXT;
        /* Return command-specific completion code: 0x82 (Invalid Component ID) */
        rsp->data_len = len;
        rsp->completion_code = 0x82;
        return;
    }

    rsp->data[len++] = IPMI_PICMG_GRP_EXT;

    switch (comp_properties_selector) {
    case 0x00:
        /* General component properties */
        rsp->data[len++] = hpm_components[comp_id].properties.byte;
        rsp->completion_code = IPMI_CC_OK;
        break;
    case 0x01:      /* Current firmware version */
    case 0x03:      /* Rollback firmware version (no separate image is kept) */
    case 0x04:      /* Deferred upgrade firmware version (none is kept) */
    {
        uint8_t rev[6];
        uint8_t i;

        hpm_fw_version(comp_id, rev);
        rev[0] &= 0x7F;             /* HPM.1: bit 7 of the major byte is reserved */
        for (i = 0; i < 6; i++) {
            rsp->data[len++] = rev[i];
        }
        rsp->completion_code = IPMI_CC_OK;
        break;
    }
    case 0x02:
        /* Description string */
        memcpy( &rsp->data[len], &hpm_components[comp_id].description[0], 12 );
        len += 12;
        rsp->completion_code = IPMI_CC_OK;
        break;
    default:
        /* Return command-specific completion code: 0x83 (Invalid Component properties selector) */
        rsp->completion_code = 0x83;
    }

    rsp->data_len = len;
    /* This is not a long-duration command, so we don't need to update neither cmd_in_progress nor last_cmd_cc variables */
}

IPMI_HANDLER(ipmi_picmg_initiate_upgrade_action, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_INITIATE_UPGRADE_ACTION, ipmi_msg *req, ipmi_msg* rsp)
{
    uint8_t len = rsp->data_len = 0;

    uint8_t comp_id;

    /* Set the component that'll be upgraded */
    /*
     * As specified in the Hardware Platform Management IPM Controller Firmware Upgrade Specification, Table 3-4,
     * treat the component selection as a bit field.
     */
    uint8_t base = hpm_arg_base(req);
    switch (req->data[base]) {
    case 0x01:
        comp_id = 0;
        break;
    case 0x02:
        comp_id = 1;
        break;
    case 0x04:
        comp_id = 2;
        break;
    default:
        /* Component ID out of range */
        rsp->data[len++] = IPMI_PICMG_GRP_EXT;
        /* Return command-specific completion code: 0x82 (Invalid Component ID) */
        rsp->data_len = len;
        rsp->completion_code = 0x82;
        return;
    }

    uint8_t upgrade_action = req->data[base + 1];
    expected_block_n = 0x00;
    rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
    active_component = &hpm_components[comp_id];
    rsp->data[len++] = IPMI_PICMG_GRP_EXT;

    switch (upgrade_action) {
    case 0x00:
        /* Backup component */
        break;
    case 0x01:
    case 0x02:
        /* Upload for upgrade */
        if (active_component->hpm_prepare_comp_f) {
            rsp->completion_code = active_component->hpm_prepare_comp_f();
        }
        break;
    case 0x03:
        /* Upload for compare */
        break;
    default:
        break;
    }

    rsp->data_len = len;

    /* This is a long-duration command, update both cmd_in_progress and last_cmd_cc */
    cmd_in_progress = req->cmd;
    last_cmd_cc = rsp->completion_code;
}

IPMI_HANDLER(ipmi_picmg_get_upgrade_status, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_GET_UPGRADE_STATUS, ipmi_msg *req, ipmi_msg* rsp)
{
    uint8_t len = rsp->data_len = 0;

    if (active_component->hpm_get_upgrade_status_f) {
        /* WARNING: This function can't block! */
        last_cmd_cc = active_component->hpm_get_upgrade_status_f();
    } else {
        /* Returning IPMI_CC_OK for debug purposes only, should be IPMI_CC_UNSPECIFIED_ERROR */
        last_cmd_cc = IPMI_CC_OK;
    }

    rsp->data[len++] = IPMI_PICMG_GRP_EXT;
    rsp->data[len++] = cmd_in_progress;
    rsp->data[len++] = last_cmd_cc;
    rsp->data_len = len;
    rsp->completion_code = IPMI_CC_OK;

    /* This is not a long-duration command, so we don't need to update neither cmd_in_progress nor last_cmd_cc variables */
}

IPMI_HANDLER(ipmi_picmg_query_self_results, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_QUERY_SELF_RESULTS,
             ipmi_msg *req, ipmi_msg *rsp)
{
    uint8_t len = rsp->data_len = 0;
    (void)req;

    /* The MMC has no self-test phase: report success (0x55) so HPM clients
     * that poll the result after activating accept the new image. */
    rsp->data[len++] = IPMI_PICMG_GRP_EXT;
    rsp->data[len++] = 0x55;    /* self-test passed */
    rsp->data[len++] = 0x00;
    rsp->data_len = len;
    rsp->completion_code = IPMI_CC_OK;
}

IPMI_HANDLER(ipmi_picmg_query_rollback_status, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_QUERY_ROLLBACK_STATUS,
             ipmi_msg *req, ipmi_msg *rsp)
{
    uint8_t len = rsp->data_len = 0;
    (void)req;

    /* No rollback support: report "no component needs a rollback". */
    rsp->data[len++] = IPMI_PICMG_GRP_EXT;
    rsp->data[len++] = 0x00;
    rsp->data_len = len;
    rsp->completion_code = IPMI_CC_OK;
}

IPMI_HANDLER(ipmi_picmg_abort_firmware_upgrade, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_ABORT_FIRMWARE_UPGRADE, ipmi_msg *req, ipmi_msg* rsp)
{
    uint8_t len = rsp->data_len = 0;

    rsp->data[len++] = IPMI_PICMG_GRP_EXT;

    rsp->data_len = len;
    rsp->completion_code = IPMI_CC_OK;
    /* This is not a long-duration command, so we don't need to update neither cmd_in_progress nor last_cmd_cc variables */
}

IPMI_HANDLER(ipmi_picmg_upload_firmware_block, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_UPLOAD_FIRMWARE_BLOCK, ipmi_msg *req, ipmi_msg* rsp)
{
    uint8_t len = rsp->data_len = 0;
    uint8_t block_data[HPM_BLOCK_SIZE];
    uint8_t base = hpm_arg_base(req);
    uint8_t block_sz = req->data_len - base - 1;

    if(block_sz > HPM_BLOCK_SIZE){
       rsp->data_len = len;
       rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
       return;
    }

    if (active_component == NULL) {
        /* Component ID out of range */
        rsp->data[len++] = IPMI_PICMG_GRP_EXT;
        /* Return command-specific completion code: 0x81 (Invalid Component ID) */
        rsp->data_len = len;
        rsp->completion_code = 0x81;
        return;
    }

    memcpy(&block_data[0], &req->data[base + 1], block_sz);

    /* TODO: perform checksum of the block */

    if (active_component->hpm_upload_block_f) {
        /* WARNING: This function can't block! */
        /* data[base] holds the block number */
        if(req->data[base] == expected_block_n) {
               uint8_t cc = active_component->hpm_upload_block_f(&block_data[0], block_sz);
               rsp->completion_code = cc;
               /* Only advance on success: a failed block (flash program
                * error) must be retried by the uploader, not silently
                * skipped - otherwise the rest of the image is lost. */
               if (cc == IPMI_CC_OK || cc == IPMI_CC_COMMAND_IN_PROGRESS) {
                   expected_block_n++;
               }
        } else {	/* Repeated block, ignore it */
               rsp->completion_code = IPMI_CC_OK;
        }
    } else {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
    }

    rsp->data[len++] = IPMI_PICMG_GRP_EXT;
    rsp->data_len = len;

    /* This is a long-duration command, update both cmd_in_progress and last_cmd_cc */
    cmd_in_progress = req->cmd;
    last_cmd_cc = rsp->completion_code;
}

IPMI_HANDLER(ipmi_picmg_finish_firmware_upload, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_FINISH_FIRMWARE_UPLOAD, ipmi_msg *req, ipmi_msg* rsp)
{
    uint8_t len = rsp->data_len = 0;

    uint8_t base = hpm_arg_base(req);
    /* component ID at data[base], image length at data[base+1 .. base+4] */
    uint32_t image_len = (req->data[base + 4] << 24) | (req->data[base + 3] << 16) |
                         (req->data[base + 2] << 8) | (req->data[base + 1]);

    /* TODO: implement HPM.1 REQ3.59 */

    if ( active_component->hpm_finish_upload_f) {
        rsp->completion_code = active_component->hpm_finish_upload_f( image_len );
    } else {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
    }

    rsp->data[len++] = IPMI_PICMG_GRP_EXT;

    rsp->data_len = len;

    /* This is a long-duration command, update both cmd_in_progress and last_cmd_cc */
    cmd_in_progress = req->cmd;
    last_cmd_cc = rsp->completion_code;
}

IPMI_HANDLER(ipmi_picmg_activate_firmware, NETFN_GRPEXT, IPMI_PICMG_CMD_HPM_ACTIVATE_FIRMWARE, ipmi_msg *req, ipmi_msg* rsp)
{
    uint8_t len = rsp->data_len = 0;

    /* TODO: Compare firmware revisions before activating */

    if (active_component->hpm_activate_firmware_f) {
        rsp->completion_code = active_component->hpm_activate_firmware_f();
    } else {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
    }

    rsp->data[len++] = IPMI_PICMG_GRP_EXT;

    rsp->data_len = len;

    /* This is a long-duration command, update both cmd_in_progress and last_cmd_cc */
    cmd_in_progress = req->cmd;
    last_cmd_cc = rsp->completion_code;
}
