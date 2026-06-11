//
// URLEndpointListenerTest.cc
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

#include "URLEndpointListenerTest.hh"
#include "CBLPrivate.h"
#include "TLSIdentityTest.hh"
#include "fleece/Fleece.hh"
#include <cstddef>
#include <chrono>
#include <fstream>
#include <string>
#include <sstream>
using namespace std::chrono;

#ifdef COUCHBASE_ENTERPRISE

namespace {
#ifdef __APPLE__
    TLSIdentityTest::ExternalKey* externalKey(void* context) {
        return (TLSIdentityTest::ExternalKey*)context;
    }

    bool kc_publicKeyData(void* context, void* output, size_t outputMaxLen, size_t* outputLen) {
        return externalKey(context)->publicKeyData(output, outputMaxLen, outputLen);
    }

    bool kc_decrypt(void* context, FLSlice input, void* output, size_t outputMaxLen, size_t* outputLen) {
        return externalKey(context)->decrypt(input, output, outputMaxLen, outputLen);
    }

    bool kc_sign(void* context, CBLSignatureDigestAlgorithm digestAlgorithm, FLSlice inputData, void* outSignature) {
        return externalKey(context)->sign(digestAlgorithm, inputData, outSignature);
    }

    void kc_free(void* context) {
        delete externalKey(context);
    }
#endif
}; // anonymous namespace

CBLEndpoint* URLEndpointListenerTest::clientEndpoint(CBLURLEndpointListener* listener, CBLError* outError) {
    uint16_t port = CBLURLEndpointListener_Port(listener);
    const CBLURLEndpointListenerConfiguration* config = CBLURLEndpointListener_Config(listener);
    std::stringstream ss;
    ss << (config->disableTLS ? "ws" : "wss");
    ss << "://localhost:" << port << "/";
    auto listenerConfig = CBLURLEndpointListener_Config(listener);
    REQUIRE(listenerConfig->collectionCount > 0);
    auto db = CBLCollection_Database(listenerConfig->collections[0]);
    REQUIRE(db);
    auto dbname = CBLDatabase_Name(db);
    ss << string(dbname);
    return CBLEndpoint_CreateWithURL(slice(ss.str().c_str()), outError);
}

vector<CBLReplicationCollection> URLEndpointListenerTest::collectionConfigs(vector<CBLCollection*>collections) {
    vector<CBLReplicationCollection> configs(collections.size());
    for (int i = 0; i < collections.size(); i++) {
        configs[i].collection = collections[i];
    }
    return configs;
}

CBLTLSIdentity* URLEndpointListenerTest::createTLSIdentity(bool isServer, bool withExternalKey) {
    std::unique_ptr<CBLKeyPair, void(*)(CBLKeyPair*)> keypair{
        nullptr,
        [](CBLKeyPair* k) {
            CBLKeyPair_Release(k);
        }
    };
    if (!withExternalKey) {
        keypair.reset(CBLKeyPair_GenerateRSAKeyPair(fleece::nullslice, nullptr));
    } else {
#ifdef __APPLE__
        keypair.reset(CBLKeyPair_CreateWithExternalKey(2048,
                                                       TLSIdentityTest::ExternalKey::generateRSA(2048),
                                                       CBLExternalKeyCallbacks{kc_publicKeyData, kc_decrypt, kc_sign, kc_free},
                                                       nullptr));
#else
        return nullptr;
#endif
    }
    if (!keypair) return nullptr;

    fleece::MutableDict attributes = fleece::MutableDict::newDict();

    attributes[kCBLCertAttrKeyCommonName] = isServer ? "URLEndpointListener" : "URLEndpointListener_Client";

    static constexpr auto validity = seconds(31536000); // one year

    CBLKeyUsages usages = isServer ? kCBLKeyUsagesServerAuth : kCBLKeyUsagesClientAuth;

    return  CBLTLSIdentity_CreateIdentityWithKeyPair(usages,
                                                     keypair.get(),
                                                     attributes,
                                                     duration_cast<milliseconds>(validity).count(),
                                                     nullptr);
}

void URLEndpointListenerTest::configOneShotReplicator(CBLURLEndpointListener* listener,
                                                      std::vector<CBLReplicationCollection>& colConfigs) {
    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    expectedDocumentCount = 20;
    colConfigs = collectionConfigs({cx[0], cx[1]});
    config.acceptOnlySelfSignedServerCertificate = true;
    config.collections = colConfigs.data();
    config.collectionCount = colConfigs.size();
    config.replicatorType = kCBLReplicatorTypePush;
    CBLError outError{};
    config.endpoint = clientEndpoint(listener, &outError);
    REQUIRE(outError.code == 0);
    REQUIRE(config.endpoint);
}

