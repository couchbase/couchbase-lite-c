//
// ReplicatorPropEncTest.cc
//
// Copyright © 2021 Couchbase. All rights reserved.
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

#ifdef COUCHBASE_ENTERPRISE     // Property Encryption is an EE feature.

class ReplicatorPropertyEncryptionTest : public ReplicatorTest {
public:
    Database otherDB;
    Collection otherDBDefaultCol = otherDB.getDefaultCollection();
    
    int encryptCount = 0;
    int decryptCount = 0;
    
    slice keyID = nullptr;
    slice algorithm = nullptr;
    
    bool skipEncryption = false;
    bool skipDecryption = false;
    
    CBLError encryptionError = { };
    CBLError decryptionError = { };
    
    ReplicatorPropertyEncryptionTest()
    :otherDB(openDatabaseNamed("otherDB", true)) // empty
    ,ReplicatorTest()
    {
        config.endpoint = CBLEndpoint_CreateWithLocalDB(otherDB.ref());
    }
    
    ~ReplicatorPropertyEncryptionTest() {
        otherDB.close();
        otherDB = nullptr;
    }
    
    /** Delete & recreate database and reset replicator instance.  */
    void reset() {
        resetDatabase(true);
        
        resetReplicator();
        
        resetDefaultReplicatorConfig();
    }
    
    void setupEncryptionCallback(bool encryptor = true, bool decryptor = true) {
        if (encryptor) {
            config.documentPropertyEncryptor = [](void* context,
                                                  FLString scope,
                                                  FLString collection,
                                                  FLString docID,
                                                  FLDict props,
                                                  FLString path,
                                                  FLSlice input,
                                                  FLStringResult* alg,
                                                  FLStringResult* kid,
                                                  CBLError* error) -> FLSliceResult {
                return ((ReplicatorPropertyEncryptionTest*)context) -> encrypt(context, docID, props, path, input, alg, kid, error);
            };
        }
        
        if (decryptor) {
            config.documentPropertyDecryptor = [](void* context,
                                                  FLString scope,
                                                  FLString collection,
                                                  FLString docID,
                                                  FLDict props,
                                                  FLString path,
                                                  FLSlice input,
                                                  FLString alg,
                                                  FLString kid,
                                                  CBLError* error) -> FLSliceResult {
                return ((ReplicatorPropertyEncryptionTest*)context) -> decrypt(context, docID, props, path, input, alg, kid, error);
            };
        }
    }
    
    /** Use the legacy database config instead of collections. */
    void useLegacyDatabaseConfig() {
        config.database = db.ref();
        config.collections = nullptr;
        config.collectionCount = 0;
    }

    void setupLegacyEncryptionCallback(bool encryptor = true, bool decryptor = true) {
        if (encryptor) {
            config.propertyEncryptor = [](void* context,
                                          FLString docID,
                                          FLDict props,
                                          FLString path,
                                          FLSlice input,
                                          FLStringResult* alg,
                                          FLStringResult* kid,
                                          CBLError* error) -> FLSliceResult {
                return ((ReplicatorPropertyEncryptionTest*)context) -> encrypt(context, docID, props, path, input, alg, kid, error);
            };
        }

        if (decryptor) {
            config.propertyDecryptor = [](void* context,
                                          FLString docID,
                                          FLDict props,
                                          FLString path,
                                          FLSlice input,
                                          FLString alg,
                                          FLString kid,
                                          CBLError* error) -> FLSliceResult {
                return ((ReplicatorPropertyEncryptionTest*)context) -> decrypt(context, docID, props, path, input, alg, kid, error);
            };
        }
    }

    FLSliceResult encrypt(void* context, FLString documentID, FLDict properties, FLString keyPath,
                          FLSlice input, FLStringResult* alg, FLStringResult* kid, CBLError* error)
    {
        encryptCount++;
        
        if (skipEncryption) { // Not allow and will result to an crypto error
            return FLSliceResult_CreateWith(nullptr, 0);
        }
        
        if (encryptionError.code > 0) {
            *error = encryptionError;
            return FLSliceResult_CreateWith(nullptr, 0);
        }
        
        alloc_slice encrypted(input);
        for (size_t i = 0; i < input.size; ++i) {
            (uint8_t&)encrypted[i] = encrypted[i] ^ 'K';
        }
        
        if (algorithm)
            *alg = FLStringResult(algorithm);
        
        if (keyID)
            *kid = FLStringResult(keyID);
        
        return FLSliceResult(encrypted);
    }
    
