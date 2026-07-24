//
// URLEndpointListenerTest_Cpp.hh
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

#pragma once

#include "CBLTest_Cpp.hh"
#include "TLSIdentityTest.hh"
#include "fleece/Fleece.hh"
#include "fleece/Mutable.hh"
#include <chrono>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cbl++/CouchbaseLite.hh"

#ifdef COUCHBASE_ENTERPRISE

class URLEndpointListenerTest_Cpp : public CBLTest_Cpp {
public:
    using clock   = std::chrono::high_resolution_clock;
    using time    = clock::time_point;
    using seconds = std::chrono::duration<double, std::ratio<1,1>>;

    enum class IdleAction {
        kStopReplicator,    ///< Stop Replicator
        kContinueMonitor,   ///< Continue checking status
        kFinishMonitor      ///< Finish checking status
    };

    URLEndpointListenerTest_Cpp()
    :db2(openDatabaseNamed("otherdb", true)) // empty
    ,config({std::vector<cbl::CollectionConfiguration>(), cbl::Endpoint::databaseEndpoint(db2)}) // placeholder; reassigned per test
    {
        cx.push_back(db.createCollection("colA", "scopeA"));
        cx.push_back(db.createCollection("colB", "scopeA"));
        cx.push_back(db.createCollection("colC", "scopeA"));

        cy.push_back(db2.createCollection("colA", "scopeA"));
        cy.push_back(db2.createCollection("colB", "scopeA"));
        cy.push_back(db2.createCollection("colC", "scopeA"));
    }

    ~URLEndpointListenerTest_Cpp() {
        if (db2) {
            db2.close();
            db2 = nullptr;
        }

        std::set<fleece::alloc_slice> labelSet;
        for (const auto& label : identityLabelsToDelete) {
            auto res = labelSet.insert(label);
            // Make sure that a test does not generate identical labels.
            CHECK(res.second);
#if !defined(__linux__) && !defined(__ANDROID__)
            CHECK(cbl::TLSIdentity::deleteIdentityWithLabel((std::string_view)fleece::slice(label)));
#else
            assert(false);
#endif
        }
    }

    // Builds a URL endpoint pointing at `listener`, deriving the scheme from `listenerConfig`
    // and the database name from its first collection (there's no C++ equivalent of the C API's
    // CBLURLEndpointListener_Config, so the config the test itself built is used instead).
    cbl::Endpoint clientEndpoint(const cbl::URLEndpointListenerConfiguration& listenerConfig,
                                 const cbl::URLEndpointListener& listener) {
        auto collections = listenerConfig.collections();
        REQUIRE(!collections.empty());
        std::stringstream ss;
        ss << (listenerConfig.disableTLS ? "ws" : "wss");
        ss << "://localhost:" << listener.port() << "/" << collections[0].database().name();
        return cbl::Endpoint::urlEndpoint(ss.str());
    }

    // Verifies fromListener (from CBLURLEndpointListener_Config) matches source.
    // c.f. URLEndpointListener::URLEndpointListener(),
    //      URLEndpointListenerConfiguration::toCConfigWithoutCollections() (private -- fields
    //      compared here directly instead)
    static void checkConfiguration(const cbl::URLEndpointListenerConfiguration& source,
                                   const CBLURLEndpointListenerConfiguration* fromListener)
    {
        REQUIRE(fromListener);
        CHECK(fromListener->port == source.port);
        CHECK(fromListener->disableTLS == source.disableTLS);
        CHECK(fromListener->enableDeltaSync == source.enableDeltaSync);
        CHECK(fromListener->readOnly == source.readOnly);
        CHECK(fleece::slice(fromListener->networkInterface) == fleece::slice(source.networkInterface));
        CHECK(fromListener->tlsIdentity == source.tlsIdentity.ref());
        // Unlike tlsIdentity/collections (which the listener retains via CBL_Retain, so the same
        // pointer identity holds), the listener's constructor heap-allocates its own copy of the
        // CBLListenerAuthenticator struct itself (CBLURLEndpointListener_Internal.hh, in the EE
        // sibling repo: `_conf.authenticator = new CBLListenerAuthenticator(*_conf.authenticator)`).
        // So fromListener->authenticator is never pointer-equal to what was configured -- only
        // presence/absence is checkable here, not identity.
        CHECK((fromListener->authenticator != nullptr) == (source.authenticator.ref() != nullptr));

        auto collections = source.collections();
        REQUIRE(fromListener->collectionCount == collections.size());
        for ( size_t i = 0; i < collections.size(); i++ ) {
            CHECK(fromListener->collections[i] == collections[i].ref());
        }
    }

