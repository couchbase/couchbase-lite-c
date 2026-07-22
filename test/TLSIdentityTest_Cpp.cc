//
// TLSIdentityTest_Cpp.cc
//
// Copyright © 2026 Couchbase. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "CBLTest_Cpp.hh"
#include "TLSIdentityTest.hh"
#include "URLEndpointListenerTest.hh"     // for the shared readFile() asset helper, and the
                                          // listener/replicator fixture used below
#include "fleece/Mutable.hh"
#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "cbl++/CouchbaseLite.hh"
#include "cbl++/TLSIdentity.hh"

#ifdef COUCHBASE_ENTERPRISE

using namespace std::chrono;
using namespace fleece;
using namespace cbl;

class TLSIdentityTest_Cpp : public CBLTest_Cpp {
};

TEST_CASE_METHOD(TLSIdentityTest_Cpp, "C++ Self-Signed Cert Identity", "[TLSIdentity]") {
    // Load a known RSA private key (instead of the private, test-only
    // CBLKeyPair_GenerateRSAKeyPair API, which has no C++ wrapper) via the public
    // KeyPair::createWithPrivateKeyData factory.
    string pem = URLEndpointListenerTest::readFile("private_key_of_self_signed_cert.pem");
    KeyPair keypair = KeyPair::createWithPrivateKeyData(slice{pem.c_str(), pem.size() + 1});

    MutableDict attributes = MutableDict::newDict();
    attributes[kCBLCertAttrKeyCommonName] = TLSIdentityTest::CN;

    auto expire = system_clock::now() + TLSIdentityTest::OneYear;

    TLSIdentity identity = TLSIdentity::createIdentity(
        kCBLKeyUsagesServerAuth, keypair, attributes,
        duration_cast<milliseconds>(TLSIdentityTest::OneYear).count());
    CHECK(identity);

    Cert cert = identity.certificates();
    CHECK(cert);
    CHECK(!cert.nextInChain());

    // CBLTimestamp is in milliseconds; can differ by up to 60 seconds.
    CBLTimestamp certExpire  = identity.expiration();
    CBLTimestamp paramExpire = duration_cast<milliseconds>(expire.time_since_epoch()).count();
    CHECK(std::abs(certExpire - paramExpire) / 1000 < seconds(61).count());

    CHECK(cert.subjectNameComponent(kCBLCertAttrKeyCommonName) == TLSIdentityTest::CN.asString());
    CHECK(cert.subjectName() == "CN=" + TLSIdentityTest::CN.asString());

    // The cert's public key should match the input KeyPair's.
    string pubDigest1 = keypair.publicKeyDigest();
    KeyPair pkOfCert = cert.publicKey();
    CHECK(pkOfCert);
    string pubDigest2 = pkOfCert.publicKeyDigest();
    CHECK( (!pubDigest1.empty() && pubDigest1 == pubDigest2) );
}

#ifdef __APPLE__

TEST_CASE_METHOD(TLSIdentityTest_Cpp, "C++ External Keys", "[TLSIdentity]") {
    constexpr size_t keySizeInBits  = 2048;

    TLSIdentityTest::ExternalKey* externalKey = TLSIdentityTest::ExternalKey::generateRSA(keySizeInBits);
    REQUIRE(externalKey);

    bool badPublicKeyData = false;
    bool throwingPublicKeyData = false;
    SECTION("Successful Callback") {
        badPublicKeyData = false;
    }
    SECTION("Failing Callback") {
        badPublicKeyData = true;
    }
    SECTION("Throwing Callback") {
        // Regression test for the createWithExternalKey trampolines catching exceptions:
        // without that, this throw would cross a noexcept boundary in litecore and
        // std::terminate() the whole test process instead of just failing this operation.
        throwingPublicKeyData = true;
    }

    KeyPair::ExternalKeyHolder holder(
        externalKey,
        [badPublicKeyData, throwingPublicKeyData](void* ctx) -> std::optional<alloc_slice> {
            if (throwingPublicKeyData) throw std::runtime_error("publicKeyData boom");
            if (badPublicKeyData) return std::nullopt;
            return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->publicKeyData();
        },
        [](void* ctx, slice input) -> std::optional<alloc_slice> {
            return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->decrypt(input);
        },
        [](void* ctx, KeyPair::SignatureDigestAlgorithm digestAlgorithm, slice input) -> std::optional<alloc_slice> {
            return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->sign(digestAlgorithm, input);
        },
        [](void* ctx) { delete (TLSIdentityTest::ExternalKey*)ctx; }
    );

    KeyPair keypair = KeyPair::createWithExternalKey(keySizeInBits, std::move(holder));

    string publicKeyData   = keypair.publicKeyData();
    string publicKeyDigest = keypair.publicKeyDigest();
    string privateKeyData  = keypair.privateKeyData();

    if (badPublicKeyData || throwingPublicKeyData) {
        CHECK(publicKeyData.empty());
        CHECK(publicKeyDigest.empty());
    } else {
        CHECK(!publicKeyData.empty());
        CHECK(!publicKeyDigest.empty());
    }
    // Private key data is not available for external keys.
    CHECK(privateKeyData.empty());
}

