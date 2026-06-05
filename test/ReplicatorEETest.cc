//
// ReplicatorEETest.cc
//
// Copyright © 2020 Couchbase. All rights reserved.
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
#include <string>


#ifdef COUCHBASE_ENTERPRISE     // Local-to-local replication is an EE feature


class ReplicatorLocalTest : public ReplicatorTest {
public:
    Database otherDB;
    Collection otherDBDefaultCol = otherDB.getDefaultCollection();

    ReplicatorLocalTest()
    :otherDB(openDatabaseNamed("otherDB", true)) // empty
    ,ReplicatorTest()
    {
        config.endpoint = CBLEndpoint_CreateWithLocalDB(otherDB.ref());
    }
};


TEST_CASE_METHOD(ReplicatorLocalTest, "Replicate empty db", "[Replicator]") {
    config.replicatorType = kCBLReplicatorTypePush;
    replicate();
    CBLReplicatorStatus status = CBLReplicator_Status(repl);
    CHECK(status.error.code == 0);
    CHECK(status.progress.complete == 1.0);
    CHECK(status.progress.documentCount == 0);
    
    config.replicatorType = kCBLReplicatorTypePull;
    resetReplicator();
    replicate();
    status = CBLReplicator_Status(repl);
    CHECK(status.error.code == 0);
    CHECK(status.progress.complete == 1.0);
    CHECK(status.progress.documentCount == 0);
    
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    resetReplicator();
    replicate();
    status = CBLReplicator_Status(repl);
    CHECK(status.error.code == 0);
    CHECK(status.progress.complete == 1.0);
    CHECK(status.progress.documentCount == 0);
}


TEST_CASE_METHOD(ReplicatorLocalTest, "Push to local db", "[Replicator]") {
    config.replicatorType = kCBLReplicatorTypePush;
    
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    defaultCollection.saveDocument(doc);

    replicate();

    CHECK(asVector(replicatedDocIDs) == vector<string>{"foo"});

    Document copiedDoc = otherDBDefaultCol.getDocument("foo");
    REQUIRE(copiedDoc);
    CHECK(copiedDoc["greeting"].asString() == "Howdy!"_sl);
}


TEST_CASE_METHOD(ReplicatorLocalTest, "Continuous Push to local db", "[Replicator]") {
    config.replicatorType = kCBLReplicatorTypePush;
    config.continuous = true;
    
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    defaultCollection.saveDocument(doc);

    replicate();

    CHECK(asVector(replicatedDocIDs) == vector<string>{"foo"});

    Document copiedDoc = otherDBDefaultCol.getDocument("foo");
    REQUIRE(copiedDoc);
    CHECK(copiedDoc["greeting"].asString() == "Howdy!"_sl);
}


TEST_CASE_METHOD(ReplicatorLocalTest, "Set Suspended", "[Replicator]") {
    config.replicatorType = kCBLReplicatorTypePush;
    config.continuous = true;
    idleAction = IdleAction::kFinishMonitor;
    
    replicate();
    
    REQUIRE(CBLReplicator_Status(repl).activity == kCBLReplicatorIdle);
    CBLReplicator_SetSuspended(repl, true);
    REQUIRE(waitForActivityLevel(kCBLReplicatorOffline, 10.0));
    
    CBLReplicator_SetSuspended(repl, false);
    REQUIRE(waitForActivityLevel(kCBLReplicatorIdle, 10.0));
    
    CBLReplicator_Stop(repl);
    REQUIRE(waitForActivityLevel(kCBLReplicatorStopped, 10.0));
}

