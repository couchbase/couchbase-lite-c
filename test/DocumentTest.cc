//
// DocumentTest.cc
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

#include "CBLTest.hh"
#include "CBLPrivate.h"
#include "fleece/Fleece.hh"
#include "fleece/Mutable.hh"
#include <thread>

using namespace fleece;
using namespace std;

static constexpr const slice kCollectionName = "CBLTestCollection";
static constexpr const slice kOtherCollectionName = "CBLTestOtherCollection";

static int collectionListenerCalls = 0;
static int docListenerCalls = 0;

static void collectionListener(void *context, const CBLCollectionChange* change) {
    ++collectionListenerCalls;
    CHECK(change->collection == (CBLCollection*)context);
    CHECK(change->numDocs == 1);
    CHECK(slice(change->docIDs[0]) == "foo"_sl);
}

static void docListener(void *context, const CBLDocumentChange* change) {
    ++docListenerCalls;
    CHECK(change->collection == (CBLCollection*)context);
    CHECK(slice(change->docID) == "foo"_sl);
}

class DocumentTest : public CBLTest {
public:
    CBLCollection* otherCol = nullptr;
    CBLCollection* col = nullptr;
    
    DocumentTest() {
        CBLError error {};
        
        col = CBLDatabase_CreateCollection(db, kCollectionName, kCBLDefaultScopeName, &error);
        if (col) {
            CHECK(CBLCollection_Count(col) == 0);
        } else {
            FAIL("Can't create test collection: " << error.domain << "/" << error.code);
        }
        
        otherCol = CBLDatabase_CreateCollection(db, kOtherCollectionName, kCBLDefaultScopeName, &error);
        if (otherCol) {
            CHECK(CBLCollection_Count(otherCol) == 0);
        } else {
            FAIL("Can't create test other collection: " << error.domain << "/" << error.code);
        }
    }
    
    void createDocument(CBLCollection* collection, slice docID, slice property, slice value) {
        CBLDocument* doc = CBLDocument_CreateWithID(docID);
        MutableDict props = CBLDocument_MutableProperties(doc);
        FLSlot_SetString(FLMutableDict_Set(props, property), value);
        CBLError error = {};
        bool saved = CBLCollection_SaveDocumentWithConcurrencyControl(collection, doc,
                                                                      kCBLConcurrencyControlFailOnConflict,
                                                                      &error);
        CBLDocument_Release(doc);
        REQUIRE(saved);
    }

    ~DocumentTest() {
        CBLCollection_Release(col);
        CBLCollection_Release(otherCol);
    }
};

TEST_CASE_METHOD(DocumentTest, "Missing Document", "[Document]") {
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "foo"_sl, &error);
    CHECK(doc == nullptr);
    CHECK(error.code == 0);

    CBLDocument* mdoc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(mdoc == nullptr);
    CHECK(error.code == 0);

    CHECK(!CBLCollection_PurgeDocumentByID(col, "foo"_sl, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorNotFound);
}

TEST_CASE_METHOD(DocumentTest, "New Document", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    CHECK(doc != nullptr);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_RevisionID(doc) == nullslice);
    CHECK(CBLDocument_Sequence(doc) == 0);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{}"_sl);
    CHECK(CBLDocument_MutableProperties(doc) == CBLDocument_Properties(doc));
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "New Document With Auto ID", "[Document]") {
    CBLDocument* doc = CBLDocument_Create();
    CHECK(doc != nullptr);
    CHECK(CBLDocument_ID(doc) != nullslice);
    CHECK(CBLDocument_RevisionID(doc) == nullslice);
    CHECK(CBLDocument_Sequence(doc) == 0);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{}"_sl);
    CHECK(CBLDocument_MutableProperties(doc) == CBLDocument_Properties(doc));
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Mutable Copy Mutable Document", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    CHECK(doc != nullptr);
    MutableDict props = CBLDocument_MutableProperties(doc);
    props["greeting"_sl] = "Howdy!"_sl;
    
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_RevisionID(doc) == nullslice);
    CHECK(CBLDocument_Sequence(doc) == 0);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    
    CBLDocument* mDoc = CBLDocument_MutableCopy(doc);
    CHECK(mDoc != doc);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(mDoc) == 0);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    
    CBLDocument_Release(doc);
    CBLDocument_Release(mDoc);
}

