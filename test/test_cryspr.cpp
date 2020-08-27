#include <stdio.h>
#include <string.h>
#include <sstream>
#include <iostream>
#include "gtest/gtest.h"

#define SRT_ENABLE_ENCRYPTION
#ifdef SRT_ENABLE_ENCRYPTION
#include "common.h"
#include "hcrypt.h"
#include "version.h"
#include "crypto.h"
#include "../haicrypt/haicrypt.h"

using namespace std;

void print_hex(const uint8_t* buff, size_t len, const char* desc)
{
    std::stringstream ss;
    ss << desc << "[" << len << "]: 0x";
    const uint8_t* ptr = (const uint8_t*) buff;
    for (size_t i = 0; i < len; ++i)
        ss << std::hex << std::uppercase << int(ptr[i]);
    cerr << ss.str() << endl;;
}


#if (CRYSPR_VERSION_NUMBER >= 0x010100)
#define WITH_FIPSMODE 1 /* 1: openssl-evp branch */
#endif

#define UT_PKT_MAXLEN 1500

const void *nullPtr = NULL;

/* TestCRYSPRmethods: Test presense of required cryspr methods */

class TestCRYSPRmethods
    : public ::testing::Test
{
protected:
    TestCRYSPRmethods()
    {
        // initialization code here
        cryspr_m = NULL;
        cryspr_fbm = NULL;
    }

    ~TestCRYSPRmethods()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

    // SetUp() is run immediately before a test starts.
#if defined(__GNUC__) && (__GNUC___ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
//  override only supported for GCC>=4.7
    void SetUp() override {
#else
    void SetUp() {
#endif
        cryspr_m = cryspr4SRT();
        cryspr_fbm = crysprInit(&cryspr_fb);

        ASSERT_NE(cryspr_m, nullPtr);
        ASSERT_NE(cryspr_fbm, nullPtr);
        ASSERT_EQ(cryspr_fbm, &cryspr_fb);
    }

#if defined(__GNUC__) && (__GNUC___ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
    void TearDown() override {
#else
    void TearDown() {
#endif
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
    }

protected:

    CRYSPR_methods *cryspr_m;   /* methods */
    CRYSPR_methods cryspr_fb, *cryspr_fbm; /* fall back methods */
//    CRYSPR_cb *cryspr_cb;       /* Control block */
};


TEST_F(TestCRYSPRmethods, MethodOpen)
{
    EXPECT_NE(cryspr_m, nullPtr);
    EXPECT_NE(cryspr_m->open, nullPtr);
}


TEST_F(TestCRYSPRmethods, init)
{
    ASSERT_NE(cryspr_m, nullPtr);
}

#if WITH_FIPSMODE
TEST_F(TestCRYSPRmethods, fipsmode)
{
    if(cryspr_m->fips_mode_set == NULL || cryspr_m->fips_mode_set == cryspr_fbm->fips_mode_set ) {
#if defined(CRYSPR_FIPSMODE)    //undef: not supported, 0: supported and Off by default, 1: enabled by default
        EXPECT_NE(cryspr_m->fips_mode_set, cryspr_fbm->fips_mode_set); //Fallback method cannot set FIPS mode
        EXPECT_EQ(cryspr_m->fips_mode_set(CRYSPR_FIPSMODE ? 0 : 1), CRYSPR_FIPSMODE);
        EXPECT_EQ(cryspr_m->fips_mode_set(CRYSPR_FIPSMODE), (CRYSPR_FIPSMODE? 0 : 1));
#endif /* CRYSPR_FIPSMODE */
    }
}
#endif /* WITH_FIPSMODE */

TEST_F(TestCRYSPRmethods, open)
{
    EXPECT_NE(cryspr_m->open, nullPtr);
}

TEST_F(TestCRYSPRmethods, close)
{
    EXPECT_NE(cryspr_m->close, nullPtr);
}

TEST_F(TestCRYSPRmethods, prng)
{
    EXPECT_NE(cryspr_m->prng, nullPtr);
}

TEST_F(TestCRYSPRmethods, aes_set_key)
{
    EXPECT_NE(cryspr_m->aes_set_key, nullPtr);
}

TEST_F(TestCRYSPRmethods, AESecb)
{
    if(cryspr_m->km_wrap == cryspr_fbm->km_wrap) {
        /* fallback KM_WRAP method used
         * AES-ECB method then required
         */
        EXPECT_NE(cryspr_m->aes_ecb_cipher, nullPtr);
        EXPECT_NE(cryspr_m->aes_ecb_cipher, cryspr_fbm->aes_ecb_cipher);
    }
}
TEST_F(TestCRYSPRmethods, AESctr)
{
    EXPECT_NE(cryspr_m->aes_ctr_cipher, nullPtr);
}

TEST_F(TestCRYSPRmethods, SHA1)
{
    if(cryspr_m->sha1_msg_digest == NULL || cryspr_m->km_pbkdf2 == cryspr_fbm->km_pbkdf2 ) {
        /* fallback PBKDF2 used
         * then sha1 method required.
         */
        EXPECT_NE(cryspr_m->sha1_msg_digest, nullPtr);
        EXPECT_NE(cryspr_m->sha1_msg_digest, cryspr_fbm->sha1_msg_digest);
    }
}


/* CRYSPR control block test */
class TestCRYSPRcypto
    : public ::testing::Test
{
protected:
    TestCRYSPRcypto()
    {
        // initialization code here
        cryspr_m = NULL;
        cryspr_fbm = NULL;
        cryspr_cb = NULL;
    }

    ~TestCRYSPRcypto()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

    // SetUp() is run immediately before a test starts.
#if defined(__GNUC__) && (__GNUC___ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
//  override only supported for GCC>=4.7
    void SetUp() override {
#else
    void SetUp() {
#endif
        cryspr_m = cryspr4SRT();
        cryspr_fbm = crysprInit(&cryspr_fb);

        ASSERT_NE(cryspr_m, nullPtr);
        ASSERT_NE(cryspr_fbm, nullPtr);
        ASSERT_EQ(cryspr_fbm, &cryspr_fb);
        cryspr_cb = cryspr_m->open(cryspr_m, UT_PKT_MAXLEN);
        ASSERT_NE(cryspr_cb, nullPtr);
    }

#if defined(__GNUC__) && (__GNUC___ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
    void TearDown() override {
#else
    void TearDown() {
#endif
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        if (cryspr_m && cryspr_cb) {
            EXPECT_EQ(cryspr_m->close(cryspr_cb), 0);
        }
    }

protected:

    CRYSPR_methods *cryspr_m;   /* methods */
    CRYSPR_methods cryspr_fb, *cryspr_fbm; /* fall back methods */
    CRYSPR_cb *cryspr_cb;       /* Control block */
};

TEST_F(TestCRYSPRcypto, CtrlBlock)
{
    EXPECT_EQ(cryspr_m, cryspr_cb->cryspr); //methods set in control block
}

/*PBKDF2-----------------------------------------------------------------------------------------*/

/* See https://asecuritysite.com/encryption/PBKDF2z
   to generate "known good" PBKDF2 hash
*/
/* Test Vector 1.1 to 1.3 */

struct UTVcryspr_pbkdf2 {
    const char *name;
    const char *passwd;
    const char *salt;
    int itr;
    size_t keklen;
    unsigned char kek[256/8];
};

/* PBKDF2 test vectors */
struct UTVcryspr_pbkdf2 pbkdf2_tv[] = {
    {//[0]
        /* testname */  "PBKDF2 tv1.128",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    128/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79}
    },
    {//[1]
        /* testname */  "PBKDF2 tv1.192",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    192/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad}
    },
    {//[2]
        /* testname */  "PBKDF2 tv1.256",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    256/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad,0x19,0x59,0xcf,0x18,0xac,0x91,0x53,0x3d}
    },
    {//[3]
        /* testname */  "PBKDF2 tv2.1",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 1,
        /* keklen */    20,
        /* kek */       {0x0c,0x60,0xc8,0x0f,0x96,0x1f,0x0e,0x71,0xf3,0xa9,0xb5,0x24,0xaf,0x60,0x12,0x06,
                         0x2f,0xe0,0x37,0xa6}
    },
    {//[4]
        /* testname */  "PBKDF2 tv2.20",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 2,
        /* keklen */    20,
        /* kek */       {0xea,0x6c,0x01,0x4d,0xc7,0x2d,0x6f,0x8c,0xcd,0x1e,0xd9,0x2a,0xce,0x1d,0x41,0xf0,
                         0xd8,0xde,0x89,0x57}
    },
    {//[5]
        /* testname */  "PBKDF2 tv2.4096",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 4096,
        /* keklen */    20,
        /* kek */       {0x4b,0x00,0x79,0x01,0xb7,0x65,0x48,0x9a,0xbe,0xad,0x49,0xd9,0x26,0xf7,0x21,0xd0,
                         0x65,0xa4,0x29,0xc1}
    },
    {//[6]
        /* testname */  "PBKDF2 tv3.0",
        /* passwd */    "passwordPASSWORDpassword",
        /* salt */      "saltSALTsaltSALTsaltSALTsaltSALTsalt",
        /* iteration */ 4096,
        /* keklen */    25,
        /* kek */       {0x3d,0x2e,0xec,0x4f,0xe4,0x1c,0x84,0x9b,0x80,0xc8,0xd8,0x36,0x62,0xc0,0xe4,0x4a,
                         0x8b,0x29,0x1a,0x96,0x4c,0xf2,0xf0,0x70,0x38}
    },
};

void test_pbkdf2(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi) //test vector index
{
    unsigned char kek[256/8];

    if(tvi < sizeof(pbkdf2_tv)/sizeof(pbkdf2_tv[0])) {
        struct UTVcryspr_pbkdf2 *tv = &pbkdf2_tv[tvi];

        ASSERT_NE(cryspr_m->km_pbkdf2, nullPtr);

        cryspr_m->km_pbkdf2(
            cryspr_cb,
            (char *)tv->passwd,         /* passphrase */
            strnlen(tv->passwd, 80),    /* passphrase len */
            (unsigned char *)tv->salt,  /* salt */
            strnlen(tv->salt, 80),      /* salt_len */
            tv->itr,                    /* iterations */
            tv->keklen,                 /* desired key len {(}16,24,32}*/
            kek);                       /* derived key */

        EXPECT_EQ(memcmp(kek, tv->kek, tv->keklen),0);
    }
}


TEST_F(TestCRYSPRcypto, PBKDF2_tv1_k128)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 0);
}