// Ported from TLSIdentityTest.cc's "Self-Signed Identity with PrivateKey Callback": builds the
// identity via the C++ API, then bridges into the still-C CBLURLEndpointListener/replicator via
// KeyPair::ref()/TLSIdentity::ref() -- there's no C++ URLEndpointListener wrapper yet. This is
// the one deferred test worth porting now (see the comment further down), since it's the only
// one that actually drives ExternalKeyHolder's decrypt/sign callbacks through a real TLS
// handshake; the others mostly re-exercise listener/replicator plumbing that's unrelated to
// this header.
TEST_CASE_METHOD(URLEndpointListenerTest, "C++ Self-Signed Identity with PrivateKey Callback", "[TLSIdentity]") {
    constexpr size_t keySizeInBits = 2048;

    // Creates a KeyPair wrapping an Apple Keychain-backed external key (callbacks defined in
    // TLSIdentityTest+Apple.mm), counting how many times publicKeyData/sign/free are invoked.
    TLSIdentityTest::ExternalKey* externalKey = TLSIdentityTest::ExternalKey::generateRSA(keySizeInBits);
    REQUIRE(externalKey);

    int counterPublicKeyData = 0;
    int counterSign          = 0;
    int counterFree          = 0;

    KeyPair::ExternalKeyHolder holder(
        externalKey,
        [&counterPublicKeyData](void* ctx) -> std::optional<alloc_slice> {
            counterPublicKeyData++;
            return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->publicKeyData();
        },
        [](void* ctx, slice input) -> std::optional<alloc_slice> {
            return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->decrypt(input);
        },
        [&counterSign](void* ctx, KeyPair::SignatureDigestAlgorithm digestAlgorithm, slice input) -> std::optional<alloc_slice> {
            counterSign++;
            return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->sign(digestAlgorithm, input);
        },
        [&counterFree](void* ctx) {
            counterFree++;
            delete (TLSIdentityTest::ExternalKey*)ctx;
        });

    KeyPair keypair = KeyPair::createWithExternalKey(keySizeInBits, std::move(holder));

    MutableDict attributes = MutableDict::newDict();
    attributes[kCBLCertAttrKeyCommonName] = TLSIdentityTest::CN;
    TLSIdentity identity = TLSIdentity::createIdentity(
        kCBLKeyUsagesServerAuth, keypair, attributes,
        duration_cast<milliseconds>(TLSIdentityTest::OneYear).count());
    CHECK(identity);

    // Initializes a listener with a config using the identity, bridged via TLSIdentity::ref()
    // since CBLURLEndpointListenerConfiguration is still a plain C API.
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        false      // disableTLS
    };
    listenerConfig.tlsIdentity = identity.ref();
    listenerConfig.authenticator = CBLListenerAuth_CreatePassword([](void* ctx, FLString usr, FLString psw) {
        return usr == TLSIdentityTest::kUser && psw == TLSIdentityTest::kPassword;
    }, nullptr);
    REQUIRE(listenerConfig.authenticator);

    CBLError outError{};
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &outError);
    CHECK(outError.code == 0);
    CHECK(listener);

    // Starts the listener.
    outError.code = 0;
    bool started = CBLURLEndpointListener_Start(listener, &outError);
    CHECK(outError.code == 0);
    CHECK(started);

    // Starts a single shot replicator connecting to the listener.
    std::vector<CBLCollectionConfiguration> colConfigs;
    configOneShotReplicator(listener, colConfigs);
    config.authenticator = CBLAuth_CreatePassword(TLSIdentityTest::kUser, TLSIdentityTest::kPassword);
    REQUIRE(config.authenticator);

    replicate();

    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);

    CBLURLEndpointListener_Release(listener);
    CBLListenerAuth_Free(listenerConfig.authenticator);

    // Release the identity and keypair now (rather than waiting for them to go out of scope)
    // so the counters below reflect the callbacks having actually fired.
    identity = nullptr;
    keypair  = nullptr;
    CHECK(counterFree == 1);
    CHECK(counterSign > 0);
    CHECK(counterPublicKeyData > 0);
}

