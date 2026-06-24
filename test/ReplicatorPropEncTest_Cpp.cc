//
// ReplicatorPropEncTest_Cpp.cc
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

#include "CBLTest_Cpp.hh"
#include "fleece/Fleece.hh"
#include "fleece/Mutable.hh"
#include <string>
#include <chrono>
#include <thread>
#include <unordered_map>

#include "cbl++/CouchbaseLite.hh"

#ifdef COUCHBASE_ENTERPRISE     // Property Encryption is an EE feature.

using namespace std;
using namespace fleece;
using namespace cbl;

class ReplicatorPropertyEncryptionTest_Cpp : public CBLTest_Cpp {
public:
    Database otherDB;
    Collection otherDBDefaultCol;
    Replicator repl;

    struct ReplicatedDoc {
        CBLDocumentFlags flags {};
        CBLError error {};
    };
    unordered_map<string, ReplicatedDoc> replicatedDocs;

    ReplicatorPropertyEncryptionTest_Cpp()
    :otherDB(openDatabaseNamed("otherDB", true))
    ,otherDBDefaultCol(otherDB.getDefaultCollection())
    {}

    ~ReplicatorPropertyEncryptionTest_Cpp() {
        otherDB.close();
        otherDB = nullptr;
    }

    void replicate(ReplicatorConfiguration& config, bool resetCheckpoint = false) {
        repl = Replicator(config);
        doReplicate(resetCheckpoint);
    }