TEST_CASE_METHOD(DocumentTest, "Mutable Copy Immutable Document", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    CHECK(doc != nullptr);
    MutableDict props = CBLDocument_MutableProperties(doc);
    props["greeting"_sl] = "Howdy!"_sl;
    
    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    
    const CBLDocument* rDoc = CBLCollection_GetDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(rDoc) == "foo"_sl);
    CHECK(CBLDocument_RevisionID(rDoc) != nullslice);
    CHECK(CBLDocument_Sequence(rDoc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(rDoc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(rDoc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");

    CBLDocument* mDoc = CBLDocument_MutableCopy(rDoc);
    CHECK(mDoc != rDoc);
    CHECK(CBLDocument_ID(mDoc) == "foo"_sl);
    CHECK(CBLDocument_RevisionID(mDoc) == CBLDocument_RevisionID(rDoc));
    CHECK(CBLDocument_Sequence(mDoc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    
    CBLDocument_Release(doc);
    CBLDocument_Release(rDoc);
    CBLDocument_Release(mDoc);
}

TEST_CASE_METHOD(DocumentTest, "Access nested collections from mutable props", "[Document]") {
    CBLError error;
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    REQUIRE(doc);
    REQUIRE(CBLDocument_SetJSON(doc, "{\"name\":{\"first\": \"Jane\"}, \"phones\": [\"650-123-4567\"]}"_sl, &error));
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    
    CBLDocument* mDoc = nullptr;
    FLMutableDict mProps = nullptr;
    
    // Note: When a mutable properties of a document is created, the shallow copy from
    // the original properties will be made.
    
    SECTION("A mutable doc") {
        mDoc = doc;
        CBLDocument_Retain(doc);
    }
    
    SECTION("A mutable doc read from database") {
        mDoc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    }
    
    SECTION("Mutable copy from an immutable doc") {
        const CBLDocument* doc1 = CBLCollection_GetDocument(col, "foo"_sl, &error);
        mDoc = CBLDocument_MutableCopy(doc1);
        CBLDocument_Release(doc1);
    }

    SECTION("Mutable copy from a mutable doc") {
        CBLDocument* mDoc1 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
        mDoc = CBLDocument_MutableCopy(mDoc1);
        CBLDocument_Release(mDoc1);
    }
    
    REQUIRE(mDoc);
    
    mProps = CBLDocument_MutableProperties(mDoc);
    
    // Dict:
    FLDict dict = FLValue_AsDict(FLDict_Get(mProps, "name"_sl));
    REQUIRE(dict);
    CHECK(FLDict_Count(dict) == 1);
    CHECK(FLValue_AsString(FLDict_Get(dict, "first"_sl)) == "Jane"_sl);
    FLMutableDict mDict = FLDict_AsMutable(dict); // Immutable
    CHECK(!mDict);
    
    mDict = FLMutableDict_GetMutableDict(mProps, "name"_sl);
    REQUIRE(mDict);
    CHECK(FLDict_Count(mDict) == 1);
    CHECK(FLValue_AsString(FLDict_Get(mDict, "first"_sl)) == "Jane"_sl);
    
    // Array:
    FLArray array = FLValue_AsArray(FLDict_Get(mProps, "phones"_sl));
    REQUIRE(array);
    REQUIRE(FLArray_Count(array) == 1);
    CHECK(FLValue_AsString(FLArray_Get(array, 0)) == "650-123-4567"_sl);
    FLMutableArray mArray = FLArray_AsMutable(array); // Immutable
    CHECK(!mArray);
    
    mArray = FLMutableDict_GetMutableArray(mProps, "phones"_sl);
    REQUIRE(mArray);
    REQUIRE(FLArray_Count(mArray) == 1);
    CHECK(FLValue_AsString(FLArray_Get(mArray, 0)) == "650-123-4567"_sl);
    
    CBLDocument_Release(mDoc);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Access nested collections from a copy of modified mutable doc", "[Document]") {
    CBLError error;
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    REQUIRE(doc);
    REQUIRE(CBLDocument_SetJSON(doc, "{\"name\":{\"first\": \"Jane\"}, \"phones\": [\"650-123-4567\"]}"_sl, &error));
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    
    FLMutableDict mProps = CBLDocument_MutableProperties(doc);
    
    // Modify Dict:
    FLMutableDict mDict = FLMutableDict_GetMutableDict(mProps, "name"_sl);
    REQUIRE(mDict);
    CHECK(FLDict_Count(mDict) == 1);
    CHECK(FLValue_AsString(FLDict_Get(mDict, "first"_sl)) == "Jane"_sl);
    FLMutableDict_SetString(mDict, "first"_sl, "Julie"_sl);
    CHECK(FLValue_AsString(FLDict_Get(mDict, "first"_sl)) == "Julie"_sl);
    
    // Modify Array:
    FLMutableArray mArray = FLMutableDict_GetMutableArray(mProps, "phones"_sl);
    REQUIRE(mArray);
    REQUIRE(FLArray_Count(mArray) == 1);
    CHECK(FLValue_AsString(FLArray_Get(mArray, 0)) == "650-123-4567"_sl);
    FLMutableArray_SetString(mArray, 0, "415-123-4567"_sl);
    CHECK(FLValue_AsString(FLArray_Get(mArray, 0)) == "415-123-4567"_sl);
    
    // Copy:
    CBLDocument* mDoc = CBLDocument_MutableCopy(doc);
    mProps = CBLDocument_MutableProperties(mDoc);
    
    // Check Dict:
    FLDict dict = FLValue_AsDict(FLDict_Get(mProps, "name"_sl));
    REQUIRE(dict);
    CHECK(FLDict_Count(dict) == 1);
    CHECK(FLValue_AsString(FLDict_Get(dict, "first"_sl)) == "Julie"_sl);
    FLMutableDict mDict2 = FLDict_AsMutable(dict); // Already mutable
    CHECK(mDict2);
    
    mDict2 = FLMutableDict_GetMutableDict(mProps, "name"_sl);
    REQUIRE(mDict2);
    CHECK(FLDict_Count(mDict2) == 1);
    CHECK(FLValue_AsString(FLDict_Get(mDict2, "first"_sl)) == "Julie"_sl);
    CHECK(mDict2 != mDict);
    
    // Check Array:
    FLArray array = FLValue_AsArray(FLDict_Get(mProps, "phones"_sl));
    REQUIRE(array);
    REQUIRE(FLArray_Count(array) == 1);
    CHECK(FLValue_AsString(FLArray_Get(array, 0)) == "415-123-4567"_sl);
    FLArray mArray2 = FLArray_AsMutable(array);
    CHECK(mArray2); // Already mutable
    
    mArray2 = FLMutableDict_GetMutableArray(mProps, "phones"_sl);
    REQUIRE(mArray2);
    REQUIRE(FLArray_Count(mArray2) == 1);
    CHECK(FLValue_AsString(FLArray_Get(mArray2, 0)) == "415-123-4567"_sl);
    CHECK(mArray2 != mArray);
    
    CBLDocument_Release(mDoc);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Set Properties", "[Document]") {
    CBLDocument* doc1 = CBLDocument_Create();
    FLMutableDict prop1 = CBLDocument_MutableProperties(doc1);
    FLMutableDict_SetString(prop1, "greeting"_sl, "hello"_sl);
    
    CBLDocument* doc2 = CBLDocument_Create();
    CBLDocument_SetProperties(doc2, prop1);
    FLMutableDict prop2 = CBLDocument_MutableProperties(doc2);
    CHECK(FLValue_AsString(FLDict_Get(prop2, "greeting"_sl)) == "hello"_sl );
    CHECK(prop1 == prop2);
    
    CBLDocument_Release(doc1);
    CHECK(FLValue_AsString(FLDict_Get(prop2, "greeting"_sl)) == "hello"_sl );
    CBLDocument_Release(doc2);
}

TEST_CASE_METHOD(DocumentTest, "Get Non Existing Document") {
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "foo"_sl, &error);
    REQUIRE(doc == nullptr);
    CHECK(error.code == 0);
}

TEST_CASE_METHOD(DocumentTest, "Get Document with Empty ID", "[Document]") {
    CBLError error;
    ExpectingExceptions x;
    const CBLDocument* doc = CBLCollection_GetDocument(col, ""_sl, &error);
    REQUIRE(doc == nullptr);
    CHECK(error.code == 0);
}

#pragma mark - Save Document:

TEST_CASE_METHOD(DocumentTest, "Save Empty Document", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    auto revID = alloc_slice(CBLDocument_RevisionID(doc));
    CHECK(!revID.empty());
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{}"_sl);
    CBLDocument_Release(doc);

    doc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    CHECK(CBLDocument_RevisionID(doc) == revID);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{}"_sl);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document With Properties", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    MutableDict props = CBLDocument_MutableProperties(doc);
    props["greeting"_sl] = "Howdy!"_sl;
    // or alternatively:  FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");

    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    auto revID = alloc_slice(CBLDocument_RevisionID(doc));
    CHECK(!revID.empty());
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CBLDocument_Release(doc);

    doc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    CHECK(CBLDocument_RevisionID(doc) == revID);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document Twice", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    MutableDict props = CBLDocument_MutableProperties(doc);
    props["greeting"_sl] = "Howdy!"_sl;
    
    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");

    CBLDocument* savedDoc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(savedDoc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(savedDoc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(savedDoc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(savedDoc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CBLDocument_Release(savedDoc);
    
    // Modify Again:
    props = CBLDocument_MutableProperties(doc);
    props["greeting"_sl] = "Hello!"_sl;
    
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 2);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Hello!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Hello!\"}");
    CBLDocument_Release(doc);

    savedDoc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(savedDoc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(savedDoc) == 2);
    CHECK(alloc_slice(CBLDocument_CreateJSON(savedDoc)) == "{\"greeting\":\"Hello!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(savedDoc)).toJSONString() == "{\"greeting\":\"Hello!\"}");
    CBLDocument_Release(savedDoc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document with LastWriteWin", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);

    CBLError error;
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc, kCBLConcurrencyControlLastWriteWins, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CBLDocument_Release(doc);

    CBLDocument* doc1 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc1) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc1) == 1);
    
    CBLDocument* doc2 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc2) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc2) == 1);
    
    FLMutableDict props1 = CBLDocument_MutableProperties(doc1);
    FLMutableDict_SetString(props1, "name"_sl, "bob"_sl);
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc1, kCBLConcurrencyControlLastWriteWins, &error));
    CHECK(CBLDocument_Sequence(doc1) == 2);
    CBLDocument_Release(doc1);
    
    FLMutableDict props2 = CBLDocument_MutableProperties(doc2);
    FLMutableDict_SetString(props2, "name"_sl, "sally"_sl);
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc2, kCBLConcurrencyControlLastWriteWins, &error));
    CHECK(CBLDocument_Sequence(doc2) == 3);
    CBLDocument_Release(doc2);
    
    doc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 3);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\",\"name\":\"sally\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"sally\"}");
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document with FailOnConflict", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);

    CBLError error;
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc, kCBLConcurrencyControlFailOnConflict, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CBLDocument_Release(doc);

    CBLDocument* doc1 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc1) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc1) == 1);
    
    CBLDocument* doc2 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc2) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc2) == 1);
    
    FLMutableDict props1 = CBLDocument_MutableProperties(doc1);
    FLMutableDict_SetString(props1, "name"_sl, "bob"_sl);
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc1, kCBLConcurrencyControlFailOnConflict, &error));
    CHECK(CBLDocument_Sequence(doc1) == 2);
    CBLDocument_Release(doc1);
    
    FLMutableDict props2 = CBLDocument_MutableProperties(doc2);
    FLMutableDict_SetString(props2, "name"_sl, "sally"_sl);
    CHECK(!CBLCollection_SaveDocumentWithConcurrencyControl(col, doc2, kCBLConcurrencyControlFailOnConflict, &error));
    CBLDocument_Release(doc2);
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorConflict);
    
    error = {};
    doc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 2);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\",\"name\":\"bob\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"bob\"}");
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document with Conflict Handler", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);
    
    CBLConflictHandler failConflict = [](void *c, CBLDocument *mine, const CBLDocument *existing) -> bool {
        CHECK(!c);
        return false;
    };
    
    CBLConflictHandler mergeConflict = [](void *c, CBLDocument *mine, const CBLDocument *existing) -> bool {
        FLMutableDict mergedProps = CBLDocument_MutableProperties(mine);
        FLValue anotherName = FLDict_Get(CBLDocument_Properties(existing),"name"_sl);
        FLMutableDict_SetValue(mergedProps, "anotherName"_sl, anotherName);
        return true;
    };
    
    CBLError error;
    REQUIRE(CBLCollection_SaveDocumentWithConflictHandler(col, doc, failConflict, nullptr, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CBLDocument_Release(doc);

    CBLDocument* doc1 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc1) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc1) == 1);
    
    CBLDocument* doc2 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc2) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc2) == 1);
    
    FLMutableDict props1 = CBLDocument_MutableProperties(doc1);
    FLMutableDict_SetString(props1, "name"_sl, "bob"_sl);
    REQUIRE(CBLCollection_SaveDocumentWithConflictHandler(col, doc1, failConflict, nullptr, &error));
    CHECK(CBLDocument_Sequence(doc1) == 2);
    CBLDocument_Release(doc1);
    
    FLMutableDict props2 = CBLDocument_MutableProperties(doc2);
    FLMutableDict_SetString(props2, "name"_sl, "sally"_sl);
    CHECK(!CBLCollection_SaveDocumentWithConflictHandler(col, doc2, failConflict, nullptr, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorConflict);
    
    error = {};
    CHECK(CBLCollection_SaveDocumentWithConflictHandler(col, doc2, mergeConflict, nullptr, &error));
    CBLDocument_Release(doc2);
    
    doc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 3);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\",\"name\":\"sally\",\"anotherName\":\"bob\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"sally\",\"anotherName\":\"bob\"}");
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document with Conflict Handler : Called twice", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);
    
    CBLConflictHandler mergeConflict = [](void *c, CBLDocument *mine, const CBLDocument *existing) -> bool {
        CHECK(c != nullptr);
        CBLCollection *theCol = (CBLCollection*)c;
        Dict dict = (CBLDocument_Properties(existing));
        if (dict["name"].asString() == "bob"_sl) {
            // Update the doc to cause a new conflict after first merge; the handler will be
            // called again:
            CHECK(CBLCollection_LastSequence(theCol) == 2);
            CBLError e;
            CBLDocument* doc3 = CBLCollection_GetMutableDocument(theCol, "foo"_sl, &e);
            FLMutableDict props3 = CBLDocument_MutableProperties(doc3);
            FLMutableDict_SetString(props3, "name"_sl, "max"_sl);
            REQUIRE(CBLCollection_SaveDocument(theCol, doc3, &e));
            CBLDocument_Release(doc3);
            CHECK(CBLCollection_LastSequence(theCol) == 3);
        } else {
            CHECK(CBLCollection_LastSequence(theCol) == 3);
            CHECK(dict["name"].asString() == "max"_sl);
        }
        
        FLMutableDict mergedProps = CBLDocument_MutableProperties(mine);
        FLMutableDict_SetValue(mergedProps, "anotherName"_sl, dict["name"]);
        return true;
    };
    
    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CBLDocument_Release(doc);

    CBLDocument* doc1 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc1) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc1) == 1);
    
    CBLDocument* doc2 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc2) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc2) == 1);
    
    FLMutableDict props1 = CBLDocument_MutableProperties(doc1);
    FLMutableDict_SetString(props1, "name"_sl, "bob"_sl);
    REQUIRE(CBLCollection_SaveDocument(col, doc1, &error));
    CHECK(CBLDocument_Sequence(doc1) == 2);
    CBLDocument_Release(doc1);
    
    FLMutableDict props2 = CBLDocument_MutableProperties(doc2);
    FLMutableDict_SetString(props2, "name"_sl, "sally"_sl);
    CHECK(CBLCollection_SaveDocumentWithConflictHandler(col, doc2, mergeConflict, col, &error));
    CBLDocument_Release(doc2);
    
    doc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 4);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\",\"name\":\"sally\",\"anotherName\":\"max\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"sally\",\"anotherName\":\"max\"}");
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document with Conflict Handler : On another thread", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);
    
    CBLConflictHandler mergeConflict = [](void *c, CBLDocument *myDoc, const CBLDocument *existingDoc) -> bool {
        // Shouldn't have deadlock when accessing document or database properties
        CHECK(c != nullptr);
        CHECK(CBLDocument_Sequence(myDoc) > 0);
        CHECK(CBLCollection_LastSequence((CBLCollection*)c) > 0);
        
        // Resolve in a different thread
        thread t([](CBLCollection* collection, CBLDocument *mine, const CBLDocument *existing) {
            // Shouldn't have deadlock when accessing document or database properties
            CHECK(CBLDocument_Sequence(mine) > 0);
            CHECK(CBLCollection_LastSequence(collection) > 0);
            FLMutableDict mergedProps = CBLDocument_MutableProperties(mine);
            FLMutableDict_SetString(mergedProps, "name"_sl, "max"_sl);
        }, (CBLCollection*)c, myDoc, existingDoc);
        t.join();
        
        return true;
    };
    
    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 1);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CBLDocument_Release(doc);

    CBLDocument* doc1 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc1) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc1) == 1);
    
    CBLDocument* doc2 = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc2) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc2) == 1);
    
    FLMutableDict props1 = CBLDocument_MutableProperties(doc1);
    FLMutableDict_SetString(props1, "name"_sl, "bob"_sl);
    REQUIRE(CBLCollection_SaveDocument(col, doc1, &error));
    CHECK(CBLDocument_Sequence(doc1) == 2);
    CBLDocument_Release(doc1);
    
    FLMutableDict props2 = CBLDocument_MutableProperties(doc2);
    FLMutableDict_SetString(props2, "name"_sl, "sally"_sl);
    CHECK(CBLCollection_SaveDocumentWithConflictHandler(col, doc2, mergeConflict, col, &error));
    CBLDocument_Release(doc2);
    
    doc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    CHECK(CBLDocument_ID(doc) == "foo"_sl);
    CHECK(CBLDocument_Sequence(doc) == 3);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"greeting\":\"Howdy!\",\"name\":\"max\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"max\"}");
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document into Different Collection", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);
    
    CBLError error;
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    
    ExpectingExceptions x;
    CHECK(!CBLCollection_SaveDocument(otherCol, doc, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorInvalidParameter);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save Document into Same Collection Different Instance", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);
    
    CBLError error;
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    
    CBLScope* scope = CBLCollection_Scope(col);
    CBLCollection* col2 = CBLDatabase_Collection(db, CBLCollection_Name(col), CBLScope_Name(scope), &error);
    CHECK(col != col2);
    CHECK(CBLCollection_SaveDocument(col2, doc, &error));
    
    CBLDocument_Release(doc);
    CBLScope_Release(scope);
    CBLCollection_Release(col2);
}

