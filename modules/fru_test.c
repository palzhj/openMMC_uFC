/*
 * RENODE_TEST only: FRU write/read self-test with a RAM-backed EEPROM.
 *
 * This exercises the real IPMI Write/Read FRU Data handlers and the
 * fru_write()/fru_read() size-growth logic.  It does NOT test the I2C
 * transport (Renode's STM32F103 platform has no TCA9548/24AA025E48 model).
 */

#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ipmi.h"
#include "ipmb.h"
#include "fru.h"
#include "utils.h"

extern fru_data_t fru[FRU_COUNT];

#define FRU_TEST_SIZE       122
#define FRU_TEST_CHUNK      16

static uint8_t test_eeprom[FRU_MAX_SIZE];

/* 0 = fail/not-run, 1 = pass.  Readable from GDB. */
volatile uint8_t fru_test_result = 0;

size_t fru_test_read( uint8_t id, uint16_t address, uint8_t *buffer, size_t len, uint32_t timeout )
{
    (void)id;
    (void)timeout;

    if ( ( address + len ) > sizeof(test_eeprom) ) {
        return 0;
    }
    memcpy( buffer, &test_eeprom[address], len );
    return len;
}

size_t fru_test_write( uint8_t id, uint16_t address, uint8_t *buffer, size_t len, uint32_t timeout )
{
    (void)id;
    (void)timeout;

    if ( ( address + len ) > sizeof(test_eeprom) ) {
        return 0;
    }
    memcpy( &test_eeprom[address], buffer, len );
    return len;
}

/* Defined by the IPMI_HANDLER macro in fru.c. */
void ipmi_handler_NETFN_STORAGE__IPMI_WRITE_FRU_DATA_CMD_f( ipmi_msg *req, ipmi_msg *rsp );
void ipmi_handler_NETFN_STORAGE__IPMI_READ_FRU_DATA_CMD_f( ipmi_msg *req, ipmi_msg *rsp );

static uint8_t fru_test_write_chunk( uint16_t offset, const uint8_t *data, uint8_t n )
{
    ipmi_msg req;
    ipmi_msg rsp;

    memset( &req, 0, sizeof(req) );
    memset( &rsp, 0, sizeof(rsp) );

    req.data[0] = FRU_AMC;
    req.data[1] = offset & 0xFF;
    req.data[2] = ( offset >> 8 ) & 0xFF;
    memcpy( &req.data[3], data, n );
    req.data_len = 3 + n;

    ipmi_handler_NETFN_STORAGE__IPMI_WRITE_FRU_DATA_CMD_f( &req, &rsp );

    if ( rsp.completion_code != IPMI_CC_OK ) {
        return 0;
    }
    return rsp.data[0];   /* count + 1 */
}

static uint8_t fru_test_read_chunk( uint16_t offset, uint8_t *data, uint8_t n )
{
    ipmi_msg req;
    ipmi_msg rsp;

    memset( &req, 0, sizeof(req) );
    memset( &rsp, 0, sizeof(rsp) );

    req.data[0] = FRU_AMC;
    req.data[1] = offset & 0xFF;
    req.data[2] = ( offset >> 8 ) & 0xFF;
    req.data[3] = n;
    req.data_len = 4;

    ipmi_handler_NETFN_STORAGE__IPMI_READ_FRU_DATA_CMD_f( &req, &rsp );

    if ( rsp.completion_code != IPMI_CC_OK ) {
        return 0;
    }
    if ( rsp.data[0] > n ) {
        return 0;
    }
    memcpy( data, &rsp.data[1], rsp.data[0] );
    return rsp.data[0];
}

void fru_test_run( void )
{
    uint8_t image[FRU_TEST_SIZE];
    uint8_t readback[FRU_TEST_SIZE];
    uint16_t offset;
    uint8_t n;

    fru_test_result = 0;

    for ( uint16_t i = 0; i < FRU_TEST_SIZE; i++ ) {
        image[i] = (uint8_t)( i ^ 0x5A );
    }

    /* 1. Write the image through the real IPMI write handler. */
    for ( offset = 0; offset < FRU_TEST_SIZE; offset += FRU_TEST_CHUNK ) {
        n = (uint8_t)( ( FRU_TEST_SIZE - offset ) > FRU_TEST_CHUNK ? FRU_TEST_CHUNK : ( FRU_TEST_SIZE - offset ) );
        if ( fru_test_write_chunk( offset, &image[offset], n ) != (uint8_t)( n + 1 ) ) {
            printf( "FRU TEST: write failed at %u\n", (unsigned)offset );
            return;
        }
    }

    if ( fru[FRU_AMC].fru_size != FRU_TEST_SIZE ) {
        printf( "FRU TEST: size %u != %u\n", (unsigned)fru[FRU_AMC].fru_size, FRU_TEST_SIZE );
        return;
    }

    /* 2. Read it back through the real IPMI read handler and compare. */
    for ( offset = 0; offset < FRU_TEST_SIZE; offset += FRU_TEST_CHUNK ) {
        n = (uint8_t)( ( FRU_TEST_SIZE - offset ) > FRU_TEST_CHUNK ? FRU_TEST_CHUNK : ( FRU_TEST_SIZE - offset ) );
        if ( fru_test_read_chunk( offset, &readback[offset], n ) != n ) {
            printf( "FRU TEST: read failed at %u\n", (unsigned)offset );
            return;
        }
    }
    if ( memcmp( image, readback, FRU_TEST_SIZE ) != 0 ) {
        printf( "FRU TEST: data mismatch\n" );
        return;
    }

    /* 3. A write past FRU_MAX_SIZE must be rejected. */
    if ( fru_test_write_chunk( FRU_MAX_SIZE - 4, &image[0], 8 ) != 0 ) {
        printf( "FRU TEST: out-of-range write was not rejected\n" );
        return;
    }

    fru_test_result = 1;
    printf( "FRU TEST: PASS (%u bytes)\n", FRU_TEST_SIZE );
}
