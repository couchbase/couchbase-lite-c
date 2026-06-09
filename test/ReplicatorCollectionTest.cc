//
// ReplicatorCollectionTest.cc
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

#include "ReplicatorTest.hh"
#include "CBLPrivate.h"
#include "fleece/Fleece.hh"
#include <string>

#ifdef COUCHBASE_ENTERPRISE

static const string kDefaultDocContent = "{\"greeting\":\"hello\"}";

class ReplicatorCollectionTest : public ReplicatorTest {
public:
    ReplicatorCollectionTest()
    :db2(openDatabaseNamed("otherDB", true)) // empty
    {
        config.endpoint = CBLEndpoint_CreateWithLocalDB(db2.ref());
        
        cx.push_back(CreateCollection(db.ref(), "colA", "scopeA"));
        cx.push_back(CreateCollection(db.ref(), "colB", "scopeA"));
        cx.push_back(CreateCollection(db.ref(), "colC", "scopeA"));
        
        cy.push_back(CreateCollection(db2.ref(), "colA", "scopeA"));
        cy.push_back(CreateCollection(db2.ref(), "colB", "scopeA"));
        cy.push_back(CreateCollection(db2.ref(), "colC", "scopeA"));
    }
    
    ~ReplicatorCollectionTest() {
        for (auto col : cx) {
            CBLCollection_Release(col);
        }
        
        for (auto col : cy) {
            CBLCollection_Release(col);
        }
    }
    
    static string docKey(CBLCollection* collection, string docID) {
        return CollectionPath(collection) + "." + docID;
    }
    
    Database db2;
    vector<CBLCollection*> cx;
    vector<CBLCollection*> cy;
};

