//
// CollectionTest_Cpp.cc
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

#include "cbl++/CouchbaseLite.hh"

using namespace std;
using namespace fleece;
using namespace cbl;

class CollectionTest_Cpp : public CBLTest_Cpp {
    
public:
    
    void testInvalidCollection(Collection& col) {
        REQUIRE(col);
        
        ExpectingExceptions x;
        
        // Properties:
        CHECK(!col.name().empty());
        CHECK(!col.scopeName().empty());
        CHECK(col.count() == 0);
        
        // Document Functions:
        CBLError error {};
        MutableDocument doc("doc1");
        try { col.saveDocument(doc); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        auto conflictHandler = [](MutableDocument d1, Document d2) -> bool { return true; };
        try { (void) col.saveDocument(doc, conflictHandler); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { (void) !col.saveDocument(doc, kCBLConcurrencyControlLastWriteWins); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.getDocument("doc1"); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.getMutableDocument("doc1"); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.deleteDocument(doc); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { (void) !col.deleteDocument(doc, kCBLConcurrencyControlLastWriteWins); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.purgeDocument(doc); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.purgeDocument("doc1"); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.getDocumentExpiration("doc1"); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.setDocumentExpiration("doc1", CBL_Now()); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.createValueIndex("Value", {}); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.createFullTextIndex("FTS", {}); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        error = {};
        try { col.getIndexNames(); } catch (const cbl::Error& e) { error = asCBLError(e); }
        CheckNotOpenError(error);
        
        auto listener = [](const CollectionChange* change) { };
        auto token = col.addChangeListener(listener);
        token.remove();
        
        auto docListener = [](const DocumentChange* change) { };
        auto docToken = col.addDocumentChangeListener("doc1", docListener);
        docToken.remove();
    }
};

#define NOT_DELETE_DEFAULT_COLLECTION

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Default Collection", "[Collection]") {
    CHECK(defaultCollection.name() == "_default");
    CHECK(defaultCollection.scopeName() == "_default");
    
    MutableArray names = db.getCollectionNames();
    REQUIRE(names);
    CHECK(names.toJSONString() == R"(["_default"])");
}

#ifndef NOT_DELETE_DEFAULT_COLLECTION
TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Delete Default Collection", "[Collection]") {
    db.deleteCollection(kCBLDefaultCollectionName);
    
    Collection col = db.getDefaultCollection();
    CHECK(col);

    MutableArray names = db.getCollectionNames();
    REQUIRE(names);
    CHECK(names.toJSONString() == R"([])");
    
    ExpectingExceptions ex;
    CBLError error {};
    try {
        db.createCollection(kCBLDefaultCollectionName);
    } catch (const cbl::Error& e) {
        error = asCBLError(e);
    }
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorInvalidParameter);
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Default Scope", "[Collection]") {
    MutableArray names = db.getScopeNames();
    REQUIRE(names);
    CHECK(names.toJSONString() == R"(["_default"])");
    
    // Delete Default Collection:
    db.deleteCollection(kCBLDefaultCollectionName);
    col = db.getDefaultCollection();
    CHECK(!col);
    
    names = db.getScopeNames();
    REQUIRE(names);
    CHECK(names.toJSONString() == R"(["_default"])");
}
#else

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Default Collection Cannot Be Deleted", "[Collection]") {

    ExpectingExceptions ex;

    // Try delete the default collection - should fail and catch error:
    try {
        db.deleteCollection(kCBLDefaultCollectionName);
    } catch (const cbl::Error& e){
        CHECK((e.domain == kCBLDomain && e.code == kCBLErrorInvalidParameter ));
    }
}

