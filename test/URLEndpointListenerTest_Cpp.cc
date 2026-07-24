//
// URLEndpointListenerTest_Cpp.cc
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

#include "URLEndpointListenerTest_Cpp.hh"
#include "CBLPrivate.h"     // for CBLURLEndpointListener_AnonymousLabel: test-only, no C++ wrapper

#ifdef COUCHBASE_ENTERPRISE

using namespace std;
using namespace std::chrono;
using namespace fleece;
using namespace cbl;

TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener Basics", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    SECTION("0 Collections") {
        // Cannot create a listener with 0 collections.
        URLEndpointListenerConfiguration cfg({});
        cfg.disableTLS = true;
        ExpectingExceptions x;
        CBLError error{};
        try {
            URLEndpointListener listener(cfg);
            FAIL("Expected an exception");
        } catch ( const cbl::Error& e ) {
            error = asCBLError(e);
        }
        CheckError(error, kCBLErrorInvalidParameter);
    }

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;

    SECTION("Comparing the Configuration from the Listener") {
        URLEndpointListener listener(listenerConfig);

        // Compares listenerConfig (what we configured) against the listener's own
        // internally-stored copy of it, fetched via the raw C accessor (see
        // checkConfiguration()'s comment for why there's no C++ wrapper for that).
        checkConfiguration(listenerConfig, CBLURLEndpointListener_Config(listener.ref()));

        listener.stop();
    }

    URLEndpointListener listener(listenerConfig);
    // Before start, the listener's port is 0.
    CHECK(0 == listener.port());
        listener.start();
        // Having started, it returns the port selected by the server.
        CHECK(listener.port() > 0);

        listener.stop();
    }

    SECTION("URLs from Listener") {
        URLEndpointListener listener(listenerConfig);

        MutableArray urls = listener.urls();
        CHECK(!urls);

        listener.start();
        urls = listener.urls();
        CHECK(urls);
        alloc_slice json = urls.toJSON();
        CHECK(json.size > 0);
        CHECK(json.containsBytes("\"ws://"));

        listener.stop();
    }
}

TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener with OneShot Replication", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;

    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListener listener(listenerConfig);
    listener.start();

    config = ReplicatorConfiguration({CollectionConfiguration(cx[0]), CollectionConfiguration(cx[1])},
                                     clientEndpoint(listenerConfig, listener));

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

    listener.stop();
}

TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener with Basic Authentication", "[URLListener]") {
    static constexpr slice kUser{"pupshaw"};
    static constexpr slice kPassword{"frank"};

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;

    SECTION("Successful Login") {
        listenerConfig.authenticator = ListenerAuthenticator::passwordAuthenticator(
            [myUser=kUser, myPassword=kPassword](std::string_view usr, std::string_view psw) {
                return slice(usr) == myUser && slice(psw) == myPassword;
            });
        expectedDocumentCount = 20;
    }

    SECTION("Wrong User") {
        listenerConfig.authenticator = ListenerAuthenticator::passwordAuthenticator(
            [](std::string_view usr, std::string_view psw) {
                return slice(usr) == "InvalidUser"_sl && slice(psw) == kPassword;
            });
        expectedError.code = 401;
    }

    SECTION("Wrong Password") {
        listenerConfig.authenticator = ListenerAuthenticator::passwordAuthenticator(
            [](std::string_view usr, std::string_view psw) {
                return slice(usr) == kUser && slice(psw) == "InvalidPassword"_sl;
            });
        expectedError.code = 401;
    }

    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListener listener(listenerConfig);
    listener.start();

    config = ReplicatorConfiguration({CollectionConfiguration(cx[0]), CollectionConfiguration(cx[1])},
                                     clientEndpoint(listenerConfig, listener));
    config.authenticator = Authenticator::basicAuthenticator((std::string_view)kUser, (std::string_view)kPassword);
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();

    listener.stop();
}

TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener with Cert Authentication", "[URLListener]") {
    constexpr bool withExternalKey = false;

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;

    SECTION("Self-signed Cert") {
        listenerConfig.tlsIdentity = createTLSIdentity(true, withExternalKey);
    }

    SECTION("Self-signed Anonymous Cert") {
        // Leave listenerConfig.tlsIdentity falsy: the listener mints an anonymous identity.
    }

    listenerConfig.authenticator = ListenerAuthenticator::certAuthenticator(
        [](const Cert& cert) {
            return cert.subjectName() == "CN=URLEndpointListener_Client";
        });
    config.acceptOnlySelfSignedServerCertificate = true;
    expectedDocumentCount = 20;

    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListener listener(listenerConfig);
    listener.start();

    config = ReplicatorConfiguration({CollectionConfiguration(cx[0]), CollectionConfiguration(cx[1])},
                                     clientEndpoint(listenerConfig, listener));
    config.acceptOnlySelfSignedServerCertificate = true;

    TLSIdentity clientIdentity = createTLSIdentity(false, withExternalKey);
    REQUIRE(clientIdentity);
    config.authenticator = Authenticator::certificateAuthenticator(clientIdentity);
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();

    listener.stop();

    if ( !listenerConfig.tlsIdentity ) {
#if !defined(__linux__) && !defined(__ANDROID__)
        alloc_slice anonymousLabel = CBLURLEndpointListener_AnonymousLabel(listener.ref());
        CHECK(anonymousLabel);
        identityLabelsToDelete.emplace_back(anonymousLabel);
#endif
    }
}

TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Get Peer TLS Certificate", "[URLListener]") {
    constexpr bool withExternalKey = false;

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;
    listenerConfig.tlsIdentity = createTLSIdentity(true, withExternalKey);
    listenerConfig.authenticator = ListenerAuthenticator::certAuthenticator(
        [](const Cert& cert) {
            return cert.subjectName() == "CN=URLEndpointListener_Client";
        });
    config.acceptOnlySelfSignedServerCertificate = true;
    expectedDocumentCount = 20;

    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListener listener(listenerConfig);
    listener.start();

    config = ReplicatorConfiguration({CollectionConfiguration(cx[0]), CollectionConfiguration(cx[1])},
                                     clientEndpoint(listenerConfig, listener));
    config.acceptOnlySelfSignedServerCertificate = true;

    TLSIdentity clientIdentity = createTLSIdentity(false, withExternalKey);
    REQUIRE(clientIdentity);
    config.authenticator = Authenticator::certificateAuthenticator(clientIdentity);
    config.replicatorType = kCBLReplicatorTypePush;

    // CBLReplicator_ServerCertificate is a raw C accessor with no C++ wrapper; bridged directly
    // here since it's only needed for this one test-only comparison.
    statusWatcher = [&](const CBLReplicatorStatus& status) {
        if ( status.activity > kCBLReplicatorConnecting ) {
            CBLCert* cert = CBLReplicator_ServerCertificate(repl.ref());
            CHECK(cert);
            alloc_slice certData = CBLCert_Data(cert, true);
            CBLCert_Release(cert);
            alloc_slice listenerData = listenerConfig.tlsIdentity.certificates().data(true);
            CHECK(certData == listenerData);
            statusWatcher = nullptr;
        }
    };
    replicate();

    listener.stop();
}

#ifdef __APPLE__
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener with Cert Authentication with External KeyPair", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;

    TLSIdentity clientIdentity;

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

    listenerConfig.authenticator = ListenerAuthenticator::certAuthenticator(
        [](const Cert& cert) {
            return cert.subjectName() == "CN=URLEndpointListener_Client";
        });
    config.acceptOnlySelfSignedServerCertificate = true;
    expectedDocumentCount = 20;

    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListener listener(listenerConfig);
    listener.start();

    config = ReplicatorConfiguration({CollectionConfiguration(cx[0]), CollectionConfiguration(cx[1])},
                                     clientEndpoint(listenerConfig, listener));
    config.acceptOnlySelfSignedServerCertificate = true;
    config.authenticator = Authenticator::certificateAuthenticator(clientIdentity);
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();

    listener.stop();
}
#endif // #ifdef __APPLE__

// T0010-1 TestPort
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener Port", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.port = 12345;
    listenerConfig.disableTLS = true;

    URLEndpointListener listener(listenerConfig);
    listener.start();
    CHECK(listener.port() == 12345);

    listener.stop();
    CHECK(listener.port() == 0);
}

// T0010-2 TestEmptyPort
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Empty Port", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;

    URLEndpointListener listener(listenerConfig);
    listener.start();
    CHECK(listener.port() > 0);

    listener.stop();
    CHECK(listener.port() == 0);
}

