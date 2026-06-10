//
// EncryptableTest_Cpp.cc
//
// Copyright (c) 2026 Couchbase, Inc All rights reserved.
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

#ifdef COUCHBASE_ENTERPRISE

using namespace std;
using namespace fleece;
using namespace cbl;


TEST_CASE_METHOD(CBLTest_Cpp, "C++ Encryptable factories", "[Encryptable]") {
    string expectedJSON;
    Encryptable enc = Encryptable::createWithNull();   // placeholder; overwritten in sections

    SECTION("Null") {
        enc = Encryptable::createWithNull();
        CHECK(enc.value().type() == kFLNull);
        expectedJSON = "{\"@type\":\"encryptable\",\"value\":null}";
    }
    SECTION("Bool") {
        enc = Encryptable::createWithBool(true);
        CHECK(enc.value().asBool() == true);
        expectedJSON = "{\"@type\":\"encryptable\",\"value\":true}";
    }
    SECTION("Int") {
        enc = Encryptable::createWithInt(256);
        CHECK(enc.value().asInt() == 256);
        expectedJSON = "{\"@type\":\"encryptable\",\"value\":256}";
    }
    SECTION("UInt") {
        enc = Encryptable::createWithUInt(1024);
        CHECK(enc.value().asUnsigned() == 1024);
        expectedJSON = "{\"@type\":\"encryptable\",\"value\":1024}";
    }
    SECTION("Float") {
        enc = Encryptable::createWithFloat(35.57f);
        CHECK(enc.value().asFloat() == 35.57f);
        expectedJSON = "{\"@type\":\"encryptable\",\"value\":35.57}";
    }
    SECTION("Double") {
        enc = Encryptable::createWithDouble(35.61);
        CHECK(enc.value().asDouble() == 35.61);
        expectedJSON = "{\"@type\":\"encryptable\",\"value\":35.61}";
    }
    SECTION("String via constructor (string_view)") {
        enc = Encryptable{"hello world"};
        CHECK(enc.value().asString() == "hello world"_sl);
        expectedJSON = "{\"@type\":\"encryptable\",\"value\":\"hello world\"}";
    }
    SECTION("Dict via Value constructor") {
        auto d = MutableDict::newDict();
        d["greeting"] = "hello";
        enc = Encryptable{Value(d)};
        CHECK(enc.value().asDict().toJSONString() == "{\"greeting\":\"hello\"}");
        expectedJSON = "{\"@type\":\"encryptable\",\"value\":{\"greeting\":\"hello\"}}";
    }

    CHECK(enc.properties().toJSON(false, true).asString() == expectedJSON);
    CHECK(Encryptable::isEncryptableValue(enc.properties()));
}


TEST_CASE_METHOD(CBLTest_Cpp, "C++ Encryptable isEncryptableValue rejects plain dict", "[Encryptable]") {
    auto plain = MutableDict::newDict();
    plain["foo"] = "bar";
    CHECK(!Encryptable::isEncryptableValue(plain));
    CHECK(!Encryptable::isEncryptableValue(Dict()));
}


TEST_CASE_METHOD(CBLTest_Cpp, "C++ Encryptable getEncryptableValue round-trip", "[Encryptable]") {
    // Save a document containing an encryptable.
    {
        MutableDocument doc("enc-doc");
        Encryptable enc{"secret-value"};
        FLMutableDict_SetEncryptableValue(doc.properties(), "secret"_sl, enc.ref());
        defaultCollection.saveDocument(doc);
    }

    // Read it back; extract the encryptable; let the wrapper go out of scope
    // BEFORE the document. A refcount imbalance in getEncryptableValue would
    // surface as a leak or use-after-free that ~CBLTest_Cpp's
    // CBL_InstanceCount() == 0 check would flag.
    Document doc = defaultCollection.getDocument("enc-doc");
    REQUIRE(doc);
    Value v = doc["secret"];
    REQUIRE(FLValue_IsEncryptableValue(v));
    {
        Encryptable got = Encryptable::getEncryptableValue(v);
        REQUIRE(got);
        CHECK(got.value().asString() == "secret-value"_sl);
    }
    // `got` destroyed here; `doc` destroyed at end of test.
}


TEST_CASE_METHOD(CBLTest_Cpp, "C++ Encryptable getEncryptableValue on non-encryptable",
                 "[Encryptable]") {
    // FLValue_GetEncryptableValue returns null for a plain (non-encryptable) dict,
    // and the wrapper should turn that into a falsy Encryptable without crashing.
    auto plain = MutableDict::newDict();
    plain["foo"] = "bar";
    Encryptable got = Encryptable::getEncryptableValue(Value(plain));
    CHECK(!got);
}


TEST_CASE_METHOD(CBLTest_Cpp, "C++ Encryptable embedded in mutable dict", "[Encryptable]") {
    Encryptable enc{"secret-value"};

    auto dict = MutableDict::newDict();
    FLSlot_SetEncryptableValue(FLMutableDict_Set(dict, "encryptable"_sl), enc.ref());

    Value v = dict["encryptable"];
    CHECK(FLValue_IsEncryptableValue(v));
    CHECK(FLValue_AsDict(v) == (FLDict)enc.properties());
}

#endif // COUCHBASE_ENTERPRISE
