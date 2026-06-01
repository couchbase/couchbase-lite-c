//
// DatabaseTest_Cpp.cc
//
// Copyright © 2019 Couchbase. All rights reserved.
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
#include <cstring>
#include <string>
#include <thread>

#include "cbl++/CouchbaseLite.hh"

using namespace std;
using namespace fleece;
using namespace cbl;


TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database") {
    CHECK(db.name() == string(kDatabaseName));
    CHECK(db.path() == string(CBLTest::databaseDir()) + kPathSeparator + string(kDatabaseName) + ".cblite2" + kPathSeparator);
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database Exist") {
    CHECK(!Database::exists(kDatabaseName, nullptr));
    CHECK(Database::exists(kDatabaseName, CBLTest::databaseDir()));
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Copy Database") {
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    defaultCollection.saveDocument(doc);

    auto dbDir = CBLTest::databaseDir();
    auto config = CBLTest::databaseConfig();
    
    const slice copiedDBName = "CBLtest_Copied";
    Database::deleteDatabase(copiedDBName, dbDir);
    REQUIRE(!Database::exists(copiedDBName, dbDir));
    
    string dbpath = db.path();
    db.close(); // Close the source db
    Database::copyDatabase(dbpath, copiedDBName, config);
    
    CHECK(Database::exists(copiedDBName, dbDir));
    auto copiedDB = cbl::Database(copiedDBName, config);
    Collection copiedCol = copiedDB.getDefaultCollection();
    CHECK(copiedCol);
    CHECK(copiedCol.count() == 1);
    
    doc = copiedCol.getMutableDocument("foo");
    CHECK(doc["greeting"].asString() == "Howdy!");
    
    copiedDB.deleteDatabase();
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Save Document With Property") {
    MutableDocument doc("foo");
    doc["greeting"] = "Howdy!";
    CHECK(doc["greeting"].asString() == "Howdy!");
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");

    defaultCollection.saveDocument(doc);
    CHECK(string(doc.id()) == "foo");
    CHECK(doc.sequence() == 1);
    CHECK(!doc.revisionID().empty());
    CHECK(doc.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CHECK(doc["greeting"].asString() == "Howdy!");

    MutableDocument doc2 = defaultCollection.getMutableDocument("foo");
    CHECK(string(doc2.id()) == "foo");
    CHECK(doc2.sequence() == 1);
    CHECK(doc2.revisionID() == doc.revisionID());
    CHECK(doc2.properties().toJSONString() == "{\"greeting\":\"Howdy!\"}");
    CHECK(doc2["greeting"].asString() == "Howdy!");
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Delete Unsaved Doc") {
    MutableDocument doc("foo");
    ExpectingExceptions x;
    CBLError error;
    REQUIRE(!CBLCollection_DeleteDocumentWithConcurrencyControl(defaultCollection.ref(), doc.ref(), kCBLConcurrencyControlLastWriteWins, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorNotFound);
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Transaction") {
    {
        Transaction t(db);

        MutableDocument doc("foo");
        doc["greeting"] = "Howdy!";
        defaultCollection.saveDocument(doc);
        doc["meeting"] = 23;
        defaultCollection.saveDocument(doc);

        t.commit();
    }

    Document checkDoc = defaultCollection.getDocument("foo");
    REQUIRE(checkDoc);
    CHECK(checkDoc.properties().get("greeting").asString() == "Howdy!");
    CHECK(checkDoc.properties().get("meeting").asInt() == 23);
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Transaction, Abort") {
    {
        Transaction t(db);

        MutableDocument doc("foo");
        doc["greeting"] = "Howdy!";
        defaultCollection.saveDocument(doc);
        doc["meeting"] = 23;
        defaultCollection.saveDocument(doc);
        
        SECTION("No commit abort") { }
        
        SECTION("Explicit abort") {
            t.abort();
        }
    }

    Document checkDoc = defaultCollection.getDocument("foo");
    REQUIRE(!checkDoc);
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Transaction With Exception", "[!throws]") {
    {
        MutableDocument doc("foo");
        doc["greeting"] = "Howdy!";
        defaultCollection.saveDocument(doc);
    }

    bool threw = false;
    try {
        Transaction t(db);

        MutableDocument doc("foo");
        doc["meeting"] = 23;
        defaultCollection.saveDocument(doc);

        if (sqrt(2) > 1.0) {
            ExpectingExceptions x;
            CBL_Log(kCBLLogDomainDatabase, kCBLLogWarning, "INTENTIONALLY THROWING EXCEPTION!");
            throw runtime_error("intentional");
        }

        t.commit();

    } catch (runtime_error &x) {
        threw = true;
        CHECK(string(x.what()) == "intentional");
    }
    CHECK(threw);

    Document doc = defaultCollection.getDocument("foo");
    REQUIRE(doc);
    CHECK(doc["greeting"].asString() == "Howdy!");
    CHECK(doc["meeting"] == nullptr);
}

TEST_CASE_METHOD(CBLTest_Cpp, "Retaining immutable Fleece") {
    MutableDocument mdoc("ubiq");
    {
        auto fldoc = fleece::Doc::fromJSON(R"({"msg":{"FOO":18,"BAR":"Wahooma"}})");
        REQUIRE(fldoc);
        Dict message = fldoc["msg"].asDict();
        REQUIRE(message);
        mdoc.setProperties(message);
        // Now the variable `fldoc` goes out of scope, but its data needs to remain valid,
        // since `doc` points into it. The Doc object is retained by the MutableDict in `mdoc`,
        // keeping it alive.
    }
    CHECK(mdoc["FOO"].asInt() == 18);
    CHECK(mdoc["BAR"].asString() == "Wahooma");
    defaultCollection.saveDocument(mdoc);
    CHECK(mdoc.propertiesAsJSON() == R"({"BAR":"Wahooma","FOO":18})");
    auto savedDoc = defaultCollection.getDocument("ubiq");
    CHECK(savedDoc.propertiesAsJSON() == mdoc.propertiesAsJSON());
}

TEST_CASE_METHOD(CBLTest_Cpp, "Empty Listener Token") {
    ListenerToken<> listenerToken;
    CHECK(!listenerToken.context());
    CHECK(!listenerToken.token());
    listenerToken.remove(); // Noops
}


#pragma mark - DatabaseConfiguration:

TEST_CASE_METHOD(CBLTest_Cpp, "C++ DatabaseConfiguration default") {
    auto cfg = DatabaseConfiguration::defaultConfiguration();
    CHECK(!cfg.fullSync);
#ifdef COUCHBASE_ENTERPRISE
    CHECK(cfg.encryptionKey.algorithm == kCBLEncryptionNone);
#endif
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ DatabaseConfiguration round-trip with CBL config") {
    string dir = string(slice(CBLTest::databaseDir()));
    DatabaseConfiguration cfg = DatabaseConfiguration::defaultConfiguration();
    cfg.directory = dir;
    cfg.fullSync = true;

    CBLDatabaseConfiguration ccfg{
        slice(cfg.directory),
#ifdef COUCHBASE_ENTERPRISE
        cfg.encryptionKey,
#endif
        cfg.fullSync
    };

    DatabaseConfiguration cfg2 = ccfg;
    CHECK(cfg2.directory == cfg.directory);
    CHECK(cfg2.fullSync == cfg.fullSync);
#ifdef COUCHBASE_ENTERPRISE
    CHECK([](EncryptionKey& key1, EncryptionKey& key2) -> bool {
        return key1.algorithm == key2.algorithm &&
        std::memcmp(key1.bytes, key2.bytes, sizeof(key1.bytes)) == 0;
    }(cfg2.encryptionKey, cfg.encryptionKey));
#endif
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database returns its config") {
    DatabaseConfiguration cfg = db.config();

    // cfg.directory contains the trailing directory separator, '/' or '\\' 
    CHECK(cfg.directory.substr(0, cfg.directory.length() - 1) == CBLTest::databaseDir().asString());
    CHECK(!cfg.fullSync);
#ifdef COUCHBASE_ENTERPRISE
    CHECK(cfg.encryptionKey.algorithm == kCBLEncryptionNone);
#endif
}


#pragma mark - Save / Get Blob:

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database saveBlob and getBlob round-trip", "[Blob]") {
    slice content = "I'm Blob.";
    Blob blob("text/plain", content);
    REQUIRE(succeeds([&]{ db.saveBlob(blob); }));

    Blob fetched = db.getBlob(blob.properties());
    REQUIRE(fetched);
    CHECK(fetched.length() == content.size);
    CHECK(fetched.contentType() == "text/plain");
    CHECK(fetched.digest() == blob.digest());
    CHECK(fetched.loadContent() == content);
    CHECK(fetched.blobEquals(blob));
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database saveBlob then read via document", "[Blob]") {
    slice content = "I'm Blob.";
    Blob blob("text/plain", content);
    REQUIRE(succeeds([&]{ db.saveBlob(blob); }));

    MutableDocument doc("doc1");
    doc["blob"] = blob.properties();
    defaultCollection.saveDocument(doc);

    Document doc2 = defaultCollection.getDocument("doc1");
    REQUIRE(doc2);
    Blob fromDoc(doc2["blob"].asDict());
    REQUIRE(fromDoc);
    CHECK(fromDoc.loadContent() == content);
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database saveBlob of already-stored blob throws",
                 "[Blob][!throws]") {
    slice content = "I'm Blob.";
    Blob blob("text/plain", content);
    REQUIRE(succeeds([&]{ db.saveBlob(blob); }));

    MutableDocument doc("doc1");
    doc["blob"] = blob.properties();
    defaultCollection.saveDocument(doc);

    Document doc2 = defaultCollection.getDocument("doc1");
    Blob fromDoc(doc2["blob"].asDict());
    REQUIRE(fromDoc);

    ExpectingExceptions x;
    CBLError thrown {};
    bool threw = false;
    try {
        db.saveBlob(fromDoc);
    } catch (const cbl::Error& e) {
        threw = true;
        thrown = asCBLError(e);
    }
    CHECK(threw);
    CHECK(thrown.domain == kCBLDomain);
    CHECK(thrown.code == kCBLErrorUnsupported);
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database getBlob non-existing returns falsy", "[Blob]") {
    auto props = MutableDict::newDict();
    props[kCBLTypeProperty] = kCBLBlobType;
    props[kCBLBlobDigestProperty] = "sha1-VVVVVVVVVVVVVVVVVVVVVVVVVVU=";

    // Legitimate "not found": no throw, falsy Blob.
    const Blob got = db.getBlob(props);
    CHECK(!got);
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database getBlob with invalid props throws",
                 "[Blob][!throws]") {
    auto props = MutableDict::newDict();
    props[kCBLTypeProperty] = kCBLBlobType;   // missing digest -> kCBLErrorInvalidParameter

    ExpectingExceptions x;
    CBLError thrown {};
    bool threw = false;
    try {
        db.getBlob(props);
    } catch (const cbl::Error& e) {
        threw = true;
        thrown = asCBLError(e);
    }
    CHECK(threw);
    CHECK(thrown.domain == kCBLDomain);
    CHECK(thrown.code == kCBLErrorInvalidParameter);
}


