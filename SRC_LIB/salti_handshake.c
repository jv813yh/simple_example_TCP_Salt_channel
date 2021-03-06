/**
 * @file salti_handshake.c
 *
 *
 * Description
 *
 */

/*======= Includes ==========================================================*/

/* C Library includes */
#include <string.h> /* memcpy, memset */


/* Salt library includes */
#include "salti_handshake.h"

/*======= Local Macro Definitions ===========================================*/

/* A1 Message defines */
#define A1_ADDRESSTYPE_ANY                      (0x00U)
#define A1_ADDRESSTYPE_ANY_SIZE                 (0x05U)
#define A1_ADDRESSTYPE_ED25519                  (0x01U)
#define A1_ADDRESSTYPE_ED25519_SIZE             (37U)


/* M1 Message defines */
#define SALT_M1_SIZE_NO_SIG                     (42U)
#define SALT_M1_SIZE_WITH_SIG                   (74U)
#define SALT_M1_MAX_SIZE                        SALT_M1_SIZE_WITH_SIG
#define SALT_M1_HEADER_VALUE                    (0x01U)
#define SALT_M1_SIG_KEY_INCLUDED_FLAG           (0x01U)

/* M2 Message defines */
#define SALT_M2_SIZE_OFFSET                     (0U)
#define SALT_M2_HEADER0_OFFSET                  (SALT_LENGTH_SIZE)
#define SALT_M2_HEADER1_OFFSET                  (SALT_LENGTH_SIZE + 1U)
#define SALT_M2_SIZE                            (38U)
#define SALT_M2_HEADER_VALUE                    (0x02U)
#define SALT_M2_ENC_KEY_INCLUDED_FLAG           (0x10U)
#define SALT_M2_RESUME_SUPPORTED_FLAG           (0x20U)
#define SALT_M2_TIME_OFFSET                     (SALT_LENGTH_SIZE + 2U)
#define SALT_M2_PUB_ENC_OFFSET                  (SALT_LENGTH_SIZE + 6U)

#define SALT_M2_HOST_OFFSET                     (200U)
#define SALT_HOST_TMP_PEER_EK_PUB_OFFSET        (242U)

/* M3 Message defines */
#define SALT_M3M4_WRAPPED_SIZE                  (120U)
#define SALT_M3M4_CLEAR_SIZE                    (96U)
#define SALT_M3_HEADER_VALUE                    (0x03U)
#define SALT_M3_SIG_KEY_INCLUDED_FLAG           (0x10U)

#define SALT_M3M4_MSG_TO_SIG_SIZE               (136U)
#define SALT_M3M4_SIGNED_MSG_SIZE               (200U)

#define SALT_M3_HOST_CLEAR_OFFSET               (238U)
#define SALT_M3_HOST_WRAPPED_OFFSET             (SALT_M3_HOST_CLEAR_OFFSET - SALT_WRAP_OVERHEAD_SIZE)
#define SALT_M3_CLIENT_IO_WRAPPED_OFFSET        (214U)
#define SALT_M3_CLIENT_WRAPPED_OFFSET           (200U)

/* M4 Message defines */
#define SALT_M4_HEADER_VALUE                    (0x04U)
#define SALT_M4_HOST_IO_WRAPPED_OFFSET          (214U)
#define SALT_M4_HOST_WRAPPED_OFFSET             (200U)
#define SALT_M4_CLIENT_CLEAR_OFFSET             (400U)
#define SALT_M4_CLIENT_IO_WRAPPED_OFFSET        (SALT_M4_CLIENT_CLEAR_OFFSET - SALT_WRAP_OVERHEAD_SIZE)

#define SALT_M3M4_SIG_VERIFY_OFFSET             (200U)

#define SALT_PUB_ENC_OFFSET                     (0U)
#define SALT_SEC_ENC_OFFSET                     (32U)
#define SALT_SIG_PREFIX_OFFSET                  (64U)
#define SALT_SIG_PREFIX_SIZE                    (8U)
#define SALT_M1_HASH_OFFSET                     (72U)
#define SALT_M2_HASH_OFFSET                     (136U)

#define SALT_PROTOCOL_INDICATOR                 "SCv2"
#define SALT_PROTOCOL_INDICATOR_SIZE            (4U)

/*======= Type Definitions ==================================================*/
/*======= Local variable declarations =======================================*/

/* Signature 1 prefix, ASCII "SC-SIG01" */
static uint8_t sig1prefix[8] = { 0x53, 0x43, 0x2d, 0x53, 0x49, 0x47, 0x30, 0x31 };
/* Signature 2 prefix, ASCII "SC-SIG02" */
static uint8_t sig2prefix[8] = { 0x53, 0x43, 0x2d, 0x53, 0x49, 0x47, 0x30, 0x32 };

/*======= Local function prototypes =========================================*/
/*======= Global function implementations ===================================*/
/*======= Local function implementations ====================================*/