TEST_CASE_METHOD(DocumentTest, "Save Document into Same Collection Different DB Instance", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);
    
    CBLError error;
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    
    auto config = databaseConfig();
    auto anotherDB = CBLDatabase_Open(kDatabaseName, &config, &error);
    REQUIRE(anotherDB);
    
    CBLScope* scope = CBLCollection_Scope(col);
    CBLCollection* col2 = CBLDatabase_Collection(anotherDB, CBLCollection_Name(col), CBLScope_Name(scope), &error);
    CHECK(col != col2);
    
    ExpectingExceptions x;
    CHECK(!CBLCollection_SaveDocument(col2, doc, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorInvalidParameter);
    
    CBLDocument_Release(doc);
    CBLScope_Release(scope);
    CBLCollection_Release(col2);
    CBLDatabase_Release(anotherDB);
}

TEST_CASE_METHOD(DocumentTest, "Save Document into Different Default Collection Instance", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "greeting"_sl, "Howdy!"_sl);
    
    CBLError error;
    CHECK(CBLCollection_SaveDocument(defaultCollection, doc, &error));
    
    CBLCollection* defaultCollection2 = CBLDatabase_DefaultCollection(db, &error);
    CHECK(defaultCollection != defaultCollection2);
    CHECK(CBLCollection_SaveDocument(defaultCollection2, doc, &error));
    
    CBLDocument_Release(doc);
    CBLCollection_Release(defaultCollection2);
}