#ifdef COUCHBASE_ENTERPRISE

#pragma mark - Encryption:

TEST_CASE_METHOD(CBLTest_Cpp, "C++ EncryptionKey from password") {
    EncryptionKey defaultKey;
    CHECK(defaultKey.algorithm == kCBLEncryptionNone);

    EncryptionKey k("sekrit"_sl);
    CHECK(k.algorithm == kCBLEncryptionAES256);

    EncryptionKey kOld("sekrit"_sl, true);
    CHECK(kOld.algorithm == kCBLEncryptionAES256);

    // The two derivation algorithms should yield different bytes for the same password:
    CHECK(memcmp(k.bytes, kOld.bytes, sizeof(k.bytes)) != 0);

    // Same password + algorithm yields the same key bytes:
    EncryptionKey k2("sekrit"_sl);
    CHECK(memcmp(k.bytes, k2.bytes, sizeof(k.bytes)) == 0);
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Open encrypted database via DatabaseConfiguration") {
    slice encDbName = "encdb_cpp";
    auto dbDir = CBLTest::databaseDir();
    cbl::Database::deleteDatabase(encDbName, dbDir);
    REQUIRE(!cbl::Database::exists(encDbName, dbDir));

    string dir = string(dbDir);
    DatabaseConfiguration cfg = DatabaseConfiguration::defaultConfiguration();
    cfg.directory = dir;
    cfg.encryptionKey = EncryptionKey("sekrit"_sl);

    {
        cbl::Database encdb(encDbName, cfg);
        REQUIRE(encdb);
        DatabaseConfiguration roundTrip = encdb.config();
        CHECK(roundTrip.encryptionKey.algorithm == kCBLEncryptionAES256);
        CHECK(memcmp(roundTrip.encryptionKey.bytes, cfg.encryptionKey.bytes,
                     sizeof(cfg.encryptionKey.bytes)) == 0);

        // A document so we know the database is real:
        MutableDocument mdoc("doc1");
        mdoc["greeting"] = "Howdy!";
        encdb.getDefaultCollection().saveDocument(mdoc);
        encdb.close();
    }

    // Reopening with the wrong key fails:
    {
        DatabaseConfiguration wrong = DatabaseConfiguration::defaultConfiguration();
        wrong.directory = dir;
        wrong.encryptionKey = EncryptionKey("wrongpassword"_sl);
        ExpectingExceptions x;
        CBLError thrown {};
        bool threw = false;
        try {
            cbl::Database bad(encDbName, wrong);
        } catch (const cbl::Error& e) {
            threw = true;
            thrown = asCBLError(e);
        }
        CHECK(threw);
        CHECK(thrown.domain == kCBLDomain);
        CHECK(thrown.code == kCBLErrorNotADatabaseFile);
    }

    // Reopening with the correct key succeeds:
    {
        cbl::Database encdb(encDbName, cfg);
        REQUIRE(encdb);
        Document doc = encdb.getDefaultCollection().getDocument("doc1");
        REQUIRE(doc);
        CHECK(doc["greeting"].asString() == "Howdy!");
        encdb.deleteDatabase();
    }
}