/*
 https://github.com/couchbaselabs/couchbase-lite-api/blob/master/spec/tests/T0005-Version-Vector.md
 4. DefaultConflictResolverDeleteWins

 Description
 Test that the default conflict resolver that the delete always wins works as expected. 
 There could be already a default conflict resolver test that can be modified to test this test case.

 Steps
 1. Create two databases the names such as "db1" and "db2".
 2. Create a document on each database as :
    - Document id "doc1" on "db1" with content as {"key": "value1"}
    - Document id "doc1" on "db2" with content as {"key": "value2"}
 3. Update the document on each database as the following order:
    - Delete document id "doc1" on "db1"
    - Update document id "doc1" on "db2" as as {"key": "value3"}
4. Start a single shot pull replicator to pull documents from "db2" to "db1".
5. Get the document "doc1" from "db1" and check that the returned document is null.
*/
TEST_CASE_METHOD(ReplicatorLocalTest, "Default Resolver : Deleted Wins", "[Replicator][Conflict]") {
    SECTION("No conflict resolved specified") {
        configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
            colConfig.conflictResolver = nullptr;
        });
    }
    
    SECTION("Specify default conflict resolver") {
        configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
            colConfig.conflictResolver = CBLDefaultConflictResolver;
        });
    }
    
    config.replicatorType = kCBLReplicatorTypePull;
    
    MutableDocument doc1("doc1");
    doc1["key"] = "value1";
    defaultCollection.saveDocument(doc1);

    MutableDocument doc2("doc1");
    doc2["key"] = "value2";
    otherDBDefaultCol.saveDocument(doc2);
    
    // Delete doc:
    defaultCollection.deleteDocument(doc1);
    
    // Update doc:
    doc2["key"] = "value3";
    otherDBDefaultCol.saveDocument(doc2);
    
    // Pull:
    resetReplicator();
    replicate();

    // Deleted doc should win:
    CHECK(asVector(replicatedDocIDs) == vector<string>{"doc1"});
    Document localDoc = defaultCollection.getDocument("doc1");
    REQUIRE(!localDoc);
    
    // Additional Check:
    
    // Push
    config.replicatorType = kCBLReplicatorTypePush;
    replicatedDocIDs.clear();
    resetReplicator();
    replicate();
    
    // Resolved doc should be pushed:
    CHECK(asVector(replicatedDocIDs) == vector<string>{"doc1"});
    Document remoteDoc = otherDBDefaultCol.getDocument("doc1");
    REQUIRE(!remoteDoc);
}

/*
 https://github.com/couchbaselabs/couchbase-lite-api/blob/master/spec/tests/T0005-Version-Vector.md
 3. DefaultConflictResolverLastWriteWins

 Description
 Test that the default conflict resolver that the last write wins works as expected.
 There could be already a default conflict resolver test that can be modified to test this test case.

 Steps
 1. Create two databases the names such as "db1" and "db2".
 2. Create a document on each database in the exact order as :
    - Document id "doc1" on "db2" with content as {"key": "value2"}
    - Document id "doc1" on "db1" with content as {"key": "value1"}
 3. Start a single shot pull replicator to pull documents from "db2" to "db1".
 4. Get the document "doc1" from "db1" and check that the content is {"key": "value1"}.
 5. Create a document on each database in the exact order as :
    - Document id "doc2" on "db1" with content as {"key": "value1"}
    - Document id "doc2" on "db2" with content as {"key": "value2"}
 6. Start a single shot pull replicator to pull documents from "db2" to "db1".
 7. Get the document "doc2" from "db1" and check that the content is {"key": "value2"}.
*/
TEST_CASE_METHOD(ReplicatorLocalTest, "Default Resolver : Last Write Wins - Client", "[Replicator][Conflict]") {
    // Test Step 1 - 4
    SECTION("No conflict resolved specified") {
        configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
            colConfig.conflictResolver = nullptr;
        });
    }
    
    SECTION("Specify default conflict resolver") {
        configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
            colConfig.conflictResolver = CBLDefaultConflictResolver;
        });
    }
    
    config.replicatorType = kCBLReplicatorTypePull;
    
    // Create:
    MutableDocument doc2("doc1");
    doc2["key"] = "value2";
    otherDBDefaultCol.saveDocument(doc2);
    
    // Create:
    MutableDocument doc1("doc1");
    doc1["key"] = "value1";
    defaultCollection.saveDocument(doc1);
    
    // Pull
    resetReplicator();
    replicate();

    // Last Write:
    CHECK(asVector(replicatedDocIDs) == vector<string>{"doc1"});
    Document localDoc = defaultCollection.getDocument("doc1");
    REQUIRE(localDoc);
    CHECK(localDoc["key"].asString() == "value1"_sl);
    
    // Additional Check:
    
    // Push
    config.replicatorType = kCBLReplicatorTypePush;
    replicatedDocIDs.clear();
    resetReplicator();
    replicate();
    
    // Resolved doc
    CHECK(asVector(replicatedDocIDs) == vector<string>{"doc1"});
    Document remoteDoc = otherDBDefaultCol.getDocument("doc1");
    REQUIRE(remoteDoc);
    CHECK(remoteDoc["key"].asString() == "value1"_sl);
}