#endif

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Create And Get Collection In Default Scope", "[Collection]") {
    Collection col = db.getCollection("colA", kCBLDefaultScopeName);
    REQUIRE(!col);
    
    SECTION("Specify scope name") {
        col = db.createCollection("colA", kCBLDefaultScopeName);
    }
    
    SECTION("Not specify scope name") {
        col = db.createCollection("colA");
    }
    
    REQUIRE(col);
    CHECK(col.name() == "colA");
    CHECK(col.scopeName() == "_default");
    
    col = db.getCollection("colA");
    REQUIRE(col);
    CHECK(col.name() == "colA");
    CHECK(col.scopeName() == "_default");
    
    MutableArray names = db.getCollectionNames();
    REQUIRE(names);
    CHECK(names.toJSONString() == R"(["_default","colA"])");
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Create And Get Collection In Named Scope", "[Collection]") {
    Collection col = db.getCollection("colA", "scopeA");
    REQUIRE(!col);
    
    col = db.createCollection("colA", "scopeA");
    REQUIRE(col);
    CHECK(col.name() == "colA");
    CHECK(col.scopeName() == "scopeA");
    
    col = db.getCollection("colA", "scopeA");
    REQUIRE(col);
    CHECK(col.name() == "colA");
    CHECK(col.scopeName() == "scopeA");
    
    MutableArray names = db.getCollectionNames("scopeA");
    REQUIRE(names);
    CHECK(names.toJSONString() == R"(["colA"])");
    
    MutableArray scopeNames = db.getScopeNames();
    REQUIRE(scopeNames);
    CHECK(scopeNames.toJSONString() == R"(["_default","scopeA"])");
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Create Existing Collection", "[Collection]") {
    Collection col1 = db.createCollection("colA", "scopeA");
    REQUIRE(col1);
    CHECK(col1.name() == "colA");
    CHECK(col1.scopeName() == "scopeA");
    
    Collection col2 = db.createCollection("colA", "scopeA");
    REQUIRE(col2);
    CHECK(col2.name() == "colA");
    CHECK(col2.scopeName() == "scopeA");
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Delete Collection", "[Collection]") {
    Collection col = db.createCollection("colA", "scopeA");
    REQUIRE(col);
    CHECK(col.name() == "colA");
    CHECK(col.scopeName() == "scopeA");
    
    col = db.getCollection("colA", "scopeA");
    REQUIRE(col);
    CHECK(col.name() == "colA");
    CHECK(col.scopeName() == "scopeA");
    
    // Add some docs:
    createNumberedDocsWithPrefix(col, 100, "doc");
    CHECK(col.count() == 100);
    
    MutableArray scopeNames = db.getScopeNames();
    REQUIRE(scopeNames);
    CHECK(scopeNames.toJSONString() == R"(["_default","scopeA"])");
    
    // Delete collection:
    db.deleteCollection("colA", "scopeA");
    col = db.getCollection("colA", "scopeA");
    CHECK(!col);
    
    scopeNames = db.getScopeNames();
    REQUIRE(scopeNames);
    CHECK(scopeNames.toJSONString() == R"(["_default"])");
    
    // Recreate collection:
    col = db.createCollection("colA", "scopeA");
    REQUIRE(col);
    CHECK(col.name() == "colA");
    CHECK(col.scopeName() == "scopeA");
    CHECK(col.count() == 0);
    
    col = db.getCollection("colA", "scopeA");
    REQUIRE(col);
    CHECK(col.name() == "colA");
    CHECK(col.scopeName() == "scopeA");
    CHECK(col.count() == 0);
    
    scopeNames = db.getScopeNames();
    REQUIRE(scopeNames);
    CHECK(scopeNames.toJSONString() == R"(["_default","scopeA"])");
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Use Invalid Collection", "[Collection]") {
    Collection col = db.createCollection("colA", "scopeA");
    REQUIRE(col);
    
    SECTION("Delete Collection") {
        db.deleteCollection("colA", "scopeA");
    }
    
    SECTION("Delete Collection from another db instance") {
        Database db2 = openDatabaseNamed(db.name());
        db2.deleteCollection("colA", "scopeA");
        db2.close();
    }
    
    SECTION("Close Database") {
        db.close();
        db = nullptr;
    }
    
    SECTION("Delete Database") {
        db.deleteDatabase();
        db = nullptr;
    }
    
    testInvalidCollection(col);
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Collection Full Name", "[Collection]") {
    // 3.1 TestGetFullNameFromDefaultCollection
    Collection col1 = db.getDefaultCollection();
    REQUIRE(col1);
    CHECK(col1.fullName() == "_default._default");
    
    // 3.2 TestGetFullNameFromNewCollectionInDefaultScope
    Collection col2 = db.createCollection("colA");
    REQUIRE(col2);
    CHECK(col2.fullName() == "_default.colA");
    
    // 3.3 TestGetFullNameFromNewCollectionInCustomScope
    Collection col3 = db.createCollection("colA", "scopeA");
    REQUIRE(col3);
    CHECK(col3.fullName() == "scopeA.colA");
    
    // 3.4 TestGetFullNameFromExistingCollectionInDefaultScope
    Collection col4 = db.getCollection("colA");
    REQUIRE(col4);
    CHECK(col4.fullName() == "_default.colA");
    
    // 3.5 TestGetFullNameFromExistingCollectionInCustomScope
    Collection col5 = db.getCollection("colA", "scopeA");
    REQUIRE(col5);
    CHECK(col5.fullName() == "scopeA.colA");
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Collection Database", "[Collection]") {
    // 3.1 TestGetDatabaseFromNewCollection
    Collection col1 = db.createCollection("colA", "scopeA");
    REQUIRE(col1);
    CHECK(col1.database().ref() == db.ref());
    
    // 3.2 TestGetDatabaseFromExistingCollection
    Collection col2 = db.getCollection("colA", "scopeA");
    REQUIRE(col2);
    CHECK(col2.database().ref() == db.ref());
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Create, Get and Delete Index", "[Collection]") {
    RetainedArray names = defaultCollection.getIndexNames();
    REQUIRE(names);
    REQUIRE(names.count() == 0);
    
    CBLValueIndexConfiguration index1 = {kCBLN1QLLanguage, "id"_sl};
    defaultCollection.createValueIndex("index1", index1);
    
    CBLValueIndexConfiguration index2 = {kCBLN1QLLanguage, "firstname, lastname"_sl};
    defaultCollection.createValueIndex("index2", index2);
    
    CBLFullTextIndexConfiguration index3 = {kCBLN1QLLanguage, "product.description"_sl, true};
    defaultCollection.createFullTextIndex("index3", index3);
    
    names = defaultCollection.getIndexNames();
    REQUIRE(names);
    REQUIRE(names.count() == 3);
    CHECK(names[0].asString() == "index1");
    CHECK(names[1].asString() == "index2");
    CHECK(names[2].asString() == "index3");
    
    defaultCollection.deleteIndex("index1");
    defaultCollection.deleteIndex("index3");
    
    names = defaultCollection.getIndexNames();
    REQUIRE(names);
    REQUIRE(names.count() == 1);
    CHECK(names[0].asString() == "index2");
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Delete Indexes", "[Collection]") {
    Collection col = db.createCollection("colA", "scopeA");
    REQUIRE(col);
    
    CBLValueIndexConfiguration index1 = {kCBLN1QLLanguage, "id"_sl};
    col.createValueIndex("index1", index1);
    
    CBLValueIndexConfiguration index2 = {kCBLN1QLLanguage, "firstname, lastname"_sl};
    col.createValueIndex("index2", index2);
    
    RetainedArray names = col.getIndexNames();
    REQUIRE(names);
    CHECK(names.toJSONString() == R"(["index1","index2"])");
    
    col.deleteIndex("index1");
    names = col.getIndexNames();
    REQUIRE(names);
    CHECK(names.toJSONString() == R"(["index2"])");
    
    col.deleteIndex("index2");
    names = col.getIndexNames();
    REQUIRE(names);
    CHECK(names.toJSONString() == R"([])");
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Collection notifications", "[Collection]") {
    int colListenerCalls = 0, fooListenerCalls = 0;
    {
        // Add a listener:
        auto colListener = defaultCollection.addChangeListener([&](CollectionChange* callbackcol) {
            ++colListenerCalls;
            CHECK(callbackcol->collection() == defaultCollection);
            CHECK(callbackcol->docIDs().size() == 1);
            CHECK(callbackcol->docIDs()[0] == "foo");
        });
        auto fooListener = defaultCollection.addDocumentChangeListener("foo", [&](DocumentChange* callbackcol) {
            ++fooListenerCalls;
            CHECK(callbackcol->collection() == defaultCollection);
            CHECK(callbackcol->docID() == "foo");
        });
        // Create a doc, check that the listener was called:
        createDocument("foo", "greeting", "Howdy!");
        CHECK(colListenerCalls == 1);
        CHECK(fooListenerCalls == 1);
    }
    // After being removed, the listener should not be called:
    colListenerCalls = fooListenerCalls = 0;
    createDocument("bar", "greeting", "yo.");
    CHECK(colListenerCalls == 0);
    CHECK(fooListenerCalls == 0);
}