#pragma mark - Generation

TEST_CASE_METHOD(DocumentTest, "Generation", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("doc1"_sl);
    REQUIRE(doc);
    CHECK(CBLDocument_Generation(doc) == 0);

    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    CHECK(CBLDocument_Generation(doc) == 1);
    CBLDocument_Release(doc);

    doc = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    CHECK(CBLDocument_Generation(doc) == 1);

    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    CHECK(CBLDocument_Generation(doc) == 2);
    CBLDocument_Release(doc);
}

#pragma mark - Revision History

/*
 https://github.com/couchbaselabs/couchbase-lite-api/blob/master/spec/tests/T0005-Version-Vector.md
 2. TestDocumentRevisionHistory

 Description
 Test that the document's timestamp returns value as expected.

 Steps
 1. Create a new document with id = "doc1"
 2. Get document's _revisionIDs and check that the value returned is an empty array.
 3. Save the document into the default collection.
 4. Get document's _revisionIDs and check that the value returned is an array containing a 
    single revision id which is the revision id of the documnt.
 5. Get the document id = "doc1" from the database.
 6. Get document's _revisionIDs and check that the value returned is an array containing a 
    single revision id which is the revision id of the documnt.
 */
TEST_CASE_METHOD(DocumentTest, "Revision History", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    
    alloc_slice revHistory = CBLDocument_GetRevisionHistory(doc);
    CHECK(revHistory == nullslice);
    
    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(col, doc, &error));
    revHistory = CBLDocument_GetRevisionHistory(doc);
    CHECK(revHistory != nullslice);
    CBLDocument_Release(doc);
    
    doc = CBLCollection_GetMutableDocument(col, "foo"_sl, &error);
    revHistory = CBLDocument_GetRevisionHistory(doc);
    CHECK(revHistory != nullslice);
    CBLDocument_Release(doc);
}