/*
 https://github.com/couchbaselabs/couchbase-lite-api/blob/master/spec/tests/T0005-Version-Vector.md
 3. DefaultConflictResolverLastWriteWins

 Description
 Test that the default conflict resolver that the last write wins works as expected.
 There could be already a default conflict resolver test that can be modified to test this test case.

 Steps
 1. Create two databases the names such as "db1" and "db2".
 2. Create a document on each database in the exact order as :
    - Document id "doc1" on "db2" with content as {"key": "value2"}
    - Document id "doc1" on "db1" with content as {"key": "value1"}
 3. Start a single shot pull replicator to pull documents from "db2" to "db1".
 4. Get the document "doc1" from "db1" and check that the content is {"key": "value1"}.
 5. Create a document on each database in the exact order as :
    - Document id "doc2" on "db1" with content as {"key": "value1"}
    - Document id "doc2" on "db2" with content as {"key": "value2"}
 6. Start a single shot pull replicator to pull documents from "db2" to "db1".
 7. Get the document "doc2" from "db1" and check that the content is {"key": "value2"}.
*/
TEST_CASE_METHOD(ReplicatorLocalTest, "Default Resolver : Last Write Wins - Server", "[Replicator][Conflict]") {
    // Test Step 5 - 7
    SECTION("No conflict resolved specified") {
        configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
            colConfig.conflictResolver = nullptr;
        });
    }
    
    SECTION("Specify default conflict resolver") {
        configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
            colConfig.conflictResolver = CBLDefaultConflictResolver;
        });
    }
    
    config.replicatorType = kCBLReplicatorTypePull;
    
    // Create:
    MutableDocument doc1("doc2");
    doc1["key"] = "value1";
    defaultCollection.saveDocument(doc1);
    
    MutableDocument doc2("doc2");
    doc2["key"] = "value2";
    otherDBDefaultCol.saveDocument(doc2);

    // Pull
    resetReplicator();
    replicate();

    // Last Write:
    CHECK(asVector(replicatedDocIDs) == vector<string>{"doc2"});
    Document localDoc = defaultCollection.getDocument("doc2");
    REQUIRE(localDoc);
    CHECK(localDoc["key"].asString() == "value2"_sl);
    
    // Additional Check:
    
    // Push
    config.replicatorType = kCBLReplicatorTypePush;
    replicatedDocIDs.clear();
    resetReplicator();
    replicate();
    
    // Resolved doc
    CHECK(asVector(replicatedDocIDs) == vector<string>{});
    Document remoteDoc = otherDBDefaultCol.getDocument("doc2");
    REQUIRE(remoteDoc);
    CHECK(remoteDoc["key"].asString() == "value2"_sl);
}

class ReplicatorConflictTest : public ReplicatorLocalTest {
public:
    enum class ResolverMode { kLocalWins, kRemoteWins, kMerge, kMergeAutoID };
    
    bool deleteLocal {false}, deleteRemote {false}, deleteMerged {false};
    ResolverMode resolverMode = {ResolverMode::kLocalWins};
    
    bool resolverCalled {false};
    alloc_slice expectedLocalRevID, expectedRemoteRevID;
    
    string docID;
    unsigned count {0};

    Collection otherDBDefaultCol = otherDB.getDefaultCollection();
    