salt_ret_t salti_handshake_server(salt_channel_t *p_channel, const uint8_t *p_with)
{

    uint32_t size = 0;
    salt_ret_t ret_code = SALT_ERROR;
    uint8_t proceed = 1;
    uint8_t *payload = NULL;

    while (proceed) {
        proceed = 0;
        switch (p_channel->state) {
            case SALT_SESSION_INITIATED:
                /*
                 * buffer = { ek_pub[32] || ek_sec[32] }
                 */
                p_channel->state = SALT_M1_IO;
                proceed = 1;
                break;
            case SALT_M1_IO:
                size = SALT_M1_MAX_SIZE; /* Maximum size of M1 */
                ret_code = salti_io_read(p_channel,
                                         &p_channel->hdshk_buffer[SALT_M1_HASH_OFFSET],
                                         &size);
                if (SALT_SUCCESS == ret_code) {

                    payload = &p_channel->hdshk_buffer[SALT_M1_HASH_OFFSET];

                    /* Smallest size for A1 is 5 bytes. */
                    SALT_VERIFY(5U <= size, SALT_ERR_BAD_PROTOCOL);

                    /* Check if this is an A2 request. */
                    if (SALT_A1_HEADER == payload[0] && 0x00U == payload[1]) {
                        p_channel->state = SALT_A1_HANDLE;
                    }
                    else {
                        /* Otherwise try to handle M1 */
                        p_channel->state = SALT_M1_HANDLE;
                    }

                    proceed = 1;

                }
                break;
            case SALT_A1_HANDLE:

                /*
                 * salti_handle_a1_create_a2 handles A1, creates A2
                 * and points p_channel->write_channel.p_data to A2
                 * with size in p_channel->write_channel.size.
                 */
                SALT_VERIFY(NULL != payload, SALT_ERR_INVALID_STATE);
                ret_code = salti_handle_a1_create_a2(p_channel, payload, size);

                if (SALT_SUCCESS == ret_code) {
                    p_channel->state = SALT_A2_IO;
                    proceed = 1;
                }

                break;
            case SALT_A2_IO:
                ret_code = salti_io_write(p_channel,
                                          p_channel->write_channel.p_data,
                                          p_channel->write_channel.size);
                if (SALT_SUCCESS == ret_code) {
                    ret_code = SALT_PENDING;
                    /*
                     * We can restart handshake after this without a new
                     * initialization.
                     */
                    p_channel->state = SALT_SESSION_INITIATED;
                }
                break;
            case SALT_M1_HANDLE:

                /*
                 * If an invalid sig key was included in M1 salti_handle_m1
                 * will set p_channel->err_code = SALT_ERR_NO_SUCH_SERVER.
                 * Then we will create M2 with that flag and last flag.
                 * After this the session is considered closed.
                 *
                 * The hash of M1 is calculated on the same location where M1 is located.
                 * buffer = { e_keyPair[64] || reservedForSigPrefix[8] || m1[42 or 74] || ... }
                 *
                 * We also now know that we need 42 bytes for m2 (38 + 4 size bytes).
                 * handle_m1 copies the clients public key to &buffer[242]
                 *
                 * After handle m1:
                 *
                 * buffer = {
                 *  e_keyPair[64] ||
                 *  reservedForSigPrefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  reservedForm2WithSize[42] ||
                 *  clientEkPub[32] || ...
                 * }
                 *
                 */

                p_channel->state = salti_handle_m1(p_channel,
                                                   &p_channel->hdshk_buffer[SALT_M1_HASH_OFFSET],
                                                   size,
                                                   &p_channel->hdshk_buffer[SALT_M1_HASH_OFFSET]);
                proceed = 1;
                break;
            case SALT_M2_INIT_NO_SUCH_SERVER:
            case SALT_M2_INIT:
                /*
                 *
                 * buffer = {
                 *  e_keyPair[64] ||
                 *  reservedForSigPrefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  m2WithSize[42] ||
                 *  clientEkPub[32] || ...
                 * }
                 *
                 */
                p_channel->state = salti_create_m2(p_channel,
                                                   &p_channel->hdshk_buffer[SALT_M2_HOST_OFFSET],
                                                   &size,
                                                   &p_channel->hdshk_buffer[SALT_M2_HASH_OFFSET]);
                salti_get_time(p_channel, &p_channel->my_epoch);
                proceed = 1;
                break;

            case SALT_M2_IO_AND_SESSION_KEY:

                /*
                 * We initiate the I/O. If the I/O is slow, we can calculate the
                 * common session key during the I/O.
                 */
                ret_code = salti_io_write(p_channel,
                                          &p_channel->hdshk_buffer[SALT_M2_HOST_OFFSET],
                                          size);

                SALT_VERIFY(SALT_ERROR != ret_code, SALT_ERR_IO_WRITE);

                if (SALT_ERROR != ret_code) {

                    /*
                     *
                     * buffer = {
                     *  e_keyPair[64] ||
                     *  reservedForSigPrefix[8] ||
                     *  m1Hash[64] ||
                     *  m2Hash[64] ||
                     *  m2WithSize[42] ||
                     *  clientEkPub[32] || ...
                     * }
                     *
                     */
                    int crypto_ret = api_crypto_box_beforenm(p_channel->ek_common,
                                                             &p_channel->hdshk_buffer[SALT_HOST_TMP_PEER_EK_PUB_OFFSET],
                                                             &p_channel->hdshk_buffer[SALT_SEC_ENC_OFFSET]);

                    SALT_VERIFY(0 == crypto_ret, SALT_ERR_CRYPTO_API);

                    p_channel->state = SALT_M2_IO;

                }

                if (SALT_SUCCESS == ret_code) {
                    proceed = 1;
                    p_channel->state = SALT_M3_INIT;
                }

                break;
            case SALT_M2_IO:
                ret_code = salti_io_write(p_channel,
                                          &p_channel->hdshk_buffer[SALT_M2_HOST_OFFSET],
                                          size);

                if (SALT_SUCCESS == ret_code) {
                    SALT_VERIFY(p_channel->err_code == SALT_ERR_NONE, p_channel->err_code);
                    p_channel->state = SALT_M3_INIT;
                    proceed = 1;
                }
                break;
            case SALT_M3_INIT:

                ret_code = salti_create_m3m4_sig(p_channel,
                                                 &p_channel->hdshk_buffer[SALT_M3_HOST_CLEAR_OFFSET],
                                                 &size);
                SALT_VERIFY(SALT_SUCCESS == ret_code, p_channel->err_code);
                /*
                 *
                 * buffer = {
                 *  m3Signature[64] ||
                 *  sig1Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  reservedForWrapping[38] ||
                 *  m3Clear[96] || ...
                 * }
                 *
                 */

                ret_code = salti_wrap(p_channel,
                                      &p_channel->hdshk_buffer[SALT_M3_HOST_WRAPPED_OFFSET],
                                      size,
                                      SALT_M3_HEADER_VALUE,
                                      &p_channel->write_channel.p_data,
                                      &p_channel->write_channel.size, false);

                /*
                 *
                 * buffer = {
                 *  m3Signature[64] ||
                 *  sig1Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  zeroPadded[10] ||
                 *  m3EncryptedAndWrappedWithSize[124] || ...
                 * }
                 *
                 */

                if (SALT_SUCCESS == ret_code) {
                    p_channel->state = SALT_M3_IO;
                    proceed = 1;
                }

                break;
            case SALT_M3_IO:
                ret_code = salti_io_write(p_channel,
                                          p_channel->write_channel.p_data,
                                          p_channel->write_channel.size);
                if (SALT_SUCCESS == ret_code) {
                    p_channel->state = SALT_M4_IO;
                    proceed = 1;
                }
                break;
            case SALT_M4_IO:
                size = SALT_M3M4_WRAPPED_SIZE;

                ret_code = salti_io_read(p_channel,
                                         &p_channel->hdshk_buffer[SALT_M4_HOST_IO_WRAPPED_OFFSET],
                                         &size);

                /*
                 *
                 * buffer = {
                 *  m3Signature[64] ||
                 *  sig1Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  reservedForUnwrapping[14] ||
                 *  m4EncryptedAndWrappedWithoutSize[120] || ...
                 * }
                 *
                 */

                if (SALT_SUCCESS == ret_code) {
                    p_channel->state = SALT_M4_HANDLE;
                    proceed = 1;
                }
                break;
            case SALT_M4_HANDLE:

                SALT_VERIFY(SALT_M3M4_WRAPPED_SIZE == size, SALT_ERR_BAD_PROTOCOL);

                uint8_t *header;

                ret_code = salti_unwrap(p_channel,
                                        &p_channel->hdshk_buffer[SALT_M4_HOST_WRAPPED_OFFSET],
                                        size,
                                        &header,
                                        &p_channel->write_channel.p_data,
                                        &p_channel->write_channel.size);
                /*
                 *
                 * buffer = {
                 *  m3Signature[64] ||
                 *  sig1Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  zeroPadded[32] ||
                 *  header[2] ||
                 *  time[4] ||
                 *  m4Clear[96] || ...
                 * }
                 *
                 */

                SALT_VERIFY(SALT_SUCCESS == ret_code, p_channel->err_code);
                SALT_VERIFY(SALT_M4_HEADER_VALUE == header[0], SALT_ERR_BAD_PROTOCOL);

                ret_code = salti_verify_m3m4_sig(p_channel,
                                                 p_channel->write_channel.p_data,
                                                 p_channel->write_channel.size);

                 /*
                 * buffer = {
                 *  m4Signature[64] ||
                 *  sig2Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  neededWhenVerifying[64] ||
                 * }
                 */

                if (SALT_SUCCESS == ret_code) {
                    /*
                     * If an expected public key of the peer is procided, check
                     * that this matches the from the one authenticated in M4.
                     */
                    if (p_with != NULL) {
                        SALT_VERIFY(memcmp(p_with, p_channel->peer_sk_pub, 32) == 0,
                                    SALT_ERR_BAD_PEER);
                    }

                    p_channel->state = SALT_SESSION_ESTABLISHED;
                }

                break;
            case SALT_ERROR_STATE:
            default:
                return SALT_ERROR;
        }
    }

    return ret_code;
}