TEST_CASE_METHOD(CBLTest_Cpp, "C++ Database changeEncryptionKey") {
    slice encDbName = "encdb_cpp_change";
    auto dbDir = CBLTest::databaseDir();
    cbl::Database::deleteDatabase(encDbName, dbDir);

    string dir = string(dbDir);
    DatabaseConfiguration cfg = DatabaseConfiguration::defaultConfiguration();
    cfg.directory = dir;

    // 1) Create an unencrypted DB and add a doc:
    {
        cbl::Database d(encDbName, cfg);
        MutableDocument mdoc("doc1");
        mdoc["greeting"] = "Howdy!";
        d.getDefaultCollection().saveDocument(mdoc);

        // Encrypt the DB:
        EncryptionKey newKey("sekrit"_sl);
        CHECK(succeeds([&]{ d.changeEncryptionKey(&newKey); }));
        d.close();
    }

    // 2) Verify it now requires the key:
    {
        ExpectingExceptions x;
        bool threw = false;
        try {
            cbl::Database unencrypted(encDbName, cfg);
        } catch (const cbl::Error&) {
            threw = true;
        }
        CHECK(threw);
    }

    // 3) Open with the key, then remove the key:
    {
        DatabaseConfiguration encCfg = cfg;
        encCfg.encryptionKey = EncryptionKey("sekrit"_sl);
        cbl::Database d(encDbName, encCfg);

        Document doc = d.getDefaultCollection().getDocument("doc1");
        REQUIRE(doc);
        CHECK(doc["greeting"].asString() == "Howdy!");

        // Remove encryption by passing nullptr:
        CHECK(succeeds([&]{ d.changeEncryptionKey(nullptr); }));
        d.close();
    }

    // 4) Should now open without a key:
    {
        cbl::Database d(encDbName, cfg);
        Document doc = d.getDefaultCollection().getDocument("doc1");
        REQUIRE(doc);
        CHECK(doc["greeting"].asString() == "Howdy!");
        d.deleteDatabase();
    }
}

#endif // COUCHBASE_ENTERPRISE
