/* Copyright 2012-2017 AOL Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this Software except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * https://www.chromium.org/quic
 * https://docs.google.com/document/d/1WJvyZflAO2pq77yOLbp9NsGjC1CHetAXV8I0fQe-B_U
 *
 */
#include "moloch.h"
#include <arpa/inet.h>
#include <dlfcn.h>
#include "openssl/evp.h"

extern MolochConfig_t        config;
LOCAL  int hostField;
LOCAL  int uaField;
LOCAL  int versionField;

#define FBZERO_MAX_SIZE 4096
typedef struct {
    unsigned char  data[FBZERO_MAX_SIZE];
    int            pos;
} FBZeroInfo_t;

typedef struct {
    int            packets;
    int            which;
} QUIC5xInfo_t;

/******************************************************************************/
LOCAL int quic_chlo_parser(MolochSession_t *session, BSB dbsb) {

    guchar   *tag = 0;
    uint16_t  tagLen = 0;

    BSB_LIMPORT_ptr(dbsb, tag, 4);
    BSB_LIMPORT_u16(dbsb, tagLen);
    BSB_LIMPORT_skip(dbsb, 2);

    if (BSB_IS_ERROR(dbsb)) {
        return 0;
    }

    moloch_session_add_protocol(session, "quic");

    if (!tag || memcmp(tag, "CHLO", 4) != 0 || BSB_REMAINING(dbsb) < tagLen*8 + 8) {
        return 0;
    }

    guchar *tagDataStart = dbsb.buf + tagLen*8 + 8;
    uint32_t dlen = BSB_SIZE(dbsb) - tagLen*8 - 8;

    uint32_t start = 0;
    while (!BSB_IS_ERROR(dbsb) && BSB_REMAINING(dbsb) && tagLen > 0) {
        guchar   *subTag = 0;
        uint32_t  endOffset = 0;

        BSB_LIMPORT_ptr(dbsb, subTag, 4);
        BSB_LIMPORT_u32(dbsb, endOffset);

        if (endOffset > dlen || start > dlen || start >= endOffset) {
            return 1;
        }

        if (!subTag)
            return 1;

        if (memcmp(subTag, "SNI\x00", 4) == 0) {
            moloch_field_string_add(hostField, session, (char *)tagDataStart+start, endOffset-start, TRUE);
        } else if (memcmp(subTag, "UAID", 4) == 0) {
            moloch_field_string_add(uaField, session, (char *)tagDataStart+start, endOffset-start, TRUE);
        } else if (memcmp(subTag, "VER\x00", 4) == 0) {
            moloch_field_string_add(versionField, session, (char *)tagDataStart+start, endOffset-start, TRUE);
        } else {
            //LOG("Subtag: %4.4s len: %d %.*s", subTag, endOffset-start, endOffset-start, tagDataStart+start);
        }
        start = endOffset;
        tagLen--;
    }
    return 1;
}
/******************************************************************************/
LOCAL int quic_2445_udp_parser(MolochSession_t *session, void *UNUSED(uw), const unsigned char *data, int len, int UNUSED(which))
{
    int version = -1;
    int offset = 1;

    if ( len < 9) {
        return 0;
    }

    // PUBLIC_FLAG_RESET
    if (data[0] & 0x02) {
        return 0;
    }

    // CID
    if (data[0] & 0x08) {
        offset += 8;
    }

    if ( len < offset+5) {
        return 0;
    }

    // Get version
    if (data[0] & 0x01 && data[offset] == 'Q') {
        version = (data[offset+1] - '0') * 100 +
                  (data[offset+2] - '0') * 10 +
                  (data[offset+3] - '0');
        offset += 4;
    }

    // Unsupported version
    if (version < 24) {
        return MOLOCH_PARSER_UNREGISTER;
    }

    // Diversification only is from server to client, so we can ignore

    // Packet number size
    if ((data[0] & 0x30) == 0) {
        offset++;
    } else {
        offset += ((data[0] & 0x30) >> 4) * 2;
    }

    // Hash
    offset += 12;

    // Private Flags
    if (version < 34)
        offset++;

    if (offset > len)
        return 0;

    BSB bsb;
    BSB_INIT(bsb, data+offset, len-offset);

    while (!BSB_IS_ERROR(bsb) && BSB_REMAINING(bsb)) {
        uint8_t type = 0;
        BSB_LIMPORT_u08(bsb, type);

        //1fdooossB
        if ((type & 0x80) == 0) {
            return 0;
        }

        int offsetLen = 0;
        if (type & 0x1C) {
            offsetLen = ((type & 0x1C) >> 2) + 1;
        }

        int streamLen = (type & 0x03) + 1;

        BSB_LIMPORT_skip(bsb, streamLen + offsetLen);

        int dataLen = BSB_REMAINING(bsb);
        if (type & 0x20) {
            BSB_LIMPORT_u16(bsb, dataLen);
            if (dataLen == 4) // Sometimes dataLen is BE, not sure why
                dataLen = 1024;
        }

        if (BSB_IS_ERROR(bsb)) {
            return 0;
        }

        BSB dbsb;
        BSB_INIT(dbsb, BSB_WORK_PTR(bsb), MIN(dataLen, BSB_REMAINING(bsb)));
        BSB_IMPORT_skip(bsb, dataLen);

        quic_chlo_parser(session, dbsb);
        return MOLOCH_PARSER_UNREGISTER;
    }

    return 0;
}
/******************************************************************************/
// Couldn't figure out this document, brute force
// https://docs.google.com/document/d/1FcpCJGTDEMblAs-Bm5TYuqhHyUqeWpqrItw2vkMFsdY/edit
LOCAL int quic_4648_udp_parser(MolochSession_t *session, void *UNUSED(uw), const unsigned char *data, int len, int UNUSED(which))
{
    int version = -1;
    int offset = 5;

    if (len < 20 || data[1] != 'Q' || (data[0] & 0xc0) != 0xc0) {
        return MOLOCH_PARSER_UNREGISTER;
    }

    // Get version
    version = (data[2] - '0') * 100 +
              (data[3] - '0') * 10 +
              (data[4] - '0');

    if (version < 46 || version > 48) {
        return MOLOCH_PARSER_UNREGISTER;
    }
    for (;offset < len - 20; offset++) {
        if (data[offset] == 'C' && memcmp(data+offset, "CHLO", 4) == 0) {
            BSB bsb;
            BSB_INIT(bsb, data + offset, len - offset);
            quic_chlo_parser(session, bsb);
            return MOLOCH_PARSER_UNREGISTER;
        }
    }
    return 0;
}
/******************************************************************************/
// Headers are encrypted?
LOCAL int quic_5x_udp_parser(MolochSession_t *session, void *uw, const unsigned char *data, int len, int which)
{
    if (len < 20 || memcmp(data+1, "Q05", 3) != 0) {
        return MOLOCH_PARSER_UNREGISTER;
    }

    QUIC5xInfo_t *info = (QUIC5xInfo_t *)uw;

    info->which |= (1 << which);

    if (info->which == 0x3) {
        moloch_session_add_protocol(session, "quic");
        return MOLOCH_PARSER_UNREGISTER;
    }
    info->packets++;
    if (info->packets > 20)
        return MOLOCH_PARSER_UNREGISTER;

    return 0;
}
/******************************************************************************/
LOCAL void quic_2445_udp_classify(MolochSession_t *session, const unsigned char *data, int len, int UNUSED(which), void *UNUSED(uw))
{
    if (len > 100 && (data[0] & 0x83) == 0x01) {
        moloch_parsers_register(session, quic_2445_udp_parser, 0, 0);
    }
}
/******************************************************************************/
LOCAL void quic_4648_udp_classify(MolochSession_t *session, const unsigned char *data, int len, int UNUSED(which), void *UNUSED(uw))
{
    if (len > 100 && (data[0] & 0xc0) == 0xc0) {
        moloch_parsers_register(session, quic_4648_udp_parser, 0, 0);
    }
}
/******************************************************************************/
LOCAL void quic_5x_free(MolochSession_t UNUSED(*session), void *uw)
{
    QUIC5xInfo_t            *info          = uw;

    MOLOCH_TYPE_FREE(QUIC5xInfo_t, info);
}
/******************************************************************************/
LOCAL void quic_5x_udp_classify(MolochSession_t *session, const unsigned char *data, int len, int UNUSED(which), void *UNUSED(uw))
{
    if (len > 100 && (data[0] & 0xc0) == 0xc0) {
        QUIC5xInfo_t *info = MOLOCH_TYPE_ALLOC(QUIC5xInfo_t);
        info->packets = 0;
        info->which = 1 << which;
        moloch_parsers_register(session, quic_5x_udp_parser, info, quic_5x_free);
    }
}
/******************************************************************************/
LOCAL void quic_add(MolochSession_t *UNUSED(session), const unsigned char *UNUSED(data), int UNUSED(len), int UNUSED(which), void *UNUSED(uw))
{
    moloch_session_add_protocol(session, "quic");
}
/******************************************************************************/
LOCAL void quic_fbzero_free(MolochSession_t UNUSED(*session), void *uw)
{
    FBZeroInfo_t            *fbzero          = uw;

    MOLOCH_TYPE_FREE(FBZeroInfo_t, fbzero);
}
/******************************************************************************/
LOCAL int quic_fb_tcp_parser(MolochSession_t *session, void *uw, const unsigned char *data, int remaining, int which)
{
    if (which != 0)
        return 0;

    FBZeroInfo_t *fbzero = uw;

    remaining = MIN(remaining, FBZERO_MAX_SIZE - fbzero->pos);
    memcpy(fbzero->data + fbzero->pos, data, remaining);
    fbzero->pos += remaining;

    if (fbzero->pos < 7)
        return 0;

    int len = (fbzero->data[6] << 8) | fbzero->data[5];
    if (fbzero->pos < len + 9)
        return 0;

    BSB dbsb;
    BSB_INIT(dbsb, fbzero->data+9, len);

    if (quic_chlo_parser(session, dbsb))
        moloch_session_add_protocol(session, "fbzero");

    return MOLOCH_PARSER_UNREGISTER;
}