salt_ret_t salti_handshake_client(salt_channel_t *p_channel, const uint8_t *p_with)
{
    uint32_t size = 0;
    salt_ret_t ret_code = SALT_ERROR;
    uint8_t proceed = 1;

    while (proceed) {
        proceed = 0;
        switch (p_channel->state) {
            case SALT_SESSION_INITIATED:
                /*
                 * Create the M1 message at hdshk_buffer[128] and save the hash at
                 * p_channel->hdshk_buffer[64] (64 bytes). We save the hash so we later
                 * can verify that the message M1 was not modified by a MITM. No
                 * support for virtual server yet, so the size of M1 is always 42
                 * bytes.
                 *
                 * buffer = { ek_pub[32] || ek_sec[32] }
                 *
                 */

                ret_code = salti_create_m1(p_channel,
                                           &p_channel->hdshk_buffer[SALT_M2_HASH_OFFSET],
                                           &size,
                                           &p_channel->hdshk_buffer[SALT_M1_HASH_OFFSET],
                                           p_with);
                /* If hash calculation fails due to crypto API error, stop. */
                SALT_VERIFY(SALT_SUCCESS == ret_code, p_channel->err_code);
                /*
                 *
                 * buffer = {
                 *  ek_pub[32] ||
                 *  ek_sec[32] ||
                 *  reservedForSigPrefix[8] ||
                 *  m1Hash[64] ||
                 *  m1WithSize[46 or 78] || ...
                 * }
                 */


                salti_get_time(p_channel, &p_channel->my_epoch);
                p_channel->state = SALT_M1_IO;
                proceed = 1;
                break;
            case SALT_M1_IO:


                ret_code = salti_io_write(p_channel,
                                          &p_channel->hdshk_buffer[SALT_M2_HASH_OFFSET],
                                          size);

                if (SALT_SUCCESS == ret_code) {
                    p_channel->state = SALT_M2_IO;
                    proceed = 1;
                }
                break;
            case SALT_M2_IO:
                /*
                 * Read the M2 message to hdshk_buffer[128]. If the message is OK the
                 * hash is saved to hdshk_buffer[64]. Now we have the hashes of M1
                 * and M2 in hdshk_buffer[0:127]. The only valid size of M2 is 38 bytes.
                 */
                size = SALT_M2_SIZE;

                ret_code = salti_io_read(p_channel,
                                         &p_channel->hdshk_buffer[SALT_M2_HASH_OFFSET],
                                         &size);

                if (SALT_SUCCESS == ret_code) {
                    p_channel->state = SALT_M2_HANDLE;
                    proceed = 1;
                }
                break;
            case SALT_M2_HANDLE:

                /*
                 *
                 * buffer = {
                 *  ek_pub[32] ||
                 *  ek_sec[32] ||
                 *  reservedForSigPrefix[8] ||
                 *  m1Hash[64] ||
                 *  m2[38] || ...
                 * }
                 */

                p_channel->state = salti_handle_m2(p_channel,
                                                   &p_channel->hdshk_buffer[SALT_M2_HASH_OFFSET],
                                                   size, &p_channel->hdshk_buffer[SALT_M2_HASH_OFFSET]);
                /*
                 * buffer = {
                 *  ek_pub[32] ||
                 *  ek_sec[32] ||
                 *  reservedForSigPrefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] || ...
                 * }
                 */

                proceed = 1;
                break;
            case SALT_M3_INIT:

                /*
                 * While the server calculates / sends the M3 message we can create
                 * the M4 message. This is good if the I/O channel is slow. Much of
                 * the time will be waiting for I/O. The M4 message is encrypted and
                 * the crypto library API requires that the first 32 bytes of the
                 * clear text message is 0x00. Also 16 bytes HMAC are added for
                 * authentication of the message. The size of the clear text M4 msg
                 * is 102 bytes.
                 *
                 * Further, the API for signing a msg
                 * will take a message m[n] and create a signed message sm[n+64].
                 * Thus, we need a sligther larger buffer for creating the msg:
                 * 1. M4 = { header[2] , timestamp[4] , pubSigKey[32] }
                 * 2. Sign the hashes of M1 and M2 and put the signed message at
                 *    the end of M4:
                 *    M4[230] = { header[2] , timestamp[4] , pubSigKey[32] , signedMsg[192] }
                 *    where signedMsg[192] = { sig[64] , M1Hash[64] , M2Hash[64] }.
                 *    The peer however can will calculate these hashes so we
                 *    don't send them. I.e.:
                 *    M4[102] = { header[2] , timestamp[4] , pubSigKey[32] , sig[64] }
                 *
                 *
                 */
                salti_create_m3m4_sig(p_channel,
                                      &p_channel->hdshk_buffer[SALT_M4_CLIENT_CLEAR_OFFSET],
                                      &p_channel->write_channel.size);

                /*
                 * buffer = {
                 *  m4Signature[64] ||
                 *  sig2Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  reservedForM4[200] ||
                 *  m4Clear[96] || ...
                 * }
                 */

                p_channel->state = SALT_M3_IO;
                proceed = 1;
                break;

            case SALT_M3_IO:

                size = 120; /* Maximum size of M3 */

                ret_code = salti_io_read(p_channel,
                                         &p_channel->hdshk_buffer[SALT_M3_CLIENT_IO_WRAPPED_OFFSET],
                                         &size);
                if (SALT_SUCCESS == ret_code) {

                    /*
                     *
                     * buffer = {
                     *  m3Signature[64] ||
                     *  sig1Prefix[8] ||
                     *  m1Hash[64] ||
                     *  m2Hash[64] ||
                     *  reservedForUnwrapping[14] ||
                     *  m3EncryptedAndWrappedWithoutSize[120] ||
                     *  notUsed[66] ||
                     *  m4Clear[96] || ...
                     *
                     * }
                     *
                     */

                    p_channel->state = SALT_M3_HANDLE;
                    proceed = 1;
                }
                break;
            case SALT_M3_HANDLE:

                /* Wrapped and encrypted M3 MUST be 120 bytes long. */
                SALT_VERIFY(size == SALT_M3M4_WRAPPED_SIZE, SALT_ERR_BAD_PROTOCOL);
                uint8_t *header;

                ret_code = salti_unwrap(p_channel,
                                        &p_channel->hdshk_buffer[SALT_M3_CLIENT_WRAPPED_OFFSET],
                                        size,
                                        &header,
                                        &p_channel->read_channel.p_data,
                                        &p_channel->read_channel.size);

                /*
                 * buffer = {
                 *  m4Signature[64] ||
                 *  sig2Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  zeroPadded[32] ||
                 *  header[2] ||
                 *  time[4] ||
                 *  m3Clear[96] ||
                 *  notUsed[66] ||
                 *  m4Clear[96] || ...
                 * }
                 */

                SALT_VERIFY(SALT_SUCCESS == ret_code, p_channel->err_code);
                SALT_VERIFY(SALT_M3_HEADER_VALUE == header[0], SALT_ERR_BAD_PROTOCOL);

                ret_code = salti_verify_m3m4_sig(p_channel,
                                                 p_channel->read_channel.p_data,
                                                 p_channel->read_channel.size);

                /*
                 * buffer = {
                 *  m4Signature[64] ||
                 *  sig2Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  sig2Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  neededWhenVerifying[64] ||
                 *  m4Clear[96] || ...
                 * }
                 */

                SALT_VERIFY(SALT_SUCCESS == ret_code, p_channel->err_code);

                if (p_with != NULL) {
                    SALT_VERIFY(memcmp(p_with, p_channel->peer_sk_pub, 32) == 0,
                                SALT_ERR_BAD_PEER);
                }
                p_channel->state = SALT_M4_WRAP;
                proceed = 1;

                break;
            case SALT_M4_WRAP:
                /*
                 * Clear text M3 was previous created in p_channel->hdshk_buffer[406].
                 * The wrapping requires 38 bytes overhead and the clear text buffer
                 * must be placed in 38 bytes offset.
                 */
                ret_code = salti_wrap(p_channel,
                                      &p_channel->hdshk_buffer[SALT_M4_CLIENT_IO_WRAPPED_OFFSET],
                                      p_channel->write_channel.size,
                                      SALT_M4_HEADER_VALUE,
                                      &p_channel->write_channel.p_data,
                                      &p_channel->write_channel.size, false);

                /*
                 * buffer = {
                 *  m4Signature[64] ||
                 *  sig2Prefix[8] ||
                 *  m1Hash[64] ||
                 *  m2Hash[64] ||
                 *  unused[72] ||
                 *  m4EncryptedAndWrappedWithSize[124] || ...
                 * }
                 */

                SALT_VERIFY(SALT_SUCCESS == ret_code, p_channel->err_code);

                p_channel->state = SALT_M4_IO;
                proceed = 1;
                break;
            case SALT_M4_IO:

                ret_code = salti_io_write(p_channel,
                                          p_channel->write_channel.p_data,
                                          p_channel->write_channel.size);

                if (SALT_SUCCESS == ret_code) {
                    p_channel->state = SALT_SESSION_ESTABLISHED;
                }
                break;
            case SALT_ERROR_STATE:
            default:
                return SALT_ERROR;
        }
    }

    return ret_code;
}