#pragma mark - Delete Document:

TEST_CASE_METHOD(DocumentTest, "Delete Non Existing Document", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    
    ExpectingExceptions x;
    CBLError error;
    CHECK(!CBLCollection_DeleteDocument(col, doc, &error));
    CBLDocument_Release(doc);
    
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorNotFound);
    
    error = {};
    CHECK(!CBLCollection_DeleteDocumentByID(col, "foo"_sl, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorNotFound);
}

TEST_CASE_METHOD(DocumentTest, "Delete Document", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");
    createDocument(col, "doc2", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);
    CHECK(CBLDocument_Sequence(doc) == 1);
    
    CHECK(CBLCollection_DeleteDocument(col, doc, &error));
    CHECK(CBLDocument_Sequence(doc) == 3);
    CBLDocument_Release(doc);
    CHECK(!CBLCollection_GetDocument(col, "doc1"_sl, &error));
    
    CHECK(CBLCollection_DeleteDocumentByID(col, "doc2"_sl, &error));
    CHECK(!CBLCollection_GetDocument(col, "doc2"_sl, &error));
}

TEST_CASE_METHOD(DocumentTest, "Delete Already Deleted Document") {
    createDocument(col, "doc1", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);
    
    CHECK(CBLCollection_DeleteDocument(col, doc, &error));
    CHECK(CBLDocument_Sequence(doc) == 2);
    CHECK(!CBLCollection_GetDocument(col, "doc1"_sl, &error));
    
    CHECK(CBLCollection_DeleteDocument(col, doc, &error));
    CHECK(CBLDocument_Sequence(doc) == 3);
    CBLDocument_Release(doc);
    
    CHECK(CBLCollection_DeleteDocumentByID(col, "doc1"_sl, &error));
}

