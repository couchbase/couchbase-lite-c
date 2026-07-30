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
#include "URLEndpointListenerTest_Cpp.hh" // for the shared readFile() asset helper, and the
                                          // C++ listener/replicator fixture used below
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

using namespace std;
using namespace std::chrono;
using namespace fleece;
using namespace cbl;

class TLSIdentityTest_Cpp : public CBLTest_Cpp {
};

TEST_CASE_METHOD(TLSIdentityTest_Cpp, "C++ Self-Signed Cert Identity", "[TLSIdentity]") {
    // Load a known RSA private key (instead of the private, test-only
    // CBLKeyPair_GenerateRSAKeyPair API, which has no C++ wrapper) via the public
    // KeyPair::createWithPrivateKeyData factory.
    string pem = URLEndpointListenerTest_Cpp::readFile("private_key_of_self_signed_cert.pem");
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
// identity via the C++ API and drives it through a real TLS handshake using the C++
// URLEndpointListener wrapper -- this is the one test that actually exercises
// ExternalKeyHolder's decrypt/sign callbacks through a live connection; the others mostly
// re-exercise listener/replicator plumbing unrelated to this header (see further down).
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Self-Signed Identity with PrivateKey Callback", "[TLSIdentity]") {
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

    // Initializes a listener with a config using the identity. Scoped in a block so the
    // listener and its config -- both of which retain their own copy of `identity` (the C++
    // config struct stores a TLSIdentity by value, unlike the C API's borrowed raw pointer
    // field, and the listener itself retains its own internal copy too) -- release those
    // copies before the counterFree check below.
    {
        createNumberedDocsWithPrefix(cy[0], 20, "doc2");
        createNumberedDocsWithPrefix(cy[1], 20, "doc2");
        URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
        listenerConfig.disableTLS = false;
        listenerConfig.tlsIdentity = identity;
        listenerConfig.authenticator = ListenerAuthenticator::passwordAuthenticator(
            [](std::string_view usr, std::string_view psw) {
                return slice(usr) == TLSIdentityTest::kUser && slice(psw) == TLSIdentityTest::kPassword;
            });

        URLEndpointListener listener(listenerConfig);
        listener.start();

        // Starts a single shot replicator connecting to the listener.
        configOneShotReplicator(listenerConfig, listener);
        config.authenticator = Authenticator::basicAuthenticator((std::string_view)TLSIdentityTest::kUser,
                                                                 (std::string_view)TLSIdentityTest::kPassword);

        replicate();

        listener.stop();
    }

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
    string pemChain = URLEndpointListenerTest_Cpp::readFile("cert_chain.pem");
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

#if !defined(__linux__) && !defined(__ANDROID__)

TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Use Identity Created with Label", "[TLSIdentity]") {
    TLSIdentity identity;

    SECTION("First Pass - Create Identity with Label") {
        // Clean the identity from the system.
        (void)TLSIdentity::deleteIdentityWithLabel(TLSIdentityTest::Label);

        MutableDict attributes = MutableDict::newDict();
        attributes[kCBLCertAttrKeyCommonName] = TLSIdentityTest::CN;
        identity = TLSIdentity::createIdentity(kCBLKeyUsagesServerAuth, attributes,
                                               duration_cast<milliseconds>(TLSIdentityTest::OneYear).count(),
                                               TLSIdentityTest::Label);
    }

    SECTION("Second Pass - Retrieve the Identity by the Label") {
        identity = TLSIdentity::identityWithLabel(TLSIdentityTest::Label);
        identityLabelsToDelete.emplace_back(TLSIdentityTest::Label);
    }

    CHECK(identity);

    // Initializes a listener with a config with the TLS identity.
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;
    listenerConfig.tlsIdentity = identity;
    listenerConfig.authenticator = ListenerAuthenticator::certAuthenticator(
        [](const Cert& cert) {
            return cert.subjectName() == "CN=URLEndpointListener_Client";
        });

    URLEndpointListener listener(listenerConfig);
    listener.start();

    // Starts a single shot replicator to the listener.
    configOneShotReplicator(listenerConfig, listener);
    TLSIdentity clientIdentity = createTLSIdentity(false, false);
    REQUIRE(clientIdentity);
    config.authenticator = Authenticator::certificateAuthenticator(clientIdentity);

    replicate();

    // Checks that the replicator stopped without an error.
    listener.stop();
}

// T0011-3 TestCreateAndUseSelfSignedIdentityWithPrivateKeyData
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Self-Signed Identity with Private KeyData", "[TLSIdentity]") {
    // Gets a pre-created RSA private key data in PEM format with a password from file.
    string pemString = URLEndpointListenerTest_Cpp::readFile("private_key_pass.pem");
    KeyPair privateKey = KeyPair::createWithPrivateKeyData(slice{pemString.c_str(), pemString.size() + 1}, "pass");
    CHECK(privateKey);

    // Create a self-signed identity with the KeyPair and CN = "CBL-Server".
    MutableDict attributes = MutableDict::newDict();
    attributes[kCBLCertAttrKeyCommonName] = TLSIdentityTest::CN;
    TLSIdentity identity = TLSIdentity::createIdentity(
        kCBLKeyUsagesServerAuth, privateKey, attributes,
        duration_cast<milliseconds>(TLSIdentityTest::OneYear).count());
    CHECK(identity);

    // Initializes a listener with a config with the identity.
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;
    listenerConfig.tlsIdentity = identity;
    listenerConfig.authenticator = ListenerAuthenticator::passwordAuthenticator(
        [](std::string_view usr, std::string_view psw) {
            return slice(usr) == TLSIdentityTest::kUser && slice(psw) == TLSIdentityTest::kPassword;
        });

    URLEndpointListener listener(listenerConfig);
    listener.start();

    // Starts a single shot replicator to the listener.
    configOneShotReplicator(listenerConfig, listener);
    config.authenticator = Authenticator::basicAuthenticator((std::string_view)TLSIdentityTest::kUser,
                                                             (std::string_view)TLSIdentityTest::kPassword);

    replicate();

    // Checks that the replicator stopped without an error.
    listener.stop();
}

#endif // #if !defined(__linux__) && !defined(__ANDROID__)

// T0011-5 TestCreateAndUseIdentityFromKeyPairAndCerts
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Identity from KeyPair and Certs", "[TLSIdentity]") {
    // Create a KeyPair object from a private key loaded from a PEM file.
    string pem = URLEndpointListenerTest_Cpp::readFile("private_key_of_self_signed_cert.pem");
    KeyPair privateKey = KeyPair::createWithPrivateKeyData(slice{pem.c_str(), pem.size() + 1});
    CHECK(privateKey);

    // Creates a Cert object from a PEM file.
    pem = URLEndpointListenerTest_Cpp::readFile("self_signed_cert.pem");
    Cert cert = Cert::createWithData(slice{pem.c_str(), pem.size() + 1});
    CHECK(cert);

    // Create an identity from the KeyPair and Cert object.
    TLSIdentity identity = TLSIdentity::identityWithKeyPairAndCerts(privateKey, cert);
    CHECK(identity);

    // Initializes a listener with a config with the identity.
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;
    listenerConfig.tlsIdentity = identity;

    URLEndpointListener listener(listenerConfig);
    listener.start();

    // Starts a single shot replicator to the listener.
    configOneShotReplicator(listenerConfig, listener);

    replicate();

    // Checks that the replicator stopped without an error.
    listener.stop();
}

#endif // #ifdef COUCHBASE_ENTERPRISE