TEST_CASE_METHOD(CollectionTest_Cpp, "C++ Scheduled collection notifications at database level", "[Collection]") {
    // Add a listener:
    int colListenerCalls = 0, fooListenerCalls = 0, barListenerCalls = 0, notificationsReadyCalls = 0;
    auto colListener = defaultCollection.addChangeListener([&](CollectionChange *callbackcol) {
            ++colListenerCalls;
            CHECK(callbackcol->collection() == defaultCollection);
            CHECK(callbackcol->docIDs().size() == 2);
            CHECK(callbackcol->docIDs()[0] == "foo");
            CHECK(callbackcol->docIDs()[1] == "bar");
        });
    auto fooListener = defaultCollection.addDocumentChangeListener("foo", [&](DocumentChange* callbackcol) {
            ++fooListenerCalls;
            CHECK(callbackcol->collection() == defaultCollection);
            CHECK(callbackcol->docID() == "foo");
        });
    auto barListener = defaultCollection.addDocumentChangeListener("bar", [&](DocumentChange* callbackcol) {
        ++barListenerCalls;
        CHECK(callbackcol->collection() == defaultCollection);
        CHECK(callbackcol->docID() == "bar");
        });

    db.bufferNotifications([&](Database callbackdb) {
        ++notificationsReadyCalls;
        CHECK(callbackdb == db);
    });

    // Create two docs; no listeners should be called yet:
    createDocument("foo", "greeting", "Howdy!");
    CHECK(colListenerCalls == 0);
    CHECK(fooListenerCalls == 0);
    CHECK(barListenerCalls == 0);

    createDocument("bar", "greeting", "yo.");
    CHECK(colListenerCalls == 0);
    CHECK(fooListenerCalls == 0);
    CHECK(barListenerCalls == 0);

    // Now the listeners will be called:
    db.sendNotifications();
    CHECK(colListenerCalls == 1);
    CHECK(fooListenerCalls == 1);
    CHECK(barListenerCalls == 1);

    // There should be no more notifications:
    db.sendNotifications();
    CHECK(colListenerCalls == 1);
    CHECK(fooListenerCalls == 1);
    CHECK(barListenerCalls == 1);
}

