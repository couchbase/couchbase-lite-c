//
// DatabaseTest.cc
//
// Copyright © 2025 Couchbase. All rights reserved.
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

#include "TLSIdentityTest.hh"
#include "URLEndpointListenerTest.hh"
#include "CBLPrivate.h"
#include "fleece/Mutable.hh"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace std::chrono;
using namespace fleece;

#ifdef COUCHBASE_ENTERPRISE

#ifdef  __APPLE__
namespace {
    struct ExternalKeyContext {
        ExternalKeyContext(TLSIdentityTest::ExternalKey* key) : externalKey(key) {}
        TLSIdentityTest::ExternalKey* externalKey;
        int counterPublicKeyData = 0;
        int counterDecrypt       = 0;
        int counterSign          = 0;
        int counterFree          = 0;
        bool badPublicKeyData    = false;
    };

    bool kc_publicKeyData(void* context, void* output, size_t outputMaxLen, size_t* outputLen) {
        ExternalKeyContext* ctx = reinterpret_cast<ExternalKeyContext*>(context);
        if (ctx->badPublicKeyData) return false;

        ctx->counterPublicKeyData++;
        return ctx->externalKey->publicKeyData(output, outputMaxLen, outputLen);
    }

    bool kc_decrypt(void* context, FLSlice input, void* output, size_t outputMaxLen, size_t* outputLen) {
        ExternalKeyContext* ctx = reinterpret_cast<ExternalKeyContext*>(context);
        ctx->counterDecrypt++;
        return ctx->externalKey->decrypt(input, output, outputMaxLen, outputLen);
    }

    bool kc_sign(void* context, CBLSignatureDigestAlgorithm digestAlgorithm, FLSlice inputData, void* outSignature) {
        ExternalKeyContext* ctx = reinterpret_cast<ExternalKeyContext*>(context);
        ctx->counterSign++;
        return ctx->externalKey->sign(digestAlgorithm, inputData, outSignature);
    }

    void kc_free(void* context) {
        ExternalKeyContext* ctx = reinterpret_cast<ExternalKeyContext*>(context);
        ctx->counterFree++;
        delete ctx->externalKey;
    }

}; // anonymous namespace for external key

TEST_CASE_METHOD(TLSIdentityTest, "Self-Signed Cert Identity", "[TSLIdentity]") {
    CBLError outError{};

    CBLKeyPair* keypair = CBLKeyPair_GenerateRSAKeyPair(fleece::nullslice, &outError);
    fleece::MutableDict attributes = fleece::MutableDict::newDict();
    attributes[kCBLCertAttrKeyCommonName] = CN;

    auto                    expire = system_clock::now() + OneYear;

    // Server ID
    CBLTLSIdentity* identity = CBLTLSIdentity_CreateIdentityWithKeyPair(kCBLKeyUsagesServerAuth,
                                                                        keypair,
                                                                        attributes,
                                                                        duration_cast<milliseconds>(OneYear).count(),
                                                                        &outError);
    CHECK(identity);

    CBLCert* certOfIdentity = CBLTLSIdentity_Certificates(identity);
    CHECK(certOfIdentity);
    CHECK( !CBLCert_CertNextInChain(certOfIdentity) );

    // CBLTimestamp is in milliseconds
    CBLTimestamp certExpire  = CBLTLSIdentity_Expiration(identity);
    CBLTimestamp paramExpire = duration_cast<milliseconds>(expire.time_since_epoch()).count();

    // Can differ by 60 seconds
    CHECK(std::abs(certExpire - paramExpire)/1000 < seconds(61).count());

    // checking cert of TLSIdentity
    alloc_slice subjectName = CBLCert_SubjectNameComponent(certOfIdentity, kCBLCertAttrKeyCommonName);
    CHECK(subjectName == CN);

    subjectName = CBLCert_SubjectName(certOfIdentity);
    CHECK(subjectName == "CN="s + CN.asString());

    // Check the digest of public keys of the input KeyPair and that inside the Cert.
    alloc_slice pubDigest1 = CBLKeyPair_PublicKeyDigest(keypair);
    CBLKeyPair* pkOfCert = CBLCert_PublicKey(certOfIdentity);
    CHECK(pkOfCert);
    alloc_slice pubDigest2 = CBLKeyPair_PublicKeyDigest(pkOfCert);
    CHECK( (pubDigest1 && pubDigest1 == pubDigest2) );

    CBLTLSIdentity_Release(identity);
    CBLKeyPair_Release(keypair);
    CBLKeyPair_Release(pkOfCert);
}