string URLEndpointListenerTest::readFile(const char* filename) {
    string path = GetAssetFilePath(filename);
    std::ifstream file(path, std::ios::binary);  // Use binary to preserve newlines
    return string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TEST_CASE_METHOD(URLEndpointListenerTest, "Listener Basics", "[URLListener]") {
    CBLError error {};

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0
    };
    listenerConfig.disableTLS = true;

    CBLURLEndpointListener* listener = nullptr;
    SECTION("0 Collections") {
        ExpectingExceptions x;
        listenerConfig.collectionCount = 0;
        // Cannot create listener with 0 collections.
        listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
        CHECK( (nullptr == listener && error.code == kCBLErrorInvalidParameter && error.domain == kCBLDomain) );
    }

    SECTION("Comparing the Configuration from the Listener") {
        listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
        REQUIRE(listener);

        auto configFromListener = CBLURLEndpointListener_Config(listener);
        REQUIRE(configFromListener);
        // Listener keeps the config by a copy.
        CHECK(&listenerConfig != configFromListener);
        size_t offset = offsetof(CBLURLEndpointListenerConfiguration, collectionCount);
        CHECK(0 == memcmp((char*)configFromListener + offset,
                          (char*)&listenerConfig + offset,
                          sizeof(CBLURLEndpointListenerConfiguration) - offset));
        CHECK(0 == memcmp(configFromListener->collections, listenerConfig.collections,
                          listenerConfig.collectionCount * sizeof(CBLCollection*)));
    }

    SECTION("Port from the Listener") {
        listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
        REQUIRE(listener);
        // Before successful start, the port from the configuration is retuned
        CHECK( 0 == CBLURLEndpointListener_Port(listener));

        REQUIRE(CBLURLEndpointListener_Start(listener, nullptr));
        // Having started, it returns the port selected by the server.
        CHECK( CBLURLEndpointListener_Port(listener) > 0 );
    }

    SECTION("URLs from Listener") {
        listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
        REQUIRE(listener);

        FLMutableArray array = CBLURLEndpointListener_Urls(listener);
        CHECK(array == nullptr);

        REQUIRE(CBLURLEndpointListener_Start(listener, nullptr));
        array = CBLURLEndpointListener_Urls(listener);
        CHECK(array);
        FLSliceResult json = FLValue_ToJSON((FLValue)array);
        CHECK(json.size > 0);
        CHECK(slice(json).containsBytes("\"ws://"));

        FLSliceResult_Release(json);
        FLMutableArray_Release(array);
    }

    if (listener) {
        CBLURLEndpointListener_Stop(listener);
        CBLURLEndpointListener_Release(listener);
    }
}

TEST_CASE_METHOD(URLEndpointListenerTest, "Listener with OneShot Replication", "[URLListener]") {
    CBLError error {};

    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0
    };
    listenerConfig.disableTLS = true;

    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();

    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
    REQUIRE(listener);

    CHECK(CBLURLEndpointListener_Start(listener, &error));

    CBLEndpoint* replEndpoint = clientEndpoint(listener, &error);
    REQUIRE(replEndpoint);

    config.endpoint = replEndpoint;
    // the lifetime of replEndpoint is passed to config.
    replEndpoint = nullptr;

    SECTION("PUSH") {
        config.replicatorType = kCBLReplicatorTypePush;
        expectedDocumentCount = 20;
        replicate();
    }

    SECTION("PULL") {
        config.replicatorType = kCBLReplicatorTypePull;
        expectedDocumentCount = 40;
        replicate();
    }

    SECTION("PUSH-PULL") {
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        expectedDocumentCount = 60;
        replicate();
    }

    CBLURLEndpointListener_Stop(listener);
    CBLURLEndpointListener_Release(listener);
}

TEST_CASE_METHOD(URLEndpointListenerTest, "Listener with Basic Authentication", "[URLListener]") {
    struct Context {
        int rand = 6801;
    } context;

    static constexpr slice kUser{"pupshaw"};
    static constexpr slice kPassword{"frank"};

    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0         // port
    };
    listenerConfig.disableTLS = true;

    SECTION("Successful Login") {
        listenerConfig.authenticator = CBLListenerAuth_CreatePassword([](void* ctx, FLString usr, FLString psw) {
            auto context = reinterpret_cast<Context*>(ctx);
            CHECK(context-> rand  == 6801);
            return usr == kUser && psw == kPassword;
        }, &context);
        expectedDocumentCount = 20;
    }

    SECTION("Wrong User") {
        listenerConfig.authenticator = CBLListenerAuth_CreatePassword([](void* ctx, FLString usr, FLString psw) {
            auto context = reinterpret_cast<Context*>(ctx);
            CHECK(context-> rand  == 6801);
            return usr == "InvalidUser"_sl && psw == kPassword;
        }, &context);
        expectedError.code = 401;
    }

    SECTION("Wrong Password") {
        listenerConfig.authenticator = CBLListenerAuth_CreatePassword([](void* ctx, FLString usr, FLString psw) {
            auto context = reinterpret_cast<Context*>(ctx);
            CHECK(context-> rand  == 6801);
            return usr == kUser && psw == "InvalidPassword"_sl;
        }, &context);
        expectedError.code = 401;
    }

    REQUIRE(listenerConfig.authenticator);

    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();

    CBLError error {};
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
    REQUIRE(listener);

    CHECK(CBLURLEndpointListener_Start(listener, &error));

    config.endpoint = clientEndpoint(listener, &error);
    REQUIRE(config.endpoint);
    config.authenticator = CBLAuth_CreatePassword(kUser, kPassword);
    REQUIRE(config.authenticator);
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();

    CBLURLEndpointListener_Stop(listener);
    CBLURLEndpointListener_Release(listener);
    if (listenerConfig.authenticator) CBLListenerAuth_Free(listenerConfig.authenticator);
}