// T0010-3 TestBusyPort
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Busy Port", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;

    URLEndpointListener listener(listenerConfig);
    listener.start();
    auto port = listener.port();
    REQUIRE(port > 0);

    URLEndpointListenerConfiguration listener2Config({cy[0], cy[1]});
    listener2Config.port = port;
    listener2Config.disableTLS = true;
    URLEndpointListener listener2(listener2Config);

    {
        ExpectingExceptions x;
        // Checks that an error (EADDRINUSE or equivalent) is thrown starting the second listener.
        CHECK(!succeeds([&] { listener2.start(); }));
    }

    listener.stop();
    listener2.stop();
}

// T0010-4 TestURLs
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener URLs", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;

    URLEndpointListener listener(listenerConfig);
    listener.start();
    auto port = listener.port();
    REQUIRE(port > 0);

    MutableArray urls = listener.urls();
    CHECK(urls.count() > 0);

    std::stringstream ss;
    ss << ":" << port << "/";
    string portSuffix = ss.str();
    for ( Array::iterator iter(urls); iter; ++iter ) {
        auto url = iter.value().asString().asString();
        // Checks that each URL contains the specified port (may not hold on every platform).
        CHECK(url.find(portSuffix) != string::npos);
    }

    listener.stop();

    // Checks that the listener's urls are now empty.
    MutableArray urls2 = listener.urls();
    CHECK(urls2.count() == 0);
}

// T0010-5 TestConnectionStatus
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener Connection Status", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({cy[0]}); // one collection
    listenerConfig.disableTLS = true;

    URLEndpointListener listener(listenerConfig);
    listener.start();

    CBLConnectionStatus status = listener.status();
    CHECK(status.connectionCount == 0);
    CHECK(status.activeConnectionCount == 0);

    createNumberedDocsWithPrefix(cy[0], 1, "doc2");

    CBLConnectionStatus statusDuringPull{};
    auto colConfig = CollectionConfiguration(cx[0]);
    colConfig.pullFilter = [&](Document doc, CBLDocumentFlags flags) -> bool {
        statusDuringPull = listener.status();
        return true;
    };
    config = ReplicatorConfiguration({colConfig}, clientEndpoint(listenerConfig, listener));
    config.replicatorType = kCBLReplicatorTypePull;
    replicate();

    CHECK(statusDuringPull.connectionCount == 1);
    CHECK(statusDuringPull.activeConnectionCount == 1);

    listener.stop();
}

// T0010-6 TestListenerWithDefaultAnonymousIdentity
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Anonymous Identity", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;

    // Anonymous identity means both of:
    REQUIRE(!listenerConfig.tlsIdentity);
    REQUIRE(!listenerConfig.disableTLS);

    URLEndpointListener listener(listenerConfig);
    listener.start();

    CHECK(listener.tlsIdentity());

#if !defined(__linux__) && !defined(__ANDROID__)
    alloc_slice anonymousLabel = CBLURLEndpointListener_AnonymousLabel(listener.ref());
    CHECK(anonymousLabel);
    identityLabelsToDelete.emplace_back(anonymousLabel);
#endif

    configOneShotReplicator(listenerConfig, listener);
    config.acceptOnlySelfSignedServerCertificate = true;
    replicate();

    listener.stop();
}

// T0010-7 TestListenerWithSpecifiedIdentity: covered by other test cases above.

// T0010-8 TestPasswordAuthenticator
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Password Authenticator", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;
    listenerConfig.authenticator = ListenerAuthenticator::passwordAuthenticator(
        [](std::string_view usr, std::string_view psw) {
            return slice(usr) == TLSIdentityTest::kUser && slice(psw) == TLSIdentityTest::kPassword;
        });

    URLEndpointListener listener(listenerConfig);
    listener.start();

    configOneShotReplicator(listenerConfig, listener);

    SECTION("Without Client Auth") {
        // No password authenticator on the client: expect an HTTP auth error.
        expectedError.code = 401;
        expectedDocumentCount = -1;
    }

    SECTION("Incorrect Password") {
        config.authenticator = Authenticator::basicAuthenticator((std::string_view)TLSIdentityTest::kUser, "wrong-password");
        expectedError.code = 401;
        expectedDocumentCount = -1;
    }

    SECTION("Good Password") {
        config.authenticator = Authenticator::basicAuthenticator((std::string_view)TLSIdentityTest::kUser,
                                                                  (std::string_view)TLSIdentityTest::kPassword);
    }

    replicate();

    listener.stop();
}