    FLSliceResult decrypt(void* context, FLString documentID, FLDict properties, FLString keyPath,
                          FLSlice input, FLString alg, FLString kid, CBLError* error)
    {
        decryptCount++;
        
        if (skipDecryption) { // Not allow and will result to an crypto error
            return FLSliceResult_CreateWith(nullptr, 0);
        }
        
        if (decryptionError.code > 0) {
            *error = decryptionError;
            return FLSliceResult_CreateWith(nullptr, 0);
        }
        
        alloc_slice decrypted(input);
        for (size_t i = 0; i < input.size; ++i) {
            (uint8_t&)decrypted[i] = decrypted[i] ^ 'K';
        }
        
        if (algorithm)
            CHECK(alg == algorithm);
        else
            CHECK(alg == "CB_MOBILE_CUSTOM"_sl);
        
        if (keyID)
            CHECK(kid == keyID);
        else
            CHECK(kid == kFLSliceNull);
        
        return FLSliceResult(decrypted);
    }
    
    void createEncryptedDoc(CBLCollection* collection, FLString docID, FLString secret ="Secret 1"_sl) {
        auto doc = CBLDocument_CreateWithID(docID);
        FLMutableDict props = CBLDocument_MutableProperties(doc);
        
        auto encryptable = CBLEncryptable_CreateWithString(secret);
        FLMutableDict_SetEncryptableValue(props, "secret"_sl, encryptable);
        
        CBLError error {};
        REQUIRE(CBLCollection_SaveDocument(collection, doc, &error));
        CBLDocument_Release(doc);
        CBLEncryptable_Release(encryptable);
    }
};

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Create Encryptable", "[Encryptable]") {
    string expJson;
    FLMutableDict dictValue = nullptr;
    FLMutableArray arrayValue = nullptr;
    CBLEncryptable* encryptable = nullptr;
    
    SECTION("Null") {
        encryptable = CBLEncryptable_CreateWithNull();
        FLValue v = CBLEncryptable_Value(encryptable);
        CHECK(FLValue_GetType(v) == kFLNull);
        expJson = "{\"@type\":\"encryptable\",\"value\":null}";
    }
    
    SECTION("Bool") {
        encryptable = CBLEncryptable_CreateWithBool(true);
        CHECK(FLValue_AsBool(CBLEncryptable_Value(encryptable)) == true);
        expJson = "{\"@type\":\"encryptable\",\"value\":true}";
    }

    SECTION("Int") {
        encryptable = CBLEncryptable_CreateWithInt(256);
        CHECK(FLValue_AsInt(CBLEncryptable_Value(encryptable)) == 256);
        expJson = "{\"@type\":\"encryptable\",\"value\":256}";
    }

    SECTION("UInt") {
        encryptable = CBLEncryptable_CreateWithUInt(1024);
        CHECK(FLValue_AsUnsigned(CBLEncryptable_Value(encryptable)) == 1024);
        expJson = "{\"@type\":\"encryptable\",\"value\":1024}";
    }

    SECTION("Float") {
        encryptable = CBLEncryptable_CreateWithFloat(35.57f);
        CHECK(FLValue_AsFloat(CBLEncryptable_Value(encryptable)) == 35.57f);
        expJson = "{\"@type\":\"encryptable\",\"value\":35.57}";
    }

    SECTION("Dobule") {
        encryptable = CBLEncryptable_CreateWithDouble(35.61);
        CHECK(FLValue_AsDouble(CBLEncryptable_Value(encryptable)) == 35.61);
        expJson = "{\"@type\":\"encryptable\",\"value\":35.61}";
    }

    SECTION("String") {
        encryptable = CBLEncryptable_CreateWithString("foo"_sl);
        CHECK(FLValue_AsString(CBLEncryptable_Value(encryptable)) == "foo"_sl);
        expJson = "{\"@type\":\"encryptable\",\"value\":\"foo\"}";
    }

    SECTION("Dict") {
        dictValue = FLMutableDict_New();
        FLSlot_SetString(FLMutableDict_Set(dictValue, "greeting"_sl), "hello"_sl);
        encryptable = CBLEncryptable_CreateWithDict(dictValue);
        CHECK(Dict(FLValue_AsDict(CBLEncryptable_Value(encryptable))).toJSONString() == "{\"greeting\":\"hello\"}");
        expJson = "{\"@type\":\"encryptable\",\"value\":{\"greeting\":\"hello\"}}";
    }

    SECTION("Array") {
        arrayValue = FLMutableArray_New();
        FLSlot_SetString(FLMutableArray_Append(arrayValue), "item1"_sl);
        FLSlot_SetString(FLMutableArray_Append(arrayValue), "item2"_sl);
        encryptable = CBLEncryptable_CreateWithArray(arrayValue);
        CHECK(Array(FLValue_AsArray(CBLEncryptable_Value(encryptable))).toJSONString() == "[\"item1\",\"item2\"]");
        expJson = "{\"@type\":\"encryptable\",\"value\":[\"item1\",\"item2\"]}";
    }
    
    SECTION("FLValue") {
        dictValue = FLMutableDict_New();
        FLSlot_SetString(FLMutableDict_Set(dictValue, "greeting"_sl), "hello"_sl);
        encryptable = CBLEncryptable_CreateWithValue((FLValue)dictValue);
        CHECK(Dict(FLValue_AsDict(CBLEncryptable_Value(encryptable))).toJSONString() == "{\"greeting\":\"hello\"}");
        expJson = "{\"@type\":\"encryptable\",\"value\":{\"greeting\":\"hello\"}}";
    }
    
    REQUIRE(encryptable);
    
    auto dict = FLMutableDict_New();
    FLSlot_SetEncryptableValue(FLMutableDict_Set(dict, "encryptable"_sl), encryptable);
    FLValue value = FLDict_Get(dict, "encryptable"_sl);
    
    CHECK(FLValue_IsEncryptableValue(value));
    CHECK(FLValue_AsDict(value) == CBLEncryptable_Properties(encryptable));
    CHECK(Dict(CBLEncryptable_Properties(encryptable)).toJSON(false, true) == expJson);
    
    FLMutableDict_Release(dict);
    FLMutableDict_Release(dictValue);
    FLMutableArray_Release(arrayValue);
    CBLEncryptable_Release(encryptable);
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Save and Get document with Encryptable", "[Encryptable]") {
    auto doc = CBLDocument_CreateWithID("doc1"_sl);
    
    // Set encryptable:
    FLMutableDict props = CBLDocument_MutableProperties(doc);
    auto encryptable = CBLEncryptable_CreateWithString("foo"_sl);
    FLMutableDict_SetEncryptableValue(props, "encryptable"_sl, encryptable);
    
    // Set non encryptable dict:
    auto nonencryptable = FLMutableDict_New();
    FLSlot_SetString(FLMutableDict_Set(nonencryptable, "greeting"_sl), "hello"_sl);
    FLSlot_SetDict(FLMutableDict_Set(props, "nonencryptable"_sl), nonencryptable);

    // Set non dict:
    FLSlot_SetString(FLMutableDict_Set(props, "string"_sl), "mystring"_sl);
    
    // Save doc:
    CBLError error;
    CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
    CBLDocument_Release(doc);
    CBLEncryptable_Release(encryptable);
    
    // Get doc:
    doc = CBLCollection_GetMutableDocument(defaultCollection.ref(), "doc1"_sl, &error);
    props = CBLDocument_MutableProperties(doc);
    
    FLValue value = FLDict_Get(props, "encryptable"_sl);
    auto getEncryptable = FLValue_GetEncryptableValue(value);
    CHECK(Dict(CBLEncryptable_Properties(getEncryptable)).toJSON(false, true) ==
          "{\"@type\":\"encryptable\",\"value\":\"foo\"}");
    
    value = FLDict_Get(props, "nonencryptable"_sl);
    CHECK(!FLValue_GetEncryptableValue(value));
    
    value = FLDict_Get(props, "string"_sl);
    CHECK(!FLValue_GetEncryptableValue(value));
    
    CBLDocument_Release(doc);
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Unsupport : Encryptables in array", "[Encryptable]") {
    CBLError error;
    auto doc = CBLDocument_CreateWithID("doc1"_sl);
    auto array = FLMutableArray_New();
    auto enc1 = CBLEncryptable_CreateWithString("foo1"_sl);
    auto enc2 = CBLEncryptable_CreateWithString("foo2"_sl);
    FLMutableArray_AppendDict(array, CBLEncryptable_Properties(enc1));
    FLMutableArray_AppendDict(array, CBLEncryptable_Properties(enc2));
    
    SECTION("Update mutable properties") {
        FLMutableDict props = CBLDocument_MutableProperties(doc);
        FLMutableDict_SetArray(props, "array"_sl, array);
    }
    
    SECTION("Set doc with JSON") { // Doc with have shallow mutable properties
        FLMutableDict props = FLMutableDict_New();
        FLMutableDict_SetArray(props, "array"_sl, array);
        FLStringResult json = FLValue_ToJSON((FLValue)props);
        REQUIRE(CBLDocument_SetJSON(doc, FLSliceResult_AsSlice(json), &error));
        FLMutableDict_Release(props);
    }
 
    // Save doc:
    ExpectingExceptions x;
    REQUIRE(!CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorUnsupported);
    CBLDocument_Release(doc);
    CBLEncryptable_Release(enc1);
    CBLEncryptable_Release(enc2);
    FLMutableArray_Release(array);
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Unsupport : Encryptables in nested array in dict", "[Encryptable]") {
    CBLError error;
    auto doc = CBLDocument_CreateWithID("doc1"_sl);
    FLMutableDict dict = FLMutableDict_New();
    auto array = FLMutableArray_New();
    auto enc1 = CBLEncryptable_CreateWithString("foo1"_sl);
    auto enc2 = CBLEncryptable_CreateWithString("foo2"_sl);
    FLMutableArray_AppendDict(array, CBLEncryptable_Properties(enc1));
    FLMutableArray_AppendDict(array, CBLEncryptable_Properties(enc2));
    FLMutableDict_SetArray(dict, "array"_sl, array);
    
    SECTION("Update mutable properties") {
        FLMutableDict props = CBLDocument_MutableProperties(doc);
        FLMutableDict_SetDict(props, "dict"_sl, dict);
    }
    
    SECTION("Set doc with JSON") { // Doc with have shallow mutable properties
        FLMutableDict props = FLMutableDict_New();
        FLMutableDict_SetDict(props, "dict"_sl, dict);
        FLStringResult json = FLValue_ToJSON((FLValue)props);
        REQUIRE(CBLDocument_SetJSON(doc, FLSliceResult_AsSlice(json), &error));
        FLMutableDict_Release(dict);
    }
 
    // Save doc:
    ExpectingExceptions x;
    REQUIRE(!CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorUnsupported);
    CBLDocument_Release(doc);
    CBLEncryptable_Release(enc1);
    CBLEncryptable_Release(enc2);
    FLMutableArray_Release(array);
    FLMutableDict_Release(dict);
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Unsupport : Encryptables in nested array in array", "[Encryptable]") {
    CBLError error;
    auto doc = CBLDocument_CreateWithID("doc1"_sl);
    
    FLMutableArray outerArray = FLMutableArray_New();
    FLMutableDict dict = FLMutableDict_New();
    auto array = FLMutableArray_New();
    auto enc1 = CBLEncryptable_CreateWithString("foo1"_sl);
    auto enc2 = CBLEncryptable_CreateWithString("foo2"_sl);
    FLMutableArray_AppendDict(array, CBLEncryptable_Properties(enc1));
    FLMutableArray_AppendDict(array, CBLEncryptable_Properties(enc2));
    FLMutableArray_AppendArray(outerArray, array);
    
    SECTION("Update mutable properties") {
        FLMutableDict props = CBLDocument_MutableProperties(doc);
        FLMutableDict_SetArray(props, "array"_sl, outerArray);
    }
    
    SECTION("Set doc with JSON") { // Doc with have shallow mutable properties
        FLMutableDict props = FLMutableDict_New();
        FLMutableDict_SetArray(props, "array"_sl, outerArray);
        FLStringResult json = FLValue_ToJSON((FLValue)props);
        REQUIRE(CBLDocument_SetJSON(doc, FLSliceResult_AsSlice(json), &error));
        FLMutableDict_Release(dict);
    }
 
    // Save doc:
    ExpectingExceptions x;
    REQUIRE(!CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorUnsupported);
    CBLDocument_Release(doc);
    CBLEncryptable_Release(enc1);
    CBLEncryptable_Release(enc2);
    FLMutableArray_Release(array);
    FLMutableArray_Release(outerArray);
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Legacy - Encrypt and decrypt one property", "[Replicator][Encryptable]") {
    {
        auto doc = CBLDocument_CreateWithID("doc1"_sl);
        FLMutableDict props = CBLDocument_MutableProperties(doc);

        auto secret = CBLEncryptable_CreateWithString("Secret 1"_sl);
        FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret);

        CBLError error;
        CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));

        CBLDocument_Release(doc);
        CBLEncryptable_Release(secret);

        config.replicatorType = kCBLReplicatorTypePushAndPull;
        useLegacyDatabaseConfig();
        setupLegacyEncryptionCallback();
        replicate();

        doc = CBLCollection_GetMutableDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error);
        CHECK(Dict(CBLDocument_Properties(doc)).toJSON(false, true) ==
              "{\"encrypted$secret1\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
        CHECK(encryptCount == 1);
        CBLDocument_Release(doc);
    }

    {
        reset();
        useLegacyDatabaseConfig();
        replicate();

        CBLError error;
        auto doc = CBLCollection_GetMutableDocument(defaultCollection.ref(), "doc1"_sl, &error);
        CHECK(Dict(CBLDocument_Properties(doc)).toJSON(false, true) ==
              "{\"secret1\":{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}}");
        CHECK(decryptCount == 1);
        CBLDocument_Release(doc);
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Encrypt and decrypt one property", "[Replicator][Encryptable]") {
    {
        auto doc = CBLDocument_CreateWithID("doc1"_sl);
        FLMutableDict props = CBLDocument_MutableProperties(doc);
        
        auto secret = CBLEncryptable_CreateWithString("Secret 1"_sl);
        FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret);
        
        CBLError error;
        CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
        
        CBLDocument_Release(doc);
        CBLEncryptable_Release(secret);
        
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        setupEncryptionCallback();
        replicate();
        
        doc = CBLCollection_GetMutableDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error);
        CHECK(Dict(CBLDocument_Properties(doc)).toJSON(false, true) ==
              "{\"encrypted$secret1\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
        CHECK(encryptCount == 1);
        CBLDocument_Release(doc);
    }
    
    {
        reset();
        replicate();
        
        CBLError error;
        auto doc = CBLCollection_GetMutableDocument(defaultCollection.ref(), "doc1"_sl, &error);
        CHECK(Dict(CBLDocument_Properties(doc)).toJSON(false, true) ==
              "{\"secret1\":{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}}");
        CHECK(decryptCount == 1);
        CBLDocument_Release(doc);
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Encrypt and decrypt multiple properties", "[Replicator][Encryptable]") {
    {
        auto doc = CBLDocument_CreateWithID("doc1"_sl);
        auto props = CBLDocument_MutableProperties(doc);
        
        auto secret1 = CBLEncryptable_CreateWithString("Secret 1"_sl);
        FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret1);
        
        auto secret2 = CBLEncryptable_CreateWithInt(10);
        FLMutableDict_SetEncryptableValue(props, "secret2"_sl, secret2);
        
        auto nestedDict = FLMutableDict_New();
        auto secret3 = CBLEncryptable_CreateWithBool(true);
        FLMutableDict_SetEncryptableValue(nestedDict, "secret3"_sl, secret3);
        
        FLSlot_SetDict(FLMutableDict_Set(props, "nested"_sl), nestedDict);
        
        CBLError error;
        CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
        
        CBLDocument_Release(doc);
        FLMutableDict_Release(nestedDict);
        CBLEncryptable_Release(secret1);
        CBLEncryptable_Release(secret2);
        CBLEncryptable_Release(secret3);
        
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        setupEncryptionCallback();
        replicate();
        
        doc = CBLCollection_GetMutableDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error);
        props = CBLDocument_MutableProperties(doc);

        CHECK(Dict(FLValue_AsDict(FLDict_Get(props, "encrypted$secret1"_sl))).toJSON(false, true) ==
              "{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}");

        CHECK(Dict(FLValue_AsDict(FLDict_Get(props, "encrypted$secret2"_sl))).toJSON(false, true) ==
              "{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"ens=\"}");

        auto nested = FLValue_AsDict(FLDict_Get(props, "nested"_sl));
        CHECK(Dict(FLValue_AsDict(FLDict_Get(nested, "encrypted$secret3"_sl))).toJSON(false, true) ==
              "{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"Pzk+Lg==\"}");
        
        CHECK(encryptCount == 3);
        CBLDocument_Release(doc);
    }
    
    {
        reset();
        replicate();

        CBLError error;
        auto doc = CBLCollection_GetMutableDocument(defaultCollection.ref(), "doc1"_sl, &error);
        auto props = CBLDocument_Properties(doc);

        CHECK(Dict(FLValue_AsDict(FLDict_Get(props, "secret1"_sl))).toJSON(false, true) ==
              "{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}");

        CHECK(Dict(FLValue_AsDict(FLDict_Get(props, "secret2"_sl))).toJSON(false, true) ==
              "{\"@type\":\"encryptable\",\"value\":10}");

        auto nested = FLValue_AsDict(FLDict_Get(props, "nested"_sl));
        CHECK(Dict(FLValue_AsDict(FLDict_Get(nested, "secret3"_sl))).toJSON(false, true) ==
              "{\"@type\":\"encryptable\",\"value\":true}");
        
        CHECK(decryptCount == 3);
        CBLDocument_Release(doc);
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "No encryptor : crypto error", "[Replicator][Encryptable]") {
    auto doc = CBLDocument_CreateWithID("doc1"_sl);
    auto props = CBLDocument_MutableProperties(doc);
    
    auto secret1 = CBLEncryptable_CreateWithString("Secret 1"_sl);
    FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret1);
    
    CBLError error;
    CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
    
    CBLDocument_Release(doc);
    CBLEncryptable_Release(secret1);
    
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    {
        ExpectingExceptions x;
        replicate();
    }
    
    CHECK(replicatedDocs.size() == 1);
    CHECK(replicatedDocs["doc1"].error.code == kCBLErrorCrypto);
    CHECK(replicatedDocs["doc1"].error.domain == kCBLDomain);
    CHECK(!CBLCollection_GetDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error));
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "No decryptor : ok", "[Replicator][Encryptable]") {
    {
        auto doc = CBLDocument_CreateWithID("doc1"_sl);
        auto props = CBLDocument_MutableProperties(doc);
        
        auto secret1 = CBLEncryptable_CreateWithString("Secret 1"_sl);
        FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret1);
        
        CBLError error;
        CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
        
        CBLDocument_Release(doc);
        CBLEncryptable_Release(secret1);
        
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        setupEncryptionCallback(true, false /* no decryptor */);
        replicate();
    }
    
    {
        replicatedDocs.clear();
        reset();
        replicate();

        CBLError error;
        auto doc = CBLCollection_GetMutableDocument(defaultCollection.ref(), "doc1"_sl, &error);
        auto props = CBLDocument_Properties(doc);

        CHECK(Dict(FLValue_AsDict(FLDict_Get(props, "encrypted$secret1"_sl))).toJSON(false, true) ==
              "{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}");
        
        CBLDocument_Release(doc);
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Skip encryption : crypto error", "[Replicator][Encryptable]") {
    auto doc = CBLDocument_CreateWithID("doc1"_sl);
    auto props = CBLDocument_MutableProperties(doc);
    
    auto secret1 = CBLEncryptable_CreateWithString("Secret 1"_sl);
    FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret1);
    
    CBLError error;
    CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
    
    CBLDocument_Release(doc);
    CBLEncryptable_Release(secret1);
    
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    
    skipEncryption = true;
    setupEncryptionCallback();
    
    {
        ExpectingExceptions x;
        replicate();
    }
    
    CHECK(replicatedDocs.size() == 1);
    CHECK(replicatedDocs["doc1"].error.code == kCBLErrorCrypto);
    CHECK(replicatedDocs["doc1"].error.domain == kCBLDomain);
    CHECK(!CBLCollection_GetDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error));
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Skip decryption : ok", "[Replicator][Encryptable]") {
    {
        auto doc = CBLDocument_CreateWithID("doc1"_sl);
        auto props = CBLDocument_MutableProperties(doc);
        
        auto secret1 = CBLEncryptable_CreateWithString("Secret 1"_sl);
        FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret1);
        
        CBLError error;
        CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
        
        CBLDocument_Release(doc);
        CBLEncryptable_Release(secret1);
        
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        
        setupEncryptionCallback();
        replicate();
        
        doc = CBLCollection_GetMutableDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error);
        CHECK(Dict(CBLDocument_Properties(doc)).toJSON(false, true) ==
              "{\"encrypted$secret1\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
        CHECK(encryptCount == 1);
        CBLDocument_Release(doc);
    }
    
    {
        reset();
        skipDecryption = true;
        replicate();
        
        CBLError error;
        auto doc = CBLCollection_GetMutableDocument(defaultCollection.ref(), "doc1"_sl, &error);
        CHECK(doc);
        CHECK(Dict(CBLDocument_Properties(doc)).toJSON(false, true) ==
              "{\"encrypted$secret1\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
        CHECK(encryptCount == 1);
        CBLDocument_Release(doc);
    }
}


TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Encryption error", "[Replicator][Encryptable]") {
    auto doc = CBLDocument_CreateWithID("doc1"_sl);
    auto props = CBLDocument_MutableProperties(doc);
    
    auto secret1 = CBLEncryptable_CreateWithString("Secret 1"_sl);
    FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret1);
    
    CBLError error;
    CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
    
    CBLDocument_Release(doc);
    CBLEncryptable_Release(secret1);
    
    config.replicatorType = kCBLReplicatorTypePush;
    setupEncryptionCallback(true, false);
    
    CBLError expectedDocReplError = { };
    bool willRetryToSyncAgain = false;
    
    SECTION("503 Error") {
        encryptionError = {kCBLWebSocketDomain, 503};
        expectedDocReplError = encryptionError;
        expectedError = encryptionError;
        willRetryToSyncAgain = true; // The doc should be retried to sync again
    }
    
    SECTION("Crypto Error") {
        encryptionError = {kCBLDomain, kCBLErrorCrypto};
        expectedDocReplError = encryptionError;
        expectedError = { };
    }
    
    SECTION("Other Error") {
        encryptionError = {kCBLDomain, kCBLErrorUnexpectedError};
        expectedDocReplError = {kCBLDomain, kCBLErrorUnexpectedError};
        expectedError = { };
    }
    
    ExpectingExceptions x;
    replicate();
    
    CHECK(replicatedDocs.size() == 1);
    CHECK(replicatedDocs["doc1"].error.domain == expectedDocReplError.domain);
    CHECK(replicatedDocs["doc1"].error.code == expectedDocReplError.code);
    CHECK(!CBLCollection_GetDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error));
    
    // Now try to replicate again with no error:
    
    replicatedDocs.clear();
    encryptionError = { };
    
    expectedError = { };
    replicate();
    
    if (willRetryToSyncAgain) {
        CHECK(replicatedDocs.size() == 1);
        CHECK(replicatedDocs["doc1"].error.domain == 0);
        CHECK(replicatedDocs["doc1"].error.code == 0);
        
        const CBLDocument* doc1 = CBLCollection_GetDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error);
        CHECK(doc1);
        CBLDocument_Release(doc1);
    } else {
        CHECK(replicatedDocs.size() == 0);
        CHECK(!CBLCollection_GetDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error));
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Decryption error", "[Replicator][Encryptable]") {
    {
        auto doc = CBLDocument_CreateWithID("doc1"_sl);
        auto props = CBLDocument_MutableProperties(doc);
        
        auto secret1 = CBLEncryptable_CreateWithString("Secret 1"_sl);
        FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret1);
        
        CBLError error;
        CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
        
        CBLDocument_Release(doc);
        CBLEncryptable_Release(secret1);
        
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        setupEncryptionCallback();
        replicate();
        
        doc = CBLCollection_GetMutableDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error);
        CHECK(doc);
        CBLDocument_Release(doc);
    }
    
    {
        replicatedDocs.clear();
        reset();
        
        CBLError expectedDocReplError = { };
        bool willRetryToSyncAgain = false;
        
        SECTION("503 Error") {
            decryptionError = {kCBLWebSocketDomain, 503};
            expectedDocReplError = decryptionError;
            expectedError = decryptionError;
            willRetryToSyncAgain = true; // The doc should be retried to sync again
        }
        
        SECTION("Crypto Error") {
            decryptionError = {kCBLDomain, kCBLErrorCrypto};
            expectedDocReplError = decryptionError;
            expectedError = { };
        }
        
        SECTION("Other Error") {
            decryptionError = {kCBLDomain, kCBLErrorUnexpectedError};
            expectedDocReplError = {kCBLDomain, kCBLErrorUnexpectedError};
            expectedError = { };
        }
        
        CHECK(replicatedDocs.size() == 0);
        ExpectingExceptions x;
        replicate();
        
        CHECK(replicatedDocs.size() == 1);
        CHECK(replicatedDocs["doc1"].error.domain == expectedDocReplError.domain);
        CHECK(replicatedDocs["doc1"].error.code == expectedDocReplError.code);
        
        CBLError error;
        CHECK(!CBLCollection_GetDocument(defaultCollection.ref(), "doc1"_sl, &error));

        // Now try to replicate again with no error:
        replicatedDocs.clear();
        decryptionError = { };
        
        expectedError = { };
        replicate();
        
        if (willRetryToSyncAgain) {
            CHECK(replicatedDocs.size() == 1);
            CHECK(replicatedDocs["doc1"].error.domain == 0);
            CHECK(replicatedDocs["doc1"].error.code == 0);
            
            const CBLDocument* doc = CBLCollection_GetDocument(defaultCollection.ref(), "doc1"_sl, &error);
            CHECK(doc);
            CBLDocument_Release(doc);
        } else {
            CHECK(replicatedDocs.size() == 0);
            CHECK(!CBLCollection_GetDocument(defaultCollection.ref(), "doc1"_sl, &error));
        }
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Encrypt already encrypted values", "[Replicator][Encryptable]") {
    {
        auto doc = CBLDocument_CreateWithID("doc1"_sl);
        auto props = CBLDocument_MutableProperties(doc);
        
        auto secret = FLMutableDict_New();
        FLSlot_SetString(FLMutableDict_Set(secret, "alg"_sl), "CB_MOBILE_CUSTOM"_sl);
        FLSlot_SetString(FLMutableDict_Set(secret, "ciphertext"_sl), "aRguKDkuP2t6aQ=="_sl);
        FLSlot_SetDict(FLMutableDict_Set(props, "encrypted$secret"_sl), secret);
        
        CBLError error;
        CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
        
        CBLDocument_Release(doc);
        FLMutableDict_Release(secret);
        
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        setupEncryptionCallback();
        replicate();
        
        doc = CBLCollection_GetMutableDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error);
        props = CBLDocument_MutableProperties(doc);
        
        CHECK(Dict(FLValue_AsDict(FLDict_Get(props, "encrypted$secret"_sl))).toJSON(false, true) ==
              "{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}");
        
        CHECK(encryptCount == 0);
        CBLDocument_Release(doc);
    }
}


TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Key ID and Algorithm", "[Replicator][Encryptable]") {
    {
        auto doc = CBLDocument_CreateWithID("doc1"_sl);
        FLMutableDict props = CBLDocument_MutableProperties(doc);
        
        auto secret = CBLEncryptable_CreateWithString("Secret 1"_sl);
        FLMutableDict_SetEncryptableValue(props, "secret1"_sl, secret);
        
        CBLError error;
        CHECK(CBLCollection_SaveDocument(defaultCollection.ref(), doc, &error));
        
        CBLDocument_Release(doc);
        CBLEncryptable_Release(secret);
        
        config.replicatorType = kCBLReplicatorTypePushAndPull;
        
        keyID = "MY_KEY_ID"_sl;
        algorithm = "XOR_ALG"_sl;
        setupEncryptionCallback();
        
        replicate();
        
        doc = CBLCollection_GetMutableDocument(otherDBDefaultCol.ref(), "doc1"_sl, &error);
        CHECK(Dict(CBLDocument_Properties(doc)).toJSON(false, true) ==
              "{\"encrypted$secret1\":{\"alg\":\"XOR_ALG\",\"ciphertext\":\"aRguKDkuP2t6aQ==\",\"kid\":\"MY_KEY_ID\"}}");
        CHECK(encryptCount == 1);
        CBLDocument_Release(doc);
    }
    
    {
        reset();
        replicate();
        
        CBLError error;
        auto doc = CBLCollection_GetMutableDocument(defaultCollection.ref(), "doc1"_sl, &error);
        CHECK(Dict(CBLDocument_Properties(doc)).toJSON(false, true) ==
              "{\"secret1\":{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}}");
        CHECK(decryptCount == 1);
        CBLDocument_Release(doc);
    }
}