TEST_CASE_METHOD(URLEndpointListenerTest, "Listener with Cert Authentication", "[URLListener]") {
    constexpr bool withExternalKey = false;

    struct Context {
        int rand = 6801;
    } context;

    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0         // port
    };
    listenerConfig.disableTLS = false;

    SECTION("Self-signed Cert") {
        listenerConfig.tlsIdentity = createTLSIdentity(true, withExternalKey);
    }

    SECTION("Self-signed Anonymous Cert") {
    }

    listenerConfig.authenticator = CBLListenerAuth_CreateCertificate([](void* ctx, CBLCert* cert) {
        auto context = reinterpret_cast<Context*>(ctx);
        CHECK(context->rand  == 6801);
        alloc_slice sname = CBLCert_SubjectName(cert);
        return sname == slice("CN=URLEndpointListener_Client");
    }, &context);
    config.acceptOnlySelfSignedServerCertificate = true;
    expectedDocumentCount = 20;

    REQUIRE(listenerConfig.authenticator);
    
    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    CBLError error {};
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
    REQUIRE(listener);
    
    CHECK(CBLURLEndpointListener_Start(listener, &error));

    config.endpoint = clientEndpoint(listener, &error);
    REQUIRE(config.endpoint);
    
    CBLTLSIdentity* clientIdentity = createTLSIdentity(false, withExternalKey);
    REQUIRE(clientIdentity);
    config.authenticator = CBLAuth_CreateCertificate(clientIdentity);
    REQUIRE(config.authenticator);
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();

    CBLURLEndpointListener_Stop(listener);

    alloc_slice anonymousLabel = CBLURLEndpointListener_AnonymousLabel(listener);
    CBLURLEndpointListener_Release(listener);
    CBLTLSIdentity_Release(clientIdentity);
    if (listenerConfig.authenticator) CBLListenerAuth_Free(listenerConfig.authenticator);
    if (listenerConfig.tlsIdentity) {
        CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
    } else {
#if !defined(__linux__) && !defined(__ANDROID__)
        CHECK(anonymousLabel);
        identityLabelsToDelete.emplace_back(anonymousLabel);
#else
        CHECK(!anonymousLabel);
#endif
    }
}

TEST_CASE_METHOD(URLEndpointListenerTest, "Get Peer TLS Certificate", "[URLListener]") {
    constexpr bool withExternalKey = false;

    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0         // port
    };
    listenerConfig.disableTLS = false;
    listenerConfig.tlsIdentity = createTLSIdentity(true, withExternalKey);

    listenerConfig.authenticator = CBLListenerAuth_CreateCertificate([](void* ctx, CBLCert* cert) {
        alloc_slice sname = CBLCert_SubjectName(cert);
        return sname == slice("CN=URLEndpointListener_Client");
    }, nullptr);
    config.acceptOnlySelfSignedServerCertificate = true;
    expectedDocumentCount = 20;

    REQUIRE(listenerConfig.authenticator);

    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    CBLError error {};
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
    REQUIRE(listener);

    CHECK(CBLURLEndpointListener_Start(listener, &error));

    config.endpoint = clientEndpoint(listener, &error);
    REQUIRE(config.endpoint);

    CBLTLSIdentity* clientIdentity = createTLSIdentity(false, withExternalKey);
    REQUIRE(clientIdentity);
    config.authenticator = CBLAuth_CreateCertificate(clientIdentity);
    REQUIRE(config.authenticator);
    config.replicatorType = kCBLReplicatorTypePush;
    statusWatcher = [&, this](const CBLReplicatorStatus& status) {
        if (status.activity > kCBLReplicatorConnecting) {
            CBLCert* cert = CBLReplicator_ServerCertificate(repl);
            CHECK(cert);
            alloc_slice certData = CBLCert_Data(cert, true);
            CBLCert_Release(cert);
            CBLCert* listenerCert =  CBLTLSIdentity_Certificates(listenerConfig.tlsIdentity);
            REQUIRE(listenerCert);
            alloc_slice listenerData = CBLCert_Data(listenerCert, true);
            CHECK(certData == listenerData);
            statusWatcher = nullptr;
        }
    };
    replicate();

    CBLURLEndpointListener_Stop(listener);

    CBLURLEndpointListener_Release(listener);
    CBLTLSIdentity_Release(clientIdentity);
    if (listenerConfig.authenticator) CBLListenerAuth_Free(listenerConfig.authenticator);
    if (listenerConfig.tlsIdentity) {
        CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
    }
}

#ifdef __APPLE__
TEST_CASE_METHOD(URLEndpointListenerTest, "Listener with Cert Authentication with External KeyPair", "[URLListener]") {
    struct Context {
        int rand = 6801;
    } context;
    
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0         // port
    };
    listenerConfig.disableTLS = false;

    CBLTLSIdentity* clientIdentity = nullptr;

    SECTION("Server External KeyPair") {
        listenerConfig.tlsIdentity = createTLSIdentity(true, true);
        clientIdentity = createTLSIdentity(false, false);
    }

    SECTION("Client External KeyPair") {
        listenerConfig.tlsIdentity = createTLSIdentity(true, false);
        clientIdentity = createTLSIdentity(false, true);
    }

    SECTION("Server & Client External KeyPairs") {
        listenerConfig.tlsIdentity = createTLSIdentity(true, true);
        clientIdentity = createTLSIdentity(false, true);
    }

    REQUIRE(listenerConfig.tlsIdentity);
    REQUIRE(clientIdentity);

    listenerConfig.authenticator = CBLListenerAuth_CreateCertificate([](void* ctx, CBLCert* cert) {
        auto context = reinterpret_cast<Context*>(ctx);
        CHECK(context->rand  == 6801);
        alloc_slice sname = CBLCert_SubjectName(cert);
        return sname == slice("CN=URLEndpointListener_Client");
    }, &context);
    config.acceptOnlySelfSignedServerCertificate = true;
    expectedDocumentCount = 20;

    REQUIRE(listenerConfig.authenticator);
    
    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    CBLError error {};
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &error);
    REQUIRE(listener);

    CHECK(CBLURLEndpointListener_Start(listener, &error));

    config.endpoint = clientEndpoint(listener, &error);
    REQUIRE(config.endpoint);

    config.authenticator = CBLAuth_CreateCertificate(clientIdentity);
    REQUIRE(config.authenticator);
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();

    CBLURLEndpointListener_Stop(listener);
    CBLURLEndpointListener_Release(listener);
    CBLTLSIdentity_Release(clientIdentity);
    CBLListenerAuth_Free(listenerConfig.authenticator);
    CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
}
#endif // #ifdef __APPLE__