    // Can be called multiple times; different document id will be used each time.
    void testConflict(bool delLocal, bool delRemote, bool delMerged, ResolverMode resMode) {
        deleteLocal = delLocal;
        deleteRemote = delRemote;
        deleteMerged = delMerged;
        resolverMode = resMode;
        
        expectedLocalRevID = nullptr;
        expectedRemoteRevID = nullptr;
        
        docID = "doc" + to_string(++count);
        
        // Save the same doc to each db (will have the same revision),
        MutableDocument doc(docID);
        doc["greeting"] = "Howdy!";
        defaultCollection.saveDocument(doc);
        if (deleteLocal) {
            defaultCollection.deleteDocument(doc);
        } else {
            doc["expletive"] = "Shazbatt!";
            auto blob = Blob("text/plain"_sl, "Blob!"_sl);
            doc["signature"] = blob.properties();
            defaultCollection.saveDocument(doc);
            expectedLocalRevID = doc.revisionID();
        }

        doc = MutableDocument(docID);
        doc["greeting"] = "Howdy!";
        otherDBDefaultCol.saveDocument(doc);
        if (deleteRemote) {
            otherDBDefaultCol.deleteDocument(doc);
        } else {
            doc["expletive"] = "Frak!";
            auto blob = Blob("text/plain"_sl, "Pop!"_sl);
            doc["signature"] = blob.properties();
            otherDBDefaultCol.saveDocument(doc);
            expectedRemoteRevID = CBLDocument_CanonicalRevisionID(doc.ref());
        }

        replicatedDocIDs.clear();
        resolverCalled = false;
        
        configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
            colConfig.conflictResolver = [](void *context,
                                            FLString documentID,
                                            const CBLDocument *localDocument,
                                            const CBLDocument *remoteDocument) -> const CBLDocument* {
                cerr << "--- Entering custom conflict resolver! (local=" << localDocument <<
                ", remote=" << remoteDocument << ")\n";
                auto resolved = ((ReplicatorConflictTest*)context)->conflictResolver(documentID, localDocument, remoteDocument);
                cerr << "--- Returning " << resolved << " from custom conflict resolver\n";
                return resolved;
            };
        });
        
        // Pull and Resolve Conflict:
        config.replicatorType = kCBLReplicatorTypePull;
        resetReplicator();
        replicate();

        // Check:
        CHECK(asVector(replicatedDocIDs) == vector<string>{docID});
        
        Document localDoc = defaultCollection.getDocument(docID);
        if (resolverMode == ResolverMode::kLocalWins) {
            if (deleteLocal) {
                REQUIRE(!localDoc);
            } else {
                REQUIRE(localDoc);
                CHECK(localDoc["greeting"].asString() == "Howdy!"_sl);
                CHECK(localDoc["expletive"].asString() == "Shazbatt!"_sl);
                Blob blob(localDoc["signature"].asDict());
                CHECK(blob.loadContent() == "Blob!"_sl);
            }
        } else if (resolverMode == ResolverMode::kRemoteWins) {
            if (deleteRemote) {
                REQUIRE(!localDoc);
            } else {
                REQUIRE(localDoc);
                CHECK(localDoc["greeting"].asString() == "Howdy!"_sl);
                CHECK(localDoc["expletive"].asString() == "Frak!"_sl);
                Blob blob(localDoc["signature"].asDict());
                CHECK(blob.loadContent() == "Pop!"_sl);
            }
        } else {
            if (deleteMerged) {
                REQUIRE(!localDoc);
            } else {
                REQUIRE(localDoc);
                CHECK(localDoc["greeting"].asString() == "Howdy!"_sl);
                CHECK(localDoc["expletive"].asString() == "Frak!"_sl);
                Blob blob(localDoc["signature"].asDict());
                CHECK(blob.loadContent() == "Bob!"_sl);
                Blob blob2(localDoc["signature2"].asDict());
                CHECK(blob2.loadContent() == "Bob!"_sl);
            }
        }
        
        // Push Resolved Doc to Remote Server:
        config.replicatorType = kCBLReplicatorTypePush;
        resetReplicator();
        replicate();
        
        Document remoteDoc = otherDBDefaultCol.getDocument(docID);
        if (resolverMode == ResolverMode::kLocalWins) {
            if (deleteLocal) {
                REQUIRE(!remoteDoc);
            } else {
                REQUIRE(remoteDoc);
                CHECK(remoteDoc["greeting"].asString() == "Howdy!"_sl);
                CHECK(remoteDoc["expletive"].asString() == "Shazbatt!"_sl);
                Blob blob(localDoc["signature"].asDict());
                CHECK(blob.loadContent() == "Blob!"_sl);
            }
        } else if (resolverMode == ResolverMode::kRemoteWins) {
            if (deleteRemote) {
                REQUIRE(!remoteDoc);
            } else {
                REQUIRE(remoteDoc);
                CHECK(remoteDoc["greeting"].asString() == "Howdy!"_sl);
                CHECK(remoteDoc["expletive"].asString() == "Frak!"_sl);
                Blob blob(localDoc["signature"].asDict());
                CHECK(blob.loadContent() == "Pop!"_sl);
            }
        } else {
            if (deleteMerged) {
                REQUIRE(!remoteDoc);
            } else {
                REQUIRE(remoteDoc);
                CHECK(remoteDoc["greeting"].asString() == "Howdy!"_sl);
                CHECK(remoteDoc["expletive"].asString() == "Frak!"_sl);
                Blob blob(localDoc["signature"].asDict());
                CHECK(blob.loadContent() == "Bob!"_sl);
                Blob blob2(localDoc["signature2"].asDict());
                CHECK(blob2.loadContent() == "Bob!"_sl);
            }
        }
    }


    const CBLDocument* conflictResolver(slice documentID,
                                        const CBLDocument *localDocument,
                                        const CBLDocument *remoteDocument)
    {
        CHECK(!resolverCalled);
        resolverCalled = true;

        CHECK(string(documentID) == docID);
        if (deleteLocal) {
            REQUIRE(!localDocument);
            REQUIRE(!expectedLocalRevID);
        } else {
            REQUIRE(localDocument);
            CHECK(string(CBLDocument_ID(localDocument)) == docID);
            CHECK(slice(CBLDocument_RevisionID(localDocument)) == expectedLocalRevID);
            Dict localProps(CBLDocument_Properties(localDocument));
            CHECK(localProps["greeting"].asString() == "Howdy!"_sl);
            CHECK(localProps["expletive"].asString() == "Shazbatt!"_sl);
        }
        if (deleteRemote) {
            REQUIRE(!remoteDocument);
            REQUIRE(!expectedRemoteRevID);
        } else {
            REQUIRE(remoteDocument);
            CHECK(string(CBLDocument_ID(remoteDocument)) == docID);
            CHECK(slice(CBLDocument_RevisionID(remoteDocument)) == expectedRemoteRevID);
            Dict remoteProps(CBLDocument_Properties(remoteDocument));
            CHECK(remoteProps["greeting"].asString() == "Howdy!"_sl);
            CHECK(remoteProps["expletive"].asString() == "Frak!"_sl);
        }
        
        if (deleteMerged) {
            REQUIRE(resolverMode == ResolverMode::kMerge);
            return nullptr;
        } else {
            switch (resolverMode) {
                case ResolverMode::kLocalWins:
                    return localDocument;
                case ResolverMode::kRemoteWins:
                    return remoteDocument;
                default:
                    // NOTE: The merged doc will be:
                    // {'greeting': 'Howdy!', 'expletive': 'Frak!', 'signature': blob, 'signature2': blob}
                    // ** signature and signature2 have the same blob
                    
                    CBLDocument *mergedDoc;
                    if (resolverMode == ResolverMode::kMergeAutoID)
                        mergedDoc = CBLDocument_Create(); // Allowed but there will be a warning message
                    else
                        mergedDoc = CBLDocument_CreateWithID(documentID);
       
                    FLMutableDict mergedProps;
                    if (localDocument) {
                        FLDict props = CBLDocument_Properties(localDocument);
                        FLMutableDict mProps = FLDict_MutableCopy(props, kFLDefaultCopy);
                        CBLDocument_SetProperties(mergedDoc, mProps);
                        FLMutableDict_Release(mProps);
                        mergedProps = CBLDocument_MutableProperties(mergedDoc);
                    } else if (remoteDocument) {
                        FLDict props = CBLDocument_Properties(remoteDocument);
                        FLMutableDict mProps = FLDict_MutableCopy(props, kFLDefaultCopy);
                        CBLDocument_SetProperties(mergedDoc, mProps);
                        FLMutableDict_Release(mProps);
                        mergedProps = CBLDocument_MutableProperties(mergedDoc);
                    } else {
                        mergedProps = CBLDocument_MutableProperties(mergedDoc);
                        FLMutableDict_SetString(mergedProps, "greeting"_sl, "Howdy!"_sl);
                    }
                    
                    if (remoteDocument) {
                        FLDict props = CBLDocument_Properties(remoteDocument);
                        FLValue expletive = FLDict_Get(props, "expletive"_sl);
                        FLMutableDict_SetValue(mergedProps, "expletive"_sl, expletive);
                    } else {
                        FLMutableDict_SetString(mergedProps, "expletive"_sl, "Frak!"_sl);
                    }
                    
                    CBLBlob* blob = CBLBlob_CreateWithData("text/plain"_sl, "Bob!"_sl);
                    FLMutableDict_SetBlob(mergedProps, "signature"_sl, blob);
                    FLMutableDict_SetBlob(mergedProps, "signature2"_sl, blob); // Use same blob
                    
                    // Do not release `mergedDoc`, otherwise it would be freed before returning!
                    return mergedDoc;
            }
        }
    }
};