TEST_CASE_METHOD(DocumentTest, "Delete Then Update Document", "[.CBL-6483]") {
    createDocument(col, "doc1", "foo", "bar");
    
    CBLError error;
    CBLDocument* doc = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);
    
    CHECK(CBLCollection_DeleteDocument(col, doc, &error));
    CHECK(CBLDocument_Sequence(doc) == 2);
    CHECK(!CBLCollection_GetDocument(col, "doc1"_sl, &error));
    
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetString(props, "foo"_sl, "bar1"_sl);
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    
    CHECK(CBLDocument_ID(doc) == "doc1"_sl);
    CHECK(CBLDocument_Sequence(doc) == 3);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc)) == "{\"foo\":\"bar1\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc)).toJSONString() == "{\"foo\":\"bar1\"}");
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Delete Document with LastWriteWin", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");

    CBLError error;
    CBLDocument* doc1 = CBLCollection_GetMutableDocument(col,"doc1"_sl, &error);
    CHECK(CBLDocument_ID(doc1) == "doc1"_sl);
    CHECK(CBLDocument_Sequence(doc1) == 1);
    
    CBLDocument* doc2 = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    CHECK(CBLDocument_ID(doc2) == "doc1"_sl);
    CHECK(CBLDocument_Sequence(doc2) == 1);
    
    FLMutableDict props1 = CBLDocument_MutableProperties(doc1);
    FLMutableDict_SetString(props1, "foo"_sl, "bar1"_sl);
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc1, kCBLConcurrencyControlLastWriteWins, &error));
    CHECK(CBLDocument_Sequence(doc1) == 2);
    CBLDocument_Release(doc1);
    
    REQUIRE(CBLCollection_DeleteDocumentWithConcurrencyControl(col, doc2, kCBLConcurrencyControlLastWriteWins, &error));
    CHECK(CBLDocument_Sequence(doc2) == 3);
    CBLDocument_Release(doc2);
    
    REQUIRE(!CBLCollection_GetDocument(col, "doc1"_sl, &error));
}

TEST_CASE_METHOD(DocumentTest, "Delete Document with FailOnConflict", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");

    CBLError error;
    CBLDocument* doc1 = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    CHECK(CBLDocument_ID(doc1) == "doc1"_sl);
    CHECK(CBLDocument_Sequence(doc1) == 1);
    
    CBLDocument* doc2 = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    CHECK(CBLDocument_ID(doc2) == "doc1"_sl);
    CHECK(CBLDocument_Sequence(doc2) == 1);
    
    FLMutableDict props1 = CBLDocument_MutableProperties(doc1);
    FLMutableDict_SetString(props1, "foo"_sl, "bar1"_sl);
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc1, kCBLConcurrencyControlFailOnConflict, &error));
    CHECK(CBLDocument_Sequence(doc1) == 2);
    CBLDocument_Release(doc1);
    
    REQUIRE(!CBLCollection_DeleteDocumentWithConcurrencyControl(col, doc2, kCBLConcurrencyControlFailOnConflict, &error));
    CBLDocument_Release(doc2);
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorConflict);
    
    error = {};
    doc1 = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    REQUIRE(doc1);
    CHECK(CBLDocument_Sequence(doc1) == 2);
    CHECK(alloc_slice(CBLDocument_CreateJSON(doc1)) == "{\"foo\":\"bar1\"}"_sl);
    CHECK(Dict(CBLDocument_Properties(doc1)).toJSONString() == "{\"foo\":\"bar1\"}");
    CBLDocument_Release(doc1);
}