// T0010-1 TestPort
TEST_CASE_METHOD(URLEndpointListenerTest, "Listener Port", "[URLListener]") {
    // Initializes a listener with a config that specifies a port.
    
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        12345   // port
    };
    listenerConfig.disableTLS = true;
    
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    // Starts the listener.
    REQUIRE(CBLURLEndpointListener_Start(listener, nullptr));

    //Checks that the listener’s port is the same as the port specified in the config.
    
    auto port = CBLURLEndpointListener_Port(listener);
    CHECK(port == 12345);

    //Stops the listener.
    CBLURLEndpointListener_Stop(listener);
    
    //Checks that the listener’s port is zero.
    CHECK(CBLURLEndpointListener_Port(listener) == 0);
    
    CBLURLEndpointListener_Release(listener);
}

// T0010-2 TestEmptyPort
TEST_CASE_METHOD(URLEndpointListenerTest, "Empty Port", "[URLListener]") {
    // Initializes a listener with a config that specifies zero port.
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0   // port
    };
    listenerConfig.disableTLS = true;

    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    // Starts the listener.
    REQUIRE(CBLURLEndpointListener_Start(listener, nullptr));

    //Checks that the listener’s port is more than zero.
    CHECK(CBLURLEndpointListener_Port(listener) > 0);

    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);
    
    // Checks that the listener’s port is zero.
    CHECK(CBLURLEndpointListener_Port(listener) == 0);

    CBLURLEndpointListener_Release(listener);
}

// T0010-3 TestBusyPort
TEST_CASE_METHOD(URLEndpointListenerTest, "Busy Port", "[URLListener]") {
    // Initializes a listener with a config that specifies zero port.
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0   // port
    };
    listenerConfig.disableTLS = true;

    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    // Starts the listener.
    REQUIRE(CBLURLEndpointListener_Start(listener, nullptr));

    auto port = CBLURLEndpointListener_Port(listener);
    REQUIRE(port > 0);

    // Initializes a second Listener with a config that uses the same port.
    CBLURLEndpointListenerConfiguration listener2Config {
        cy.data(),
        2,
        port   // port
    };
    listener2Config.disableTLS = true;

    CBLURLEndpointListener* listener2 = CBLURLEndpointListener_Create(&listener2Config, nullptr);

    CBLError outError{};
    
    // Starts the second listener.
    {
        ExpectingExceptions x;
        bool succ = CBLURLEndpointListener_Start(listener2, &outError);
        CHECK(!succ);
    }

    // Checks that an error is returned as POSIX/EADDRINUSE or equivalent when starting the second listener.
    CHECK(outError.code); // EADDRINUSE (Error messages may differ by platforms)

    // Stops both listeners.
    CBLURLEndpointListener_Stop(listener);
    CBLURLEndpointListener_Stop(listener2);

    CBLURLEndpointListener_Release(listener);
    CBLURLEndpointListener_Release(listener2);
}

// T0010-4 TestURLs
TEST_CASE_METHOD(URLEndpointListenerTest, "Listener URLs", "[URLListener]") {
    // Initializes a listener with a config that specifies zero port.
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0   // port
    };
    listenerConfig.disableTLS = true;
    
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);
    
    // Starts the listener.
    REQUIRE(CBLURLEndpointListener_Start(listener, nullptr));

    auto port = CBLURLEndpointListener_Port(listener);
    REQUIRE(port > 0);
    
    // Checks that the listener’s urls are not empty.
    FLMutableArray urls = CBLURLEndpointListener_Urls(listener);
    auto count = FLArray_Count(urls);
    CHECK(count > 0);

    Array urlArray(urls);
    std::stringstream ss;
    ss << ":" << port << "/";
    string portSuffix = ss.str();
    for (Array::iterator iter(urlArray); iter; ++iter) {
        auto url = iter.value().asString().asString();
        // Checks that the listener’s urls contains the specified port. This step may be skipped on the platform that is difficult to check.
        CHECK(url.find(portSuffix) != string::npos);
    }

    // Stops both listeners.
    CBLURLEndpointListener_Stop(listener);

    // Checks that the listener’s urls are now empty.
    FLMutableArray urls2 = CBLURLEndpointListener_Urls(listener);
    count = FLArray_Count(urls2);
    CHECK(count == 0);

    CBLURLEndpointListener_Release(listener);
    FLMutableArray_Release(urls);
    FLMutableArray_Release(urls2);
}