TEST_CASE_METHOD(TLSIdentityTest, "External Keys", "[TSLIdentity]") {
    CBLError outError{};

    TLSIdentityTest::ExternalKey* externalKey = TLSIdentityTest::ExternalKey::generateRSA(2048);
    REQUIRE(externalKey);
    ExternalKeyContext ekContext{externalKey};

    SECTION("Successful Callback") {
        ekContext.badPublicKeyData = false;
    }

    SECTION("Failing Callback") {
        ekContext.badPublicKeyData = true;
    }

    // Creates a RSA KeyPair from the KeyPair callback.
    // These callbacks are defined in TLSIdentityTest+Apple.mm
    CBLKeyPair* cblKeyPair = CBLKeyPair_CreateWithExternalKey(2048,
                                                              &ekContext,
                                                              CBLExternalKeyCallbacks{kc_publicKeyData, kc_decrypt, kc_sign, kc_free},
                                                              &outError);

    REQUIRE(outError.code == 0);
    REQUIRE(cblKeyPair);

    alloc_slice publicKeyData = CBLKeyPair_PublicKeyData(cblKeyPair);
    alloc_slice publicKeyDigest = CBLKeyPair_PublicKeyDigest(cblKeyPair);
    alloc_slice privateKeyData = CBLKeyPair_PrivateKeyData(cblKeyPair);

    if (ekContext.badPublicKeyData) {
        CHECK(publicKeyData.empty());
        CHECK(publicKeyDigest.empty());
    } else {
        CHECK(!publicKeyData.empty());
        CHECK(!publicKeyDigest.empty());
    }
    // Private Key Data is not available for external keys.
    CHECK(privateKeyData.empty());

    CBLKeyPair_Release(cblKeyPair);
}

#if !defined(__linux__) && !defined(__ANDROID__)

// T0011-1 TestCreateGetDeleteIdentityWithLabel
TEST_CASE_METHOD(TLSIdentityTest, "Identity With Label", "[TSLIdentity]") {
    CBLError outError{};

    // Clean the identity from the system.
    CBLTLSIdentity_DeleteIdentityWithLabel(Label, nullptr);

    // Creates a self-signed identity with CN = "CBL-Server” and label = "CBL-Server-Cert".

    fleece::MutableDict attributes = fleece::MutableDict::newDict();
    attributes[kCBLCertAttrKeyCommonName] = CN;
    CBLTLSIdentity* identity = CBLTLSIdentity_CreateIdentity(kCBLKeyUsagesServerAuth,
                                                             attributes,
                                                             duration_cast<milliseconds>(OneYear).count(),
                                                             Label,
                                                             &outError);

    // Checks that the identity was created successfully.
    if (outError.code) {
        alloc_slice msg = CBLError_Message(&outError);
        WARN("Error Code=" << outError.code << ", msg=" << msg.asString());
    }
    CHECK(identity);

    // Gets an identity with label = "CBL-Server-Cert".

    outError.code = 0;
    CBLTLSIdentity* identity2 = CBLTLSIdentity_IdentityWithLabel(Label, &outError);
    if (outError.code) {
        alloc_slice msg = CBLError_Message(&outError);
        WARN("Error Code=" << outError.code << ", msg=" << msg.asString());
    }
    CHECK(identity2);
    CHECK(outError.code == 0);

    // Checks that the identity contains a certificate with CN = "CBL-Server”.

    CBLCert* cert = CBLTLSIdentity_Certificates(identity2);
    CHECK(cert);
    alloc_slice alloc_sbname = CBLCert_SubjectName(cert);
    slice sbname{alloc_sbname};
    CHECK(sbname.hasPrefix("CN="));
    sbname.moveStart(3);
    CHECK(sbname == CN);

    // Delete the identity with label = "CBL-Server-Cert".
    // Checks that the identity was deleted successfully.

    outError.code = 0;
    CHECK(CBLTLSIdentity_DeleteIdentityWithLabel(Label, &outError));
    CHECK(outError.code == 0);

    // Gets an identity with label = "CBL-Server-Cert".

    outError.code = 0;
    CBLTLSIdentity* identity3 = CBLTLSIdentity_IdentityWithLabel(Label, &outError);
    CHECK(outError.code == 0);
    CHECK(identity3 == nullptr);

    CBLTLSIdentity_Release(identity);
    CBLTLSIdentity_Release(identity2);
}