/**
 * @brief Handles A1 in p_data with size *size.
 *
 */
salt_ret_t salti_handle_a1_create_a2(salt_channel_t *p_channel,
                                     uint8_t *p_data,
                                     uint32_t size)
{
    /*
     * AddressType in p_data[2], must be 1 for ed25519 pub key, 0 for any.
     */
    if (A1_ADDRESSTYPE_ANY == p_data[2]) {

        /*
         * If AddressType == any, the size of A1 MUST be 5 bytes:
         * A1 = { header[2] , AddressType[1] , size[2] }
         * And size == { 0x00, 0x00 }
         */

        SALT_VERIFY(A1_ADDRESSTYPE_ANY_SIZE == size, SALT_ERR_BAD_PROTOCOL);

        SALT_VERIFY(0x00 == p_data[3] && 0x00 == p_data[4],
                    SALT_ERR_BAD_PROTOCOL);

    }
    else if (A1_ADDRESSTYPE_ED25519 == p_data[2]) {

        SALT_VERIFY(A1_ADDRESSTYPE_ED25519_SIZE == size, SALT_ERR_BAD_PROTOCOL);

        /* AddressSize in p_data[3:4], must be 32. */
        SALT_VERIFY(salti_bytes_to_u16(&p_data[3]) == 32U,
                    SALT_ERR_BAD_PROTOCOL);

        /* Check is address is us, if not response with NO_SUCH_SERVER */
        if (memcmp(&p_data[5], p_channel->my_sk_pub, 32) != 0) {
            p_channel->write_channel.p_data = &p_channel->hdshk_buffer[64];
            p_channel->write_channel.p_data[SALT_LENGTH_SIZE + 0] = SALT_A2_HEADER;
            p_channel->write_channel.p_data[SALT_LENGTH_SIZE + 1] = SALT_NO_SUCH_SERVER_FLAG;
            p_channel->write_channel.p_data[SALT_LENGTH_SIZE + 1] |= SALT_LAST_FLAG;
            p_channel->write_channel.p_data[SALT_LENGTH_SIZE + 2] = 0x00U;
            p_channel->write_channel.p_data[SALT_LENGTH_SIZE + 3] = 0x00U;
            salti_u32_to_bytes(p_channel->write_channel.p_data, 4);
            p_channel->write_channel.size = 8U;
            return SALT_SUCCESS;
        }
    }
    else {
        SALT_TRIGGER_ERROR(SALT_ERR_BAD_PROTOCOL);
    }

    /*
     * If no supported protocols is set we answer that we supporting
     * salt-channel v2 and reveals nothing about overlying protocol(s).
     * I.e., the answer will be:
     *
     * SupportedProtocols = "SCv2------","----------"
     *
     * This message is created in p_channel->hdshk_buffer[64] since
     * we have the ephemeral keypair in p_channel->hdshk_buffer[0:63]
     */
    if (p_channel->p_protocols == NULL || p_channel->p_protocols->count == 0) {
        salt_protocols_t protocols;
        /*
         * No need to check return code from salt_protocols_*
         * since we know the handshake buffer is big enough.
         */
        SALT_VERIFY(salt_protocols_create(&protocols,
                    &p_channel->hdshk_buffer[64],
                    p_channel->hdshk_buffer_size - 64) == SALT_SUCCESS,
                    SALT_ERR_INVALID_STATE);
        SALT_VERIFY(salt_protocols_append(&protocols, "----------", 10)  == SALT_SUCCESS,
                    SALT_ERR_INVALID_STATE);
        p_channel->write_channel.p_data = protocols.p_buffer;
        p_channel->write_channel.size = protocols.buf_used;
    }
    else {
        p_channel->write_channel.p_data = p_channel->p_protocols->p_buffer;
        p_channel->write_channel.size = p_channel->p_protocols->buf_used;
    }

    return SALT_SUCCESS;

}

