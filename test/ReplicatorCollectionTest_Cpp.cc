//
// ReplicatorCollectionTest_Cpp.cc
//
// Copyright © 2022 Couchbase. All rights reserved.
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
#include "fleece/Fleece.hh"
#include "fleece/Mutable.hh"
#include <string>
#include <chrono>
#include <thread>

#include "cbl++/CouchbaseLite.hh"

using namespace std;
using namespace fleece;
using namespace cbl;

#ifdef COUCHBASE_ENTERPRISE

static const string kDefaultDocContent = "{\"greeting\":\"hello\"}";

class ReplicatorCollectionTest_Cpp : public CBLTest_Cpp {
public:
    using clock    = chrono::high_resolution_clock;
    using time     = clock::time_point;
    using seconds  = chrono::duration<double, std::ratio<1,1>>;
    
    enum class IdleAction {
        kStopReplicator,    ///< Stop Replicator
        kContinueMonitor,   ///< Continue checking status
        kFinishMonitor      ///< Finish checking status
    };
    
    ReplicatorCollectionTest_Cpp()
    :db2(openDatabaseNamed("otherDB", true)) // empty
    ,config({vector<CollectionConfiguration>(), Endpoint::databaseEndpoint(db2)})
    {
        cx.push_back(db.createCollection("colA"_sl, "scopeA"_sl));
        cx.push_back(db.createCollection("colB"_sl, "scopeA"_sl));
        cx.push_back(db.createCollection("colC"_sl, "scopeA"_sl));
        
        cy.push_back(db2.createCollection("colA"_sl, "scopeA"_sl));
        cy.push_back(db2.createCollection("colB"_sl, "scopeA"_sl));
        cy.push_back(db2.createCollection("colC"_sl, "scopeA"_sl));
    }
    
    ~ReplicatorCollectionTest_Cpp() { }
    
    vector<CollectionConfiguration> collectionConfigs(vector<Collection>collections) {
        auto rcols = vector<CollectionConfiguration>();
        for (auto col : collections) {
            rcols.push_back(CollectionConfiguration(col));
        }
        return rcols;
    }
    
    void createConfigWithCollections(vector<Collection>collections) {
        createConfig(collectionConfigs(collections));
    }
    
    void createConfig(vector<CollectionConfiguration>collections) {
        Endpoint endpoint = Endpoint::databaseEndpoint(db2);
        config = ReplicatorConfiguration(collections, endpoint);
    }
    
    void replicate(bool reset =false) {
        CBLReplicatorStatus status;
        if (!repl) {
            repl = Replicator(config);
            status = repl.status();
            CHECK(status.activity == kCBLReplicatorStopped);
            CHECK(status.progress.complete == 0.0);
            CHECK(status.progress.documentCount == 0);
            CHECK(status.error.code == 0);
        }
        REQUIRE(repl);
        
        auto listener = repl.addChangeListener([&](Replicator r, const CBLReplicatorStatus& status) {
            statusChanged(r, status);
        });
        
        using DocumentReplicationListener = cbl::ListenerToken<Replicator, bool,
            const std::vector<CBLReplicatedDocument>>;
        
        auto docListener = repl.addDocumentReplicationListener([&](Replicator r,
                                                                   bool isPush,
                                                                   const vector<CBLReplicatedDocument> docs) {
            docProgress(r, isPush, docs);
        });
        
        repl.start(reset);
        
        time start = clock::now();
        cerr << "Waiting...\n";
        while (std::chrono::duration_cast<seconds>(clock::now() - start).count() < timeoutSeconds) {
            status = repl.status();
            if (config.continuous && status.activity == kCBLReplicatorIdle) {
                if (idleAction == IdleAction::kStopReplicator) {
                    cerr << "Stop the continuous replicator...\n";
                    repl.stop();
                } else if (idleAction == IdleAction::kFinishMonitor) {
                    break;
                }
            } else if (status.activity == kCBLReplicatorStopped)
                break;
            this_thread::sleep_for(100ms);
        }
        cerr << "Finished with activity=" << static_cast<int>(status.activity)
             << ", complete=" << status.progress.complete
             << ", documentCount=" << status.progress.documentCount
             << ", error=(" << status.error.domain << "/" << status.error.code << ")\n";
        
        if (config.continuous && idleAction == IdleAction::kFinishMonitor)
            CHECK(status.activity == kCBLReplicatorIdle);
        else
            CHECK(status.activity == kCBLReplicatorStopped);

        if (expectedError.code > 0) {
            CHECK(status.error.code == expectedError.code);
            CHECK(status.progress.complete < 1.0);
        } else {
            CHECK(status.error.code == 0);
            CHECK(status.progress.complete == 1.0);
        }

        if (expectedDocumentCount >= 0) {
            CHECK(status.progress.documentCount == expectedDocumentCount);
        }
    }
    