// T0011-2 TestUseIdentityCreatedWithLabel
TEST_CASE_METHOD(URLEndpointListenerTest, "Use Identity Created with Label", "[TSLIdentity]") {
    CBLError outError{};

    CBLTLSIdentity* identity = nullptr;
    SECTION("First Pass - Create Identity with Label") {
        // Clean the identity from the system.
        CBLTLSIdentity_DeleteIdentityWithLabel(TLSIdentityTest::Label, nullptr);

        //  Creates an identity with and label = "CBL-Server-Cert".
        fleece::MutableDict attributes = fleece::MutableDict::newDict();
        attributes[kCBLCertAttrKeyCommonName] = TLSIdentityTest::CN;
        identity = CBLTLSIdentity_CreateIdentity(kCBLKeyUsagesServerAuth,
                                                 attributes,
                                                 duration_cast<milliseconds>(TLSIdentityTest::OneYear).count(),
                                                 TLSIdentityTest::Label,
                                                 &outError);

        // Checks that the identity was created successfully.
        if (outError.code) {
            alloc_slice msg = CBLError_Message(&outError);
            WARN("Error Code=" << outError.code << ", msge=" << msg.asString());
        }
    }

    SECTION("Second Pass - Retrieve the Identity by the Label") {
        // Repeat Step 1 - 7 with the certificate retrieved by the label.
        outError.code = 0;
        identity = CBLTLSIdentity_IdentityWithLabel(TLSIdentityTest::Label, &outError);
        if (outError.code) {
            alloc_slice msg = CBLError_Message(&outError);
            WARN("Error Code=" << outError.code << ", msge=" << msg.asString());
        }
        CHECK(outError.code == 0);
        identityLabelsToDelete.emplace_back(TLSIdentityTest::Label);
    }

    CHECK(identity);

    // Initializes a listener with a config with the TLS identity.
    struct Context {
        int rand = 6801;
    } context;
    
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0         // port
    };
    listenerConfig.disableTLS = false;
    listenerConfig.tlsIdentity = identity;
    listenerConfig.authenticator = CBLListenerAuth_CreateCertificate([](void* ctx, CBLCert* cert) {
        auto context = reinterpret_cast<Context*>(ctx);
        CHECK(context->rand  == 6801);
        alloc_slice sname = CBLCert_SubjectName(cert);
        return sname == slice("CN=URLEndpointListener_Client");
    }, &context);
    REQUIRE(listenerConfig.authenticator);

    // Starts the listener.
    outError.code = 0;
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &outError);
    CHECK(outError.code == 0);
    CHECK(listener);
    outError.code = 0;
    CHECK(CBLURLEndpointListener_Start(listener, &outError));
    CHECK(outError.code == 0);

    // Starts a single shot replicator to the listener connecting to the listener.
    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);
    CBLTLSIdentity* clientIdentity = createTLSIdentity(false, false);
    REQUIRE(clientIdentity);
    config.authenticator = CBLAuth_CreateCertificate(clientIdentity);
    REQUIRE(config.authenticator);

    replicate();

    // Checks that the replicator stopped without an error.
    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);

    CBLURLEndpointListener_Release(listener);
    CBLTLSIdentity_Release(clientIdentity);
    if (listenerConfig.authenticator) CBLListenerAuth_Free(listenerConfig.authenticator);
    if (listenerConfig.tlsIdentity)
        CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
}

// T0011-3 TestCreateAndUseSelfSignedIdentityWithPrivateKeyData
TEST_CASE_METHOD(URLEndpointListenerTest, "Self-Signed Identity with Private KeyData", "[TSLIdentity]") {
    CBLError outError{};

    // Gets a pre-created RSA private key data in PEM format with a password from file.
    string pemString = URLEndpointListenerTest::readFile("private_key_pass.pem");

    // Creates a RSA KeyPair from the data.
    outError.code = 0;
    CBLKeyPair* privateKey = CBLKeyPair_CreateWithPrivateKeyData(slice{pemString.c_str(), pemString.size() + 1},
                                                                 slice{"pass"},
                                                                 &outError);
    
    //    Checks that the KeyPair is created successfully.

    CHECK(outError.code == 0);
    CHECK(privateKey);

    // Create a self-signed identity with the KeyPair and CN = "CBL-Server”.

    fleece::MutableDict attributes = fleece::MutableDict::newDict();
    attributes[kCBLCertAttrKeyCommonName] = TLSIdentityTest::CN;
    outError.code = 0;
    CBLTLSIdentity* identity = CBLTLSIdentity_CreateIdentityWithKeyPair(kCBLKeyUsagesServerAuth,
                                                                        privateKey,
                                                                        attributes,
                                                                        duration_cast<milliseconds>(TLSIdentityTest::OneYear).count(),
                                                                        &outError);
    
    CHECK(outError.code == 0);
    CHECK(identity);

    // Initializes a listener with a config with the identity.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        false      // disableTLS
    };
    listenerConfig.tlsIdentity = identity;
    listenerConfig.authenticator = CBLListenerAuth_CreatePassword([](void* ctx, FLString usr, FLString psw) {
        return usr == TLSIdentityTest::kUser && psw == TLSIdentityTest::kPassword;
    }, nullptr);
    REQUIRE(listenerConfig.authenticator);
    
    outError.code = 0;
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &outError);
    CHECK(outError.code == 0);
    CHECK(listener);

    // Starts the listener.

    outError.code = 0;
    bool succ = CBLURLEndpointListener_Start(listener, &outError);
    CHECK(outError.code == 0);
    CHECK(succ);

    // Starts a single shot replicator to the listener connecting to the listener.

    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);
    config.authenticator = CBLAuth_CreatePassword(TLSIdentityTest::kUser, TLSIdentityTest::kPassword);
    REQUIRE(config.authenticator);

    replicate();

    // Checks that the replicator stopped without an error.
    // Stops the listener.

    CBLURLEndpointListener_Stop(listener);

    CBLURLEndpointListener_Release(listener);
    CBLListenerAuth_Free(listenerConfig.authenticator);
    CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
    CBLKeyPair_Release(privateKey);
}

