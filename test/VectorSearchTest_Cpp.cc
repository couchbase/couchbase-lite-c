//
// VectorSearchTest_Cpp.cc
//
// Copyright © 2024 Couchbase. All rights reserved.
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
#include "CBLTest_Cpp.hh"
#include "VectorSearchTest.hh"

#ifdef VECTOR_SEARCH_TEST_ENABLED

using namespace cbl;

/** Sanity Test Vector Index C++ API */
class VectorSearchTest_Cpp : public VectorSearchTest {
public:
    // Use a diffferent name from the based VectorSearchTest:
    constexpr static const fleece::slice kPredictiveModelCppName = "WordEmbeddingCpp";
    
    VectorSearchTest_Cpp() {
        _wordDB = Database(kWordsDatabaseName, databaseConfig());
        _wordsColl = _wordDB.getCollection(kWordsCollectionName);
        
        registerPredictiveModel();
    }
    
    ~VectorSearchTest_Cpp() {
        unregisterPredictiveModel();
    }
    
    Database _wordDB;
    Collection _wordsColl;

    class WordPredictiveModel {
    public:
        WordPredictiveModel(VectorSearchTest* test) : _test(test) { }

        fleece::MutableDict operator()(fleece::Dict input) noexcept {
            auto word = input["word"].asString();
            if (!word) return MutableDict(nullptr);
            
            auto vector = _test->vectorArrayForWord(word, kWordsCollectionName);
            if (!vector) return MutableDict(nullptr);
            
            auto output = MutableDict::newDict();
            output["vector"] = Array(vector);
            return output;
        }
    private:
        VectorSearchTest* _test {};
    };

    void registerPredictiveModel() {
        Prediction::registerModel(kPredictiveModelCppName, WordPredictiveModel(this));
    }

    void unregisterPredictiveModel() {
        Prediction::unregisterModel(kPredictiveModelCppName);
    }
};

TEST_CASE_METHOD(VectorSearchTest_Cpp, "Sanity - Create Vector Index C++", "[VectorSearchCpp]") {
    VectorIndexConfiguration config = { kCBLN1QLLanguage, "vector"_sl, 300, 8};
    
    SECTION("Create with default encoding") { }
    
    SECTION("Create with none encoding") {
        config.encoding = VectorEncoding::none();
    }
    
    SECTION("Create with SQ encoding") {
        config.encoding = VectorEncoding::scalarQuantizer(kCBLSQ4);
    }
    
    SECTION("Create with PQ encoding") {
        config.encoding = VectorEncoding::productQuantizer(2, 8);
    }
    
    _wordsColl.createVectorIndex(kWordsIndexName, config);
    
    auto results = executeWordsQuery(20);
    CHECK(CountResults(results) == 20);
    CBLResultSet_Release(results);
}

TEST_CASE_METHOD(VectorSearchTest_Cpp, "Sanity - Create Vector Index Using Predictive Model C++", "[VectorSearchCpp]") {
    VectorIndexConfiguration config = { kCBLN1QLLanguage, "prediction(WordEmbeddingCpp,{\"word\": word}).vector"_sl, 300, 8};
    
    _wordsColl.createVectorIndex(kWordsIndexName, config);
    
    auto results = executeWordsQuery(20, "prediction(WordEmbeddingCpp,{\"word\": word}).vector");
    CHECK(CountResults(results) == 20);
    CBLResultSet_Release(results);
}

TEST_CASE_METHOD(VectorSearchTest_Cpp, "Lazy Vector Index Sanity C++", "[VectorSearchCpp]") {
    VectorIndexConfiguration config = { kCBLN1QLLanguage, "word"_sl, 300, 8};
    config.isLazy = true;
    config.numProbes = 8;
    
    _wordsColl.createVectorIndex(kWordsIndexName, config);
    
    auto results = executeWordsQuery(20, "word");
    CHECK(CountResults(results) == 0);
    CBLResultSet_Release(results);
    
    auto index = _wordsColl.getIndex(kWordsIndexName);
    CHECK(index);
    CHECK(index.name() == kWordsIndexName.asString());
    CHECK(index.collection() == _wordsColl);
    
    int count = 0;
    while (true) {
        IndexUpdater updater = index.beginUpdate(100);
        if (!updater) break;
        
        for (int i = 0; i < updater.count(); i++) {
            auto value = updater.value(i);
            auto vector = vectorForWord(value.asString());
            REQUIRE(vector.size() > 0);
            updater.setVector(i, vector.data(), vector.size());
            count++;
        }
        updater.finish();
    }
    
    CHECK(count == 300);
    results = executeWordsQuery(300, "word");
    CHECK(CountResults(results) == 300);
    CBLResultSet_Release(results);
}

#endif
