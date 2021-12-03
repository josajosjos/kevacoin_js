// Copyright (c) 2012-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>

#include <base58.h>
#include <script/script.h>
#include <uint256.h>
#include <util.h>
#include <utilstrencodings.h>
#include <test/test_bitcoin.h>

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

static const std::string strSecret1C = "MdT1ZhNgpaNhvtcobunP8MHdxbBB1HLUf1FJAUkEmQqtSLtjCCo4";
static const std::string strSecret2C = "MesUJk4iXuXdau3LgYX1Se1VPzFyGkm9S4MEPRfXrYnqmaRySqDV";
static const std::string addr1C = "VRwo4qgyARQSy331bhyBPrdbp9pjgjPZiC";
static const std::string addr2C = "VMJySeGqf5o9YNkw6ma5GGWYTL5Z4Kuisw";

static const std::string strAddressBad = "Lbi6bpMhSwp2CXkivEeUK9wzyQEFzHDfSr";


BOOST_FIXTURE_TEST_SUITE(key_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(key_test1)
{
    CBitcoinSecret bsecret1C, bsecret2C, baddress1;
    BOOST_CHECK( bsecret1C.SetString(strSecret1C));
    BOOST_CHECK( bsecret2C.SetString(strSecret2C));
    BOOST_CHECK(!baddress1.SetString(strAddressBad));

    CKey key1C = bsecret1C.GetKey();
    BOOST_CHECK(key1C.IsCompressed() == true);
    CKey key2C = bsecret2C.GetKey();
    BOOST_CHECK(key2C.IsCompressed() == true);

    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    BOOST_CHECK(key1C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(key2C.VerifyPubKey(pubkey2C));

#if 0
    // Kevacoin: addr1C is Script-hash-addresses, cannot be compared
    // to legacy public key hash.
    BOOST_CHECK(DecodeDestination(addr1C) == CTxDestination(pubkey1C.GetID()));
    BOOST_CHECK(DecodeDestination(addr2C) == CTxDestination(pubkey2C.GetID()));
#endif

    for (int n=0; n<16; n++)
    {
        std::string strMsg = strprintf("Very secret message %i: 11", n);
        uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());

        // normal signatures

        std::vector<unsigned char> sign1C, sign2C;

        BOOST_CHECK(key1C.Sign(hashMsg, sign1C));
        BOOST_CHECK(key2C.Sign(hashMsg, sign2C));

        BOOST_CHECK( pubkey1C.Verify(hashMsg, sign1C));
        BOOST_CHECK(!pubkey1C.Verify(hashMsg, sign2C));

        BOOST_CHECK(!pubkey2C.Verify(hashMsg, sign1C));
        BOOST_CHECK( pubkey2C.Verify(hashMsg, sign2C));

        // compact signatures (with key recovery)

        std::vector<unsigned char> csign1C, csign2C;

        BOOST_CHECK(key1C.SignCompact(hashMsg, csign1C));
        BOOST_CHECK(key2C.SignCompact(hashMsg, csign2C));

        CPubKey rkey1C, rkey2C;

        BOOST_CHECK(rkey1C.RecoverCompact(hashMsg, csign1C));
        BOOST_CHECK(rkey2C.RecoverCompact(hashMsg, csign2C));

        BOOST_CHECK(rkey1C == pubkey1C);
        BOOST_CHECK(rkey2C == pubkey2C);
    }

    // test deterministic signing

    std::vector<unsigned char> detsigc;
    std::string strMsg = "Very deterministic message";
    uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());
    BOOST_CHECK(key1C.Sign(hashMsg, detsigc));
    BOOST_CHECK(detsigc == ParseHex("304402201e5a963b63e4b7f4a22ab47080fa745e313d722e92e1c38127f6675cbac4851d02205a5e67b5c1f72a496424a33e68692af9373e94ffcafbc39e4270dbd5017f078a"));
    BOOST_CHECK(key2C.Sign(hashMsg, detsigc));
    BOOST_CHECK(detsigc == ParseHex("3044022041d16f2e09478c24599a94710a12025f77431af913d9322f9baeac5d810d968f022056cc1b07e17b4f803454cbe3d842168ac4b04d676dfd18d601079b662c1df443"));
    BOOST_CHECK(key1C.SignCompact(hashMsg, detsigc));
    BOOST_CHECK(detsigc == ParseHex("1f1e5a963b63e4b7f4a22ab47080fa745e313d722e92e1c38127f6675cbac4851d5a5e67b5c1f72a496424a33e68692af9373e94ffcafbc39e4270dbd5017f078a"));
    BOOST_CHECK(key2C.SignCompact(hashMsg, detsigc));
    BOOST_CHECK(detsigc == ParseHex("2041d16f2e09478c24599a94710a12025f77431af913d9322f9baeac5d810d968f56cc1b07e17b4f803454cbe3d842168ac4b04d676dfd18d601079b662c1df443"));
}

BOOST_AUTO_TEST_SUITE_END()