TEST_CASE_METHOD(DocumentTest, "Delete Document from Different Collection", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);
    
    ExpectingExceptions x;
    CHECK(!CBLCollection_DeleteDocument(otherCol, doc, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorInvalidParameter);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Delete Document from Same Collection Different Instance", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);
    
    CBLScope* scope = CBLCollection_Scope(col);
    CBLCollection* col2 = CBLDatabase_Collection(db, CBLCollection_Name(col), CBLScope_Name(scope), &error);
    CHECK(col != col2);
    
    CHECK(CBLCollection_DeleteDocument(col2, doc, &error));
    
    CBLScope_Release(scope);
    CBLCollection_Release(col2);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Delete Document from Same Collection Different DB Instance", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);

    auto config = databaseConfig();
    auto anotherDB = CBLDatabase_Open(kDatabaseName, &config, &error);
    REQUIRE(anotherDB);
    
    CBLScope* scope = CBLCollection_Scope(col);
    CBLCollection* col2 = CBLDatabase_Collection(anotherDB, CBLCollection_Name(col), CBLScope_Name(scope), &error);
    CHECK(col != col2);
    
    ExpectingExceptions x;
    CHECK(!CBLCollection_DeleteDocument(col2, doc, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorInvalidParameter);
    
    CBLDocument_Release(doc);
    CBLScope_Release(scope);
    CBLCollection_Release(col2);
    CBLDatabase_Release(anotherDB);
}

TEST_CASE_METHOD(DocumentTest, "Delete Document from Different Default Collection Instance", "[Document]") {
    createDocument(defaultCollection, "doc1", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(defaultCollection, "doc1"_sl, &error);
    REQUIRE(doc);
    
    CBLCollection* defaultCollection2 = CBLDatabase_DefaultCollection(db, &error);
    CHECK(defaultCollection != defaultCollection2);
    
    CHECK(CBLCollection_DeleteDocument(defaultCollection2, doc, &error));
    
    CBLCollection_Release(defaultCollection2);
    CBLDocument_Release(doc);
}

#pragma mark - Purge Document:

TEST_CASE_METHOD(DocumentTest, "Purge Non Existing Document", "[Document]") {
    CBLDocument* doc = CBLDocument_CreateWithID("foo"_sl);
    
    ExpectingExceptions x;
    CBLError error;
    CHECK(!CBLCollection_PurgeDocument(col, doc, &error));
    CBLDocument_Release(doc);
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorNotFound);
    
    error = {};
    CHECK(!CBLCollection_PurgeDocumentByID(col, "foo"_sl, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorNotFound);
}

TEST_CASE_METHOD(DocumentTest, "Purge Document", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");
    createDocument(col, "doc2", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);
    CHECK(CBLDocument_Sequence(doc) == 1);
    
    CHECK(CBLCollection_PurgeDocument(col, doc, &error));
    CBLDocument_Release(doc);
    CHECK(!CBLCollection_GetDocument(col, "doc1"_sl, &error));
    
    CHECK(CBLCollection_PurgeDocumentByID(col, "doc2"_sl, &error));
    CHECK(!CBLCollection_GetDocument(col, "doc2"_sl, &error));
}

TEST_CASE_METHOD(DocumentTest, "Purge Already Purged Document", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);
    
    CHECK(CBLCollection_PurgeDocument(col, doc, &error));
    CHECK(!CBLCollection_GetDocument(col, "doc1"_sl, &error));
    
    CHECK(!CBLCollection_PurgeDocument(col, doc, &error));
    CBLDocument_Release(doc);
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorNotFound);
    
    error = {};
    CHECK(!CBLCollection_PurgeDocumentByID(col, "doc1"_sl, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorNotFound);
}

TEST_CASE_METHOD(DocumentTest, "Purge Document from Different Collection", "[Document]") {
    createDocument(col, "doc1", "foo", "bar");
    
    CBLError error;
    const CBLDocument* doc = CBLCollection_GetDocument(col, "doc1"_sl, &error);
    REQUIRE(doc);
    
    ExpectingExceptions x;
    CHECK(!CBLCollection_PurgeDocument(otherCol, doc, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorInvalidParameter);
    CBLDocument_Release(doc);
}

#pragma mark - Document Expiry:

TEST_CASE_METHOD(DocumentTest, "Document Expiration", "[Document][Expiry]") {
    createDocument(col, "doc1", "foo", "bar");
    createDocument(col, "doc2", "foo", "bar");
    createDocument(col, "doc3", "foo", "bar");

    CBLError error {};
    CBLTimestamp future = CBL_Now() + 1000;
    CHECK(CBLCollection_SetDocumentExpiration(col, "doc1"_sl, future, &error));
    CHECK(CBLCollection_SetDocumentExpiration(col, "doc3"_sl, future, &error));
    CHECK(CBLCollection_Count(col) == 3);

    CHECK(CBLCollection_GetDocumentExpiration(col, "doc1"_sl, &error) == future);
    CHECK(CBLCollection_GetDocumentExpiration(col, "doc2"_sl, &error) == 0);
    CHECK(CBLCollection_GetDocumentExpiration(col, "docX"_sl, &error) == 0);

    this_thread::sleep_for(2000ms);
    CHECK(CBLCollection_Count(col) == 1);
}

TEST_CASE_METHOD(DocumentTest, "Document Expiring After Reopen", "[Document][Expiry]") {
    createDocument(col, "doc1", "foo", "bar");
    createDocument(col, "doc2", "foo", "bar");
    createDocument(col, "doc3", "foo", "bar");

    CBLError error {};
    CBLTimestamp future = CBL_Now() + 2000;
    CHECK(CBLCollection_SetDocumentExpiration(col, "doc1"_sl, future, &error));
    CHECK(CBLCollection_SetDocumentExpiration(col, "doc3"_sl, future, &error));
    CHECK(CBLCollection_Count(col) == 3);

    // Close & reopen the database:
    REQUIRE(CBLDatabase_Close(db, &error));
    CBLDatabase_Release(db);
    
    auto config = databaseConfig();
    db = CBLDatabase_Open(kDatabaseName, &config, &error);

    // Now wait for expiration:
    this_thread::sleep_for(3000ms);
    CBLCollection* col2 = CBLDatabase_Collection(db, kCollectionName, kCBLDefaultScopeName, &error);
    REQUIRE(col2);
    CHECK(CBLCollection_Count(col2) == 1);
    CBLCollection_Release(col2);
}

TEST_CASE_METHOD(DocumentTest, "Get and Set Expiration on Non Existing Doc", "[Document][Expiry]") {
    CBLError error {};
    CHECK(CBLCollection_GetDocumentExpiration(col, "NonExistingDoc"_sl, &error) == 0);
    CHECK(error.code == 0);
    
    ExpectingExceptions ex;
    CBLTimestamp future = CBL_Now() + 2000;
    CHECK(!CBLCollection_SetDocumentExpiration(col, "NonExistingDoc"_sl, future, &error));
    CheckError(error, kCBLErrorNotFound);
}

#pragma mark - Blobs:

TEST_CASE_METHOD(DocumentTest, "Set blob in document", "[Document][Blob]") {
    // Create and Save blob:
    CBLError error;
    FLSlice blobContent = FLStr("I'm Blob.");
    CBLBlob* blob = CBLBlob_CreateWithData("text/plain"_sl, blobContent);
    
    // Set blob in document
    CBLDocument* doc = CBLDocument_CreateWithID("doc1"_sl);
    FLMutableDict docProps = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetBlob(docProps, FLSTR("blob"), blob);
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    CBLDocument_Release(doc);
    CBLBlob_Release(blob);
    
    // Get blob from the saved doc and check:
    doc = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    docProps = CBLDocument_MutableProperties(doc);
    const CBLBlob* blob2 = FLValue_GetBlob(FLDict_Get(docProps, "blob"_sl));
    REQUIRE(blob2);
    FLSliceResult content = CBLBlob_Content(blob2, &error);
    CHECK((slice)content == blobContent);
    FLSliceResult_Release(content);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Set blob in document using indirect properties", "[Document][Blob]") {
    // Create and Save blob:
    CBLError error;
    FLSlice blobContent = FLStr("I'm Blob.");
    CBLBlob* blob = CBLBlob_CreateWithData("text/plain"_sl, blobContent);
    FLMutableDict copiedBlobProps = FLDict_MutableCopy(CBLBlob_Properties(blob), kFLDefaultCopy);
    CHECK(copiedBlobProps);
    
    // Set blob in document using indirect (copied) properties
    CBLDocument* doc = CBLDocument_CreateWithID("doc1"_sl);
    FLMutableDict docProps = CBLDocument_MutableProperties(doc);
    FLSlot_SetDict(FLMutableDict_Set(docProps, FLStr("blob")), copiedBlobProps);
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    CBLDocument_Release(doc);
    
    // Get blob from the saved doc and check:
    doc = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    docProps = CBLDocument_MutableProperties(doc);
    const CBLBlob* blob3 = FLValue_GetBlob(FLDict_Get(docProps, "blob"_sl));
    FLSliceResult content = CBLBlob_Content(blob3, &error);
    CHECK((slice)content == blobContent);
    FLSliceResult_Release(content);
    CBLDocument_Release(doc);
    
    // Release original blob and copied of blob properties:
    CBLBlob_Release(blob);
    FLMutableDict_Release(copiedBlobProps);
}

TEST_CASE_METHOD(DocumentTest, "Save blob and set blob in document", "[Document][Blob]") {
    // Create and Save blob:
    CBLError error;
    FLSlice blobContent = FLStr("I'm Blob.");
    CBLBlob* blob = CBLBlob_CreateWithData("text/plain"_sl, blobContent);
    CHECK(CBLDatabase_SaveBlob(db, blob, &error));
    
    // Set blob in document
    CBLDocument* doc = CBLDocument_CreateWithID("doc1"_sl);
    FLMutableDict docProps = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetBlob(docProps, FLSTR("blob"), blob);
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    CBLDocument_Release(doc);
    CBLBlob_Release(blob);
    
    // Get blob from the saved doc and check:
    doc = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    CHECK(doc);
    docProps = CBLDocument_MutableProperties(doc);
    const CBLBlob* blob2 = FLValue_GetBlob(FLDict_Get(docProps, "blob"_sl));
    FLSliceResult content = CBLBlob_Content(blob2, &error);
    CHECK((slice)content == blobContent);
    FLSliceResult_Release(content);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Save blob and set blob properties in document", "[Document][Blob]") {
    // Create and Save blob:
    CBLError error;
    FLSlice blobContent = FLStr("I'm Blob.");
    CBLBlob* blob = CBLBlob_CreateWithData("text/plain"_sl, blobContent);
    CHECK(CBLDatabase_SaveBlob(db, blob, &error));
    
    // Copy blob properties and release blob:
    FLMutableDict blobProps = FLDict_MutableCopy(CBLBlob_Properties(blob), kFLDefaultCopy);
    CBLBlob_Release(blob);
    
    // Use the blob properties in a document:
    CBLDocument* doc = CBLDocument_CreateWithID("doc1"_sl);
    FLMutableDict docProps = CBLDocument_MutableProperties(doc);
    FLSlot_SetDict(FLMutableDict_Set(docProps, FLStr("blob")), blobProps);
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    CBLDocument_Release(doc);
    FLMutableDict_Release(blobProps);
    
    // Get blob from the saved doc and check:
    doc = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    CHECK(doc);
    docProps = CBLDocument_MutableProperties(doc);
    const CBLBlob* blob2 = FLValue_GetBlob(FLDict_Get(docProps, "blob"_sl));
    FLSliceResult content = CBLBlob_Content(blob2, &error);
    CHECK((slice)content == blobContent);
    FLSliceResult_Release(content);
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(DocumentTest, "Set blob in array", "[Document][Blob]") {
    // Create and Save blob:
    CBLError error;
    FLSlice blobContent1 = FLStr("I'm Blob 1.");
    CBLBlob* blob1 = CBLBlob_CreateWithData("text/plain"_sl, blobContent1);
    
    FLSlice blobContent2 = FLStr("I'm Blob 2.");
    CBLBlob* blob2 = CBLBlob_CreateWithData("text/plain"_sl, blobContent2);
    
    FLMutableArray blobs = FLMutableArray_New();
    
    SECTION("Use Append Blob")
    {
        FLMutableArray_AppendBlob(blobs, blob1);
        FLMutableArray_AppendBlob(blobs, blob2);
    }
    
    SECTION("Use Set Blob")
    {
        FLMutableArray_Resize(blobs, 2);
        FLMutableArray_SetBlob(blobs, 0, blob1);
        FLMutableArray_SetBlob(blobs, 1, blob2);
    }
    
    // Set blobs in document
    CBLDocument* doc = CBLDocument_CreateWithID("doc1"_sl);
    FLMutableDict docProps = CBLDocument_MutableProperties(doc);
    FLMutableDict_SetArray(docProps, "blobs"_sl, blobs);
    CHECK(CBLCollection_SaveDocument(col, doc, &error));
    CBLDocument_Release(doc);
    
    CBLBlob_Release(blob1);
    CBLBlob_Release(blob2);
    FLArray_Release(blobs);
    
    // Get blobs from the saved doc and check:
    doc = CBLCollection_GetMutableDocument(col, "doc1"_sl, &error);
    docProps = CBLDocument_MutableProperties(doc);
    FLArray blobArray = FLValue_AsArray(FLDict_Get(docProps, "blobs"_sl));
    REQUIRE(blobArray);
    
    auto blob1a = FLValue_GetBlob(FLArray_Get(blobArray, 0));
    REQUIRE(blob1a);
    FLSliceResult content = CBLBlob_Content(blob1a, &error);
    CHECK((slice)content == blobContent1);
    FLSliceResult_Release(content);
    
    auto blob2a = FLValue_GetBlob(FLArray_Get(blobArray, 1));
    REQUIRE(blob2a);
    content = CBLBlob_Content(blob2a, &error);
    CHECK((slice)content == blobContent2);
    FLSliceResult_Release(content);
    
    CBLDocument_Release(doc);
}

#pragma mark - Listeners:

TEST_CASE_METHOD(DocumentTest, "Collection Change Notifications", "[Document]") {
    // Add a listener:
    collectionListenerCalls = docListenerCalls = 0;
    auto token = CBLCollection_AddChangeListener(col, collectionListener, col);
    auto docToken = CBLCollection_AddDocumentChangeListener(col, "foo"_sl, docListener, col);

    // Create a doc, check that the listener was called:
    createDocument(col, "foo", "greeting", "Howdy!");
    CHECK(collectionListenerCalls == 1);
    CHECK(docListenerCalls == 1);

    CBLListener_Remove(token);
    CBLListener_Remove(docToken);

    // After being removed, the listener should not be called:
    collectionListenerCalls = docListenerCalls = 0;
    createDocument(col, "bar", "greeting", "yo.");
    CHECK(collectionListenerCalls == 0);
    CHECK(docListenerCalls == 0);
}