TEST_CASE_METHOD(ReplicatorPropertyEncryptionTest, "Encrypt and decrypt with multiple collections", "[Replicator][Encryptable]") {
    auto c1x = CreateCollection(db.ref(), "colA", "scopeA");
    auto c2x = CreateCollection(db.ref(), "colB", "scopeA");
    
    auto c1y = CreateCollection(otherDB.ref(), "colA", "scopeA");
    auto c2y = CreateCollection(otherDB.ref(), "colB", "scopeA");
    
    createEncryptedDoc(c1x, "doc1"_sl);
    createEncryptedDoc(c2x, "doc2"_sl);
    
    config.replicatorType = kCBLReplicatorTypePushAndPull;
    config.documentPropertyEncryptor = [](void* context,
                                          FLString scope,
                                          FLString collection,
                                          FLString docID,
                                          FLDict props,
                                          FLString path,
                                          FLSlice input,
                                          FLStringResult* alg,
                                          FLStringResult* kid,
                                          CBLError* error) -> FLSliceResult
    {
        CHECK(slice(scope) == "scopeA"_sl);
        if (slice(collection) == "colA")
            CHECK(slice(docID) == "doc1"_sl);
        else if (slice(collection) == "colB")
            CHECK(slice(docID) == "doc2"_sl);
        else
            FAIL("Invalid docID");
        return ((ReplicatorPropertyEncryptionTest*)context) -> encrypt(context, docID, props, path,
                                                                       input, alg, kid, error);
    };
    
    config.documentPropertyDecryptor = [](void* context,
                                          FLString scope,
                                          FLString collection,
                                          FLString docID,
                                          FLDict props,
                                          FLString path,
                                          FLSlice input,
                                          FLString alg,
                                          FLString kid,
                                          CBLError* error) -> FLSliceResult
    {
        CHECK(slice(scope) == "scopeA"_sl);
        if (slice(collection) == "colA")
            CHECK(slice(docID) == "doc1"_sl);
        else if (slice(collection) == "colB")
            CHECK(slice(docID) == "doc2"_sl);
        else
            FAIL("Invalid docID");
        return ((ReplicatorPropertyEncryptionTest*)context) -> decrypt(context, docID, props, path,
                                                                       input, alg, kid, error);
    };
    
    vector<CBLReplicationCollection> colConfigs = collectionConfigs( {c1x, c2x });
    config.collections = colConfigs.data();
    config.collectionCount = colConfigs.size();
    
    // Push:
    replicate();
    
    CBLError error {};
    auto doc1 = CBLCollection_GetMutableDocument(c1y, "doc1"_sl, &error);
    CHECK(Dict(CBLDocument_Properties(doc1)).toJSON(false, true) ==
          "{\"encrypted$secret\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
    CBLDocument_Release(doc1);
    
    auto doc2 = CBLCollection_GetMutableDocument(c2y, "doc2"_sl, &error);
    CHECK(Dict(CBLDocument_Properties(doc2)).toJSON(false, true) ==
          "{\"encrypted$secret\":{\"alg\":\"CB_MOBILE_CUSTOM\",\"ciphertext\":\"aRguKDkuP2t6aQ==\"}}");
    CBLDocument_Release(doc2);
    
    // Purge and pull again with reset checkpoint:
    REQUIRE(CBLCollection_PurgeDocumentByID(c1x, "doc1"_sl, &error));
    REQUIRE(CBLCollection_PurgeDocumentByID(c2x, "doc2"_sl, &error));
    replicate(true);
    
    doc1 = CBLCollection_GetMutableDocument(c1x, "doc1"_sl, &error);
    CHECK(Dict(CBLDocument_Properties(doc1)).toJSON(false, true) ==
          "{\"secret\":{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}}");
    CBLDocument_Release(doc1);
    
    doc2 = CBLCollection_GetMutableDocument(c2x, "doc2"_sl, &error);
    CHECK(Dict(CBLDocument_Properties(doc2)).toJSON(false, true) ==
          "{\"secret\":{\"@type\":\"encryptable\",\"value\":\"Secret 1\"}}");
    CBLDocument_Release(doc2);
    
    CBLCollection_Release(c1x);
    CBLCollection_Release(c2x);
    CBLCollection_Release(c1y);
    CBLCollection_Release(c2y);
}

#endif