#endif // #if !defined(__linux__) && !defined(__ANDROID__)

// T0011-4 TestCreateAndUseSelfSignedIdentityWithPrivateKeyCallback
TEST_CASE_METHOD(URLEndpointListenerTest, "Self-Signed Identity with PrivateKey Callback", "[TSLIdentity]") {
    CBLError outError{};

    // Creates a KeyPair callback using keychain implementation
    // Apple-backed external key, with callbacks defined in TLSIdentityTest+Apple.mm

    TLSIdentityTest::ExternalKey* externalKey = TLSIdentityTest::ExternalKey::generateRSA(2048);
    REQUIRE(externalKey);
    ExternalKeyContext ekContext{externalKey};

    // Creates a RSA KeyPair from the KeyPair callback.
    // These callbacks are defined in TLSIdentityTest+Apple.mm
    CBLKeyPair* cblKeyPair = CBLKeyPair_CreateWithExternalKey(2048,
                                                              &ekContext,
                                                              CBLExternalKeyCallbacks{kc_publicKeyData, kc_decrypt, kc_sign, kc_free},
                                                              &outError);
    
    CHECK(outError.code == 0);
    CHECK(cblKeyPair);

    // Create a self-signed identity with the KeyPair and CN = "CBL-Server”.

    fleece::MutableDict attributes = fleece::MutableDict::newDict();
    attributes[kCBLCertAttrKeyCommonName] = TLSIdentityTest::CN;
    outError.code = 0;
    CBLTLSIdentity* identity = CBLTLSIdentity_CreateIdentityWithKeyPair(kCBLKeyUsagesServerAuth,
                                                                        cblKeyPair,
                                                     attributes,
                                                     duration_cast<milliseconds>(TLSIdentityTest::OneYear).count(),
                                                     &outError);

    // Checks that the KeyPair is created successfully.
    CHECK(outError.code == 0);
    CHECK(identity);

    // Initializes a listener with a config with the identity.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        false      // disableTLS
    };
    listenerConfig.tlsIdentity = identity;
    listenerConfig.authenticator = CBLListenerAuth_CreatePassword([](void* ctx, FLString usr, FLString psw) {
        return usr == TLSIdentityTest::kUser && psw == TLSIdentityTest::kPassword;
    }, nullptr);
    REQUIRE(listenerConfig.authenticator);

    outError.code = 0;
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &outError);
    CHECK(outError.code == 0);
    CHECK(listener);

    // Starts the listener.
    
    outError.code = 0;
    bool succ = CBLURLEndpointListener_Start(listener, &outError);
    CHECK(outError.code == 0);
    CHECK(succ);

    // Starts a single shot replicator to the listener connecting to the listener.

    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);
    config.authenticator = CBLAuth_CreatePassword(TLSIdentityTest::kUser, TLSIdentityTest::kPassword);
    REQUIRE(config.authenticator);

    replicate();

    // Checks that the replicator stopped without an error.

    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);

    // Final releases

    CBLURLEndpointListener_Release(listener);
    CBLListenerAuth_Free(listenerConfig.authenticator);
    CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
    CBLKeyPair_Release(cblKeyPair);
    CHECK(ekContext.counterFree == 1);
    CHECK(ekContext.counterSign > 0);
    CHECK(ekContext.counterPublicKeyData > 0);
}
#endif //#ifdef  __APPLE__