TEST_F(TestCRYSPRcypto, PBKDF2_tv1_k192)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 1);
}

TEST_F(TestCRYSPRcypto, PBKDF2_tv1_k256)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 2);
}

TEST_F(TestCRYSPRcypto, PBKDF2_tv2_i1)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 3);
}

TEST_F(TestCRYSPRcypto, PBKDF2_tv2_i20)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 4);
}

TEST_F(TestCRYSPRcypto, PBKDF2_tv2_i4096)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 5);
}

TEST_F(TestCRYSPRcypto, PBKDF2_tv3_0)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 6);
}

/*AES KeyWrap -----------------------------------------------------------------------------------*/

struct UTVcryspr_km_wrap {
    const char *name;
    unsigned char sek[256/8];       /* key to wrap (unwrap result)*/
    size_t seklen;
    unsigned char kek[256/8];
    unsigned char wrap[8+256/8];    /* wrapped sek (wrap result) */
};

/* KMWRAP/KMUNWRAP test vectors */
struct UTVcryspr_km_wrap UTV_cryspr_km_wrap[] = {
    {//[0]
        /*name */       "tv1.128",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79},
        /* wrap */      {0xF8,0xB6,0x12,0x1B,0xF2,0x03,0x62,0x40,0x80,0x32,0x60,0x8D,0xED,0x0B,0x8E,0x4B,
                         0x29,0x7E,0x80,0x17,0x4E,0x89,0x68,0xF1}
    },
    {//[1]
        /*name */       "tv1.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad},
        /* wrap */      {0xC1,0xA6,0x58,0x9E,0xC0,0x52,0x6D,0x37,0x84,0x3C,0xBD,0x3B,0x02,0xDD,0x79,0x3F,
                         0xE6,0x14,0x2D,0x81,0x69,0x4B,0x8E,0x07,0x26,0x4F,0xCD,0x86,0xD6,0x6A,0x70,0x62},
    },
    {//[2]
        /*name */       "tv1.256",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    256/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad,0x19,0x59,0xcf,0x18,0xac,0x91,0x53,0x3d},
        /* wrap */      {0x94,0xBE,0x9C,0xA6,0x7A,0x27,0x20,0x56,0xED,0xEA,0xA0,0x8F,0x71,0xB1,0xF1,0x85,
                         0xF6,0xC5,0x67,0xF4,0xA9,0xC2,0x1E,0x78,0x49,0x36,0xA5,0xAE,0x60,0xD0,0x1C,0x30,
                         0x68,0x27,0x4F,0x66,0x56,0x5A,0x55,0xAA},
    },
};

