//
// Document_Cpp.cc
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
#include <thread>

#include "cbl++/CouchbaseLite.hh"

using namespace std;
using namespace fleece;
using namespace cbl;

static constexpr const slice kCollectionCppName = "CBLTestCollectionCpp";
static constexpr const slice kOtherCollectionCppName = "CBLTestOtherCollectionCpp";

class DocumentTest_Cpp : public CBLTest_Cpp {
public:
    Collection otherCol;
    
    DocumentTest_Cpp() {
        defaultCollection = db.createCollection(kCollectionCppName);
        REQUIRE(defaultCollection);
        CHECK(defaultCollection.count() == 0);
        
        otherCol = db.createCollection(kOtherCollectionCppName);
        REQUIRE(otherCol);
        CHECK(otherCol.count() == 0);
    }
    
    MutableDocument createDocument(Collection& collection, slice docID, slice property, slice value) {
        MutableDocument doc(docID);
        doc[property] = value;
        collection.saveDocument(doc);
        return doc;
    }
};

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Missing Document", "[Document]") {
    Document doc = defaultCollection.getDocument("foo");
    CHECK(!doc);
    
    MutableDocument mdoc = defaultCollection.getMutableDocument("foo");
    CHECK(!mdoc);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ New Document", "[Document]") {
    MutableDocument doc("foo");
    CHECK(doc);
    CHECK(doc.id() == "foo");
    CHECK(doc.sequence() == 0);
    CHECK(doc.properties().toJSONString() == "{}");
    CHECK(!doc.collection());

    Document immDoc = doc;
    CHECK((doc.properties() == immDoc.properties()));
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ New Document with Auto ID", "[Document]") {
    MutableDocument doc(nullptr);
    CHECK(doc);
    CHECK(!doc.id().empty());
    CHECK(doc.sequence() == 0);
    CHECK(!doc.collection());
    CHECK(doc.properties().toJSONString() == "{}");

    Document immDoc = doc;
    CHECK((doc.properties() == immDoc.properties()));
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Mutable Copy Mutable Document", "[Document]") {
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    
    CHECK(doc.id() == "foo");
    CHECK(doc.sequence() == 0);
    CHECK(!doc.collection());
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");
    
    MutableDocument copiedDoc = doc.mutableCopy();
    CHECK(doc != copiedDoc);
    
    CHECK(copiedDoc.id() == "foo");
    CHECK(copiedDoc.sequence() == 0);
    CHECK(!doc.collection());
    CHECK(copiedDoc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Mutable Copy Immutable Document", "[Document]") {
    MutableDocument newdoc("foo");
    newdoc["greeting"] = "Howdy!";
    defaultCollection.saveDocument(newdoc);
    CHECK(newdoc.collection() == defaultCollection);
    
    Document doc = defaultCollection.getDocument("foo");
    REQUIRE(doc);
    CHECK(doc.sequence() == 1);
    CHECK(doc.collection() == defaultCollection);
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");
    
    MutableDocument copiedDoc = doc.mutableCopy();
    CHECK(doc != copiedDoc);
    
    CHECK(copiedDoc.id() == "foo");
    CHECK(copiedDoc.sequence() == 1);
    CHECK(copiedDoc.collection() == defaultCollection);
    CHECK(copiedDoc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Set Properties", "[Document]") {
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    CHECK(doc.id() == "foo");
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");
    
    MutableDict newProps = MutableDict::newDict();
    newProps["greeting"] = "Hello!";
    doc.setProperties(newProps);
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Hello!\"}");
    
    defaultCollection.saveDocument(doc);
    doc = defaultCollection.getMutableDocument("foo");
    CHECK(doc.id() == "foo");
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Hello!\"}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Get Document with Empty ID", "[Document]") {
    ExpectingExceptions x;
    Document doc = defaultCollection.getDocument("");
    REQUIRE(!doc);
}