/**
 * @brief Creates the M1 message to initiate a salt channel.
 *
 * Also the Hash of M1 is saved for later signing.
 *
 * Message structure:
 *
 * If the client is expecting to handshake with a specific host, this is provided
 * in M1. If just any, this is the structure:
 *
 * M1 = {
 *  protocolIndicator[4] ,  // ASCII "Scv2"
 *  header[2] ,             // 0x01, 0x00
 *  time[4] ,
 *  pubEncryptionKey[32]
 * }
 *
 * Otherwise:
 *
 * M1 = {
 *  protocolIndicator[4] ,  // ASCII "Scv2"
 *  header[2] ,             // 0x01, 0x01
 *  time[4] ,
 *  pubEncryptionKey[32] ,
 *  hostPubSigKey[32] ,
 * }
 *
 * If time is supported, the time 1 (0x01, 0x00, 0x00, 0x00) is set in the
 * time data. Otherwise, it is set to 0x00 for all bytes.
 *
 * Further, we create the message with the size bytes included:
 *
 * M1WithSize = { size[4] , M1[n] }
 *
 */
salt_ret_t salti_create_m1(salt_channel_t *p_channel,
                           uint8_t *p_data,
                           uint32_t *size,
                           uint8_t *p_hash,
                           const uint8_t *p_with)
{
    /* First 4 bytes is reserved for size. */

    (*size) = SALT_M1_SIZE_NO_SIG;

    /* Protocol indicator */
    memcpy(&p_data[SALT_LENGTH_SIZE],
           SALT_PROTOCOL_INDICATOR,
           SALT_PROTOCOL_INDICATOR_SIZE);

    /*
     * SALT_PROTOCOL_INDICATOR_SIZE is equal to 4.
     * Continue creating M1 after the protocol indicator.
     */

    p_data[SALT_LENGTH_SIZE + 4] = SALT_M1_HEADER_VALUE;

    if (p_with != NULL) {
        p_data[SALT_LENGTH_SIZE + 5] = SALT_M1_SIG_KEY_INCLUDED_FLAG;
        memcpy(&p_data[SALT_LENGTH_SIZE + SALT_M1_SIZE_NO_SIG], p_with, 32);
        (*size) += 32U;
    }
    else {
        p_data[SALT_LENGTH_SIZE + 5] = 0x00U; /* No tickets */
    }

    memset(&p_data[SALT_LENGTH_SIZE + 6], 0x00U, 4);
    if (p_channel->time_impl != NULL) {
        p_data[SALT_LENGTH_SIZE + 6] = 0x01U;
    }

    memcpy(&p_data[SALT_LENGTH_SIZE + 10],
           &p_channel->hdshk_buffer[SALT_PUB_ENC_OFFSET],
           api_crypto_box_PUBLICKEYBYTES);

    int ret = api_crypto_hash_sha512(p_hash, &p_data[SALT_LENGTH_SIZE], (*size));
    SALT_VERIFY(0 == ret, SALT_ERR_CRYPTO_API);

    salti_u32_to_bytes(p_data, (*size));

    (*size) += SALT_LENGTH_SIZE;

    return SALT_SUCCESS;

}