    // Creates a self-signed TLS identity for the given role. The C fixture mints a fresh RSA
    // keypair per call via CBLKeyPair_GenerateRSAKeyPair, but that's a private, test-only API
    // with no C++ wrapper (see TLSIdentityTest_Cpp.cc's "C++ Self-Signed Cert Identity"), so the
    // non-external-key path here loads a fixed pre-generated key asset instead. Server and client
    // identities end up sharing key material, but each still gets its own self-signed cert with a
    // distinct CN, which is all these tests actually verify.
    cbl::TLSIdentity createTLSIdentity(bool isServer, bool withExternalKey) {
        cbl::KeyPair keypair;
        if ( !withExternalKey ) {
            std::string pem = readFile("private_key_of_self_signed_cert.pem");
            keypair = cbl::KeyPair::createWithPrivateKeyData(fleece::slice{pem.c_str(), pem.size() + 1});
        } else {
#ifdef __APPLE__
            auto* externalKey = TLSIdentityTest::ExternalKey::generateRSA(2048);
            REQUIRE(externalKey);
            cbl::KeyPair::ExternalKeyHolder holder(
                externalKey,
                [](void* ctx) -> std::optional<fleece::alloc_slice> {
                    return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->publicKeyData();
                },
                [](void* ctx, fleece::slice input) -> std::optional<fleece::alloc_slice> {
                    return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->decrypt(input);
                },
                [](void* ctx, cbl::KeyPair::SignatureDigestAlgorithm digestAlgorithm, fleece::slice input) -> std::optional<fleece::alloc_slice> {
                    return static_cast<TLSIdentityTest::ExternalKey*>(ctx)->sign(digestAlgorithm, input);
                },
                [](void* ctx) { delete (TLSIdentityTest::ExternalKey*)ctx; });
            keypair = cbl::KeyPair::createWithExternalKey(2048, std::move(holder));
#else
            return cbl::TLSIdentity();
#endif
        }

        fleece::MutableDict attributes = fleece::MutableDict::newDict();
        attributes[kCBLCertAttrKeyCommonName] = isServer ? "URLEndpointListener" : "URLEndpointListener_Client";
        CBLKeyUsages usages = isServer ? kCBLKeyUsagesServerAuth : kCBLKeyUsagesClientAuth;
        return cbl::TLSIdentity::createIdentity(usages, keypair, attributes,
                                                std::chrono::duration_cast<std::chrono::milliseconds>(TLSIdentityTest::OneYear).count());
    }

    static std::string readFile(const char* filename) {
        std::string path = GetAssetFilePath(filename);
        std::ifstream file(path, std::ios::binary);  // Use binary to preserve newlines
        return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    // Configures `config` as a one-shot, self-signed-only push replicator from cx[0]/cx[1] to
    // `listener`, matching URLEndpointListenerTest::configOneShotReplicator.
    void configOneShotReplicator(const cbl::URLEndpointListenerConfiguration& listenerConfig,
                                 const cbl::URLEndpointListener& listener) {
        createNumberedDocsWithPrefix(cx[0], 10, "doc");
        createNumberedDocsWithPrefix(cx[1], 10, "doc");
        expectedDocumentCount = 20;
        config = cbl::ReplicatorConfiguration({cbl::CollectionConfiguration(cx[0]), cbl::CollectionConfiguration(cx[1])},
                                              clientEndpoint(listenerConfig, listener));
        config.acceptOnlySelfSignedServerCertificate = true;
        config.replicatorType = kCBLReplicatorTypePush;
    }

    void replicate(bool reset =false) {
        CBLReplicatorStatus status;
        if ( !repl ) {
            repl = cbl::Replicator(config);
            status = repl.status();
            CHECK(status.activity == kCBLReplicatorStopped);
            CHECK(status.progress.complete == 0.0);
            CHECK(status.progress.documentCount == 0);
            CHECK(status.error.code == 0);
        }
        REQUIRE(repl);

        auto changeListener = repl.addChangeListener([&](cbl::Replicator r, const CBLReplicatorStatus& status) {
            statusChanged(r, status);
        });

        repl.start(reset);

        time start = clock::now();
        std::cerr << "Waiting...\n";
        while ( std::chrono::duration_cast<seconds>(clock::now() - start).count() < timeoutSeconds ) {
            status = repl.status();
            if ( config.continuous && status.activity == kCBLReplicatorIdle ) {
                if ( idleAction == IdleAction::kStopReplicator ) {
                    std::cerr << "Stop the continuous replicator...\n";
                    repl.stop();
                } else if ( idleAction == IdleAction::kFinishMonitor ) {
                    break;
                }
            } else if ( status.activity == kCBLReplicatorStopped ) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cerr << "Finished with activity=" << static_cast<int>(status.activity)
                  << ", complete=" << status.progress.complete
                  << ", documentCount=" << status.progress.documentCount
                  << ", error=(" << status.error.domain << "/" << status.error.code << ")\n";

        if ( config.continuous && idleAction == IdleAction::kFinishMonitor )
            CHECK(status.activity == kCBLReplicatorIdle);
        else
            CHECK(status.activity == kCBLReplicatorStopped);

        if ( expectedError.code > 0 ) {
            CHECK(status.error.code == expectedError.code);
        } else {
            CHECK(status.error.code == 0);
            CHECK(status.progress.complete == 1.0);
        }

        if ( expectedDocumentCount >= 0 ) {
            CHECK(status.progress.documentCount == expectedDocumentCount);
        }
    }

    void statusChanged(cbl::Replicator& r, const CBLReplicatorStatus& status) {
        CHECK(r == repl);
        std::cerr << "--- PROGRESS: status=" << static_cast<int>(status.activity)
                  << ", fraction=" << status.progress.complete
                  << ", err=" << status.error.domain << "/" << status.error.code << "\n";
        if ( statusWatcher ) statusWatcher(status);
    }

    cbl::Database db2;
    std::vector<cbl::Collection> cx;
    std::vector<cbl::Collection> cy;

    cbl::ReplicatorConfiguration config;
    cbl::Replicator repl;

    double timeoutSeconds = 30.0;
    IdleAction idleAction = IdleAction::kStopReplicator;
    std::function<void(const CBLReplicatorStatus&)> statusWatcher;

    CBLError expectedError = {};
    int64_t expectedDocumentCount = -1;

    std::vector<fleece::alloc_slice> identityLabelsToDelete;
};

#endif //#ifdef COUCHBASE_ENTERPRISE