TEST_CASE_METHOD(ReplicatorConflictTest, "Custom resolver : local wins", "[Replicator][Conflict]") {
    testConflict(false, false, false, ResolverMode::kLocalWins);
    testConflict(false, true, false, ResolverMode::kLocalWins); // Remote deletion
    testConflict(true, false, false, ResolverMode::kLocalWins); // Local deletion
}


TEST_CASE_METHOD(ReplicatorConflictTest, "Custom resolver : remote wins", "[Replicator][Conflict]") {
    testConflict(false, false, false, ResolverMode::kRemoteWins);
    testConflict(false, true, false, ResolverMode::kRemoteWins); // Remote deletion
    testConflict(true, false, false, ResolverMode::kRemoteWins); // Local deletion
}


TEST_CASE_METHOD(ReplicatorConflictTest, "Custom resolver : merge", "[Replicator][Conflict]") {
    testConflict(false, false, false, ResolverMode::kMerge);
    testConflict(false, false, false, ResolverMode::kMergeAutoID);
    testConflict(false, true, false, ResolverMode::kMerge); // Remote deletion
    testConflict(true, false, false, ResolverMode::kMerge); // Local deletion
    testConflict(false, false, true, ResolverMode::kMerge); // Merge deletion
}


TEST_CASE_METHOD(ReplicatorLocalTest, "Document Replication Listener", "[Replicator]") {
    // No listener:
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    defaultCollection.saveDocument(doc);
    
    config.replicatorType = kCBLReplicatorTypePush;
    enableDocReplicationListener = false;
    replicate();
    CHECK(replicatedDocIDs.empty());
    
    // Add listener:
    doc["greeting"] = "Hello!";
    defaultCollection.saveDocument(doc);
    enableDocReplicationListener = true;
    replicate();
    CHECK(asVector(replicatedDocIDs) == vector<string>{"foo"});
    
    // Remove listener:
    doc["greeting"] = "Hi!";
    defaultCollection.saveDocument(doc);
    enableDocReplicationListener = false;
    replicatedDocIDs.clear();
    replicate();
    CHECK(replicatedDocIDs.empty());
}