    void statusChanged(Replicator& r, const CBLReplicatorStatus& status) {
        CHECK(r == repl);
        cerr << "--- PROGRESS: status=" << static_cast<int>(status.activity)
             << ", fraction=" << status.progress.complete
             << ", err=" << status.error.domain << "/" << status.error.code << "\n";
    }
    
    void docProgress(Replicator& r, bool isPush, const vector<CBLReplicatedDocument>& docs) {
        CHECK(r == repl);
        cerr << "--- " << docs.size() << " docs " << (isPush ? "pushed" : "pulled") << ":";
        cerr << "\n";
        
        for (auto& doc : docs) {
            ReplicatedDoc rdoc {};
            rdoc.scope = slice(doc.scope).asString();;
            rdoc.collection = slice(doc.collection).asString();
            rdoc.docID = slice(doc.ID).asString();
            rdoc.flags = doc.flags;
            rdoc.error = doc.error;
            string key = rdoc.scope + "." + rdoc.collection + "." + rdoc.docID;
            replicatedDocs[key] = rdoc;
        }
    }
    
    Database db2;
    vector<Collection> cx;
    vector<Collection> cy;
    
    ReplicatorConfiguration config;
    Replicator repl;
    
    double timeoutSeconds = 30.0;
    IdleAction idleAction = IdleAction::kStopReplicator;
    
    CBLError expectedError = {};
    int64_t expectedDocumentCount = -1;
    