void test_kmwrap(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi) //Test vector index
{
    unsigned char wrap[HAICRYPT_WRAPKEY_SIGN_SZ+256/8];
    int rc1,rc2;

    if (tvi < sizeof(UTV_cryspr_km_wrap)/sizeof(UTV_cryspr_km_wrap[0]))
    {
        struct UTVcryspr_km_wrap *tv = &UTV_cryspr_km_wrap[tvi];
        size_t wraplen=HAICRYPT_WRAPKEY_SIGN_SZ+tv->seklen;

        if(cryspr_m && cryspr_cb) {
            ASSERT_NE(cryspr_m->km_setkey, nullPtr);
            ASSERT_NE(cryspr_m->km_wrap, nullPtr);

            rc1 = cryspr_m->km_setkey(
                cryspr_cb,
                true,       //Wrap
                tv->kek,
                tv->seklen);

            rc2 = cryspr_m->km_wrap(
                cryspr_cb,
                wrap,
                tv->sek,
                tv->seklen);

            ASSERT_EQ(rc1, 0);
            ASSERT_EQ(rc2, 0);
            EXPECT_EQ(memcmp(tv->wrap, wrap, wraplen), 0);
        }
    }
}

void test_kmunwrap(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi) //Test vector index
{
    unsigned char sek[256/8];
    int rc1,rc2;

    if(tvi < sizeof(UTV_cryspr_km_wrap)/sizeof(UTV_cryspr_km_wrap[0]))
    {
        struct UTVcryspr_km_wrap *tv = &UTV_cryspr_km_wrap[tvi];
        size_t wraplen=HAICRYPT_WRAPKEY_SIGN_SZ+tv->seklen;

        if(cryspr_m && cryspr_cb) {
            ASSERT_NE(cryspr_m->km_setkey, nullPtr);
            ASSERT_NE(cryspr_m->km_unwrap, nullPtr);

            rc1 = cryspr_m->km_setkey(
                cryspr_cb,
                false,       //Unwrap
                tv->kek,
                tv->seklen);

            rc2 = cryspr_m->km_unwrap(
                cryspr_cb,
                sek,
                tv->wrap,
                wraplen);

            ASSERT_EQ(rc1, 0);
            ASSERT_EQ(rc2, 0);
            EXPECT_EQ(memcmp(tv->sek, sek, tv->seklen), 0);
        }
    }
}


TEST_F(TestCRYSPRcypto, KMWRAP_tv1_k128)
{
    test_kmwrap(cryspr_m, cryspr_cb, 0);
}
TEST_F(TestCRYSPRcypto, KMWRAP_tv1_k192)
{
    test_kmwrap(cryspr_m, cryspr_cb, 1);
}
TEST_F(TestCRYSPRcypto, KMWRAP_tv1_k256)
{
    test_kmwrap(cryspr_m, cryspr_cb, 2);
}

TEST_F(TestCRYSPRcypto, KMUNWRAP_tv1_k128)
{
    test_kmunwrap(cryspr_m, cryspr_cb, 0);
}
TEST_F(TestCRYSPRcypto, KMUNWRAP_tv1_k192)
{
    test_kmunwrap(cryspr_m, cryspr_cb, 1);
}
TEST_F(TestCRYSPRcypto, KMUNWRAP_tv1_k256)
{
    test_kmunwrap(cryspr_m, cryspr_cb, 2);
}