TEST_CASE_METHOD(ReplicatorLocalTest, "DocIDs Push Filters", "[Replicator]") {
    MutableDocument doc1("foo1");
    doc1["greeting"] = "Howdy!";
    defaultCollection.saveDocument(doc1);
    
    MutableDocument doc2("foo2");
    doc2["greeting"] = "Howdy!";
    defaultCollection.saveDocument(doc2);
    
    config.replicatorType = kCBLReplicatorTypePush;
    
    auto docIDs = FLMutableArray_NewFromJSON("[\"foo1\"]"_sl, NULL);
    configureCollectionConfigs(config, [docIDs](CBLCollectionConfiguration& colConfig) {
        colConfig.documentIDs = docIDs;
    });
    
    expectedDocumentCount = 1;
    replicate();
    CHECK(asVector(replicatedDocIDs) == vector<string>{"foo1"});
    
    FLMutableArray_Release(docIDs);
}

TEST_CASE_METHOD(ReplicatorLocalTest, "DocIDs Pull Filters", "[Replicator]") {
    MutableDocument doc1("foo1");
    doc1["greeting"] = "Howdy!";
    otherDBDefaultCol.saveDocument(doc1);
    
    MutableDocument doc2("foo2");
    doc2["greeting"] = "Howdy!";
    otherDBDefaultCol.saveDocument(doc2);
    
    config.replicatorType = kCBLReplicatorTypePull;
    
    auto docIDs = FLMutableArray_NewFromJSON("[\"foo1\"]"_sl, NULL);
    configureCollectionConfigs(config, [docIDs](CBLCollectionConfiguration& colConfig) {
        colConfig.documentIDs = docIDs;
    });
    
    expectedDocumentCount = 1;
    replicate();
    CHECK(asVector(replicatedDocIDs) == vector<string>{"foo1"});
    
    FLMutableArray_Release(docIDs);
}