#pragma mark - Save Document:

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Save Empty Document", "[Document]") {
    MutableDocument doc("foo");
    defaultCollection.saveDocument(doc);
    CHECK(doc.id()== "foo");
    CHECK(doc.sequence() == 1);
    CHECK(!doc.revisionID().empty());
    CHECK(doc.properties().toJSONString() == "{}");

    MutableDocument doc2 = defaultCollection.getMutableDocument("foo");
    CHECK(doc2.id() == "foo");
    CHECK(doc2.sequence() == 1);
    CHECK(doc2.revisionID() == doc.revisionID());
    CHECK(doc2.properties().toJSONString() == "{}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Save Document With Properties", "[Document]") {
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    CHECK(doc["greeting"].asString() == "Howdy!");
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");

    defaultCollection.saveDocument(doc);
    CHECK(doc.id() == "foo");
    CHECK(doc.sequence() == 1);
    CHECK(!doc.revisionID().empty());
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");

    MutableDocument doc2 = defaultCollection.getMutableDocument("foo");
    CHECK(doc2.id() == "foo");
    CHECK(doc2.sequence() == 1);
    CHECK(doc2.revisionID() == doc.revisionID());
    CHECK(doc2.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Save Document with LastWriteWin", "[Document]") {
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    CHECK(doc["greeting"].asString() == "Howdy!");
    REQUIRE(defaultCollection.saveDocument(doc, kCBLConcurrencyControlLastWriteWins));
    CHECK(doc.sequence() == 1);
    
    MutableDocument doc1 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc1);
    CHECK(doc1.id() == "foo");
    CHECK(doc1.sequence() == 1);
    
    MutableDocument doc2 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc2);
    CHECK(doc2.id() == "foo");
    CHECK(doc2.sequence() == 1);

    doc1["name"] = "bob";
    REQUIRE(defaultCollection.saveDocument(doc1, kCBLConcurrencyControlLastWriteWins));
    CHECK(doc1.sequence() == 2);
    
    doc2["name"] = "sally";
    REQUIRE(defaultCollection.saveDocument(doc2, kCBLConcurrencyControlLastWriteWins));
    CHECK(doc2.sequence() == 3);
    
    MutableDocument doc3 = defaultCollection.getMutableDocument("foo");
    CHECK(doc3.sequence() == 3);
    CHECK(doc3.properties().toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"sally\"}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Save Document with FailOnConflict", "[Document]") {
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    CHECK(doc["greeting"].asString() == "Howdy!");
    REQUIRE(defaultCollection.saveDocument(doc, kCBLConcurrencyControlFailOnConflict));
    CHECK(doc.sequence() == 1);
    
    MutableDocument doc1 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc1);
    CHECK(doc1.id() == "foo");
    CHECK(doc1.sequence() == 1);
    
    MutableDocument doc2 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc2);
    CHECK(doc2.id() == "foo");
    CHECK(doc2.sequence() == 1);

    doc1["name"] = "bob";
    REQUIRE(defaultCollection.saveDocument(doc1, kCBLConcurrencyControlFailOnConflict));
    CHECK(doc1.sequence() == 2);
    
    doc2["name"] = "sally";
    REQUIRE(!defaultCollection.saveDocument(doc2, kCBLConcurrencyControlFailOnConflict));
    CHECK(doc2.sequence() == 1);
    
    doc = defaultCollection.getMutableDocument("foo");
    CHECK(doc.sequence() == 2);
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"bob\"}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Save Document with Conflict Handler", "[Document]") {
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    CHECK(doc["greeting"].asString() == "Howdy!");
    REQUIRE(defaultCollection.saveDocument(doc, kCBLConcurrencyControlFailOnConflict));
    CHECK(doc.sequence() == 1);
    
    CollectionConflictHandler failConflict = [](MutableDocument mine, Document other) -> bool {
        return false;
    };
    
    CollectionConflictHandler mergeConflict = [](MutableDocument mine, Document other) -> bool {
        mine["anotherName"] = other["name"];
        return true;
    };
    
    MutableDocument doc1 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc1);
    CHECK(doc1.id() == "foo");
    CHECK(doc1.sequence() == 1);
    
    MutableDocument doc2 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc2);
    CHECK(doc2.id() == "foo");
    CHECK(doc2.sequence() == 1);

    doc1["name"] = "bob";
    REQUIRE(defaultCollection.saveDocument(doc1, failConflict));
    CHECK(doc1.sequence() == 2);
    
    doc2["name"] = "sally";
    REQUIRE(!defaultCollection.saveDocument(doc2, failConflict));
    CHECK(doc2.sequence() == 1);
    
    doc = defaultCollection.getMutableDocument("foo");
    CHECK(doc.sequence() == 2);
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"bob\"}");
    
    doc2["name"] = "sally";
    REQUIRE(defaultCollection.saveDocument(doc2, mergeConflict));
    CHECK(doc2.sequence() == 3);
    
    doc = defaultCollection.getMutableDocument("foo");
    CHECK(doc.sequence() == 3);
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\",\"name\":\"sally\",\"anotherName\":\"bob\"}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Save Document into Different Collection", "[Document]") {
    createDocument(defaultCollection, "foo", "greeting", "Howdy");
    
    MutableDocument doc = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc);
    
    ExpectingExceptions ex;
    CBLError error {};
    try { otherCol.saveDocument(doc); } catch (const cbl::Error& e) { error = asCBLError(e); }
    CheckError(error, kCBLErrorInvalidParameter);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "Add new key") {
    // Regression test for <https://github.com/couchbaselabs/couchbase-lite-C/issues/18>
    // Add doc to col:
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    defaultCollection.saveDocument(doc);

    // Add a new, shareable key:
    doc.set("new", 10);
    defaultCollection.saveDocument(doc);

    CHECK(doc["new"].asInt() == 10);
    doc["new"] = 999;
    CHECK(doc["new"].asInt() == 999);
    CHECK(doc.properties().count() == 2);

    doc = defaultCollection.getMutableDocument("foo");
    CHECK(doc["new"].asInt() == 10);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "Data disappears") {
    // Regression test for <https://github.com/couchbaselabs/couchbase-lite-C/issues/19>
    MutableDocument doc = MutableDocument("foo");
    doc["var1"]= 1;
    defaultCollection.saveDocument(doc);
    CHECK(doc.properties().toJSONString() == "{\"var1\":1}");

    doc = defaultCollection.getMutableDocument("foo");
    doc["var2"]= 2;
    defaultCollection.saveDocument(doc);
    CHECK(doc.properties().toJSONString() == "{\"var1\":1,\"var2\":2}");

    doc = defaultCollection.getMutableDocument("foo");
    doc["var3"]= 3;
    defaultCollection.saveDocument(doc);
    CHECK(doc.properties().toJSONString() == "{\"var1\":1,\"var2\":2,\"var3\":3}");
}