// T0011-5 TestCreateAndUseIdentityFromKeyPairAndCerts
TEST_CASE_METHOD(URLEndpointListenerTest, "Identity from KeyPair and Certs", "[TSLIdentity]") {
    CBLError outError{};

    // Create a KeyPair object from a private key loaded from a PEM file.

    string pem = readFile("private_key_of_self_signed_cert.pem");
    outError.code = 0;
    CBLKeyPair* privateKey = CBLKeyPair_CreateWithPrivateKeyData(slice{pem.c_str(), pem.size() + 1},
                                                                 nullslice, // password
                                                                 &outError);
    CHECK(outError.code == 0);
    CHECK(privateKey);

    // Creates a Cert object from a PEM file.

    pem = readFile("self_signed_cert.pem");
    outError.code = 0;
    CBLCert* cert = CBLCert_CreateWithData(slice{pem.c_str(), pem.size() + 1}, &outError);
    CHECK(outError.code == 0);
    CHECK(cert);

    // Create an identity from the KeyPair and Cert object.

    outError.code = 0;
    CBLTLSIdentity* identity = CBLTLSIdentity_IdentityWithKeyPairAndCerts(privateKey,
                                                                          cert,
                                                                          &outError);
    CHECK(outError.code == 0);
    CHECK(identity);

    // Initializes a listener with a config with the identity.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        false      // disableTLS
    };
    listenerConfig.tlsIdentity = identity;

    outError.code = 0;
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &outError);
    CHECK(outError.code == 0);
    CHECK(listener);
    
    // Starts the listener.
    
    outError.code = 0;
    bool succ = CBLURLEndpointListener_Start(listener, &outError);
    CHECK(outError.code == 0);
    CHECK(succ);

    // Starts a single shot replicator to the listener connecting to the listener.

    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);

    replicate();

    // Checks that the replicator stopped without an error.
    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);

    // Final releases

    CBLURLEndpointListener_Release(listener);
    CBLTLSIdentity_Release(identity);
    CBLKeyPair_Release(privateKey);
    CBLCert_Release(cert);
}

namespace {
    std::string timestampToDate(long long millis) {
        // Convert milliseconds to system_clock::time_point
        auto duration = std::chrono::milliseconds(millis);
        auto time_point = std::chrono::system_clock::time_point(duration);
        
        // Convert to time_t for formatting
        std::time_t tt = std::chrono::system_clock::to_time_t(time_point);
        
        // Convert to local time (or gmtime for UTC)
        std::tm* tm = std::localtime(&tt);
        
        // Format as string (e.g., "2025-04-17 12:34:56")
        std::stringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
} // anonymous namespace

// T0011-6 TestGetCertChain
TEST_CASE_METHOD(TLSIdentityTest, "Get CertChain", "[TSLIdentity]") {
    CBLError outError{};

    // Create a Cert object with the PEM file containing a cert chain.

    string pemChain = URLEndpointListenerTest::readFile("cert_chain.pem");

    outError.code = 0;
    CBLCert* cert = CBLCert_CreateWithData(slice{pemChain}, &outError);
    CHECK(outError.code == 0);
    CHECK(cert);

    // From the Cert object, iterate through the cert chain.
    // For each cert chain, verify the CN.
    // For each cert chain, verify that the cert is not expired.

    constexpr const char* CNs[] = {
        "Intermediate3 CA", "Intermediate2 CA", "Intermediate1 CA", "My Root CA"
    };
    constexpr int CNCount = sizeof(CNs) / sizeof(CNs[0]);
    int i = 0;
    CBLCert* iter = cert;
    std::vector<CBLCert*> certsToRelease;
    CBLTimestamp now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    cout << now << endl;
    for ( ; iter && i < CNCount; iter = CBLCert_CertNextInChain(iter), i++) {
        certsToRelease.push_back(iter);
        alloc_slice cn = CBLCert_SubjectNameComponent(iter, slice("CN"));
        CHECK(cn == CNs[i]);
        CBLTimestamp created, expires;
        CBLCert_ValidTimespan(iter, &created, &expires);
        string expireDate = timestampToDate(expires);
        std::cout << "Certificate " << cn.asString() << " will expire on " << expireDate << std::endl;
        CHECK((created < now && now < expires));
    }
    CHECK(i == CNCount);
    CHECK(iter == nullptr);

    for (auto c: certsToRelease) {
        CBLCert_Release(c);
    }
}
#endif // #ifdef COUCHBASE_ENTERPRISE