class ReplicatorFilterTest : public ReplicatorLocalTest {
public:
    int count = 0;
    int deletedCount = 0;
    alloc_slice deletedDocID;
    bool rejectAll = false;
    
    void testFilter(bool isPush, bool rejectAllChanges) {
        cbl::Collection sourceCollection;

        if (isPush) {
            sourceCollection = db.getDefaultCollection();
            
            config.replicatorType = kCBLReplicatorTypePush;
            configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
                colConfig.pushFilter = [](void *context, CBLDocument* doc, CBLDocumentFlags flags) -> bool {
                    ReplicatorFilterTest* test = ((ReplicatorFilterTest*)context);
                    return test->filter(context, doc, flags);
                };
            });
            
        } else {
            sourceCollection = otherDB.getDefaultCollection();
            
            config.replicatorType = kCBLReplicatorTypePull;
            configureCollectionConfigs(config, [](CBLCollectionConfiguration& colConfig) {
                colConfig.pullFilter = [](void *context, CBLDocument* doc, CBLDocumentFlags flags) -> bool {
                    ReplicatorFilterTest* test = ((ReplicatorFilterTest*)context);
                    return test->filter(context, doc, flags);
                };
            });
            
            // Make local db non-empty for pulling a deleted doc:
            MutableDocument doc0("doc0");
            defaultCollection.saveDocument(doc0);
        }

        MutableDocument doc1("doc1");
        sourceCollection.saveDocument(doc1);
        
        MutableDocument doc2("doc2");
        sourceCollection.saveDocument(doc2);
        
        MutableDocument doc3("doc3");
        sourceCollection.saveDocument(doc3);
        sourceCollection.deleteDocument(doc3);
        
        count = 0;
        deletedCount = 0;
        deletedDocID = nullptr;
        rejectAll = rejectAllChanges;
        
        resetReplicator();
        replicate();
        
        CBLReplicatorStatus status = CBLReplicator_Status(repl);
        CHECK(status.error.code == 0);
        CHECK(status.progress.complete == 1.0);
        CHECK(status.progress.documentCount == (rejectAllChanges ? 0 : 2));
        
        CHECK(count == 3);
        CHECK(deletedCount == 1);
        CHECK(deletedDocID == "doc3"_sl);
    }
    
    bool filter(void *context, CBLDocument* doc, CBLDocumentFlags flags) {
        count++;
        
        if (flags & kCBLDocumentFlagsDeleted) {
            deletedCount++;
            deletedDocID = CBLDocument_ID(doc);
        }
        
        if (rejectAll || FLSlice_Equal(CBLDocument_ID(doc), "doc2"_sl))
            return false;
        
        return true;
    }
};


TEST_CASE_METHOD(ReplicatorFilterTest, "Push Filter", "[Replicator][Filter]") {
    testFilter(true, true);
    testFilter(true, false);
}


TEST_CASE_METHOD(ReplicatorFilterTest, "Pull Filter", "[Replicator][Filter]") {
    testFilter(false, true);
    testFilter(false, false);
}


#endif // COUCHBASE_ENTERPRISE