#pragma mark - Delete Document:

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Delete Non Existing Doc", "[Document]") {
    MutableDocument doc("foo");
    
    ExpectingExceptions x;
    CBLError error {};
    try { defaultCollection.deleteDocument(doc); } catch (const cbl::Error& e) { error = asCBLError(e); }
    CheckError(error, kCBLErrorNotFound);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Delete Doc", "[Document]") {
    createDocument(defaultCollection, "foo", "greeting", "Howdy");
    
    Document doc = defaultCollection.getDocument("foo");
    CHECK(doc);
    
    defaultCollection.deleteDocument(doc);
    
    doc = defaultCollection.getDocument("foo");
    CHECK(!doc);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Delete Doc with LastWriteWin", "[Document]") {
    createDocument(defaultCollection, "foo", "greeting", "Howdy");
    
    MutableDocument doc1 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc1);
    CHECK(doc1.id() == "foo");
    CHECK(doc1.sequence() == 1);
    
    MutableDocument doc2 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc2);
    CHECK(doc2.id() == "foo");
    CHECK(doc2.sequence() == 1);
    
    doc1["name"] = "bob";
    REQUIRE(defaultCollection.saveDocument(doc1, kCBLConcurrencyControlLastWriteWins));
    CHECK(doc1.sequence() == 2);
    
    REQUIRE(defaultCollection.deleteDocument(doc2, kCBLConcurrencyControlLastWriteWins));
    
    Document doc = defaultCollection.getDocument("foo");
    REQUIRE(!doc);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Delete Doc with FailOnConflict", "[Document]") {
    createDocument(defaultCollection, "foo", "greeting", "Howdy");
    
    MutableDocument doc1 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc1);
    CHECK(doc1.id() == "foo");
    CHECK(doc1.sequence() == 1);
    
    MutableDocument doc2 = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc2);
    CHECK(doc2.id() == "foo");
    CHECK(doc2.sequence() == 1);
    
    doc1["name"] = "bob";
    REQUIRE(defaultCollection.saveDocument(doc1, kCBLConcurrencyControlFailOnConflict));
    CHECK(doc1.sequence() == 2);
    
    REQUIRE(!defaultCollection.deleteDocument(doc2, kCBLConcurrencyControlFailOnConflict));
    
    Document readDoc = defaultCollection.getDocument("foo");
    REQUIRE(readDoc);
    CHECK(readDoc.properties().toJSONString() == "{\"greeting\":\"Howdy\",\"name\":\"bob\"}");
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Delete Document into Different Collection", "[Document]") {
    MutableDocument doc = createDocument(defaultCollection, "foo", "greeting", "Howdy");
    
    ExpectingExceptions ex;
    CBLError error {};
    try { otherCol.deleteDocument(doc); } catch (const cbl::Error& e) { error = asCBLError(e); }
    CheckError(error, kCBLErrorInvalidParameter);
}