/******************************************************************************/
LOCAL void quic_fb_tcp_classify(MolochSession_t *session, const unsigned char *UNUSED(data), int len, int which, void *UNUSED(uw))
{
    if (which == 0 && len > 13) {
        FBZeroInfo_t *fbzero = MOLOCH_TYPE_ALLOC(FBZeroInfo_t);
        fbzero->pos = 0;
        moloch_parsers_register(session, quic_fb_tcp_parser, fbzero, quic_fbzero_free);
    }
}
/******************************************************************************/
LOCAL uint64_t quic_get_number(BSB *bsb)
{
    uint64_t result = 0;
    int      tmp = 0;

    // Top 2 bits of first value maps to 1/2/4/8 bytes
    BSB_IMPORT_u08(*bsb, tmp);
    if (BSB_IS_ERROR(*bsb))
        return 0;
    if ((tmp & 0xc0) == 0)
        return tmp & 0x3f;

    BSB_IMPORT_rewind(*bsb, 1);
    switch (tmp & 0xc0) {
    case 0x40:
        BSB_IMPORT_u16(*bsb, result);
        result &= 0x3FFF;
        break;
    case 0x80:
        BSB_IMPORT_u32(*bsb, result);
        result &= 0x3FFFFFFF;
        break;
    case 0xc0:
        BSB_IMPORT_u64(*bsb, result);
        result &= 0x3FFFFFFFFFFFFFFFL;
        break;
    }
    return result;
}
/******************************************************************************/
LOCAL void hkdfExpandLabel(uint8_t *secret, int secretLen, char *label, uint8_t *okm, gsize okmLen)
{
    uint8_t data[100];
    BSB bsb;
    BSB_INIT(bsb, data, sizeof(data));

    int labelLen = strlen(label);
    BSB_EXPORT_u16(bsb, okmLen);
    BSB_EXPORT_u08(bsb, labelLen);
    BSB_EXPORT_ptr(bsb, label, labelLen);
    BSB_EXPORT_u08(bsb, 0); //contextLength

    // I think there is supposed to be a complex loop here, not sure if needed for what we are doing
    GHmac *hmac = g_hmac_new(G_CHECKSUM_SHA256, secret, secretLen);
    g_hmac_update(hmac, data, BSB_LENGTH(bsb));
    uint8_t one = 1;
    g_hmac_update(hmac, &one, 1);

    // If we are truncating the digest get the whole thing and copy just the first part
    if (okmLen < 32) {
        uint8_t digest[32];
        gsize len = 32;
        g_hmac_get_digest(hmac, digest, &len);
        memcpy(okm, digest, okmLen);
    } else {
        g_hmac_get_digest(hmac, okm, &okmLen);
    }
    g_hmac_unref(hmac);
}
/******************************************************************************/
LOCAL void quic_ietf_udp_classify(MolochSession_t *session, const unsigned char *data, int len, int UNUSED(which), void *UNUSED(uw))
{
// This is the most obfuscate protocol ever
// Thank you wireshark/tshark/quicgo and other tools to verify (kindof) implementation

    static int init = 1;
    static void (*process_client_hello_data)(MolochSession_t *session, const uint8_t *data, int len) = NULL;

    // Do this once. Has to be here since parsers can be loaded in any order
    if (init) {
        init = 0;
        process_client_hello_data = dlsym(RTLD_DEFAULT, "tls_process_client_hello_data");
    }

    // Min length for quic packets because of padding
    if (len < 1200 || len > 3000)
        return;

    // Only look for long form initial
    if ((data[0] & 0xf0) != 0xc0)
        return;

    int rc;
    BSB bsb;
    BSB_INIT(bsb, data, len);

  // Decode Header
    uint8_t flags = 0;
    BSB_IMPORT_u08(bsb, flags); // Still partially encrypted
    BSB_IMPORT_skip(bsb, 4); // version

    int dlen = 0;
    // Destination
    BSB_IMPORT_u08(bsb, dlen);
    uint8_t *did = BSB_WORK_PTR(bsb);
    BSB_IMPORT_skip(bsb, dlen);

    // Source
    int slen = 0;
    BSB_IMPORT_u08(bsb, slen);
    if (slen != 0)
        return;
    BSB_IMPORT_skip(bsb, slen);

    // Token
    int tlen = quic_get_number(&bsb);
    BSB_IMPORT_skip(bsb, tlen);

    // Length
    int packet_len = quic_get_number(&bsb);
    if (packet_len != BSB_REMAINING(bsb)) {
        char ipStr[200];
        moloch_session_pretty_string(session, ipStr, sizeof(ipStr));
        LOG("Couldn't parse header packet len %d remaining %ld %s", packet_len, BSB_REMAINING(bsb), ipStr);
        return;
    }

    if (BSB_IS_ERROR(bsb))
        return;

  // HKDF - HMAC-based Key Derivation Function
  // https://datatracker.ietf.org/doc/html/rfc5869

  // HKDF-Extract(salt, IKM) -> PRK
    static uint8_t salt[20] = { 0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17, 0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a };
    GHmac *hmac = g_hmac_new(G_CHECKSUM_SHA256, salt, 20);
    g_hmac_update(hmac, (guchar*)did, dlen);
    uint8_t prk[65];
    gsize   prkLen = sizeof(prk);
    g_hmac_get_digest(hmac, (guchar*)prk, &prkLen);
    g_hmac_unref(hmac);

  // Calculate secrets for later
    uint8_t clientOkm[32];
    hkdfExpandLabel(prk, prkLen, "tls13 client in", clientOkm, sizeof(clientOkm));

    uint8_t hpOkm[16];
    hkdfExpandLabel(clientOkm, sizeof(clientOkm), "tls13 quic hp", hpOkm, sizeof(hpOkm));

    uint8_t keyOkm[16];
    hkdfExpandLabel(clientOkm, sizeof(clientOkm), "tls13 quic key", keyOkm, sizeof(keyOkm));

    uint8_t ivOkm[12];
    hkdfExpandLabel(clientOkm, sizeof(clientOkm), "tls13 quic iv", ivOkm, sizeof(ivOkm));

  // Get mask input data
    BSB_IMPORT_skip(bsb, 4);
    uint8_t maskInput[16];
    BSB_IMPORT_byte(bsb, maskInput, 16);

    if (BSB_IS_ERROR(bsb))
        return;

    BSB_IMPORT_rewind(bsb, 20); // Go back

  // Calculate mask for packet number
    uint8_t mask[100];
    int     maskLen = sizeof(mask);

    EVP_CIPHER_CTX      *hp_cipher_ctx;
    const EVP_CIPHER    *hp_cipher = EVP_aes_128_ecb();
    hp_cipher_ctx = EVP_CIPHER_CTX_new();
    rc = EVP_EncryptInit(hp_cipher_ctx, hp_cipher, hpOkm, NULL);
    rc += EVP_EncryptUpdate(hp_cipher_ctx, mask, &maskLen, maskInput, 16);
    // EVP_EncryptFinal(hp_cipher_ctx, mask, &maskLen); --> Not sure why this isn't needed
    EVP_CIPHER_CTX_free(hp_cipher_ctx);

    if (rc != 2) {
        if (config.debug)
            LOG("Couldn't encrypt mask: %d", rc);
        return;
    }

  // Decrypt Packet Number using mask
  // https://datatracker.ietf.org/doc/html/draft-ietf-quic-tls-33#section-5.4.1
    uint8_t packet0 = flags;
    if ((packet0 & 0x80) == 0x80) {
        packet0 ^= mask[0] & 0x0f;
    } else {
        packet0 ^= mask[0] & 0x1f;
    }
    int pn_length = (packet0 & 0x03) + 1;
    uint64_t pn = 0;

    if (pn_length > 2)
        return;

    for (int i = 0; pn_length > 0; pn_length--, i++) {
        uint8_t tmp = 0;
        BSB_IMPORT_u08(bsb, tmp);
        pn |= (tmp ^ mask[i+1]) << (8*(pn_length - 1));
    }

  // Make copy, with decrypted first byte and packet number
    uint8_t buffer[3100];
    uint16_t headerLen = BSB_POSITION(bsb);

    memcpy(buffer, data, len);

    buffer[0] = packet0;
    buffer[headerLen - 1] = pn & 0xff;
    if (pn_length == 2) {
        buffer[headerLen - 2] = (pn & 0xff) >> 8;
    }

  // Make nonce
    uint8_t nonce[12];
    memcpy(nonce, ivOkm, sizeof(nonce));
    nonce[10] ^= (pn & 0xff) >> 8;
    nonce[11] ^= (pn & 0xff);

  // Decrypt Packet
    EVP_CIPHER_CTX      *pp_cipher_ctx;
    const EVP_CIPHER    *pp_cipher = EVP_aes_128_gcm();
    uint8_t out[3000];
    int outLen = sizeof(out);

    pp_cipher_ctx = EVP_CIPHER_CTX_new();
    rc = EVP_DecryptInit(pp_cipher_ctx, pp_cipher, keyOkm, nonce);
    rc += EVP_DecryptUpdate(pp_cipher_ctx, out, &outLen, BSB_WORK_PTR(bsb), BSB_REMAINING(bsb)-16);
    //rc = EVP_DecryptFinal(pp_cipher_ctx, out, &outLen); --> Not sure why this isn't needed
    EVP_CIPHER_CTX_free(pp_cipher_ctx);
    if (rc != 2) {
        if (config.debug)
            LOG("Couldn't decrypt packet: %d", rc);
        return;
    }

    BSB_INIT(bsb, out, outLen);

    int     clen = 0;
    uint8_t cbuf[8000];

    // Loop thru all the frames. The crypto frames can be out of order. Worst. Protocol. Every.
    while (!BSB_IS_ERROR(bsb) && BSB_REMAINING(bsb) > 1) {
        uint8_t type = 0;
        BSB_IMPORT_u08(bsb, type);
        if (type == 0 || type == 1) // PADDING or PING
            continue;

        if (type == 6) { // CRYPTO
            moloch_session_add_protocol(session, "quic");
            int offset = quic_get_number(&bsb);
            int length = quic_get_number(&bsb);

            if (offset + length < (int)sizeof(cbuf) && BSB_REMAINING(bsb) >= length) {
                memcpy(cbuf + offset, BSB_WORK_PTR(bsb), length);
                clen += length;
            }

            BSB_IMPORT_skip(bsb, length);

            continue;
        }
        break;
    }

    // Now actually decode the client hello
    if (clen > 0 && process_client_hello_data) {
        process_client_hello_data(session, cbuf, clen);
    }
}
/******************************************************************************/
void moloch_parser_init()
{
    moloch_parsers_classifier_register_udp("quic", NULL, 1, (const unsigned char *)"Q05", 3, quic_5x_udp_classify);
    moloch_parsers_classifier_register_udp("quic", NULL, 1, (const unsigned char *)"Q04", 3, quic_4648_udp_classify);
    moloch_parsers_classifier_register_udp("quic", NULL, 9, (const unsigned char *)"Q04", 3, quic_2445_udp_classify);
    moloch_parsers_classifier_register_udp("quic", NULL, 9, (const unsigned char *)"Q03", 3, quic_2445_udp_classify);
    moloch_parsers_classifier_register_udp("quic", NULL, 9, (const unsigned char *)"Q02", 3, quic_2445_udp_classify);
    moloch_parsers_classifier_register_tcp("fbzero", NULL, 0, (const unsigned char *)"\x31QTV", 4, quic_fb_tcp_classify);
    moloch_parsers_classifier_register_udp("quic", NULL, 9, (const unsigned char *)"PRST", 4, quic_add);

    moloch_parsers_classifier_register_udp("quic", NULL, 1, (const unsigned char *)"\x00\x00\x00\x01", 1, quic_ietf_udp_classify);

    hostField = moloch_field_define("quic", "lotermfield",
        "host.quic", "Hostname", "quic.host",
        "QUIC host header field",
        MOLOCH_FIELD_TYPE_STR_GHASH,  MOLOCH_FIELD_FLAG_CNT,
        "category", "host",
        "aliases", "[\"quic.host\"]",
        (char *)NULL);

    moloch_field_define("quic", "lotextfield",
        "host.quic.tokens", "Hostname Tokens", "quic.hostTokens",
        "QUIC host tokens header field",
        MOLOCH_FIELD_TYPE_STR_GHASH,  MOLOCH_FIELD_FLAG_FAKE,
        "aliases", "[\"quic.host.tokens\"]",
        (char *)NULL);

    uaField = moloch_field_define("quic", "termfield",
        "quic.user-agent", "User-Agent", "quic.useragent",
        "User-Agent",
        MOLOCH_FIELD_TYPE_STR_GHASH,  MOLOCH_FIELD_FLAG_CNT,
        (char *)NULL);

    versionField = moloch_field_define("quic", "termfield",
        "quic.version", "Version", "quic.version",
        "QUIC Version",
        MOLOCH_FIELD_TYPE_STR_GHASH,  MOLOCH_FIELD_FLAG_CNT,
        (char *)NULL);
}