TEST_CASE_METHOD(ReplicatorCollectionTest, "Create Replicator with zero collections", "[Replicator]") {
    ExpectingExceptions x;
    
    auto cols = collectionConfigs({ cx[0] });
    config.collections = cols.data();
    config.collectionCount = 0; // Use Zero Size to test error
    
    CBLError error {};
    CBLReplicator* r = CBLReplicator_Create(&config, &error);
    CHECK(!r);
    CheckError(error, kCBLErrorInvalidParameter);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Use collections from different databases", "[Replicator]") {
    ExpectingExceptions x;
    
    auto cols = collectionConfigs({cx[0], cy[0]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    
    CBLError error {};
    CBLReplicator* r = CBLReplicator_Create(&config, &error);
    REQUIRE(!r);
    CheckError(error, kCBLErrorInvalidParameter);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Use invalid collections", "[Replicator]") {
    ExpectingExceptions x;
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    
    CBLError error {};
    auto name = CBLCollection_Name(cx[1]);
    auto scope = CBLCollection_Scope(cx[1]);
    REQUIRE(CBLDatabase_DeleteCollection(db.ref(), name, CBLScope_Name(scope), &error));
    CBLScope_Release(scope);
    
    CBLReplicator* r = CBLReplicator_Create(&config, &error);
    REQUIRE(!r);
    CheckError(error, kCBLErrorInvalidParameter);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Single Shot Replication", "[Replicator]") {
    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    
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

TEST_CASE_METHOD(ReplicatorCollectionTest, "Continuous Replication", "[Replicator]") {
    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.continuous = true;
    config.collections = cols.data();
    config.collectionCount = cols.size();
    
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

TEST_CASE_METHOD(ReplicatorCollectionTest, "Incremental Continuous Replication", "[Replicator]") {
    createNumberedDocsWithPrefix(cx[0], 10, "doc");
    createNumberedDocsWithPrefix(cx[1], 10, "doc");
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.continuous = true;
    config.collections = cols.data();
    config.collectionCount = cols.size();
    idleAction = IdleAction::kFinishMonitor;
    
    SECTION("PUSH") {
        config.replicatorType = kCBLReplicatorTypePush;
        replicate();

        REQUIRE(waitForActivityLevel(kCBLReplicatorIdle, 10.0));
        CBLReplicatorStatus status = CBLReplicator_Status(repl);
        CHECK(status.progress.documentCount == 20);

        createNumberedDocsWithPrefix(cx[0], 5, "doc3");
        createNumberedDocsWithPrefix(cx[1], 5, "doc3");
        REQUIRE(waitForActivityLevelAndDocumentCount(kCBLReplicatorIdle, 30, 10.0));
        status = CBLReplicator_Status(repl);
        CHECK(status.activity == kCBLReplicatorIdle);
        CHECK(status.progress.documentCount == 30);
        CHECK(status.error.code == 0);
    }
    
    SECTION("PULL") {
        config.replicatorType = kCBLReplicatorTypePull;
        replicate();
        
        REQUIRE(waitForActivityLevel(kCBLReplicatorIdle, 10.0));
        CBLReplicatorStatus status = CBLReplicator_Status(repl);
        CHECK(status.progress.documentCount == 40);
        
        createNumberedDocsWithPrefix(cy[0], 5, "doc3");
        createNumberedDocsWithPrefix(cy[1], 5, "doc3");
        REQUIRE(waitForActivityLevelAndDocumentCount(kCBLReplicatorIdle, 50, 10.0));
        status = CBLReplicator_Status(repl);
        CHECK(status.activity == kCBLReplicatorIdle);
        CHECK(status.progress.documentCount == 50);
        CHECK(status.error.code == 0);
    }
    
    SECTION("PUSH-PULL") {
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        replicate();

        REQUIRE(waitForActivityLevel(kCBLReplicatorIdle, 10.0));
        CBLReplicatorStatus status = CBLReplicator_Status(repl);
        CHECK(status.progress.documentCount == 60);

        createNumberedDocsWithPrefix(cy[0], 5, "doc3");
        createNumberedDocsWithPrefix(cy[1], 5, "doc3");
        createNumberedDocsWithPrefix(cy[0], 10, "doc4");
        createNumberedDocsWithPrefix(cy[1], 10, "doc4");
        REQUIRE(waitForActivityLevelAndDocumentCount(kCBLReplicatorIdle, 90, 10.0));
        status = CBLReplicator_Status(repl);
        CHECK(status.activity == kCBLReplicatorIdle);
        CHECK(status.progress.documentCount == 90);
        CHECK(status.error.code == 0);
    }
    
    CBLReplicator_Stop(repl);
    REQUIRE(waitForActivityLevel(kCBLReplicatorStopped, 10.0));
    CHECK(CBLReplicator_Status(repl).error.code == 0);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Reset Pull Replication", "[Replicator]") {
    createNumberedDocsWithPrefix(cy[0], 20, "doc2");
    createNumberedDocsWithPrefix(cy[1], 20, "doc2");
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 40;
    replicate();
    
    PurgeAllDocs(cx[0]);
    PurgeAllDocs(cx[1]);
    
    expectedDocumentCount = 0;
    replicate();
    
    expectedDocumentCount = 40;
    replicate(true);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Document Replication Event", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[1], "foo2", kDefaultDocContent);
    
    createDocWithJSON(cy[0], "bar1", kDefaultDocContent);
    createDocWithJSON(cy[1], "bar2", kDefaultDocContent);
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 2;
    enableDocReplicationListener = true;
    replicate();
    
    REQUIRE(replicatedDocs.size() == 2);
    auto foo1 = docKey(cx[0], "foo1");
    CHECK(replicatedDocs[foo1].docID == "foo1");
    CHECK(replicatedDocs[foo1].flags == 0);
    CHECK(replicatedDocs[foo1].error.code == 0);
    
    auto foo2 = docKey(cx[1], "foo2");
    CHECK(replicatedDocs[foo2].docID == "foo2");
    CHECK(replicatedDocs[foo2].flags == 0);
    CHECK(replicatedDocs[foo2].error.code == 0);
    
    resetReplicator();
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 2;
    replicatedDocs.clear();
    replicate();
    
    REQUIRE(replicatedDocs.size() == 2);
    auto bar1 = docKey(cx[0], "bar1");
    CHECK(replicatedDocs[bar1].docID == "bar1");
    CHECK(replicatedDocs[bar1].flags == 0);
    CHECK(replicatedDocs[bar1].error.code == 0);
    
    auto bar2 = docKey(cx[1], "bar2");
    CHECK(replicatedDocs[bar2].docID == "bar2");
    CHECK(replicatedDocs[bar2].flags == 0);
    CHECK(replicatedDocs[bar2].error.code == 0);
    
    CBLError error {};
    REQUIRE(CBLCollection_DeleteDocumentByID(cx[1], "foo2"_sl, &error));
    REQUIRE(CBLCollection_DeleteDocumentByID(cy[1], "bar2"_sl, &error));
    
    resetReplicator();
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    expectedDocumentCount = 2;
    replicatedDocs.clear();
    replicate();
    
    REQUIRE(replicatedDocs.size() == 2);
    CHECK(replicatedDocs[foo2].docID == "foo2");
    CHECK(replicatedDocs[foo2].flags == kCBLDocumentFlagsDeleted);
    CHECK(replicatedDocs[foo2].error.code == 0);
    
    CHECK(replicatedDocs[bar2].docID == "bar2");
    CHECK(replicatedDocs[bar2].flags == kCBLDocumentFlagsDeleted);
    CHECK(replicatedDocs[bar2].error.code == 0);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Default Conflict Resolver with Collections", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar1", kDefaultDocContent);
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 2;
    replicate();
    
    CBLError error {};
    auto foo1a = CBLCollection_GetMutableDocument(cx[0], "foo1"_sl, &error);
    REQUIRE(foo1a);
    REQUIRE(CBLDocument_SetJSON(foo1a, slice("{\"greeting\":\"hi\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cx[0], foo1a, &error));
    REQUIRE(CBLDocument_SetJSON(foo1a, slice("{\"greeting\":\"hey\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cx[0], foo1a, &error));
    CBLDocument_Release(foo1a);

    auto foo1b = CBLCollection_GetMutableDocument(cy[0], "foo1"_sl, &error);
    REQUIRE(foo1b);
    REQUIRE(CBLDocument_SetJSON(foo1b, slice("{\"greeting\":\"hola\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cy[0], foo1b, &error));
    CBLDocument_Release(foo1b);

    auto bar1a = CBLCollection_GetMutableDocument(cx[1], "bar1"_sl, &error);
    REQUIRE(bar1a);
    REQUIRE(CBLDocument_SetJSON(bar1a, slice("{\"greeting\":\"sawasdee\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cx[1], bar1a, &error));
    CBLDocument_Release(bar1a);

    auto bar1b = CBLCollection_GetMutableDocument(cy[1], "bar1"_sl, &error);
    REQUIRE(bar1b);
    REQUIRE(CBLDocument_SetJSON(bar1b, slice("{\"greeting\":\"salve\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cy[1], bar1b, &error));
    REQUIRE(CBLDocument_SetJSON(bar1b, slice("{\"greeting\":\"bonjour\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cy[1], bar1b, &error));
    CBLDocument_Release(bar1b);

    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 0;
    enableDocReplicationListener = true;
    replicate();
    
    REQUIRE(replicatedDocs.size() == 2);
    auto key1 = docKey(cx[0], "foo1");
    CHECK(replicatedDocs[key1].docID == "foo1");
    CHECK(replicatedDocs[key1].error.code == 409);
    CHECK(replicatedDocs[key1].error.domain == kCBLWebSocketDomain);
    
    auto key2 = docKey(cx[1], "bar1");
    CHECK(replicatedDocs[key2].docID == "bar1");
    CHECK(replicatedDocs[key2].error.code == 409);
    CHECK(replicatedDocs[key2].error.domain == kCBLWebSocketDomain);
    
    resetReplicator();
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 2;
    enableDocReplicationListener = false;
    replicate();

    auto foo1 = CBLCollection_GetDocument(cx[0], "foo1"_sl, &error);
    REQUIRE(foo1);
    CHECK(Dict(CBLDocument_Properties(foo1)).toJSONString() == "{\"greeting\":\"hey\"}");
    CBLDocument_Release(foo1);

    auto bar1 = CBLCollection_GetDocument(cx[1], "bar1"_sl, &error);
    REQUIRE(bar1);
    CHECK(Dict(CBLDocument_Properties(bar1)).toJSONString() == "{\"greeting\":\"bonjour\"}");
    CBLDocument_Release(bar1);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Conflict Resolver with Collections", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar1", kDefaultDocContent);
    
    auto conflictResolver = [](void *context,
                               FLString documentID,
                               const CBLDocument *localDocument,
                               const CBLDocument *remoteDocument) -> const CBLDocument*
    {
        return (slice(documentID) == "foo1"_sl) ? localDocument : remoteDocument;
    };
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    config.collections[0].conflictResolver = conflictResolver;
    config.collections[1].conflictResolver = conflictResolver;
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 2;
    replicate();
    
    CBLError error {};
    auto foo1a = CBLCollection_GetMutableDocument(cx[0], "foo1"_sl, &error);
    REQUIRE(foo1a);
    REQUIRE(CBLDocument_SetJSON(foo1a, slice("{\"greeting\":\"hey\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cx[0], foo1a, &error));
    CBLDocument_Release(foo1a);
    
    auto foo1b = CBLCollection_GetMutableDocument(cy[0], "foo1"_sl, &error);
    REQUIRE(foo1b);
    REQUIRE(CBLDocument_SetJSON(foo1b, slice("{\"greeting\":\"hola\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cy[0], foo1b, &error));
    CBLDocument_Release(foo1b);
    
    auto bar1a = CBLCollection_GetMutableDocument(cx[1], "bar1"_sl, &error);
    REQUIRE(bar1a);
    REQUIRE(CBLDocument_SetJSON(bar1a, slice("{\"greeting\":\"sawasdee\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cx[1], bar1a, &error));
    CBLDocument_Release(bar1a);
    
    auto bar1b = CBLCollection_GetMutableDocument(cy[1], "bar1"_sl, &error);
    REQUIRE(bar1b);
    REQUIRE(CBLDocument_SetJSON(bar1b, slice("{\"greeting\":\"bonjour\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cy[1], bar1b, &error));
    CBLDocument_Release(bar1b);
    
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 0;
    enableDocReplicationListener = true;
    replicate();
    
    REQUIRE(replicatedDocs.size() == 2);
    auto key1 = docKey(cx[0], "foo1");
    CHECK(replicatedDocs[key1].docID == "foo1");
    CHECK(replicatedDocs[key1].error.code == 409);
    CHECK(replicatedDocs[key1].error.domain == kCBLWebSocketDomain);
    
    auto key2 = docKey(cx[1], "bar1");
    CHECK(replicatedDocs[key2].docID == "bar1");
    CHECK(replicatedDocs[key2].error.code == 409);
    CHECK(replicatedDocs[key2].error.domain == kCBLWebSocketDomain);
    
    resetReplicator();
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 2;
    enableDocReplicationListener = false;
    replicate();

    auto foo1 = CBLCollection_GetDocument(cx[0], "foo1"_sl, &error);
    REQUIRE(foo1);
    CHECK(Dict(CBLDocument_Properties(foo1)).toJSONString() == "{\"greeting\":\"hey\"}");
    CBLDocument_Release(foo1);

    auto bar1 = CBLCollection_GetDocument(cx[1], "bar1"_sl, &error);
    REQUIRE(bar1);
    CHECK(Dict(CBLDocument_Properties(bar1)).toJSONString() == "{\"greeting\":\"bonjour\"}");
    CBLDocument_Release(bar1);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Resolve Pending Conflicts", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    
    auto badConflictResolver = [](void *context,
                                  FLString documentID,
                                  const CBLDocument *localDocument,
                                  const CBLDocument *remoteDocument) -> const CBLDocument*
    {
        throw std::runtime_error("An unexpected error has occurred.");
    };
    
    auto conflictResolver = [](void *context,
                               FLString documentID,
                               const CBLDocument *localDocument,
                               const CBLDocument *remoteDocument) -> const CBLDocument*
    {
        return localDocument;
    };
    
    auto cols = collectionConfigs({cx[0]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 1;
    replicate();
    
    CBLError error {};
    auto foo1a = CBLCollection_GetMutableDocument(cx[0], "foo1"_sl, &error);
    REQUIRE(foo1a);
    REQUIRE(CBLDocument_SetJSON(foo1a, slice("{\"greeting\":\"hey\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cx[0], foo1a, &error));
    CBLDocument_Release(foo1a);
    
    auto foo1b = CBLCollection_GetMutableDocument(cy[0], "foo1"_sl, &error);
    REQUIRE(foo1b);
    REQUIRE(CBLDocument_SetJSON(foo1b, slice("{\"greeting\":\"hola\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cy[0], foo1b, &error));
    CBLDocument_Release(foo1b);
    
    resetReplicator();
    config.collections[0].conflictResolver = badConflictResolver;
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 1;
    
    {
        ExpectingExceptions ex;
        replicate();
    }

    resetReplicator();
    config.collections[0].conflictResolver = conflictResolver;
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 0;
    replicate();

    auto foo1 = CBLCollection_GetDocument(cx[0], "foo1"_sl, &error);
    REQUIRE(foo1);
    CHECK(Dict(CBLDocument_Properties(foo1)).toJSONString() == "{\"greeting\":\"hey\"}");
    CBLDocument_Release(foo1);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Collection DocIDs Push Filters", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo2", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo3", kDefaultDocContent);
    
    createDocWithJSON(cx[1], "bar1", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar2", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar3", kDefaultDocContent);
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    
    FLMutableArray docIDs1 = FLMutableArray_NewFromJSON("[\"foo1\",\"foo3\"]"_sl, NULL);
    FLMutableArray docIDs2 = FLMutableArray_NewFromJSON("[\"bar2\"]"_sl, NULL);
    config.collections[0].documentIDs = docIDs1;
    config.collections[1].documentIDs = docIDs2;
    
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 3;
    replicate();
    
    FLMutableArray_Release(docIDs1);
    FLMutableArray_Release(docIDs2);
    
    CHECK(CBLCollection_Count(cy[0]) == 2);
    
    CBLError error {};
    auto foo1 = CBLCollection_GetDocument(cy[0], "foo1"_sl, &error);
    REQUIRE(foo1);
    CBLDocument_Release(foo1);
    
    auto foo2 = CBLCollection_GetDocument(cy[0], "foo2"_sl, &error);
    REQUIRE(!foo2);
    
    auto foo3 = CBLCollection_GetDocument(cy[0], "foo3"_sl, &error);
    REQUIRE(foo3);
    CBLDocument_Release(foo3);
    
    CHECK(CBLCollection_Count(cy[1]) == 1);
    
    auto bar1 = CBLCollection_GetDocument(cy[1], "bar1"_sl, &error);
    REQUIRE(!bar1);
    
    auto bar2 = CBLCollection_GetDocument(cy[1], "bar2"_sl, &error);
    REQUIRE(bar2);
    CBLDocument_Release(bar2);
    
    auto bar3 = CBLCollection_GetDocument(cy[1], "bar3"_sl, &error);
    REQUIRE(!bar3);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Collection DocIDs Pull Filters", "[Replicator]") {
    createDocWithJSON(cy[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cy[0], "foo2", kDefaultDocContent);
    createDocWithJSON(cy[0], "foo3", kDefaultDocContent);
    
    createDocWithJSON(cy[1], "bar1", kDefaultDocContent);
    createDocWithJSON(cy[1], "bar2", kDefaultDocContent);
    createDocWithJSON(cy[1], "bar3", kDefaultDocContent);
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    
    FLMutableArray docIDs1 = FLMutableArray_NewFromJSON("[\"foo1\",\"foo3\"]"_sl, NULL);
    FLMutableArray docIDs2 = FLMutableArray_NewFromJSON("[\"bar2\"]"_sl, NULL);
    config.collections[0].documentIDs = docIDs1;
    config.collections[1].documentIDs = docIDs2;
    
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 3;
    replicate();
    
    FLMutableArray_Release(docIDs1);
    FLMutableArray_Release(docIDs2);
    
    CHECK(CBLCollection_Count(cx[0]) == 2);
    
    CBLError error {};
    auto foo1 = CBLCollection_GetDocument(cx[0], "foo1"_sl, &error);
    REQUIRE(foo1);
    CBLDocument_Release(foo1);
    
    auto foo2 = CBLCollection_GetDocument(cx[0], "foo2"_sl, &error);
    REQUIRE(!foo2);
    
    auto foo3 = CBLCollection_GetDocument(cx[0], "foo3"_sl, &error);
    REQUIRE(foo3);
    CBLDocument_Release(foo3);
    
    CHECK(CBLCollection_Count(cx[1]) == 1);
    
    auto bar1 = CBLCollection_GetDocument(cx[1], "bar1"_sl, &error);
    REQUIRE(!bar1);
    
    auto bar2 = CBLCollection_GetDocument(cx[1], "bar2"_sl, &error);
    REQUIRE(bar2);
    CBLDocument_Release(bar2);
    
    auto bar3 = CBLCollection_GetDocument(cx[1], "bar3"_sl, &error);
    REQUIRE(!bar3);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Collection Push Filters", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo2", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo3", kDefaultDocContent);
    
    createDocWithJSON(cx[1], "bar1", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar2", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar3", kDefaultDocContent);
    
    auto pushFilter1 = [](void *context, CBLDocument* doc, CBLDocumentFlags flags) -> bool {
        slice id = slice(CBLDocument_ID(doc));
        CHECK(CollectionPath(CBLDocument_Collection(doc)) == "scopeA.colA");
        return id == "foo1"_sl || id == "foo3";
    };
    
    auto pushFilter2 = [](void *context, CBLDocument* doc, CBLDocumentFlags flags) -> bool {
        slice id = slice(CBLDocument_ID(doc));
        CHECK(CollectionPath(CBLDocument_Collection(doc)) == "scopeA.colB");
        return id == "bar2";
    };
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    
    config.collections[0].pushFilter = pushFilter1;
    config.collections[1].pushFilter = pushFilter2;
    
    config.replicatorType = kCBLReplicatorTypePush;
    expectedDocumentCount = 3;
    replicate();
    
    CHECK(CBLCollection_Count(cy[0]) == 2);
    
    CBLError error {};
    auto foo1 = CBLCollection_GetDocument(cy[0], "foo1"_sl, &error);
    REQUIRE(foo1);
    CBLDocument_Release(foo1);
    
    auto foo2 = CBLCollection_GetDocument(cy[0], "foo2"_sl, &error);
    REQUIRE(!foo2);
    
    auto foo3 = CBLCollection_GetDocument(cy[0], "foo3"_sl, &error);
    REQUIRE(foo3);
    CBLDocument_Release(foo3);
    
    CHECK(CBLCollection_Count(cy[1]) == 1);
    
    auto bar1 = CBLCollection_GetDocument(cy[1], "bar1"_sl, &error);
    REQUIRE(!bar1);
    
    auto bar2 = CBLCollection_GetDocument(cy[1], "bar2"_sl, &error);
    REQUIRE(bar2);
    CBLDocument_Release(bar2);
    
    auto bar3 = CBLCollection_GetDocument(cy[1], "bar3"_sl, &error);
    REQUIRE(!bar3);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Collection Pull Filters", "[Replicator]") {
    createDocWithJSON(cy[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cy[0], "foo2", kDefaultDocContent);
    createDocWithJSON(cy[0], "foo3", kDefaultDocContent);
    
    createDocWithJSON(cy[1], "bar1", kDefaultDocContent);
    createDocWithJSON(cy[1], "bar2", kDefaultDocContent);
    createDocWithJSON(cy[1], "bar3", kDefaultDocContent);
    
    auto pullFilter1 = [](void *context, CBLDocument* doc, CBLDocumentFlags flags) -> bool {
        slice id = slice(CBLDocument_ID(doc));
        CHECK(CollectionPath(CBLDocument_Collection(doc)) == "scopeA.colA");
        return id == "foo1"_sl || id == "foo3";
    };
    
    auto pullFilter2 = [](void *context, CBLDocument* doc, CBLDocumentFlags flags) -> bool {
        slice id = slice(CBLDocument_ID(doc));
        CHECK(CollectionPath(CBLDocument_Collection(doc)) == "scopeA.colB");
        return id == "bar2";
    };
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    
    config.collections[0].pullFilter = pullFilter1;
    config.collections[1].pullFilter = pullFilter2;
    
    config.replicatorType = kCBLReplicatorTypePull;
    expectedDocumentCount = 3;
    replicate();
    
    CHECK(CBLCollection_Count(cx[0]) == 2);
    
    CBLError error {};
    auto foo1 = CBLCollection_GetDocument(cx[0], "foo1"_sl, &error);
    REQUIRE(foo1);
    CBLDocument_Release(foo1);
    
    auto foo2 = CBLCollection_GetDocument(cx[0], "foo2"_sl, &error);
    REQUIRE(!foo2);
    
    auto foo3 = CBLCollection_GetDocument(cx[0], "foo3"_sl, &error);
    REQUIRE(foo3);
    CBLDocument_Release(foo3);
    
    CHECK(CBLCollection_Count(cx[1]) == 1);
    
    auto bar1 = CBLCollection_GetDocument(cx[1], "bar1"_sl, &error);
    REQUIRE(!bar1);
    
    auto bar2 = CBLCollection_GetDocument(cx[1], "bar2"_sl, &error);
    REQUIRE(bar2);
    CBLDocument_Release(bar2);
    
    auto bar3 = CBLCollection_GetDocument(cx[1], "bar3"_sl, &error);
    REQUIRE(!bar3);
}

TEST_CASE_METHOD(ReplicatorCollectionTest, "Collection Document Pending", "[Replicator]") {
    createDocWithJSON(cx[0], "foo1", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo2", kDefaultDocContent);
    createDocWithJSON(cx[0], "foo3", kDefaultDocContent);
    
    createDocWithJSON(cx[1], "bar1", kDefaultDocContent);
    createDocWithJSON(cx[1], "bar2", kDefaultDocContent);
    
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    config.replicatorType = kCBLReplicatorTypePush;
    
    CBLError error {};
    repl = CBLReplicator_Create(&config, &error);
    
    // Check Pending Docs:
    FLDict pending1 = CBLReplicator_PendingDocumentIDs(repl, cx[0], &error);
    REQUIRE(pending1);
    CHECK(FLDict_Count(pending1) == 3);
    CHECK(FLValue_AsBool(FLDict_Get(pending1, "foo1"_sl)));
    CHECK(FLValue_AsBool(FLDict_Get(pending1, "foo2"_sl)));
    CHECK(FLValue_AsBool(FLDict_Get(pending1, "foo3"_sl)));
    FLDict_Release(pending1);
    
    CHECK(CBLReplicator_IsDocumentPending(repl, "foo1"_sl, cx[0], &error));
    CHECK(CBLReplicator_IsDocumentPending(repl, "foo1"_sl, cx[0], &error));
    CHECK(CBLReplicator_IsDocumentPending(repl, "foo1"_sl, cx[0], &error));
    
    FLDict pending2 = CBLReplicator_PendingDocumentIDs(repl, cx[1], &error);
    REQUIRE(pending2);
    CHECK(FLDict_Count(pending2) == 2);
    CHECK(FLValue_AsBool(FLDict_Get(pending2, "bar1"_sl)));
    CHECK(FLValue_AsBool(FLDict_Get(pending2, "bar2"_sl)));
    FLDict_Release(pending2);
    
    CHECK(CBLReplicator_IsDocumentPending(repl, "bar1"_sl, cx[1], &error));
    CHECK(CBLReplicator_IsDocumentPending(repl, "bar2"_sl, cx[1], &error));

    // Replicate:
    expectedDocumentCount = 5;
    replicate();
    
    // Check Pending Docs:
    pending1 = CBLReplicator_PendingDocumentIDs(repl, cx[0], &error);
    REQUIRE(pending1);
    CHECK(FLDict_Count(pending1) == 0);
    FLDict_Release(pending1);
    
    CHECK(!CBLReplicator_IsDocumentPending(repl, "foo2"_sl, cx[0], &error));
    
    pending2 = CBLReplicator_PendingDocumentIDs(repl, cx[1], &error);
    REQUIRE(pending2);
    CHECK(FLDict_Count(pending2) == 0);
    FLDict_Release(pending2);
    
    CHECK(!CBLReplicator_IsDocumentPending(repl, "bar1"_sl, cx[1], &error));
    
    // Upadate Docs:
    auto foo2 = CBLCollection_GetMutableDocument(cx[0], "foo2"_sl, &error);
    REQUIRE(foo2);
    REQUIRE(CBLDocument_SetJSON(foo2, slice("{\"greeting\":\"hey\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cx[0], foo2, &error));
    CBLDocument_Release(foo2);
    
    auto bar1 = CBLCollection_GetMutableDocument(cx[1], "bar1"_sl, &error);
    REQUIRE(bar1);
    REQUIRE(CBLDocument_SetJSON(bar1, slice("{\"greeting\":\"hey\"}"), &error));
    REQUIRE(CBLCollection_SaveDocument(cx[1], bar1, &error));
    CBLDocument_Release(bar1);
    
    // Check Pending Docs:
    pending1 = CBLReplicator_PendingDocumentIDs(repl, cx[0], &error);
    REQUIRE(pending1);
    CHECK(FLDict_Count(pending1) == 1);
    CHECK(FLValue_AsBool(FLDict_Get(pending1, "foo2"_sl)));
    FLDict_Release(pending1);
    
    CHECK(CBLReplicator_IsDocumentPending(repl, "foo2"_sl, cx[0], &error));
    
    pending2 = CBLReplicator_PendingDocumentIDs(repl, cx[1], &error);
    REQUIRE(pending2);
    CHECK(FLDict_Count(pending2) == 1);
    CHECK(FLValue_AsBool(FLDict_Get(pending2, "bar1"_sl)));
    FLDict_Release(pending2);
    
    CHECK(CBLReplicator_IsDocumentPending(repl, "bar1"_sl, cx[1], &error));
}

// Note: This test is not an ideal fit for ReplicatorCollectionTest, but it is the best available location for now.
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
TEST_CASE_METHOD(ReplicatorCollectionTest, "Correlation ID", "[Replicator]") {
    auto cols = collectionConfigs({cx[0], cx[1]});
    config.collections = cols.data();
    config.collectionCount = cols.size();
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    
    CBLError error {};
    repl = CBLReplicator_Create(&config, &error);
    
    FLStringResult correlationID = CBLReplicator_CorrelationID(repl);
    CHECK(correlationID == kFLSliceNull);
    FLSliceResult_Release(correlationID);
    
    replicate();
    
    correlationID = CBLReplicator_CorrelationID(repl);
    CHECK(correlationID != kFLSliceNull);
    FLSliceResult_Release(correlationID);
}

#endif