// T0010-5 TestConnectionStatus
TEST_CASE_METHOD(URLEndpointListenerTest, "Listener Connection Status", "[URLListener]") {
    // Initializes a listener with a config that specifies zero port and the default collection.
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        1,  // one collection
        0   // port
    };
    listenerConfig.disableTLS = true;
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    // Starts the listener.
    REQUIRE(CBLURLEndpointListener_Start(listener, nullptr));

    // Checks that the active connection count and connection count are zero.
    CBLConnectionStatus status = CBLURLEndpointListener_Status(listener);
    CHECK(status.connectionCount == 0);
    CHECK(status.activeConnectionCount == 0);

    // Creates a new document in the listener’s default collection.
    createNumberedDocsWithPrefix(cy[0], 1, "doc2");

    // Setups a single shot pull replicator with a pull filter connecting the listener.
    auto cols = collectionConfigs({cx[0]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    config.replicatorType = kCBLReplicatorTypePull;
    config.endpoint = clientEndpoint(listener, nullptr);
    REQUIRE(config.endpoint);
    struct Context {
        CBLURLEndpointListener* listener;
        CBLConnectionStatus status;
    } context;
    context.listener = listener;
    config.context = &context;

    // In the pull filter, get the active connection count and connection count.

    cols[0].pullFilter = [](void *context, CBLDocument* doc, CBLDocumentFlags flags) -> bool {
        Context* ctx = reinterpret_cast<Context*>(context);
        ctx->status = CBLURLEndpointListener_Status(ctx->listener);
        return true;
    };

    // Starts the replicator and waits until the replicator is stopped.

    replicate();
    
    // Checks that the active connection count and connection count are one.

    CHECK(context.status.connectionCount == 1);
    CHECK(context.status.activeConnectionCount == 1);

    //    Stop sthe listener.
    CBLURLEndpointListener_Stop(listener);

    CBLURLEndpointListener_Release(listener);
}

// T0010-6 TestListenerWithDefaultAnonymousIdentity
TEST_CASE_METHOD(URLEndpointListenerTest, "Anonymous Identity", "[URLListener]") {
    // Initializes a listener with a config with TLS enabled without TLS identity.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0         // port
    };
    listenerConfig.disableTLS = false;

    // Anonymous Identity means following two conditions:
    REQUIRE(listenerConfig.tlsIdentity == nullptr);
    REQUIRE( !listenerConfig.disableTLS );

    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    //Starts the listener.
    CHECK(CBLURLEndpointListener_Start(listener, nullptr));

    // Checks that the listener’s TLS identity is not null.
    CHECK(CBLURLEndpointListener_TLSIdentity(listener));

#if !defined(__linux__) && !defined(__ANDROID__)
    alloc_slice anonymousLabel = CBLURLEndpointListener_AnonymousLabel(listener);
    CHECK(anonymousLabel);
    identityLabelsToDelete.emplace_back(anonymousLabel);
#endif

    // Checks that the listener’s TLS identity contains a self-signed certificate. This step may be skipped on the platform that is difficult to check.

    // Starts a continuous replicator to the listener using wss URL and with accept only self-sign cert enabled. This step is valid to use a single shot replicator as well.
    
    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);
    config.acceptOnlySelfSignedServerCertificate = true;

    replicate();

    //Checks that the replicator becomes IDLE without an error.
    //Stops the replicator and wait until the replicator is stoped.
    //Stops the listener.
    
    CBLURLEndpointListener_Stop(listener);

    // Release
    CBLURLEndpointListener_Release(listener);
}

// T0010-7 TestListenerWithSpecifiedIdentity
// This has been tested multiple times

// T0010-8 TestPasswordAuthenticator
TEST_CASE_METHOD(URLEndpointListenerTest, "Password Authenticator", "[URLListener]") {
    // Initializes a listener with a config that specifies a password authenticator.
    
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        true      // disableTLS
    };

    // Starts the listener.

    listenerConfig.authenticator = CBLListenerAuth_CreatePassword([](void* ctx, FLString usr, FLString psw) {
        return usr == TLSIdentityTest::kUser && psw == TLSIdentityTest::kPassword;
    }, nullptr);
    REQUIRE(listenerConfig.authenticator);

    CBLError outError{};
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &outError);
    CHECK(outError.code == 0);
    CHECK(listener);
    CHECK(CBLURLEndpointListener_Start(listener, nullptr));

    // Start Replicator
    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);
    
    SECTION("Without Client Auth") {
        // Starts a single shot replicator to the listener without a password authenticator.
        // Checks that the replicator stoped with a HTTP AUTH error.
        expectedError.code = 401;
        expectedDocumentCount = -1;
    }

    SECTION("Incorrect Password") {
        // Starts a single shot replicator to the listener with an incorrect password authenticator.
        config.authenticator = CBLAuth_CreatePassword(TLSIdentityTest::kUser, slice("wrong-password"));
        REQUIRE(config.authenticator);
        // Checks that the replicator stoped with a HTTP AUTH error.
        expectedError.code = 401;
        expectedDocumentCount = -1;
    }

    SECTION("Good Password") {
        // Starts a single shot replicator to the listener with a correct password authenticator.
        config.authenticator = CBLAuth_CreatePassword(TLSIdentityTest::kUser, TLSIdentityTest::kPassword);
        REQUIRE(config.authenticator);
        // Checks that the replicator stoped without an error.
    }

    replicate();

    //Stops the listener.
    CBLURLEndpointListener_Stop(listener);
    
    // Cleanup
    CBLURLEndpointListener_Release(listener);
    CBLListenerAuth_Free(listenerConfig.authenticator);
}