/*AES ECB -----------------------------------------------------------------------------------*/
#if !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP)
/* AES-ECB test vectors */
struct UTVcryspr_aes_ecb {
    const char *name;
    unsigned char sek[256/8];       /* Stream Encrypting Key*/
    size_t seklen;
    const char *cleartxt;           /* clear text (decrypt result0 */
    unsigned char ciphertxt[32];    /* cipher text (encrypt result) */
    size_t outlen;
};

struct UTVcryspr_aes_ecb UTV_cryspr_aes_ecb[] = {
    {//[0]
        /*name */       "tv1.128",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* cleartxt */  "0000000000000000",
        /* ciphertxt */ {0xE0,0x86,0x82,0xBE,0x5F,0x2B,0x18,0xA6,0xE8,0x43,0x7A,0x15,0xB1,0x10,0xD4,0x18},
        /* cipherlen */ 16,
    },
    {//[1]
        /*name */       "tv1.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* cleartxt */  "0000000000000000",
        /* ciphertxt */ {0xCC,0xFE,0xD9,0x9E,0x38,0xE9,0x60,0xF5,0xD7,0xE1,0xC5,0x9F,0x56,0x3A,0x49,0x9D},
        /* cipherlen */ 16,
    },
    {//[2]
        /*name */       "tv1.256",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    256/8,
        /* cleartxt */  "0000000000000000",
        /* ciphertxt */ {0x94,0xB1,0x3A,0x9F,0x4C,0x09,0xD4,0xD7,0x00,0x2C,0x3F,0x11,0x7D,0xB1,0x7C,0x8B},
        /* cipherlen */ 16,
    },
    {//[3]
        /*name */       "tv2.128",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* cleartxt */  "00000000000000000000000000000000",
        /* ciphertxt */ {0xE0,0x86,0x82,0xBE,0x5F,0x2B,0x18,0xA6,0xE8,0x43,0x7A,0x15,0xB1,0x10,0xD4,0x18,
                         0xE0,0x86,0x82,0xBE,0x5F,0x2B,0x18,0xA6,0xE8,0x43,0x7A,0x15,0xB1,0x10,0xD4,0x18},
        /* cipherlen */ 32,
    },
    {//[4]
        /*name */       "tv2.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* cleartxt */  "00000000000000000000000000000000",
        /* ciphertxt */ {0xCC,0xFE,0xD9,0x9E,0x38,0xE9,0x60,0xF5,0xD7,0xE1,0xC5,0x9F,0x56,0x3A,0x49,0x9D,
                         0xCC,0xFE,0xD9,0x9E,0x38,0xE9,0x60,0xF5,0xD7,0xE1,0xC5,0x9F,0x56,0x3A,0x49,0x9D},
        /* cipherlen */ 32,
    },
    {//[5]
        /*name */       "tv2.256",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    256/8,
        /* cleartxt */  "00000000000000000000000000000000",
        /* ciphertxt */ {0x94,0xB1,0x3A,0x9F,0x4C,0x09,0xD4,0xD7,0x00,0x2C,0x3F,0x11,0x7D,0xB1,0x7C,0x8B,
                         0x94,0xB1,0x3A,0x9F,0x4C,0x09,0xD4,0xD7,0x00,0x2C,0x3F,0x11,0x7D,0xB1,0x7C,0x8B},
        /* cipherlen */ 32,
    },
};

void test_AESecb(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi,
    bool bEncrypt)
{
    unsigned char result[128];
    unsigned char *intxt;
    unsigned char *outtxt;
    int rc1,rc2;

    if(tvi < sizeof(UTV_cryspr_aes_ecb)/sizeof(UTV_cryspr_aes_ecb[0]))
    {
        struct UTVcryspr_aes_ecb *tv = &UTV_cryspr_aes_ecb[tvi];
        size_t txtlen=strnlen((const char *)tv->cleartxt, 100);
        size_t outlen=sizeof(result);

        ASSERT_NE(cryspr_m->aes_set_key, nullPtr);
        ASSERT_NE(cryspr_m->aes_ecb_cipher, nullPtr);

        rc1 = cryspr_m->aes_set_key(
            bEncrypt,
            tv->sek,    /* Stream encrypting Key */
            tv->seklen,
#if WITH_FIPSMODE
            cryspr_cb->aes_sek[0]);
#else
            &cryspr_cb->aes_sek[0]);
#endif
        if(bEncrypt) {
            intxt=(unsigned char *)tv->cleartxt;
            outtxt=(unsigned char *)tv->ciphertxt;
        }else{
            intxt=(unsigned char *)tv->ciphertxt;
            outtxt=(unsigned char *)tv->cleartxt;
        }

        rc2 = cryspr_m->aes_ecb_cipher(
            bEncrypt,                   /* true:encrypt, false:decrypt */
#if WITH_FIPSMODE
            cryspr_cb->aes_sek[0],      /* CRYpto Service PRovider AES Key context */
#else
            &cryspr_cb->aes_sek[0],      /* CRYpto Service PRovider AES Key context */
#endif
            intxt,                      /* src */
            txtlen,                     /* length */
            result,                     /* dest */
            &outlen);                   /* dest length */

        ASSERT_EQ(rc1, 0);
        ASSERT_EQ(rc2, 0);
        ASSERT_EQ(outlen, ((txtlen+(CRYSPR_AESBLKSZ-1))/CRYSPR_AESBLKSZ)*CRYSPR_AESBLKSZ);
        EXPECT_EQ(memcmp(outtxt, result, txtlen), 0);
    }
}


#define ENCRYPT true
#define DECRYPT false