/**
 * @brief Parses and verifies M1 message.
 *
 * The hash of M1 message is also saved and used for later signing.
 * Message details: see \ref salti_create_m1
 *
 * The the time field in M1 differs from 0x00 time is supported by the peer
 * and we set the peer_epoch time.
 *
 */
salt_state_t salti_handle_m1(salt_channel_t *p_channel,
                             uint8_t *p_data,
                             uint32_t size,
                             uint8_t *p_hash)
{

    /* Sig key not included => size = 42, else 74. */
    if (!((SALT_M1_SIZE_NO_SIG == size) || (SALT_M1_SIZE_WITH_SIG == size))) {
        p_channel->err_code = SALT_ERR_BAD_PROTOCOL;
        return SALT_ERROR_STATE;
    }

    if (memcmp(p_data, SALT_PROTOCOL_INDICATOR, SALT_PROTOCOL_INDICATOR_SIZE) != 0) {
        p_channel->err_code = SALT_ERR_BAD_PROTOCOL;
        return SALT_ERROR_STATE;
    }

    if (SALT_M1_HEADER_VALUE != p_data[4]) {
        p_channel->err_code = SALT_ERR_BAD_PROTOCOL;
        return SALT_ERROR_STATE;
    }

    uint32_t time = salti_bytes_to_u32(&p_data[6]);

    if (1U == time) {
        salti_get_time(p_channel, &p_channel->peer_epoch);
        p_channel->time_supported &= 1;
    }
    else if (0U == time) {
        p_channel->time_supported = 0;
    }
    else {
        p_channel->err_code = SALT_ERR_BAD_PROTOCOL;
        return SALT_ERROR_STATE;
    }

    if (((p_data[5] & SALT_M1_SIG_KEY_INCLUDED_FLAG) > 0U) && (size == 74U)) {
        /*
         * The client included a public signature key. I.e., the client
         * expects the host to have that signature. Could be if we are
         * hosting several signatures, however, this is not supported at
         * this time. This means that the included signature key MUST match
         * our one and only public signature key.
         *
         * If the key does not match, we MUST close the session after M2 is sent.
         * Due to this, we does not need to store the client ephemeral public encryption
         * key.
         */
        if (memcmp(&p_data[42], p_channel->my_sk_pub, api_crypto_sign_PUBLICKEYBYTES) != 0) {
            p_channel->err_code = SALT_ERR_NO_SUCH_SERVER;
            return SALT_M2_INIT_NO_SUCH_SERVER;
        }
    }

    /* Copy the clients public ephemeral encryption key. */
    memcpy(&p_channel->hdshk_buffer[SALT_HOST_TMP_PEER_EK_PUB_OFFSET], &p_data[10], api_crypto_box_PUBLICKEYBYTES);

    /* Save the hash of M1 */
    int ret = api_crypto_hash_sha512(p_hash, p_data, size);
    if (0 != ret) {
        p_channel->err_code = SALT_ERR_CRYPTO_API;
        return SALT_ERROR_STATE;
    }

    return SALT_M2_INIT;

}