// T0010-9 TestClientCertCallbackAuthenticator
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Client Cert Callback Authenticator", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;
    listenerConfig.tlsIdentity = createTLSIdentity(true, false);

    int section = 0;
    listenerConfig.authenticator = ListenerAuthenticator::certAuthenticator(
        [&section](const Cert&) { return section != 2; });

    URLEndpointListener listener(listenerConfig);
    listener.start();

    configOneShotReplicator(listenerConfig, listener);
    config.acceptOnlySelfSignedServerCertificate = true;

    TLSIdentity clientIdentity;

    SECTION("Without Client Cert Authenticator") {
        section = 1;
        expectedError.code = kCBLNetErrTLSHandshakeFailed;
        expectedDocumentCount = -1;
    }

    SECTION("Listener Auth Callback Returns false") {
        section = 2;
        expectedError.code = kCBLNetErrTLSClientCertRejected;
        expectedDocumentCount = -1;
    }

    SECTION("Listener Auth Callback Returns true") {
        section = 3;
    }

    if ( section != 1 ) {
        clientIdentity = createTLSIdentity(false, false);
        REQUIRE(clientIdentity);
        config.authenticator = Authenticator::certificateAuthenticator(clientIdentity);
    }

    replicate();

    listener.stop();
}

// T0010-10 TestClientCertAuthenticatorWithRootCert
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Client Cert Authenticator with RootCert", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;
    listenerConfig.tlsIdentity = createTLSIdentity(true, false);

    string pemRootChain = readFile("inter1_root.pem");
    Cert rootCerts = Cert::createWithData(slice{pemRootChain});
    REQUIRE(rootCerts);
    listenerConfig.authenticator = ListenerAuthenticator::certAuthenticator(rootCerts);

    URLEndpointListener listener(listenerConfig);
    listener.start();

    configOneShotReplicator(listenerConfig, listener);

    SECTION("Not Signed By the rootCerts") {
        // A self-signed cert not descended from the root chain above should be rejected.
        string pemCert = readFile("self_signed_cert.pem");
        Cert clientCert = Cert::createWithData(slice{pemCert});
        REQUIRE(clientCert);

        string pemKey = readFile("private_key_of_self_signed_cert.pem");
        KeyPair clientKey = KeyPair::createWithPrivateKeyData(slice(pemKey));

        TLSIdentity clientIdentity = TLSIdentity::identityWithKeyPairAndCerts(clientKey, clientCert);
        REQUIRE(clientIdentity);
        config.authenticator = Authenticator::certificateAuthenticator(clientIdentity);

        expectedError.code = kCBLNetErrTLSClientCertRejected;
        expectedDocumentCount = -1;
    }

    SECTION("Signed Leaf Cert") {
        string pemCert = readFile("leaf.pem");
        Cert clientCert = Cert::createWithData(slice{pemCert});
        REQUIRE(clientCert);

        string pemKey = readFile("leaf.key");
        KeyPair clientKey = KeyPair::createWithPrivateKeyData(slice(pemKey));

        TLSIdentity clientIdentity = TLSIdentity::identityWithKeyPairAndCerts(clientKey, clientCert);
        REQUIRE(clientIdentity);
        config.authenticator = Authenticator::certificateAuthenticator(clientIdentity);
    }

    replicate();

    listener.stop();
}

// T0010-11 TestClientCertAuthenticatorWithDisabledTLS
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Client Cert Auth with Disabled TLS", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;
    listenerConfig.authenticator = ListenerAuthenticator::certAuthenticator(
        [](const Cert&) { return true; });

    ExpectingExceptions x;
    CBLError error{};
    try {
        URLEndpointListener listener(listenerConfig);
        FAIL("Expected an exception");
    } catch ( const cbl::Error& e ) {
        error = asCBLError(e);
    }
    CheckError(error, kCBLErrorInvalidParameter);
}

// T0010-12 TestInvalidNetworkInterface
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Invalid Network Interface", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;

    SECTION("Incorrect Interface 1") {
        listenerConfig.networkInterface = "1.1.1.256";
    }

    SECTION("Incorrect Interface 2") {
        listenerConfig.networkInterface = "foo";
    }

    URLEndpointListener listener(listenerConfig);

    ExpectingExceptions x;
    CBLError error{};
    try {
        listener.start();
        FAIL("Expected an exception");
    } catch ( const cbl::Error& e ) {
        error = asCBLError(e);
    }
    CHECK(error.code == 2);
}