TEST_F(TestCRYSPRcypto, EncryptAESecb_tv1_128)
{
    test_AESecb(cryspr_m, cryspr_cb, 0, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, EncryptAESecb_tv1_192)
{
    test_AESecb(cryspr_m, cryspr_cb, 1, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, EncryptAESecb_tv1_256)
{
    test_AESecb(cryspr_m, cryspr_cb, 2, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, EncryptAESecb_tv2_128)
{
    test_AESecb(cryspr_m, cryspr_cb, 3, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, EncryptAESecb_tv2_192)
{
    test_AESecb(cryspr_m, cryspr_cb, 4, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, EncryptAESecb_tv2_256)
{
    test_AESecb(cryspr_m, cryspr_cb, 5, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESecb_tv1_128)
{
    test_AESecb(cryspr_m, cryspr_cb, 0, DECRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESecb_tv1_192)
{
    test_AESecb(cryspr_m, cryspr_cb, 1, DECRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESecb_tv1_256)
{
    test_AESecb(cryspr_m, cryspr_cb, 2, DECRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESecb_tv2_128)
{
    test_AESecb(cryspr_m, cryspr_cb, 3, DECRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESecb_tv2_192)
{
    test_AESecb(cryspr_m, cryspr_cb, 4, DECRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESecb_tv2_256)
{
    test_AESecb(cryspr_m, cryspr_cb, 5, DECRYPT);
}
#endif /* !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP) */

/*AES CTR -----------------------------------------------------------------------------------*/
#if CRYSPR_HAS_AESCTR

struct UTVcryspr_aes_ctr {
    const char *name;
    unsigned char sek[256/8];       /* Stream Encrypting Key*/
    size_t seklen;
    unsigned char iv[CRYSPR_AESBLKSZ];/* initial vector */
    const char *cleartxt;           /* clear text (decrypt result0 */
    unsigned char ciphertxt[24];    /* cipher text (encrypt result) */
};

/* AES-CTR test vectors */
struct UTVcryspr_aes_ctr UTV_cryspr_aes_ctr[] = {
    {//[0]
        /*name */       "tv1.128",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* iv */        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* cleartxt */  "000000000000000000000000",
        /* ciphertxt */ {0x56,0xD9,0x7B,0xE4,0xDF,0xBA,0x1C,0x0B,0xB8,0x7C,0xCA,0x69,0xFA,0x04,0x1B,0x1E,
                         0x68,0xD2,0xCC,0xFE,0xCA,0x4E,0x00,0x51},
    },
    {//[1]
        /*name */       "tv1.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* iv */        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* cleartxt */  "000000000000000000000000",
        /* ciphertxt */ {0x9A,0xD0,0x59,0xA2,0x9C,0x8F,0x62,0x93,0xD8,0xC4,0x99,0x5E,0xF9,0x00,0x3B,0xE7,
                         0xFD,0x03,0x82,0xBA,0xF7,0x43,0xC7,0x7B},
    },
    {//[2]
        /*name */       "tv1.256",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    256/8,
        /* iv */        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* cleartxt */  "000000000000000000000000",
        /* ciphertxt */ {0xEC,0xA5,0xF0,0x48,0x92,0x70,0xB9,0xB9,0x9D,0x78,0x92,0x24,0xA2,0xB4,0x10,0xB7,
                         0x63,0x3F,0xBA,0xCB,0xF7,0x75,0x06,0x89}
    },
};

void test_AESctr(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi,
    bool bEncrypt)
{
    unsigned char result[100];
    unsigned char ivec[CRYSPR_AESBLKSZ];
    unsigned char *intxt;
    unsigned char *outtxt;
    int rc1,rc2;

    if(tvi < sizeof(UTV_cryspr_aes_ctr)/sizeof(UTV_cryspr_aes_ctr[0]))
    {
        struct UTVcryspr_aes_ctr *tv = &UTV_cryspr_aes_ctr[tvi];
        size_t txtlen=strnlen((const char *)tv->cleartxt, 100);

        ASSERT_NE(cryspr_m->aes_set_key, nullPtr);
        ASSERT_NE(cryspr_m->aes_ctr_cipher, nullPtr);

        rc1 = cryspr_m->aes_set_key(
            true,       //For CTR, Encrypt key is used for both encryption and decryption
            tv->sek,    /* Stream encrypting Key */
            tv->seklen,
#if WITH_FIPSMODE
            cryspr_cb->aes_sek[0]);
#else
            &cryspr_cb->aes_sek[0]);
#endif
        if(bEncrypt) {
            intxt=(unsigned char *)tv->cleartxt;
            outtxt=(unsigned char *)tv->ciphertxt;
        }else{
            intxt=(unsigned char *)tv->ciphertxt;
            outtxt=(unsigned char *)tv->cleartxt;
        }

        memcpy(ivec, tv->iv, sizeof(ivec)); //cipher ivec not const
        rc2 = cryspr_m->aes_ctr_cipher(
            bEncrypt,                   /* true:encrypt, false:decrypt */
#if WITH_FIPSMODE
            cryspr_cb->aes_sek[0],      /* CRYpto Service PRovider AES Key context */
#else
            &cryspr_cb->aes_sek[0],      /* CRYpto Service PRovider AES Key context */
#endif
            ivec,                       /* iv */
            intxt,                      /* src */
            txtlen,                     /* length */
            result);                    /* dest */

        ASSERT_EQ(rc1, 0);
        ASSERT_EQ(rc2, 0);
        EXPECT_EQ(memcmp(outtxt, result, txtlen), 0);
    }
}


#define ENCRYPT true
#define DECRYPT false

TEST_F(TestCRYSPRcypto, EncryptAESctr_tv1_128)
{
    test_AESctr(cryspr_m, cryspr_cb, 0, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, EncryptAESctr_tv1_192)
{
    test_AESctr(cryspr_m, cryspr_cb, 1, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, EncryptAESctr_tv1_256)
{
    test_AESctr(cryspr_m, cryspr_cb, 2, ENCRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESctr_tv1_128)
{
    test_AESctr(cryspr_m, cryspr_cb, 0, DECRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESctr_tv1_192)
{
    test_AESctr(cryspr_m, cryspr_cb, 1, DECRYPT);
}
TEST_F(TestCRYSPRcypto, DecryptAESctr_tv1_256)
{
    test_AESctr(cryspr_m, cryspr_cb, 2, DECRYPT);
}
#endif /* CRYSPR_HAS_AESCTR */

TEST(CRYSPR, KMUnwrap)
{
    unsigned char km_msg[] =
        "\x12\x20\x29\x01"
        "\x00\x00\x00\x00"
        "\x02\x00\x02\x00"
        "\x00\x00\x04\x04"
        "\x3a\x78\x19\x23"
        "\x7b\xc1\x8f\xe9"
        "\xcd\xe1\x09\x52"
        "\x83\xea\x5d\xf8"
        "\xed\x09\x74\x3d"
        "\x2e\x51\xe9\x7e"
        "\xc2\x96\x5d\x9d"
        "\x52\xd9\xce\xa8"
        "\x7c\xa6\x6f\x11"
        "\xe1\x8d\x3a\x73";

    HaiCrypt_Cfg crypto_cfg;
    memset(&crypto_cfg, 0, sizeof(crypto_cfg));
    crypto_cfg.flags = HAICRYPT_CFG_F_CRYPTO /*| (cdir == HAICRYPT_CRYPTO_DIR_TX ? HAICRYPT_CFG_F_TX : 0)*/;
    crypto_cfg.xport = HAICRYPT_XPT_SRT;
    crypto_cfg.cryspr = HaiCryptCryspr_Get_Instance();
    crypto_cfg.key_len = (size_t)4 * 4; // SEK len
    crypto_cfg.data_max_len = HAICRYPT_DEF_DATA_MAX_LENGTH;    //MTU
    crypto_cfg.km_tx_period_ms = 0;//No HaiCrypt KM inject period, handled in SRT;
    crypto_cfg.km_refresh_rate_pkt = HAICRYPT_DEF_KM_REFRESH_RATE;
    crypto_cfg.km_pre_announce_pkt = 0x10000; //SRT_CRYPT_KM_PRE_ANNOUNCE;

    const char secret[] = "passphrase";
    HaiCrypt_Secret m_CryptoSecret;
    memset(&m_CryptoSecret, 0, sizeof(m_CryptoSecret));
    m_CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    m_CryptoSecret.len = sizeof(secret) - 1;
    memcpy((m_CryptoSecret.str), secret, m_CryptoSecret.len);
    crypto_cfg.secret = m_CryptoSecret;

    HaiCrypt_Handle h_Crypto = NULL;
    if (HaiCrypt_Create(&crypto_cfg, (&h_Crypto)) != HAICRYPT_OK)
    {
        return;
    }

    uint32_t srtdata_out[SRTDATA_MAXSIZE];
    size_t   len_out = 0;
    //int res = cryspr_m->processSrtMsg_KMREQ(km_msg, sizeof km_msg, 5,
    //        (srtdata_out), (len_out));
    int rc = HaiCrypt_Rx_Process(h_Crypto, km_msg, sizeof(km_msg) - 1, NULL, NULL, 0);

    //hcryptCtx_Rx_ParseKM
    unsigned char data_pkt_bytes[] =
        "\x66\xbb\x9e\xfb"
        "\xc8\x00\x00\x01"
        "\x00\x00\x5c\xc4"
        "\x01\x61\x72\x91"
        // DATA:
        "\xd7\x38\xbc\xe5\xab\x56\xfe\x05\x9d\x70\xe9\xc2\xfc\xdf\x74\x8b"
        "\xfb\xeb\x75\xed\x3e\x89\xda\x03\x66\xa8\x4a\x4e\xc4\x13\x83\x0d"
        "\x63\x61\x30\xdf\xc0\x5a\xdc\xf0\x03\xcf\x5c\xf1\x2d\xc1\xeb\x8b"
        "\xb0\x39\x90\xf3\x72\x7b\x0d\x0e\x5e\x76\xff\x71\xdb\xcf\x51\x19"
        "\x88\x2d\x5a\x48\xe8\xd9\x13\x1b\x9a\xa9\x2f\xca\x94\x45\xc7\xb6"
        "\xb5\xcf\xd0\xdc\xf9\xd6\xf6\xec\x2b\x38\xdb\x4a\x62\x34\xa1\x4c"
        "\x0a\xe1\xf7\x4d\xf2\xeb\x2c\x10\x02\xb3\x0d\xc5\xfc\xc6\xfb\x39"
        "\x05\xac\x23\xe3\x40\xc4\x65\x30\x0b\x42\x06\x6d\x4a\x9f\x3c\x18"
        "\x41\xec\xa5\x62\x9a\xde\x18\x8f\x82\xcf\x27\x2c\xc0\x44\x10\xa3"
        "\xbe\xe9\x91\x35\x69\x92\xe3\xf4\x1a\x32\x15\x0a\x87\xdb\xcb\x31"
        "\x41\xa9\x9a\x68\x1e\xf0\x61\xeb\x7e\x16\xfd\x0f\xfc\xf5\x69\xe5"
        "\xa5\xb5\xcf\x1b\xb9\x9b\xec\xb2\xc5\x9d\xaa\xa7\x91\x2a\x4e\x75"
        "\x1a\xac\xf4\x4b\x89\x7c\xad\x09\x36\x7e\x89\x4e\x77\x82\x5c\xea"
        "\xa8\x95\x39\x77\xd5\x99\x98\xec\x14\x2c\xc8\xdf\x9a\x20\x29\x04"
        "\xcb\x51\xa3\x31\x69\xb5\x48\x40\xf3\xc7\x25\xb0\xe8\x0b\x64\xc4"
        "\xbb\x34\x5c\xf4\xa5\x1d\x6a\x9f\x26\x67\x08\x02\x10\x35\x6e\xc9"
        "\xd4\x44\x27\x84\xac\x54\xf3\xe8\x02\x56\xb9\x5b\xd5\x98\x65\x7b"
        "\x96\x9d\x85\x79\xd7\x1c\x1c\x3e\x3c\x50\x66\x4b\x27\x71\xfd\x27"
        "\x70\xcd\x35\xb2\xaa\xea\x91\x88\xd0\x94\x6c\x21\xdb\x2e\x93\xc6"
        "\x11\x64\xe6\x9d\xfb\xfb\xee\x6d\x4c\x42\x05\xe7\x8f\x22\xd1\xb9"
        "\xfd\xc2\x86\x7a\x48\x9a\x2b\xbc\x7f\x35\x99\x26\xbb\x9b\x85\x35"
        "\x6f\xa0\x6d\x21\x0a\x72\x6f\x3c\xd4\x65\x20\x56\xff\x50\xee\x81"
        "\x10\x52\x13\x21\x88\x23\x23\xc7\xe0\x57\xf4\xb6\x3d\x45\xa0\x78"
        "\x05\x94\x26\x0e\x4c\x4d\x67\xfd\x3b\xe6\xd9\x6a\x9f\x84\x1e\x30"
        "\xe5\x35\x15\x95\xfa\xca\xdf\xa4\x64\x4b\x5a\xa7\xeb\xe6\x8b\x09"
        "\x01\x1e\xb3\x56\x30\x4d\x4d\xe9\x53\x01\x8b\x37\x08\x83\xb7\xe2"
        "\xb2\xc0\x8d\x51\x50\x88\xe0\x63\xff\x15\xc7\x00\x09\xd8\xd4\x4e"
        "\xa7\x49\x13\x62\xc9\x7c\x4f\x54\xd5\xf5\xab\x50\x4a\xf0\x08\x07"
        "\x45\x3f\x03\x9f\xd3\xb9\x98\xb6\x68\xea\x09\xc8\x24\xe9\x79\x29"
        "\x2c\x9a\xd7\x34\xb4\x39\x30\x04\x4d\x17\x5e\x58\xc3\xd8\xbc\x1b"
        "\x31\x13\xdb\x49\x49\xe1\xdb\xde\xad\xa8\x32\xd1\xa7\xd2\x16\xe9"
        "\x79\x01\x03\xa4\x2a\x0e\x2d\x7b\x1f\xeb\x11\xe8\x73\xc3\x11\x76"
        "\x39\xf1\x46\x4d\x5e\xc8\x36\x58\x6d\xed\x64\xbb\x72\x66\xa9\xc9"
        "\xf2\xff\x21\x90\x2a\x7e\x7e\x48\xe0\xfb\xcb\x54\x32\xc1\x8a\xd7"
        "\x94\xb5\x01\x6e\x5b\x0a\x31\x3d\x59\xc0\x3d\x61\x95\xda\x26\xee"
        "\xd3\x66\x3a\xea\x4d\x74\x42\x26\xf6\x83\x63\xe7\xdd\x82\xc8\x3c"
        "\x4c\xe9\xa5\x86\x22\x8d\x66\x4e\x4d\xa7\x89\xf0\xa8\x05\xd0\x7b"
        "\x48\x76\x54\xd4\x85\xc6\x2e\x80\xff\x42\x54\x45\x39\xc7\x78\x1d"
        "\x70\xf4\xfd\x8a\x66\x3b\x6e\x2c\xf2\x67\x01\x06\x18\xdc\xa8\xe9"
        "\x22\xaf\xa7\x33\x91\xb8\x8e\x58\xc5\xa4\x02\xce\xf4\xbc\xe5\x49"
        "\x36\x74\x15\x93\xfb\x14\xc0\x7f\x67\x49\xf5\x51\x1f\x0b\xcd\xb6"
        "\x6c\x3d\x05\x69\x66\x60\xa0\x4a\xb9\x2c\xda\x97\x81\x3c\x34\x83"
        "\x0b\x2d\x65\xaa\x19\x86\xd9\xd7\x3e\x63\xc9\x16\x1a\xd3\xd4\xa9"
        "\x8d\x81\x01\x7b\xef\x4d\x2c\x78\xcb\xc1\x13\x4e\xf0\x1e\xda\xaf"
        "\xb8\x89\xef\x7d\xcd\x4e\xd0\x3a\xb8\x85\x01\x44\xf9\x9c\xef\x60"
        "\x2d\xd2\x31\xd2\x5a\x60\x4f\x29\x68\x26\x0f\xfe\xaa\x9d\x78\x2f"
        "\x67\x97\x89\x56\x14\x0f\x8d\xd9\x47\x9b\x74\x76\x22\x6c\xb8\xcb"
        "\x2a\xc9\x9f\xf9\x85\xda\xa6\xeb\xa8\x31\x06\x01\x5a\x91\xfd\x87"
        "\xcc\x22\xbe\x61\xbd\xdb\x8a\xb9\xe5\x70\x04\x8f\xfd\xbe\x2a\x49"
        "\x9f\x9f\x79\x8a\xb7\x1c\xb8\xe4\x8d\x6c\x18\x53\x08\xf4\x23\xbd"
        "\x0e\x76\x47\x7d\x2c\x47\xf6\xdf\x79\x17\x74\xdd\x17\x13\xf4\x54"
        "\xd0\xd3\x04\x75\x3c\x0e\x98\x9d\xe4\x17\xb5\xa6\x92\x6f\xcc\xb3"
        "\xfd\x24\x33\x7c\x82\x33\x90\xd2\x1c\xfd\xd8\x52\xbb\x5f\x73\xcd"
        "\xbc\xb8\x3c\xb2\xbe\x12\x67\xb0\x2a\x94\x72\xb7\x39\x7a\xc9\x93"
        "\x0d\x2e\x0a\x35\x80\x9c\x99\x50\x9a\xb0\x0b\xb3\x59\xf4\x5b\xd7"
        "\x24\xa1\x62\x05\x75\x14\x16\x9d\xa8\xac\x2f\x70\x29\x2b\xf0\x99"
        "\x0e\xcb\x35\x7b\x08\x7d\xcd\x82\x13\x50\xff\x87\xdc\x2d\x06\xea"
        "\x0b\xe4\x79\xc3\x1d\xbc\xc3\xde\xce\x3a\x9f\xbf\x7c\xc6\x45\x37"
        "\x70\x9d\x81\x05\x19\x05\x75\x04\x45\x26\x0c\xa0\xf9\x8f\x54\xf8"
        "\x4a\x4f\xe0\xc3\x17\x8d\xeb\x47\x46\x5d\x86\x39\x10\x76\x81\xde"
        "\x6b\x76\x95\xb3\xbd\xdb\xf7\xdf\xba\xe8\x82\x3e\x0b\xe0\xe0\x24"
        "\x4f\x6c\x56\xb2\xa6\x79\xe6\x20\xe6\x2d\x8f\x7f\xba\x4d\x74\x3a"
        "\x8c\xd8\xeb\xce\x08\x67\x7d\x30\xba\xfa\x0c\x8e\x40\xf8\x81\xdb"
        "\xfe\xf9\x80\xcd\x12\x99\x01\xbd\x2c\x60\x05\x4e\x37\xe3\x1c\x76"
        "\x9f\x74\x3f\x38\xe0\x7d\x19\x96\x08\xd8\xdb\x0d\x22\xf7\xb9\x4f"
        "\xdd\x99\x85\x76\xec\x47\x3b\x9e\xe9\x15\x16\x9e\xe0\x4c\x3b\xef"
        "\x59\x4a\xf1\x09\x38\x8e\xfd\x44\x24\xec\x0b\xb7\x6c\x6f\x64\xad"
        "\x9f\x6e\xaf\x59\x88\x3e\xd8\x61\xe9\xc1\x6c\x2d\x54\x5a\xe1\x88"
        "\xe3\xca\x43\xfe\xae\xd4\x5d\xa1\x92\x97\xc0\x00\xe9\x43\x48\xb5"
        "\x94\x73\xae\xff\x6a\xa7\xb8\xa4\xd4\x95\x70\x4e\x8c\x03\x04\x82"
        "\xe3\xab\xe6\xc8\xf2\xea\x72\x14\x59\x4c\x55\xa5\x7d\xa6\xba\xd4"
        "\xd2\x52\xc7\x4a\x7a\x60\xea\x32\x45\x85\x40\xd9\x84\x8e\x48\x07"
        "\xb4\x5c\xab\x50\x52\x68\xc7\xfc\x7e\x2b\xf3\xa3\xe4\xe8\xe0\x54"
        "\xdf\x24\xa3\x24\x4e\xc0\x40\x22\x88\x8e\x63\x92\xd4\x3b\xf2\x31"
        "\xd7\xda\xa0\xe9\xfc\x68\xe5\x8a\x80\xd7\x4e\x34\xaf\x38\xe9\x53"
        "\xcd\x63\x18\xda\xa4\x3d\x46\x13\xb0\xd2\xbe\x2e\x33\xb2\xd7\xf5"
        "\x4b\x29\x7e\x15\x1d\x95\x26\x1a\x1f\x08\x98\xce\x66\x6a\x25\x8c"
        "\x85\xb8\x98\xf5\x12\xf8\x7d\xa1\x80\xc7\xc4\x36\xad\x8c\x72\x0c"
        "\xbb\x3c\x32\xbd\xca\xc7\x00\x66\x35\xbd\x1d\xd1\xf4\x2c\x5e\xaa"
        "\xca\x16\xc9\xc2\xcc\x4f\x8f\x0b\xc2\x9a\x05\x2b\x18\x18\xee\x0e"
        "\x0a\xe4\xe4\xeb\xff\x01\x6f\xb5\x94\x7c\x21\x89\x38\xe1\x17\x82"
        "\x84\x86\xf7\x9d\xaf\xb7\x9d\x56\x26\x87\x6c\x31\x33\x2b\xd4\x80"
        "\x4a\xdc\x02\x7d";

    HaiCrypt_Rx_Data(h_Crypto, data_pkt_bytes, data_pkt_bytes + 16, sizeof(data_pkt_bytes) - 16 - 1);
    
}

#endif /* SRT_ENABLE_ENCRYPTION */