/**
 * @brief Creates the M2 message and saves the Hash of it.
 *
 * If a public signing key was included in the received M1, and does not
 * match the hosts public signature, the M2 message sends a NO_SUCH_SERVER
 * exception in M2. Also, the LAST_FLAG is in that case included in the header.
 *
 * Message details:
 *
 * With valid or no sig key in M1:
 * M2 = {
 *  header[2] ,             // 0x02, 0x00
 *  time[4] ,
 *  pubEncryptionKey[32]    // Ephemeral public encryption key for host
 * }
 *
 * If invalid sig key in M1:
 * M2 = {
 *  header[2] ,             // 0x02, 0x81 = (SALT_NO_SUCH_SERVER_FLAG | SALT_LAST_FLAG)
 *  time[4] ,
 *  pubEncryptionKey[32]    // Set to 0x00.
 * }
 *
 * If time is supported, the time 1 (0x01, 0x00, 0x00, 0x00) is set in the
 * time data. Otherwise, it is set to 0x00 for all bytes.
 *
 */
salt_state_t salti_create_m2(salt_channel_t *p_channel,
                             uint8_t *p_data,
                             uint32_t *size,
                             uint8_t *p_hash)
{

    salt_state_t next_state;

    /* First four bytes are reserved for size */
    p_data[SALT_M2_HEADER0_OFFSET] = SALT_M2_HEADER_VALUE;
    p_data[SALT_M2_HEADER1_OFFSET] = 0x00U; /* Flags */

    memset(&p_data[SALT_M2_TIME_OFFSET], 0x00U, SALT_TIME_SIZE);
    if (NULL != p_channel->time_impl) {
        p_data[SALT_M2_TIME_OFFSET] = 0x01U;
    }

    (*size) = SALT_M2_SIZE;

    if (SALT_M2_INIT_NO_SUCH_SERVER == p_channel->state) {
        p_data[SALT_M2_HEADER1_OFFSET] |= SALT_NO_SUCH_SERVER_FLAG;
        p_data[SALT_M2_HEADER1_OFFSET] |= SALT_LAST_FLAG;
        memset(&p_data[SALT_M2_PUB_ENC_OFFSET], 0x00,
               api_crypto_box_PUBLICKEYBYTES);
        next_state = SALT_M2_IO;
    }
    else {
        /* Copy ephemeral public key to M2 */
        memcpy(&p_data[SALT_M2_PUB_ENC_OFFSET],
               &p_channel->hdshk_buffer[SALT_PUB_ENC_OFFSET],
               api_crypto_box_PUBLICKEYBYTES);

        int ret = api_crypto_hash_sha512(p_hash, &p_data[SALT_LENGTH_SIZE], (*size));
        next_state = SALT_M2_IO_AND_SESSION_KEY;
        if (0 != ret) {
            next_state = SALT_ERROR_STATE;
            p_channel->err_code = SALT_ERR_CRYPTO_API;
        }
    }

    salti_u32_to_bytes(&p_data[SALT_M2_SIZE_OFFSET], (*size));
    (*size) += SALT_LENGTH_SIZE;

    return next_state;
}

/**
 * @brief Parses and verifies M2 message.
 *
 * The hash of M2 message is also saved and used for later signing.
 * Message details: see \ref salti_create_m2
 *
 * The the time field in M2 differs from 0x00 time is supported by the peer
 * and we set the peer_epoch time.
 *
 * If the server sends the NO_SUCH_SERVER flag in the header, we close the session
 * and returns error.
 *
 */