// T0010-9 TestClientCertCallbackAuthenticator
TEST_CASE_METHOD(URLEndpointListenerTest, "Client Cert Callback Authenticator", "[URLListener]") {
    // Initializes a listener with a config that enables TLS and specifies a certificate authenticator with a callback.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        false      // disableTLS
    };
    // Self-signed certificate with KeyPair
    listenerConfig.tlsIdentity = createTLSIdentity(true, false);
    
    struct Context {
        int section;
    } context;
    
    listenerConfig.authenticator = CBLListenerAuth_CreateCertificate([](void* ctx, CBLCert* cert) {
        Context* context = reinterpret_cast<Context*>(ctx);
        if (context->section == 2) return false;
        else                       return true;
    }, &context);

    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    // Starts the listener.
    CHECK(CBLURLEndpointListener_Start(listener, nullptr));

    // Start Replicator
    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);
    config.acceptOnlySelfSignedServerCertificate = true;
    
    CBLTLSIdentity* clientIdentity = nullptr;

    SECTION("Without Client Cert Authenticator") {
        // Starts a single shot replicator to the listener without a client cert authenticator.
        // Checks that the replicator stoped with a HTTP AUTH error.
        context.section = 1;
        expectedError.code = kCBLNetErrTLSHandshakeFailed;
        expectedDocumentCount = -1;
    }

    SECTION("Listener Auth Callback Returns false") {
        // Starts a single shot replicator to the listener with a client cert authenticator.
        // The listener’s client cert auth callback returns false.
        context.section = 2;
        // Checks that the replicator stoped with a HTTP AUTH error.
        expectedError.code = kCBLNetErrTLSClientCertRejected;
        expectedDocumentCount = -1;
    }

    SECTION("Listener Auth Callback Returns true") {
        // Starts a single shot replicator to the listener with a client cert authenticator.
        // The listener’s client cert auth callback returns true.
        context.section = 3;
        // Checks that the replicator stoped without an error.
    }

    if (context.section != 1) {
        clientIdentity = createTLSIdentity(false, false);
        REQUIRE(clientIdentity);
        config.authenticator = CBLAuth_CreateCertificate(clientIdentity);
        REQUIRE(config.authenticator);
    }
    
    // Starts a single shot replicator to the listener with a client cert authenticator.
    replicate();
    
    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);

    // Cleanup
    CBLURLEndpointListener_Release(listener);
    if (clientIdentity) CBLTLSIdentity_Release(clientIdentity);
    CBLListenerAuth_Free(listenerConfig.authenticator);
    CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
}

// T0010-10 TestClientCertAuthenticatorWithRootCert
TEST_CASE_METHOD(URLEndpointListenerTest, "Client Cert Authenticator with RootCert", "[URLListener]") {
    // Initializes a listener with a config that enables TLS and specifies a certificate authenticator with a parent of root cert of the client cert.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        false      // disableTLS
    };
    // Self-signed certificate with KeyPair
    listenerConfig.tlsIdentity = createTLSIdentity(true, false);

    string pemRootChain = readFile("inter1_root.pem");
    CBLCert* rootCerts = CBLCert_CreateWithData(slice{pemRootChain}, nullptr);
    REQUIRE(rootCerts);

    listenerConfig.authenticator = CBLListenerAuth_CreateCertificateWithRootCerts(rootCerts);
    REQUIRE(listenerConfig.authenticator);

    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    // Starts the listener.
    CHECK(CBLURLEndpointListener_Start(listener, nullptr));

    // Replicator setting up

    CBLTLSIdentity* clientIdentity = nullptr;
    CBLCert*            clientCert = nullptr;
    CBLKeyPair*   clientPrivateKey = nullptr;
    
    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);

    SECTION("Not Signed By the rootCerts") {
        // Starts a single shot replicator to the listener with a client cert authenticator that uses a cert that is not signed by the parent cert specified in the step one. Using a self-signed certificate is fine as well.

        string pemCert = readFile("self_signed_cert.pem");
        clientCert = CBLCert_CreateWithData(slice{pemCert}, nullptr);
        REQUIRE(clientCert);
        
        string pemKey = readFile("private_key_of_self_signed_cert.pem");
        clientPrivateKey = CBLKeyPair_CreateWithPrivateKeyData(slice(pemKey),
                                                               nullslice,
                                                               nullptr);
        REQUIRE(clientPrivateKey);

        clientIdentity = CBLTLSIdentity_IdentityWithKeyPairAndCerts(clientPrivateKey, clientCert, nullptr);
        REQUIRE(clientIdentity);

        config.authenticator = CBLAuth_CreateCertificate(clientIdentity);
        REQUIRE(config.authenticator);

        // Checks that the replicator stoped with a HTTP AUTH error.
        expectedError.code = kCBLNetErrTLSClientCertRejected;
        expectedDocumentCount = -1;
    }

    SECTION("Signed Leaf Cert") {
        // Starts a single shot replicator to the listener with a client cert authenticator that uses a cert that is signed by the parent cert specified in the step one.
        
        string pemCert = readFile("leaf.pem");
        clientCert = CBLCert_CreateWithData(slice{pemCert}, nullptr);
        REQUIRE(clientCert);
        
        string pemKey = readFile("leaf.key");
        clientPrivateKey = CBLKeyPair_CreateWithPrivateKeyData(slice(pemKey),
                                                               nullslice,
                                                               nullptr);

        clientIdentity = CBLTLSIdentity_IdentityWithKeyPairAndCerts(clientPrivateKey, clientCert, nullptr);
        REQUIRE(clientIdentity);

        config.authenticator = CBLAuth_CreateCertificate(clientIdentity);
        REQUIRE(config.authenticator);

        // Checks that the replicator stoped without an error.
    }

    replicate();

    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);

    // Cleanup:
    CBLURLEndpointListener_Release(listener);
    CBLCert_Release(rootCerts);
    CBLCert_Release(clientCert);
    CBLKeyPair_Release(clientPrivateKey);
    CBLTLSIdentity_Release(clientIdentity);
    CBLListenerAuth_Free(listenerConfig.authenticator);
    CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
    
}