    struct ReplicatedDoc {
        string scope;
        string collection;
        string docID;
        CBLDocumentFlags flags;
        CBLError error;
    };
    // Key format : <scope>.<collection>.<docID> or <docID> for default collection
    unordered_map<string, ReplicatedDoc> replicatedDocs;
};

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Create Replicator with zero collections", "[Replicator]") {
    createConfigWithCollections({});
    
    ExpectingExceptions x;
    CBLError error {};
    try { auto r = Replicator(config); } catch (const cbl::Error& e) { error = asCBLError(e); }
    CheckError(error, kCBLErrorInvalidParameter);
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ URL Endpoint", "[Replicator]") {
    Endpoint endpoint = Endpoint::urlEndpoint("wss://localhost:4985/db");
    auto config = ReplicatorConfiguration({ CollectionConfiguration(cx[0]) }, endpoint);
    
    Replicator repl = Replicator(config);
    CBLReplicator* cRepl = repl.ref();
    REQUIRE(cRepl);
    CHECK(CBLReplicator_Config(cRepl)->endpoint);
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Authenticator", "[Replicator]") {
    Endpoint endpoint = Endpoint::databaseEndpoint(db2);
    auto config = ReplicatorConfiguration({ CollectionConfiguration(cx[0]) }, endpoint);
    
    SECTION("Basic") {
        Authenticator auth = Authenticator::basicAuthenticator("user1", "pa55w0rd");
        config.authenticator = auth;
    }
    
    SECTION("Session") {
        Authenticator auth = Authenticator::sessionAuthenticator("s3ss10n", "sessionID");
        config.authenticator = auth;
    }
    
    Replicator repl = Replicator(config);
    CBLReplicator* cRepl = repl.ref();
    REQUIRE(cRepl);
    CHECK(CBLReplicator_Config(cRepl)->authenticator);
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Single Shot Replication", "[Replicator]") {
    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    
    createConfigWithCollections({cx[0], cx[1]});
    
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
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Continuous Replication", "[Replicator]") {
    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    
    createConfigWithCollections({cx[0], cx[1]});
    config.continuous = true;
    
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
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Collection Push Filters", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo2", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo3", kDefaultDocContent);
    
    createDocWithJSON(cx[1], "bar1", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar2", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar3", kDefaultDocContent);
    
    auto rcol1 = CollectionConfiguration(cx[0]);
    rcol1.pushFilter = [](Document doc, CBLDocumentFlags flags) -> bool {
        string id = doc.id();
        CHECK(doc.collection().name() == "colA");
        CHECK(doc.collection().scopeName() == "scopeA");
        return id == "foo1" || id == "foo3";
    };
    
    auto rcol2 = CollectionConfiguration(cx[1]);
    rcol2.pushFilter = [](Document doc, CBLDocumentFlags flags) -> bool {
        string id = doc.id();
        CHECK(doc.collection().name() == "colB");
        CHECK(doc.collection().scopeName() == "scopeA");
        return id == "bar2";
    };
    
    createConfig({rcol1, rcol2});
    
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 3;
    replicate();
    
    CHECK(cy[0].count() == 2);
    
    auto foo1 = cy[0].getDocument("foo1"_sl);
    REQUIRE(foo1);
    
    auto foo2 = cy[0].getDocument("foo2"_sl);
    REQUIRE(!foo2);
    
    auto foo3 = cy[0].getDocument("foo3"_sl);
    REQUIRE(foo3);
    
    CHECK(cy[1].count() == 1);
    
    auto bar1 = cy[1].getDocument("bar1"_sl);
    REQUIRE(!bar1);
    
    auto bar2 = cy[1].getDocument("bar2"_sl);
    REQUIRE(bar2);
    
    auto bar3 = cy[1].getDocument("bar3"_sl);
    REQUIRE(!bar3);
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Collection Pull Filters", "[Replicator]") {
    createDocWithJSON(cy[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cy[0], "foo2", kDefaultDocContent);
    createDocWithJSON(cy[0], "foo3", kDefaultDocContent);
    
    createDocWithJSON(cy[1], "bar1", kDefaultDocContent);
    createDocWithJSON(cy[1], "bar2", kDefaultDocContent);
    createDocWithJSON(cy[1], "bar3", kDefaultDocContent);
    
    auto rcol1 = CollectionConfiguration(cx[0]);
    rcol1.pullFilter = [](Document doc, CBLDocumentFlags flags) -> bool {
        string id = doc.id();
        CHECK(doc.collection().name() == "colA");
        CHECK(doc.collection().scopeName() == "scopeA");
        return id == "foo1" || id == "foo3";
    };
    
    auto rcol2 = CollectionConfiguration(cx[1]);
    rcol2.pullFilter = [](Document doc, CBLDocumentFlags flags) -> bool {
        string id = doc.id();
        CHECK(doc.collection().name() == "colB");
        CHECK(doc.collection().scopeName() == "scopeA");
        return id == "bar2";
    };
    
    createConfig({rcol1, rcol2});
    
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 3;
    replicate();
    
    CHECK(cx[0].count() == 2);
    
    auto foo1 = cx[0].getDocument("foo1"_sl);
    REQUIRE(foo1);
    
    auto foo2 = cx[0].getDocument("foo2"_sl);
    REQUIRE(!foo2);
    
    auto foo3 = cx[0].getDocument("foo3"_sl);
    REQUIRE(foo3);
    
    CHECK(cx[1].count() == 1);
    
    auto bar1 = cx[1].getDocument("bar1"_sl);
    REQUIRE(!bar1);
    
    auto bar2 = cx[1].getDocument("bar2"_sl);
    REQUIRE(bar2);
    
    auto bar3 = cx[1].getDocument("bar3"_sl);
    REQUIRE(!bar3);
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Collection DocIDs Push Filters", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo2", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo3", kDefaultDocContent);
    
    createDocWithJSON(cx[1], "bar1", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar2", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar3", kDefaultDocContent);
    
    auto col1 = CollectionConfiguration(cx[0]);
    col1.documentIDs = MutableArray::newArray();
    col1.documentIDs.append("foo1");
    col1.documentIDs.append("foo3");
    
    auto col2 = CollectionConfiguration(cx[1]);
    col2.documentIDs = MutableArray::newArray();
    col2.documentIDs.append("bar2");
    
    createConfig({col1, col2});
    
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 3;
    replicate();
    
    CHECK(cy[0].count() == 2);
    
    auto foo1 = cy[0].getDocument("foo1");
    REQUIRE(foo1);
    
    auto foo2 = cy[0].getDocument("foo2");
    REQUIRE(!foo2);
    
    auto foo3 = cy[0].getDocument("foo3");
    REQUIRE(foo3);
    
    CHECK(cy[1].count() == 1);
    
    auto bar1 = cy[1].getDocument("bar1");
    REQUIRE(!bar1);
    
    auto bar2 = cy[1].getDocument("bar2");
    REQUIRE(bar2);
    
    auto bar3 = cy[1].getDocument("bar3");
    REQUIRE(!bar3);
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Conflict Resolver with Collections", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar1", kDefaultDocContent);
    
    auto conflictResolver = [](slice docID, const Document localDoc, const Document remoteDoc) -> Document
    {
        return (docID == "foo1"_sl) ? localDoc : remoteDoc;
    };
    
    auto rcol1 = CollectionConfiguration(cx[0]);
    rcol1.conflictResolver = conflictResolver;
    
    auto rcol2 = CollectionConfiguration(cx[1]);
    rcol2.conflictResolver = conflictResolver;
    
    createConfig({rcol1, rcol2});
    
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 2;
    replicate();
    
    MutableDocument foo1a("foo1");
    foo1a["greeting"] = "hey";
    cx[0].saveDocument(foo1a);
    
    MutableDocument foo1b("foo1");
    foo1b["greeting"] = "hola";
    cy[0].saveDocument(foo1b);
    
    MutableDocument bar1a("bar1");
    bar1a["greeting"] = "sawasdee";
    cx[1].saveDocument(bar1a);
    
    MutableDocument bar1b("bar1");
    bar1b["greeting"] = "bonjour";
    cy[1].saveDocument(bar1b);
    
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 0;
    replicate();
    
    REQUIRE(replicatedDocs.size() == 2);
    auto key1 = CollectionPath(cx[0].ref()) + ".foo1";
    CHECK(replicatedDocs[key1].docID == "foo1");
    CHECK(replicatedDocs[key1].error.code == 409);
    CHECK(replicatedDocs[key1].error.domain == kCBLWebSocketDomain);
    
    auto key2 = CollectionPath(cx[1].ref()) + ".bar1";
    CHECK(replicatedDocs[key2].docID == "bar1");
    CHECK(replicatedDocs[key2].error.code == 409);
    CHECK(replicatedDocs[key2].error.domain == kCBLWebSocketDomain);
    
    repl = nullptr;
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 2;
    replicate();

    auto foo1 = cx[0].getDocument("foo1");
    REQUIRE(foo1);
    CHECK(foo1.properties().toJSONString() == "{\"greeting\":\"hey\"}");

    auto bar1 = cx[1].getDocument("bar1");
    REQUIRE(bar1);
    CHECK(bar1.properties().toJSONString() == "{\"greeting\":\"bonjour\"}");
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Pending Documents", "[Replicator]") {
    createConfigWithCollections({defaultCollection});
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();
    
    Dict ids = repl.pendingDocumentIDs(defaultCollection);
    CHECK(ids.count() == 0);
    
    MutableDocument doc1("foo1");
    doc1["greeting"] = "Howdy!";
    defaultCollection.saveDocument(doc1);
    
    MutableDocument doc2("foo2");
    doc2["greeting"] = "Hello!";
    defaultCollection.saveDocument(doc2);
    
    ids = repl.pendingDocumentIDs(defaultCollection);
    CHECK(ids.count() == 2);
    CHECK(ids["foo1"]);
    CHECK(ids["foo2"]);
    
    CHECK(repl.isDocumentPending("foo1", defaultCollection));
    CHECK(repl.isDocumentPending("foo2", defaultCollection));
    
    replicate();

    Collection col2 = db2.getDefaultCollection();

    CHECK(col2.getDocument("foo1"));
    CHECK(col2.getDocument("foo2"));
    
    ids = repl.pendingDocumentIDs(defaultCollection);
    CHECK(ids.count() == 0);
    
    CHECK(!repl.isDocumentPending("foo1", defaultCollection));
    CHECK(!repl.isDocumentPending("foo2", defaultCollection));
}

TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Pending Documents with Collection", "[Replicator]") {
    Collection col = cx[0];
    createConfigWithCollections({col});
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();
    
    Dict ids = repl.pendingDocumentIDs(cx[0]);
    CHECK(ids.count() == 0);
    
    MutableDocument doc1("foo1");
    doc1["greeting"] = "Howdy!";
    col.saveDocument(doc1);
    
    MutableDocument doc2("foo2");
    doc2["greeting"] = "Hello!";
    col.saveDocument(doc2);
    
    ids = repl.pendingDocumentIDs(col);
    CHECK(ids.count() == 2);
    CHECK(ids["foo1"]);
    CHECK(ids["foo2"]);
    
    CHECK(repl.isDocumentPending("foo1", col));
    CHECK(repl.isDocumentPending("foo2", col));
    
    replicate();
    
    CHECK(col.getDocument("foo1"));
    CHECK(col.getDocument("foo2"));
    
    ids = repl.pendingDocumentIDs(col);
    CHECK(ids.count() == 0);
    
    CHECK(!repl.isDocumentPending("foo1", col));
    CHECK(!repl.isDocumentPending("foo2", col));
}

// Note: This test is not an ideal fit for ReplicatorCollectionTest_Cpp, but it is the best available location for now.
// TODO: Restructure the replicator tests so both the C and C++ APIs can be tested.
//
// Spec: https://github.com/couchbaselabs/couchbase-lite-api/blob/master/spec/tests/T0013-CorrelationID.md
//
// 1. TestGetCorrelationID
//
// Description:
//   Test get the correlation id from the replicator.
//   Note: Alternatively this test can be tested DatabaseEndpoint.
//
// Steps:
//   1. Start a URLEndpointListener.
//   2. Creates a replicator with an endpoint connecting the URLEndpointListener.
//        - Continuous: No
//        - Type: Push-and-Pull
//   3. Gets the correlationID and checks that the value is null.
//   4. Starts the replicator and wait until the replicator stopped.
//   5. Gets the correlationID and checks that the value is not null or empty.
//
TEST_CASE_METHOD(ReplicatorCollectionTest_Cpp, "C++ Correlation ID", "[Replicator]") {
    createConfigWithCollections({cx[0], cx[1]});
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    repl = Replicator(config);
    
    CHECK(repl.correlationID().empty());
    
    replicate();
    
    CHECK(!repl.correlationID().empty());
}

#endif