salt_state_t salti_handle_m2(salt_channel_t *p_channel,
                             uint8_t *p_data,
                             uint32_t size,
                             uint8_t *p_hash)
{

    if (SALT_M2_SIZE != size) {
        p_channel->err_code = SALT_ERR_BAD_PROTOCOL;
        return SALT_ERROR_STATE;
    }

    if (SALT_M2_HEADER_VALUE != p_data[0]) {
        p_channel->err_code = SALT_ERR_BAD_PROTOCOL;
        return SALT_ERROR_STATE;
    }

    /*
     * If no such server condition occurs, the session is considered closed.
     * I.e., we return error here and the application will stop the handshake
     * procedure.
     */
    if ((SALT_NO_SUCH_SERVER_FLAG & p_data[1]) > 0U) {
        p_channel->err_code = SALT_ERR_NO_SUCH_SERVER;
        return SALT_ERROR_STATE;
    }

    uint32_t time = salti_bytes_to_u32(&p_data[2]);

    if (1U == time) {
        salt_ret_t ret = salti_get_time(p_channel, &p_channel->peer_epoch);
        (void) ret;
        p_channel->time_supported &= 1;
        /*
         * TODO: What to do is our timekeeping is broken, but not the peers?
         * Is the session allowed our should we close it?
         */
    }
    else if (0U == time) {
        p_channel->time_supported = 0;
    }
    else {
        p_channel->err_code = SALT_ERR_BAD_PROTOCOL;
        return SALT_ERROR_STATE;
    }

    int ret = api_crypto_box_beforenm(p_channel->ek_common,
                                  &p_data[6],
                                  &p_channel->hdshk_buffer[SALT_SEC_ENC_OFFSET]);
    if (0 != ret) {
        p_channel->err_code = SALT_ERR_CRYPTO_API;
        return SALT_ERROR_STATE;
    }

    ret = api_crypto_hash_sha512(p_hash, p_data, size);

    if (0 != ret) {
        p_channel->err_code = SALT_ERR_CRYPTO_API;
        return SALT_ERROR_STATE;
    }

    return SALT_M3_INIT;
}

/**
 * @brief Creates the signature for M3 or M4 based on the hash from m1 and m2.
 *
 * If mode is server the signature prefix "SC-SIG01" is prepended to
 * the hashes of M1 and M2. If client, "SC-SIG02".
 *
 * The handshake buffers now looks like this:
 *
 * hdshk_buffer = {
 *  dummy[64] ,
 *  reservedForPrefix[8] ,
 *  m1hash[64] ,
 *  m2hash[64]
 * }
 *
 * The message to sign is starts at hdshk_buffer[64] with length
 * 8 + 64 + 64 = 136. We put the signed message in hdshk_buffer and
 * will get the buffer to look like:
 *
 * hdshk_buffer = {
 *  sig[64] ,
 *  sigPrefix[8] ,
 *  m1hash[64] ,
 *  m2hash[64]
 * }
 *
 */
salt_ret_t salti_create_m3m4_sig(salt_channel_t *p_channel,
                                 uint8_t *p_data,
                                 uint32_t *size)
{
    int ret;

    memcpy(p_data, p_channel->my_sk_pub, 32);

    if (SALT_SERVER == p_channel->mode) {
        memcpy(&p_channel->hdshk_buffer[64], sig1prefix, 8);
    }
    else {
        memcpy(&p_channel->hdshk_buffer[64], sig2prefix, 8);
    }

    /*
     * api_crypto_sign will sign a message { m[n] } into a signed message
     * { sign[64] , m[n] }. api_crypto_sign always returns 0.
     *
     */
    ret = api_crypto_sign(p_channel->hdshk_buffer,
                          NULL,
                          &p_channel->hdshk_buffer[64],
                          SALT_M3M4_MSG_TO_SIG_SIZE,
                          p_channel->my_sk_sec);

    SALT_VERIFY(0 == ret, SALT_ERR_CRYPTO_API);

    memcpy(&p_data[32], p_channel->hdshk_buffer, 64);

    (*size) = SALT_M3M4_CLEAR_SIZE;

    return SALT_SUCCESS;

}

/**
 * @brief Verifies the signature from M3 or M4 based on the hash from m1 and m2.
 *
 * If mode is server the signature prefix "SC-SIG02" is prepended to
 * the hashes of M1 and M2. If client, "SC-SIG01".
 *
 * The handshake buffers now looks like this:
 *
 * hdshk_buffer = {
 *  dummy[64] ,
 *  reservedForPrefix[8] ,
 *  m1hash[64] ,
 *  m2hash[64]
 * }
 *
 * The signature starts in p_data[32] and is 64 bytes.
 * The public signing key of the peer starts in p_data and is 32 bytes.
 *
 * The message can't be verified on itselfs buffer.
 *
 * Procedure:
 *  1. Copy signature to hdshk_buffer
 *  2. Add sig prefix
 *
 *  hdshk_buffer = {
 *      sig[64] ,
 *      sigPrefix[8] ,
 *      m1hash[64] ,
 *      m2hash[64]
 *  }
 *
 *  3. Verify signature (open it) to hdshk_buffer[200]
 *  hdshk_buffer = {
 *      sig[64] ,
 *      sigPrefix[8] ,
 *      m1hash[64] ,
 *      m2hash[64] ,
 *      verifid[8 + 64 + 64]
 *  }
 *
 */
salt_ret_t salti_verify_m3m4_sig(salt_channel_t *p_channel,
                                 uint8_t *p_data,
                                 uint32_t size)
{

    SALT_VERIFY(size == SALT_M3M4_CLEAR_SIZE, SALT_ERR_BAD_PROTOCOL);

    memcpy(p_channel->peer_sk_pub, p_data, 32);
    memcpy(p_channel->hdshk_buffer, &p_data[32], 64);

    if (p_channel->mode == SALT_SERVER) {
        memcpy(&p_channel->hdshk_buffer[64], sig2prefix, 8);
    }
    else {
        memcpy(&p_channel->hdshk_buffer[64], sig1prefix, 8);
    }

    int ret = api_crypto_sign_open(&p_channel->hdshk_buffer[SALT_M3M4_SIG_VERIFY_OFFSET],
                                   NULL,
                                   p_channel->hdshk_buffer,
                                   SALT_M3M4_SIGNED_MSG_SIZE,
                                   p_channel->peer_sk_pub);
    SALT_VERIFY(ret == 0, SALT_ERR_BAD_PEER);

    return SALT_SUCCESS;
}