// T0010-11 TestClientCertAuthenticatorWithDisabledTLS
TEST_CASE_METHOD(URLEndpointListenerTest, "Client Cert Auth with Disabled TLS", "[URLListener]") {
    // Initializes a listener with a config that disabled TLS and specifies a certificate authenticator with a callback.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        true       // disableTLS
    };
    listenerConfig.authenticator = CBLListenerAuth_CreateCertificate([](void* ctx, CBLCert* cert) {
        return true;
    }, nullptr);

    // Starts the listener.

    CBLError outError{};
    {
        ExpectingExceptions x;
        CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, &outError);
        CHECK(outError.code == 9);
        CHECK(!listener);
    }
    
    // Cleanups
    CBLListenerAuth_Free(listenerConfig.authenticator);
}

// T0010-12 TestInvalidNetworkInterface
TEST_CASE_METHOD(URLEndpointListenerTest, "Invalid Network Interface", "[URLListener]") {

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListener* listener = nullptr;

    SECTION("Incorrect Interface 1") {
        // Initializes a listener with a config that specifies a network interface as “1.1.1.256”.

        CBLURLEndpointListenerConfiguration listenerConfig {
            cy.data(),
            2,
            0,         // port
            {"1.1.1.256"}, // networkInterface
            true      // disableTLS
        };
        listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    }
    
    SECTION("Incorrect Interface 2") {
        // Initializes a listener with a config that specifies a network interface as “foo”.

        CBLURLEndpointListenerConfiguration listenerConfig {
            cy.data(),
            2,
            0,         // port
            {"foo"},  // networkInterface
            true      // disableTLS
        };
        listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    }

    // Starts the listener.
    // Checks that an unknown host or equivalent error is returned.

    REQUIRE(listener);
    CBLError outError{};
    {
        ExpectingExceptions x;
        CHECK(!CBLURLEndpointListener_Start(listener, &outError));
    }
    CHECK(outError.code == 2);

    // Cleanups:
    CBLURLEndpointListener_Release(listener);
}

// T0010-13 TestReplicatorServerCertificate
TEST_CASE_METHOD(URLEndpointListenerTest, "Replicator Server Certificate", "[URLListener]") {
    // Initializes a listener with a config with TLS enabled which could be a self-signed certificate.

    
    // Starts the listener.
    // Starts a single shot replicator to the listener.
    // Checks that the replicator stops with a certificate error. This is correct.
    // Check that the replicator.serverCerticate is not null.
    // Check that the replicator.serverCerticate is the same certificate specified in the step 1.
    // Stops the listener.
}

// T0010-14 TestAcceptOnlySelfSignedCertificate
TEST_CASE_METHOD(URLEndpointListenerTest, "Accept Only Self-Signed Certificate", "[URLListener]") {
    // Initializes a listener with a config with TLS enabled with a TLS identity that has a cert chain.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        false      // disableTLS
    };
    {
        string pem = readFile("leaf_inter1_root.pem");
        CBLCert* cert = CBLCert_CreateWithData(slice{pem}, nullptr);
        REQUIRE(cert);
        pem = readFile("leaf.key");
        CBLKeyPair* privateKey = CBLKeyPair_CreateWithPrivateKeyData(slice(pem),
                                                                     nullslice,
                                                                     nullptr);
        REQUIRE(privateKey);
        listenerConfig.tlsIdentity = CBLTLSIdentity_IdentityWithKeyPairAndCerts(privateKey, cert, nullptr);

        CBLCert_Release(cert);
        CBLKeyPair_Release(privateKey);

        REQUIRE(listenerConfig.tlsIdentity);
    }
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    //Starts the listener.
    CHECK(CBLURLEndpointListener_Start(listener, nullptr));

    // Replicator setup

    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);

    SECTION("Self-Signed Only") {
        // Starts a single shot replicator to the listener with accept only self-signed cert mode enabled.
        // Checks that the replicator is stopped with a certificate error.
        config.acceptOnlySelfSignedServerCertificate = true;
        expectedDocumentCount = -1;
        expectedError.code = kCBLNetErrTLSCertNameMismatch;
    }
    
    SECTION("Not Self-Signed Only") {
        // Starts a single shot replicator to the listener with accept only self-signed cert mode enabled.
        // Checks that the replicator is stopped without an error.
        config.acceptOnlySelfSignedServerCertificate = false;
        expectedError.code = kCBLNetErrTLSCertUnknownRoot;
        expectedDocumentCount = -1;
    }

    // Starts a single shot replicator to the listener with accept only self-signed cert mode enabled.
    // Starts the listener.

    replicate();

    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);
    
    // Cleanups:
    CBLURLEndpointListener_Release(listener);
    CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
}