// T0010-13 TestReplicatorServerCertificate: no assertions in the C test either (stub), not ported.

// T0010-14 TestAcceptOnlySelfSignedCertificate
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Accept Only Self-Signed Certificate", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = false;
    {
        string pem = readFile("leaf_inter1_root.pem");
        Cert cert = Cert::createWithData(slice{pem});
        REQUIRE(cert);
        pem = readFile("leaf.key");
        KeyPair privateKey = KeyPair::createWithPrivateKeyData(slice(pem));
        listenerConfig.tlsIdentity = TLSIdentity::identityWithKeyPairAndCerts(privateKey, cert);
        REQUIRE(listenerConfig.tlsIdentity);
    }

    URLEndpointListener listener(listenerConfig);
    listener.start();

    configOneShotReplicator(listenerConfig, listener);

    SECTION("Self-Signed Only") {
        config.acceptOnlySelfSignedServerCertificate = true;
        expectedDocumentCount = -1;
        expectedError.code = kCBLNetErrTLSCertNameMismatch;
    }

    SECTION("Not Self-Signed Only") {
        config.acceptOnlySelfSignedServerCertificate = false;
        expectedError.code = kCBLNetErrTLSCertUnknownRoot;
        expectedDocumentCount = -1;
    }

    replicate();

    listener.stop();
}

// T0010-15 TestReadOnly
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener Read Only", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.disableTLS = true;
    listenerConfig.readOnly = true;

    URLEndpointListener listener(listenerConfig);
    listener.start();

    configOneShotReplicator(listenerConfig, listener);

    SECTION("Push Replicator") {
        config.replicatorType = kCBLReplicatorTypePush;
        expectedError.code = 403; // webSocketDomain
        expectedDocumentCount = -1;
    }

    SECTION("Push-and-Pull Replicator") {
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        expectedError.code = 403; // webSocketDomain
        expectedDocumentCount = -1;
    }

    replicate();

    listener.stop();
}

// T0010-16 TestListenerWithMultipleCollections: covered by default set-up (cy has 2+ collections).

// T0010-17 TestCloseDatabaseStopsListener
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Close Database Stops Listener", "[URLListener]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");

    URLEndpointListenerConfiguration listenerConfig({cy[0], cy[1]});
    listenerConfig.port = 54321;
    listenerConfig.disableTLS = true;

    URLEndpointListener listener(listenerConfig);
    listener.start();
    CHECK(listener.port() == 54321);

    db2.close();
    db2 = nullptr;

    CHECK(listener.port() == 0);
}

// T0010-18 TestListenerTLSIdentity
TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Listener TLS Identity", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({cy[0]});

    bool useAnonymousIdentity = false;

    SECTION("Disable TLS") {
        listenerConfig.disableTLS = true;
    }

    SECTION("With TLSIdentity") {
        listenerConfig.tlsIdentity = createTLSIdentity(true, false);
    }

    SECTION("With Anonymous TLSIdentity") {
        useAnonymousIdentity = true;
    }

    URLEndpointListener listener(listenerConfig);
    CHECK(!listener.tlsIdentity());

    listener.start();

    if ( listenerConfig.disableTLS )
        CHECK(!listener.tlsIdentity());
    else
        CHECK(listener.tlsIdentity());

#if !defined(__linux__) && !defined(__ANDROID__)
    if ( useAnonymousIdentity ) {
        alloc_slice anonymousLabel = CBLURLEndpointListener_AnonymousLabel(listener.ref());
        CHECK(anonymousLabel);
        identityLabelsToDelete.emplace_back(anonymousLabel);
    }
#endif

    listener.stop();
    CHECK(!listener.tlsIdentity());
}

TEST_CASE_METHOD(URLEndpointListenerTest_Cpp, "C++ Start and Stop Listener", "[URLListener]") {
    URLEndpointListenerConfiguration listenerConfig({defaultCollection});
    listenerConfig.disableTLS = true;

    URLEndpointListener listener(listenerConfig);
    listener.start();
    listener.stop();
}

#endif //#ifdef COUCHBASE_ENTERPRISE