#pragma mark - Purge Document:

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Purge Non Existing Doc", "[Document]") {
    MutableDocument doc("foo");
    
    ExpectingExceptions x;
    
    CBLError error {};
    try { defaultCollection.purgeDocument(doc); } catch (const cbl::Error& e) { error = asCBLError(e); }
    CheckError(error, kCBLErrorNotFound);
    
    error = {};
    try { defaultCollection.purgeDocument("foo"); } catch (const cbl::Error& e) { error = asCBLError(e); }
    CheckError(error, kCBLErrorNotFound);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Purge Doc", "[Document]") {
    createDocument(defaultCollection, "foo", "greeting", "Howdy");
    
    Document doc = defaultCollection.getDocument("foo");
    REQUIRE(doc);
    
    SECTION("Purge with Doc") {
        defaultCollection.purgeDocument(doc);
    }
    
    SECTION("Purge with ID") {
        defaultCollection.purgeDocument("foo");
    }
    
    doc = defaultCollection.getDocument("foo");
    CHECK(!doc);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Purge Already Purged Document", "[Document]") {
    createDocument(defaultCollection, "foo", "greeting", "Howdy");
    
    Document doc = defaultCollection.getDocument("foo");
    REQUIRE(doc);
    
    defaultCollection.purgeDocument(doc);
    doc = defaultCollection.getDocument("foo");
    CHECK(!doc);
    
    ExpectingExceptions ex;
    CBLError error {};
    try { defaultCollection.purgeDocument("foo"); } catch (const cbl::Error& e) { error = asCBLError(e); }
    CheckError(error, kCBLErrorNotFound);
}

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Purge Doc from Different Collection", "[Document]") {
    createDocument(defaultCollection, "foo", "greeting", "Howdy");
    
    Document doc = defaultCollection.getDocument("foo");
    REQUIRE(doc);
    
    ExpectingExceptions ex;
    CBLError error {};
    try { otherCol.purgeDocument(doc); } catch (const cbl::Error& e) { error = asCBLError(e); }
    CheckError(error, kCBLErrorInvalidParameter);
}

#pragma mark - Document Expiry:

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Document Expiration", "[Document][Expiry]") {
    createDocument(defaultCollection, "doc1", "foo", "bar");
    createDocument(defaultCollection, "doc2", "foo", "bar");
    createDocument(defaultCollection, "doc3", "foo", "bar");

    CBLTimestamp future = CBL_Now() + 1000;
    defaultCollection.setDocumentExpiration("doc1", future);
    defaultCollection.setDocumentExpiration("doc3", future);
    
    CHECK(defaultCollection.count() == 3);
    CHECK(defaultCollection.getDocumentExpiration("doc1") == future);
    CHECK(defaultCollection.getDocumentExpiration("doc3") == future);
    CHECK(defaultCollection.getDocumentExpiration("doc2") == 0);
    CHECK(defaultCollection.getDocumentExpiration("docx") == 0);

    this_thread::sleep_for(2000ms);
    CHECK(defaultCollection.count() == 1);
}

#pragma mark - Blobs:

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Blob with Collection", "[Document][Blob]") {
    Blob blob = Blob("text/plain", "I'm Blob.");
    CHECK(blob.digest() == "sha1-FKiFNQZgW201amCeRJLKJOChjAo=");
    CHECK(blob.contentType() == "text/plain");
    CHECK(blob.length() == 9);
    
    MutableDocument doc("foo");
    doc["picture"] = blob.properties();
    defaultCollection.saveDocument(doc);
    
    doc = defaultCollection.getMutableDocument("foo");
    REQUIRE(doc);
    
    CHECK(doc.properties().toJSON(true,true).asString() == "{picture:{\"@type\":\"blob\","
          "content_type:\"text/plain\",digest:\"sha1-FKiFNQZgW201amCeRJLKJOChjAo=\",length:9}}");
    CHECK(Blob::isBlob(doc["picture"].asDict()));
    CHECK(blob.digest() == "sha1-FKiFNQZgW201amCeRJLKJOChjAo=");
    CHECK(blob.contentType() == "text/plain");
    CHECK(blob.length() == 9);
    CHECK(blob.loadContent() == "I'm Blob.");
}

#pragma mark - Listeners:

TEST_CASE_METHOD(DocumentTest_Cpp, "C++ Change Listeners", "[Document]") {
    int listenerCalls = 0, docListenerCalls = 0;
    
    // Add chage listener:
    auto listener = defaultCollection.addChangeListener([&](CollectionChange* change) {
        ++listenerCalls;
        CHECK(change->collection() == defaultCollection);
        CHECK(change->docIDs().size() == 1);
        CHECK(change->docIDs()[0] == "foo");
    });
    
    // Add doc listener:
    auto docListener = defaultCollection.addDocumentChangeListener("foo", [&](DocumentChange* change) {
        ++docListenerCalls;
        CHECK(change->collection() == defaultCollection);
        CHECK(change->docID() == "foo");
    });
    
    // Create a doc, check that the listener was called:
    createDocument(defaultCollection, "foo", "greeting", "Howdy!");
    CHECK(listenerCalls == 1);
    CHECK(docListenerCalls == 1);
    
    // After being removed, the listener should not be called:
    listener.remove();
    docListener.remove();
    listenerCalls = docListenerCalls = 0;
    createDocument(defaultCollection, "bar", "greeting", "yo.");
    
    this_thread::sleep_for(1000ms);
    CHECK(listenerCalls == 0);
    CHECK(docListenerCalls == 0);
}