    // Restart the current repl (must already be set via replicate(config)).
    void replicate(bool resetCheckpoint) {
        REQUIRE(repl);
        doReplicate(resetCheckpoint);
    }

private:
    void doReplicate(bool resetCheckpoint) {
        auto docListener = repl.addDocumentReplicationListener(
            [this](Replicator, bool, const vector<CBLReplicatedDocument>& docs) {
                for (auto& doc : docs) {
                    string scopeStr = slice(doc.scope).asString();
                    string collStr  = slice(doc.collection).asString();
                    string docIDStr = slice(doc.ID).asString();
                    string key = (scopeStr == "_default" && collStr == "_default")
                                 ? docIDStr
                                 : scopeStr + "." + collStr + "." + docIDStr;
                    ReplicatedDoc rdoc {};
                    rdoc.flags = doc.flags;
                    rdoc.error = doc.error;
                    replicatedDocs[key] = rdoc;
                }
            });
        repl.start(resetCheckpoint);
        using clock = chrono::high_resolution_clock;
        using seconds = chrono::duration<double, ratio<1,1>>;
        auto t0 = clock::now();
        CBLReplicatorStatus status {};
        while (chrono::duration_cast<seconds>(clock::now() - t0).count() < 30.0) {
            status = repl.status();
            if (status.activity == kCBLReplicatorIdle)
                repl.stop();
            else if (status.activity == kCBLReplicatorStopped)
                break;
            this_thread::sleep_for(100ms);
        }
        CHECK(status.activity == kCBLReplicatorStopped);
    }
};

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ No encryptor : crypto error",
                 "[Replicator][Encryptable]") {
    MutableDocument doc("doc1");
    Encryptable secret("Secret 1");

    secret.setInto(doc.properties(), "secret1"_sl);
    defaultCollection.saveDocument(doc);

    auto config = ReplicatorConfiguration(
        { CollectionConfiguration(defaultCollection) },
        Endpoint::databaseEndpoint(otherDB));
    config.replicatorType = kCBLReplicatorTypePushAndPull;

    {
        ExpectingExceptions x;
        replicate(config);
    }

    CHECK(replicatedDocs.size() == 1);
    CHECK(replicatedDocs["doc1"].error.code == kCBLErrorCrypto);
    CHECK(replicatedDocs["doc1"].error.domain == kCBLDomain);
    CHECK(!otherDBDefaultCol.getDocument("doc1"));
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ No decryptor : ok",
                 "[Replicator][Encryptable]") {
    // --- Phase 1: push with encryptor, no decryptor ---
    {
        MutableDocument doc("doc1");
        Encryptable secret("Secret 1");
        secret.setInto(doc.properties(), "secret1"_sl);
        defaultCollection.saveDocument(doc);

        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        config.documentPropertyEncryptor = [](fleece::slice,
                                              fleece::slice,
                                              fleece::slice,
                                              fleece::Dict,
                                              fleece::slice,
                                              fleece::slice input) -> EncryptionResult {
            alloc_slice ciphertext(input);
            for (size_t i = 0; i < input.size; ++i)
                (uint8_t&)ciphertext[i] = ciphertext[i] ^ 'K';
            return {ciphertext};
        };
        replicate(config);

        Document otherDoc = otherDBDefaultCol.getDocument("doc1");
        REQUIRE(otherDoc);
        CHECK(otherDoc.properties().toJSON(false, true).asString() ==
              "{\"encrypted$secret1\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
    }

    // --- Phase 2: pull with no decryptor — encrypted form arrives as-is ---
    {
        replicatedDocs.clear();
        resetDatabase(true);

        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        replicate(config);

        Document doc = defaultCollection.getDocument("doc1");
        REQUIRE(doc);
        CHECK(doc.properties().toJSON(false, true).asString() ==
              "{\"encrypted$secret1\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ Skip encryption : crypto error",
                 "[Replicator][Encryptable]") {
    MutableDocument doc("doc1");
    Encryptable secret("Secret 1");
    secret.setInto(doc.properties(), "secret1"_sl);
    defaultCollection.saveDocument(doc);

    auto config = ReplicatorConfiguration(
        { CollectionConfiguration(defaultCollection) },
        Endpoint::databaseEndpoint(otherDB));
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    config.documentPropertyEncryptor = [](fleece::slice,
                                          fleece::slice,
                                          fleece::slice,
                                          fleece::Dict,
                                          fleece::slice,
                                          fleece::slice) -> EncryptionResult {
        return {};  // empty ciphertext = skip → crypto error
    };

    {
        ExpectingExceptions x;
        replicate(config);
    }

    CHECK(replicatedDocs.size() == 1);
    CHECK(replicatedDocs["doc1"].error.code == kCBLErrorCrypto);
    CHECK(replicatedDocs["doc1"].error.domain == kCBLDomain);
    CHECK(!otherDBDefaultCol.getDocument("doc1"));
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ Skip decryption : ok",
                 "[Replicator][Encryptable]") {
    // --- Phase 1: push with encryptor ---
    {
        MutableDocument doc("doc1");
        Encryptable secret("Secret 1");
        secret.setInto(doc.properties(), "secret1"_sl);
        defaultCollection.saveDocument(doc);

        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        config.documentPropertyEncryptor = [](fleece::slice, fleece::slice, fleece::slice,
                                              fleece::Dict, fleece::slice,
                                              fleece::slice input) -> EncryptionResult {
            alloc_slice ciphertext(input);
            for (size_t i = 0; i < input.size; ++i)
                (uint8_t&)ciphertext[i] = ciphertext[i] ^ 'K';
            return {ciphertext};
        };
        replicate(config);

        Document otherDoc = otherDBDefaultCol.getDocument("doc1");
        REQUIRE(otherDoc);
        CHECK(otherDoc.properties().toJSON(false, true).asString() ==
              "{\"encrypted$secret1\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
    }

    // --- Phase 2: pull with decryptor that skips — encrypted form arrives as-is ---
    {
        replicatedDocs.clear();
        resetDatabase(true);

        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        config.documentPropertyDecryptor = [](fleece::slice, fleece::slice, fleece::slice,
                                              fleece::Dict, fleece::slice, fleece::slice,
                                              std::optional<std::string_view>, std::optional<std::string_view>) -> DecryptionResult {
            return {};  // empty plaintext = skip → leave encrypted as-is
        };
        replicate(config);

        Document doc = defaultCollection.getDocument("doc1");
        REQUIRE(doc);
        CHECK(doc.properties().toJSON(false, true).asString() ==
              "{\"encrypted$secret1\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ Encryption error",
                 "[Replicator][Encryptable]") {
    MutableDocument doc("doc1");
    Encryptable secret("Secret 1");
    secret.setInto(doc.properties(), "secret1"_sl);
    defaultCollection.saveDocument(doc);

    CBLError encryptionError {};
    CBLError expectedDocError {};
    bool willRetryToSyncAgain = false;

    SECTION("503 Error") {
        encryptionError = {kCBLWebSocketDomain, 503};
        expectedDocError = encryptionError;
        willRetryToSyncAgain = true;
    }
    SECTION("Crypto Error") {
        encryptionError = {kCBLDomain, kCBLErrorCrypto};
        expectedDocError = encryptionError;
    }
    SECTION("Other Error") {
        encryptionError = {kCBLDomain, kCBLErrorUnexpectedError};
        expectedDocError = encryptionError;
    }

    // --- First replication: encryptor returns an error ---
    {
        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePush;
        config.documentPropertyEncryptor = [encryptionError](fleece::slice, fleece::slice,
                                                              fleece::slice, fleece::Dict,
                                                              fleece::slice,
                                                              fleece::slice) -> EncryptionResult {
            EncryptionResult r;
            r.error = encryptionError;
            return r;
        };
        ExpectingExceptions x;
        replicate(config);
    }

    CHECK(replicatedDocs.size() == 1);
    CHECK(replicatedDocs["doc1"].error.domain == expectedDocError.domain);
    CHECK(replicatedDocs["doc1"].error.code == expectedDocError.code);
    CHECK(!otherDBDefaultCol.getDocument("doc1"));

    // --- Second replication: no error ---
    replicatedDocs.clear();
    {
        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePush;
        config.documentPropertyEncryptor = [](fleece::slice, fleece::slice, fleece::slice,
                                              fleece::Dict, fleece::slice,
                                              fleece::slice input) -> EncryptionResult {
            alloc_slice ciphertext(input);
            for (size_t i = 0; i < input.size; ++i)
                (uint8_t&)ciphertext[i] = ciphertext[i] ^ 'K';
            return {ciphertext};
        };
        replicate(config);
    }

    if (willRetryToSyncAgain) {
        CHECK(replicatedDocs.size() == 1);
        CHECK(replicatedDocs["doc1"].error.domain == 0);
        CHECK(replicatedDocs["doc1"].error.code == 0);
        CHECK(otherDBDefaultCol.getDocument("doc1"));
    } else {
        CHECK(replicatedDocs.size() == 0);
        CHECK(!otherDBDefaultCol.getDocument("doc1"));
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ Decryption error",
                 "[Replicator][Encryptable]") {
    // --- Phase 1: push with encryptor so otherDB has the encrypted doc ---
    {
        MutableDocument doc("doc1");
        Encryptable secret("Secret 1");
        secret.setInto(doc.properties(), "secret1"_sl);
        defaultCollection.saveDocument(doc);

        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        config.documentPropertyEncryptor = [](fleece::slice, fleece::slice, fleece::slice,
                                              fleece::Dict, fleece::slice,
                                              fleece::slice input) -> EncryptionResult {
            alloc_slice ciphertext(input);
            for (size_t i = 0; i < input.size; ++i)
                (uint8_t&)ciphertext[i] = ciphertext[i] ^ 'K';
            return {ciphertext};
        };
        config.documentPropertyDecryptor = [](fleece::slice, fleece::slice, fleece::slice,
                                              fleece::Dict, fleece::slice, fleece::slice input,
                                              std::optional<std::string_view>, std::optional<std::string_view>) -> DecryptionResult {
            alloc_slice plaintext(input);
            for (size_t i = 0; i < input.size; ++i)
                (uint8_t&)plaintext[i] = plaintext[i] ^ 'K';
            return {plaintext};
        };
        replicate(config);

        CHECK(otherDBDefaultCol.getDocument("doc1"));
    }

    // --- Phase 2: reset local, pull with decryptor that errors ---
    {
        replicatedDocs.clear();
        resetDatabase(true);

        CBLError decryptionError {};
        CBLError expectedDocError {};
        bool willRetryToSyncAgain = false;

        SECTION("503 Error") {
            decryptionError = {kCBLWebSocketDomain, 503};
            expectedDocError = decryptionError;
            willRetryToSyncAgain = true;
        }
        SECTION("Crypto Error") {
            decryptionError = {kCBLDomain, kCBLErrorCrypto};
            expectedDocError = decryptionError;
        }
        SECTION("Other Error") {
            decryptionError = {kCBLDomain, kCBLErrorUnexpectedError};
            expectedDocError = decryptionError;
        }

        CHECK(replicatedDocs.size() == 0);
        {
            auto config = ReplicatorConfiguration(
                { CollectionConfiguration(defaultCollection) },
                Endpoint::databaseEndpoint(otherDB));
            config.replicatorType = kCBLReplicatorTypePushAndPull;
            config.documentPropertyDecryptor = [decryptionError](fleece::slice, fleece::slice,
                                                                  fleece::slice, fleece::Dict,
                                                                  fleece::slice, fleece::slice,
                                                                  std::optional<std::string_view>,
                                                                  std::optional<std::string_view>) -> DecryptionResult {
                DecryptionResult r;
                r.error = decryptionError;
                return r;
            };
            ExpectingExceptions x;
            replicate(config);
        }

        CHECK(replicatedDocs.size() == 1);
        CHECK(replicatedDocs["doc1"].error.domain == expectedDocError.domain);
        CHECK(replicatedDocs["doc1"].error.code == expectedDocError.code);
        CHECK(!defaultCollection.getDocument("doc1"));

        // --- Third replication: no error ---
        replicatedDocs.clear();
        {
            auto config = ReplicatorConfiguration(
                { CollectionConfiguration(defaultCollection) },
                Endpoint::databaseEndpoint(otherDB));
            config.replicatorType = kCBLReplicatorTypePushAndPull;
            config.documentPropertyDecryptor = [](fleece::slice, fleece::slice, fleece::slice,
                                                  fleece::Dict, fleece::slice, fleece::slice input,
                                                  std::optional<std::string_view>, std::optional<std::string_view>) -> DecryptionResult {
                alloc_slice plaintext(input);
                for (size_t i = 0; i < input.size; ++i)
                    (uint8_t&)plaintext[i] = plaintext[i] ^ 'K';
                return {plaintext};
            };
            replicate(config);
        }

        if (willRetryToSyncAgain) {
            CHECK(replicatedDocs.size() == 1);
            CHECK(replicatedDocs["doc1"].error.domain == 0);
            CHECK(replicatedDocs["doc1"].error.code == 0);
            CHECK(defaultCollection.getDocument("doc1"));
        } else {
            CHECK(replicatedDocs.size() == 0);
            CHECK(!defaultCollection.getDocument("doc1"));
        }
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ Encrypt already encrypted values",
                 "[Replicator][Encryptable]") {
    // Build a doc with a manually-constructed already-encrypted dict so the encryptor should skip it.
    MutableDocument doc("doc1");
    fleece::MutableDict secret = fleece::MutableDict::newDict();
    secret["alg"_sl]        = "CB_MOBILE_CUSTOM"_sl;
    secret["ciphertext"_sl] = "aRguKDkuP2t6aQ=="_sl;
    doc.properties()["encrypted$secret"_sl] = secret;
    defaultCollection.saveDocument(doc);

    int encryptCount = 0;
    auto config = ReplicatorConfiguration(
        { CollectionConfiguration(defaultCollection) },
        Endpoint::databaseEndpoint(otherDB));
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    config.documentPropertyEncryptor = [&encryptCount](fleece::slice, fleece::slice, fleece::slice,
                                                        fleece::Dict, fleece::slice,
                                                        fleece::slice input) -> EncryptionResult {
        ++encryptCount;
        alloc_slice ciphertext(input);
        for (size_t i = 0; i < input.size; ++i)
            (uint8_t&)ciphertext[i] = ciphertext[i] ^ 'K';
        return {ciphertext};
    };
    replicate(config);

    CHECK(encryptCount == 0);
    auto otherDoc = otherDBDefaultCol.getDocument("doc1");
    REQUIRE(otherDoc);
    CHECK(otherDoc.properties()["encrypted$secret"_sl].asDict().toJSON(false, true) ==
          "{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}");
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ Key ID and Algorithm",
                 "[Replicator][Encryptable]") {
    // Phase 1: encrypt with custom algorithm and key ID, verify in otherDB
    {
        MutableDocument doc("doc1");
        Encryptable secret("Secret 1");
        secret.setInto(doc.properties(), "secret1"_sl);
        defaultCollection.saveDocument(doc);

        int encryptCount = 0;
        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        config.documentPropertyEncryptor = [&encryptCount](fleece::slice, fleece::slice, fleece::slice,
                                                            fleece::Dict, fleece::slice,
                                                            fleece::slice input) -> EncryptionResult {
            ++encryptCount;
            alloc_slice ciphertext(input);
            for (size_t i = 0; i < input.size; ++i)
                (uint8_t&)ciphertext[i] = ciphertext[i] ^ 'K';
            return {ciphertext, "XOR_ALG", "MY_KEY_ID"};
        };
        replicate(config);

        CHECK(encryptCount == 1);
        auto otherDoc = otherDBDefaultCol.getDocument("doc1");
        REQUIRE(otherDoc);
        CHECK(otherDoc.properties().toJSON(false, true) ==
              "{\"encrypted$secret1\":{\"alg\":\"XOR_ALG\",\"ciphertext\":\"aRguKDkuP2t6aQ==\",\"kid\":\"MY_KEY_ID\"}}");
    }

    // Phase 2: reset local, pull with decryptor that checks algorithm and key ID
    {
        replicatedDocs.clear();
        resetDatabase(true);

        std::atomic<int> decryptCount{0};
        std::optional<std::string> capturedAlgorithm, capturedKeyID;
        auto config = ReplicatorConfiguration(
            { CollectionConfiguration(defaultCollection) },
            Endpoint::databaseEndpoint(otherDB));
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        config.documentPropertyDecryptor = [&decryptCount, &capturedAlgorithm, &capturedKeyID](
                                               fleece::slice, fleece::slice, fleece::slice,
                                               fleece::Dict, fleece::slice, fleece::slice input,
                                               std::optional<std::string_view> algorithm,
                                               std::optional<std::string_view> keyID) -> DecryptionResult {
            ++decryptCount;
            if (algorithm) capturedAlgorithm = std::string(*algorithm);
            if (keyID)     capturedKeyID     = std::string(*keyID);
            alloc_slice plaintext(input);
            for (size_t i = 0; i < input.size; ++i)
                (uint8_t&)plaintext[i] = plaintext[i] ^ 'K';
            return {plaintext};
        };
        replicate(config);

        CHECK(decryptCount.load() == 1);
        CHECK(capturedAlgorithm == "XOR_ALG");
        CHECK(capturedKeyID == "MY_KEY_ID");
        auto localDoc = defaultCollection.getDocument("doc1");
        REQUIRE(localDoc);
        CHECK(localDoc.properties().toJSON(false, true) ==
              "{\"secret1\":{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}}");
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest_Cpp, "C++ Encrypt and decrypt with multipe collections",
                 "[Replicator][Encryptable]") {
    auto c1x = db.createCollection("colA", "scopeA");
    auto c2x = db.createCollection("colB", "scopeA");
    auto c1y = otherDB.createCollection("colA", "scopeA");
    auto c2y = otherDB.createCollection("colB", "scopeA");

    auto createEncryptedDoc = [](Collection& col, const char* docID) {
        MutableDocument doc(docID);
        Encryptable secret("Secret 1");
        secret.setInto(doc.properties(), "secret"_sl);
        col.saveDocument(doc);
    };
    createEncryptedDoc(c1x, "doc1");
    createEncryptedDoc(c2x, "doc2");

    PropertyEncryptor encryptor = [](fleece::slice scope, fleece::slice collection,
                                     fleece::slice docID, fleece::Dict, fleece::slice,
                                     fleece::slice input) -> EncryptionResult {
        CHECK(scope == "scopeA"_sl);
        if (collection == "colA"_sl)
            CHECK(docID == "doc1"_sl);
        else if (collection == "colB"_sl)
            CHECK(docID == "doc2"_sl);
        else
            FAIL("Unexpected collection in encryptor");
        alloc_slice ciphertext(input);
        for (size_t i = 0; i < input.size; ++i)
            (uint8_t&)ciphertext[i] = ciphertext[i] ^ 'K';
        return {ciphertext};
    };

    PropertyDecryptor decryptor = [](fleece::slice scope, fleece::slice collection,
                                     fleece::slice docID, fleece::Dict, fleece::slice,
                                     fleece::slice input, std::optional<std::string_view>, std::optional<std::string_view>) -> DecryptionResult {
        CHECK(scope == "scopeA"_sl);
        if (collection == "colA"_sl)
            CHECK(docID == "doc1"_sl);
        else if (collection == "colB"_sl)
            CHECK(docID == "doc2"_sl);
        else
            FAIL("Unexpected collection in decryptor");
        alloc_slice plaintext(input);
        for (size_t i = 0; i < input.size; ++i)
            (uint8_t&)plaintext[i] = plaintext[i] ^ 'K';
        return {plaintext};
    };

    auto config = ReplicatorConfiguration(
        { CollectionConfiguration(c1x), CollectionConfiguration(c2x) },
        Endpoint::databaseEndpoint(otherDB));
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    config.documentPropertyEncryptor = encryptor;
    config.documentPropertyDecryptor = decryptor;

    replicate(config);

    auto doc1y = c1y.getDocument("doc1");
    REQUIRE(doc1y);
    CHECK(doc1y.properties().toJSON(false, true) ==
          "{\"encrypted$secret\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");

    auto doc2y = c2y.getDocument("doc2");
    REQUIRE(doc2y);
    CHECK(doc2y.properties().toJSON(false, true) ==
          "{\"encrypted$secret\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");

    // Purge local docs and pull again with reset checkpoint:
    c1x.purgeDocument("doc1");
    c2x.purgeDocument("doc2");
    replicate(true);

    auto doc1x = c1x.getDocument("doc1");
    REQUIRE(doc1x);
    CHECK(doc1x.properties().toJSON(false, true) ==
          "{\"secret\":{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}}");

    auto doc2x = c2x.getDocument("doc2");
    REQUIRE(doc2x);
    CHECK(doc2x.properties().toJSON(false, true) ==
          "{\"secret\":{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}}");
}

#endif  // COUCHBASE_ENTERPRISE