// T0010-15 TestReadOnly
TEST_CASE_METHOD(URLEndpointListenerTest, "Listener Read Only", "[URLListener]") {
    // Initializes a listener with a config that enables the readonly mode.
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        0,         // port
        {},        // networkInterface
        true       // disableTLS
    };
    listenerConfig.readOnly = true;
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    // Starts the listener.
    CHECK(CBLURLEndpointListener_Start(listener, nullptr));

    // Replicator
    std::vector<CBLReplicationCollection> colConfigs;
    configOneShotReplicator(listener, colConfigs);
    
    SECTION("Push Replicator") {
        // Starts a single shot push replicator to the listener.
        config.replicatorType = kCBLReplicatorTypePush;
        // Checks that the replicator stops with the forbidden error.
        expectedError.code = 403; // webSocketDomain
        expectedDocumentCount = -1;
    }

    SECTION("Push-and-Pull Replicator") {
        // Starts a single shot push-and-pull replicator to the listener.
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        // Checks that the replicator stops with the forbidden error.
        expectedError.code = 403; // webSocketDomain
        expectedDocumentCount = -1;
    }

    replicate();

    // Stops the listener.
    CBLURLEndpointListener_Stop(listener);

    // Cleanup
    CBLURLEndpointListener_Release(listener);
}

// T0010-16 TestListenerWithMultipleCollections
// We've used multi-collections as default set-uup

// T0010-17 TestCloseDatabaseStopsListener
TEST_CASE_METHOD(URLEndpointListenerTest, "Close Database Stops Listener", "[URLListener]") {
    // Initializes and starts the listener on a non zero port.

    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        2,
        54321,      // port
        {},        // networkInterface
        true       // disableTLS
    };
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);

    // Starts the listener.
    CHECK(CBLURLEndpointListener_Start(listener, nullptr));

    auto port = CBLURLEndpointListener_Port(listener);
    CHECK(port == 54321);

    // Closes its associated database.
    
    db2.close();
    db2 = nullptr;

    // Checks that listener’s port is zero and urls is empty.
    
    port = CBLURLEndpointListener_Port(listener);
    CHECK(port == 0);
    
    CBLURLEndpointListener_Release(listener);
}

// T0010-18 TestListgenerTLSIdentity
TEST_CASE_METHOD(URLEndpointListenerTest, "Listener TLS Identity", "[URLListener]") {
    CBLURLEndpointListenerConfiguration listenerConfig {
        cy.data(),
        1,
        0         // port
    };
    
    bool useAnonymosIdentity = false;
    
    SECTION("Disable TLS") {
        listenerConfig.disableTLS = true;
    }
    
    SECTION("With TLSIdentity") {
        listenerConfig.tlsIdentity = createTLSIdentity(true, false);
    }
    
    SECTION("With Anonymous TLSIdentity") {
        useAnonymosIdentity = true;
        listenerConfig.tlsIdentity = nullptr;
    }
    
    CBLURLEndpointListener* listener = CBLURLEndpointListener_Create(&listenerConfig, nullptr);
    REQUIRE(listener);
    
    CHECK(CBLURLEndpointListener_TLSIdentity(listener) == nullptr);
    
    CHECK(CBLURLEndpointListener_Start(listener, nullptr));
    
    if (listenerConfig.disableTLS) {
        CHECK(CBLURLEndpointListener_TLSIdentity(listener) == nullptr);
    } else {
        CHECK(CBLURLEndpointListener_TLSIdentity(listener) != nullptr);
    }
    
#if !defined(__linux__) && !defined(__ANDROID__)
    if (useAnonymosIdentity) {
        alloc_slice anonymousLabel = CBLURLEndpointListener_AnonymousLabel(listener);
        CHECK(anonymousLabel);
        identityLabelsToDelete.emplace_back(anonymousLabel);
    }
#endif
    
    CBLURLEndpointListener_Stop(listener);
    
    CHECK(CBLURLEndpointListener_TLSIdentity(listener) == nullptr);
    
    CBLURLEndpointListener_Release(listener);
    CBLTLSIdentity_Release(listenerConfig.tlsIdentity);
}

TEST_CASE_METHOD(URLEndpointListenerTest, "Start and Stop Listener", "[URLListener]") {
    CBLURLEndpointListener* listener;
    {
        CBLError error{};
        CBLURLEndpointListenerConfiguration config{};

        vector<CBLCollection*> vec;
        CBLCollection* collection = CBLDatabase_DefaultCollection(db.ref(), &error);
        vec.push_back(collection);

        config.collections = vec.data();
        config.collectionCount = vec.size();
        config.port = 0;
        config.disableTLS = true;
        listener = CBLURLEndpointListener_Create(&config, &error);
        CBLURLEndpointListener_Start(listener, &error);

        CBLCollection_Release(collection);
    }

    {
        CBLURLEndpointListener_Stop(listener);
        CBLURLEndpointListener_Release(listener);
    }
}

#endif //#ifdef COUCHBASE_ENTERPRISE