TEST_CASE_METHOD(CBLTest_Cpp, "Collection Listener Token") {
    int num = 0;
    auto cb = [&num]() { num++; };
    ListenerToken<> listenerToken = ListenerToken<>(cb);
    
    // Context / Callback:
    REQUIRE(listenerToken.context());
    (*(ListenerToken<>::Callback*)listenerToken.context())();
    CHECK(num == 1);
    
    // Token:
    CHECK(!listenerToken.token());
    auto dummy = [](void* context, const CBLCollectionChange* change){ };
    auto listener = CBLCollection_AddChangeListener(defaultCollection.ref(), dummy, nullptr);
    listenerToken.setToken(listener);
    CHECK(listenerToken.token() == listener);
    
    // Move Constructor:
    ListenerToken<> listenerToken2 = std::move(listenerToken);
    REQUIRE(listenerToken2.context());
    (*(ListenerToken<>::Callback*)listenerToken2.context())();
    CHECK(num == 2);
    CHECK(listenerToken2.token() == listener);
    
#ifndef __clang_analyzer__ // Exclude the code from being compiled for analyzer
    CHECK(!listenerToken.context());
    CHECK(!listenerToken.token());
    listenerToken.remove(); // Noops
#endif
    
    // Move Assignment:
    listenerToken = std::move(listenerToken2);
    REQUIRE(listenerToken.context());
    (*(ListenerToken<>::Callback*)listenerToken.context())();
    CHECK(num == 3);
    CHECK(listenerToken.token() == listener);
    
#ifndef __clang_analyzer__ // Exclude the code from being compiled for analyzer
    CHECK(!listenerToken2.context());
    CHECK(!listenerToken2.token());
    listenerToken2.remove(); // Noops
#endif
    
    // Remove:
    listenerToken.remove();
    CHECK(!listenerToken.context());
    CHECK(!listenerToken.context());
    listenerToken.remove(); // Noops
}