#endif //#ifdef __APPLE__

#if !defined(__linux__) && !defined(__ANDROID__)

TEST_CASE_METHOD(TLSIdentityTest_Cpp, "C++ Identity With Label", "[TLSIdentity]") {
    // Clean up any identity left behind by a previous run.
    (void) TLSIdentity::deleteIdentityWithLabel(TLSIdentityTest::Label);

    MutableDict attributes = MutableDict::newDict();
    attributes[kCBLCertAttrKeyCommonName] = TLSIdentityTest::CN;
    TLSIdentity identity = TLSIdentity::createIdentity(
        kCBLKeyUsagesServerAuth, attributes,
        duration_cast<milliseconds>(TLSIdentityTest::OneYear).count(),
        TLSIdentityTest::Label);
    CHECK(identity);

    TLSIdentity identity2 = TLSIdentity::identityWithLabel(TLSIdentityTest::Label);
    CHECK(identity2);

    Cert cert = identity2.certificates();
    CHECK(cert);
    string subjectName = cert.subjectName();
    REQUIRE(subjectName.rfind("CN=", 0) == 0);
    CHECK(subjectName.substr(3) == TLSIdentityTest::CN.asString());

    CHECK(TLSIdentity::deleteIdentityWithLabel(TLSIdentityTest::Label));

    TLSIdentity identity3 = TLSIdentity::identityWithLabel(TLSIdentityTest::Label);
    CHECK(!identity3);
}

#endif //#if !defined(__linux__) && !defined(__ANDROID__)

TEST_CASE_METHOD(TLSIdentityTest_Cpp, "C++ Get CertChain", "[TLSIdentity]") {
    string pemChain = URLEndpointListenerTest::readFile("cert_chain.pem");
    Cert cert = Cert::createWithData(slice{pemChain});
    CHECK(cert);

    // From the Cert object, iterate through the cert chain, verifying the CN and
    // that each certificate is currently valid. `iter` is reassigned each pass, so
    // the wrapper's RAII releases the previous certificate automatically.
    constexpr const char* CNs[] = {
        "Intermediate3 CA", "Intermediate2 CA", "Intermediate1 CA", "My Root CA"
    };
    constexpr int CNCount = sizeof(CNs) / sizeof(CNs[0]);

    CBLTimestamp now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    int i = 0;
    Cert iter = cert;
    for ( ; iter && i < CNCount; iter = iter.nextInChain(), i++) {
        CHECK(iter.subjectNameComponent("CN") == CNs[i]);
        CBLTimestamp created, expires;
        iter.validTimespan(&created, &expires);
        CHECK((created < now && now < expires));
    }
    CHECK(i == CNCount);
    CHECK(!iter);
}

// The following tests from TLSIdentityTest.cc exercise a TLSIdentity together with a
// CBLURLEndpointListener / replicator. They mostly re-exercise listener/replicator plumbing
// rather than anything specific to this header, so they're left for whenever
// include/cbl++/URLEndpointListener.hh gets a C++ wrapper (unlike "Self-Signed Identity with
// PrivateKey Callback" above, which was worth porting now via KeyPair::ref()/TLSIdentity::ref()
// since it's the only one that drives ExternalKeyHolder's decrypt/sign callbacks through a real
// TLS handshake). Revisit once that wrapper exists:
//   - "Use Identity Created with Label"
//   - "Self-Signed Identity with Private KeyData"
//   - "Identity from KeyPair and Certs"
#if 0

TEST_CASE_METHOD(URLEndpointListenerTest, "C++ Use Identity Created with Label", "[TLSIdentity]") {
    // TODO: port using cbl::TLSIdentity + cbl::URLEndpointListener once the latter exists.
}

TEST_CASE_METHOD(URLEndpointListenerTest, "C++ Self-Signed Identity with Private KeyData", "[TLSIdentity]") {
    // TODO: port using cbl::TLSIdentity + cbl::URLEndpointListener once the latter exists.
}

TEST_CASE_METHOD(URLEndpointListenerTest, "C++ Identity from KeyPair and Certs", "[TLSIdentity]") {
    // TODO: port using cbl::TLSIdentity + cbl::URLEndpointListener once the latter exists.
}

#endif // #if 0

#endif // #ifdef COUCHBASE_ENTERPRISE
