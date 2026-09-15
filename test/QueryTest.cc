//
// QueryTest.cc
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

#include "CBLTest.hh"
#include "cbl/CouchbaseLite.h"
#include "fleece/Fleece.hh"
#include "fleece/Mutable.hh"
#include "CBLPrivate.h"
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>

using namespace std;
using namespace fleece;


class QueryTest : public CBLTest {
public:
    QueryTest() {
        ImportJSONLines("names_100.json", defaultCollection);
        
    #ifdef DEBUG
        CBLQuery_SetListenerCallbackDelay(0);
    #endif
    }
    
    ~QueryTest() {
        CBLResultSet_Release(results);
        CBLQuery_Release(query);
    }

    CBLQuery *query =nullptr;
    CBLResultSet *results =nullptr;
};


static int countResults(CBLResultSet *results) {
     int n = 0;
     while (CBLResultSet_Next(results))
         ++n;
     return n;
}

/** For keeping track of listener callback result in LiveQuery tests. */
struct ListenerState {
    int count() {
        lock_guard<mutex> lock(_mutex);
        return _count;
    }
    
    int resultCount() {
        lock_guard<mutex> lock(_mutex);
        return _resultCount;
    }
    
    void reset() {
        lock_guard<mutex> lock(_mutex);
        _count = 0;
        _resultCount = -1;
    }
    
    void receivedCallback(void *context, CBLQuery* query, CBLListenerToken* token) {
        lock_guard<mutex> lock(_mutex);
        ++_count;
        
        CBLError error;
        auto newResults = CBLQuery_CopyCurrentResults(query, token, &error);
        REQUIRE(newResults);
        _resultCount = countResults(newResults);
        CBLResultSet_Release(newResults);
    }
    
    bool waitForCount(int target) {
        int timeoutCount = 0;
        while (timeoutCount++ < 50) {
            {
                lock_guard<mutex> lock(_mutex);
                if (_count == target)
                    return true;
            }
            this_thread::sleep_for(100ms);
        }
        return false;
    }
    
private:
    std::mutex _mutex;
    int _count {0};
    int _resultCount {-1};
};


TEST_CASE_METHOD(QueryTest, "Invalid Query", "[Query][!throws]") {
    CBLError error;
    int errPos;
    {
        ExpectingExceptions x;
        CBL_Log(kCBLLogDomainQuery, kCBLLogWarning, "INTENTIONALLY THROWING EXCEPTION!");
        query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                        "SELECT name WHERE"_sl,
                                        &errPos, &error);
    }
    REQUIRE(!query);
    CHECK(errPos == 17);
    CHECK(error.domain == kCBLDomain);
    CHECK(error.code == kCBLErrorInvalidQuery);
}


TEST_CASE_METHOD(QueryTest, "Query", "[Query]") {
    CBLError error;
    int errPos;
    
    SECTION("SQL++") {
        query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                        "SELECT name FROM _ WHERE birthday like '1959-%' ORDER BY birthday"_sl,
                                        &errPos, &error);
    }
    
    SECTION("JSON") {
        query = CBLDatabase_CreateQuery(db, kCBLJSONLanguage,
                                        "{'WHAT':[['.name']],'FROM':[{'COLLECTION':'_'}],'WHERE':['LIKE',['.birthday'],'1959-%'],'ORDER_BY':[['.birthday']]}"_sl,
                                        &errPos, &error);
    }
    
    REQUIRE(query);
    CHECK(CBLQuery_ColumnCount(query) == 1);
    CHECK(CBLQuery_ColumnName(query, 0) == "name"_sl);

    alloc_slice explanation(CBLQuery_Explain(query));
    cerr << string(explanation);

    static const slice kExpectedFirst[3] = {"Tyesha",  "Eddie",     "Diedre"};
    static const slice kExpectedLast [3] = {"Loehrer", "Colangelo", "Clinton"};

    int n = 0;
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);
    CHECK(CBLResultSet_GetQuery(results) == query);
    
    while (CBLResultSet_Next(results)) {
        FLValue name = CBLResultSet_ValueAtIndex(results, 0);
        CHECK(CBLResultSet_ValueForKey(results, "name"_sl) == name);
        FLDict dict = FLValue_AsDict(name);
        CHECK(dict);
        slice first  = FLValue_AsString(FLDict_Get(dict, "first"_sl));
        slice last   = FLValue_AsString(FLDict_Get(dict, "last"_sl));
        REQUIRE(n < 3);
        CHECK(first == kExpectedFirst[n]);
        CHECK(last == kExpectedLast[n]);
        ++n;
        cerr << first << " " << last << "\n";
    }
    CHECK(n == 3);
}

TEST_CASE_METHOD(QueryTest, "Unicode Query", "[Query]"){
    CBLError error;
    int errPos;

    CBLDocument* doc = CBLDocument_CreateWithID("first"_sl);
    REQUIRE(doc);
    REQUIRE(CBLDocument_SetJSON(doc, "{\"name\":{\"first\": \"Melanie\", \"last\": \"Bochaard\" }, \"city\": \"Manchester\"}"_sl, &error));
    REQUIRE(CBLCollection_SaveDocument(defaultCollection, doc, &error));
    CBLDocument_Release(doc);

    CBLDocument* doc2 = CBLDocument_CreateWithID("second"_sl);
    REQUIRE(doc2);
    REQUIRE(CBLDocument_SetJSON(doc2, "{\"name\":{\"first\": \"Mélanie\", \"last\": \"Bochaard\" }, \"city\": \"Manchester\"}"_sl, &error));
    REQUIRE(CBLCollection_SaveDocument(defaultCollection, doc2, &error));
    CBLDocument_Release(doc2);

    string queryString, queryString2;
    int docsCount  = -1;
    int docsCount2 = -1;

    SECTION("General"){
        SECTION("Simple"){
            queryString2 = "{WHAT: [ ['.name'] ],\
                            WHERE: ['COLLATE', {'unicode': true, 'case': false, 'diac': false},\
                                ['=', ['.name.first'], 'Mélanie']],\
                            ORDER_BY: [ ['COLLATE', {'unicode': true, 'case': false, 'diac': false},\
                                ['.name']] ]}";
            docsCount2   = 2;
        }
        SECTION("Aggregate"){
            queryString2 = "{WHAT: [ ['COLLATE', {'unicode': true, 'case': false, 'diac': false},\
                                ['.name']] ],\
                            DISTINCT: true,\
                            ORDER_BY: [ ['COLLATE', {'unicode': true, 'case': false, 'diac': false},\
                                ['.name']] ]}";
            docsCount2   = 102;
        }
    }

    SECTION("Collated Case-Insensitive"){
        SECTION("LIKE"){
            queryString  = "['COLLATE', {'unicode': true, 'case': false, 'diac': true}, ['LIKE', ['.name.first'], 'mel%']]";
            queryString2 = "['COLLATE', {'unicode': true, 'case': false, 'diac': true}, ['LIKE', ['.name.first'], 'mél%']]";
            docsCount    = 1;
            docsCount2   = 1;
        }
        SECTION("CONTAINS()"){
            queryString  = "['COLLATE', {'unicode': true, 'case': false, 'diac': true}, ['CONTAINS()', ['.name.first'], 'mel']]";
            queryString2 = "['COLLATE', {'unicode': true, 'case': false, 'diac': true}, ['CONTAINS()', ['.name.first'], 'mél']]";
            docsCount    = 1;
            docsCount2   = 1;
        }
    }

    SECTION("Collated Diacritic-Insensitive"){
        SECTION("LIKE"){
            queryString  = "['COLLATE', {'unicode': true, 'case': true, 'diac': false}, ['LIKE', ['.name.first'], 'Mél%']]";
            queryString2 = "['COLLATE', {'unicode': true, 'case': true, 'diac': false}, ['LIKE', ['.name.first'], 'mél%']]";
            docsCount    = 2;
            docsCount2   = 0;
        }
        SECTION("CONTAINS()"){
            queryString  = "['COLLATE', {'unicode': true, 'case': true, 'diac': false}, ['CONTAINS()', ['.name.first'], 'Mél']]";
            queryString2 = "['COLLATE', {'unicode': true, 'case': true, 'diac': false}, ['CONTAINS()', ['.name.first'], 'mél']]";
            docsCount    = 2;
            docsCount2   = 0;
        }
    }

    SECTION("Everything insensitive") {
        SECTION("LIKE"){
            queryString2 = "['COLLATE', {'unicode': true, 'case': false, 'diac': false}, ['LIKE', ['.name.first'], 'mél%']]";
            docsCount2   = 2;
        }
        SECTION("CONTAINS()"){
            queryString2 = "['COLLATE', {'unicode': true, 'case': false, 'diac': false}, ['CONTAINS()', ['.name.first'], 'mél']]";
            docsCount2   = 2;
        }
        
    }

    if(!queryString.empty() && docsCount != -1){
        query = CBLDatabase_CreateQuery(db, kCBLJSONLanguage, slice(queryString), &errPos, &error);
        REQUIRE(query);
        results = CBLQuery_Execute(query, &error);
        REQUIRE(results);
        CHECK(countResults(results) == docsCount);

        CBLQuery_Release(query);
        CBLResultSet_Release(results);
    }       
    
    if(!queryString2.empty() && docsCount2 != -1){
        query = CBLDatabase_CreateQuery(db, kCBLJSONLanguage, slice(queryString2), &errPos, &error);
        REQUIRE(query);
        results = CBLQuery_Execute(query, &error);
        REQUIRE(results);
        CHECK(countResults(results) == docsCount2);

        CBLQuery_Release(query);
        query = nullptr;
        CBLResultSet_Release(results);
        results = nullptr;
    }
}

TEST_CASE_METHOD(QueryTest, "Query Parameters", "[Query]") {
    CBLError error;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            cerr << "Creating index\n";
            CBLValueIndexConfiguration config = {};
            config.expressionLanguage = kCBLJSONLanguage;
            config.expressions = R"([".contact.address.zip"])"_sl;
            CHECK(CBLCollection_CreateValueIndex(defaultCollection, "zips"_sl, config, &error));
        }

        int errPos;
        query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                        "SELECT count(*) AS n FROM _ WHERE contact.address.zip BETWEEN $zip0 AND $zip1"_sl,
                                        &errPos, &error);
        REQUIRE(query);

        CHECK(CBLQuery_ColumnCount(query) == 1);
        CHECK(CBLQuery_ColumnName(query, 0) == "n"_sl);

        alloc_slice explanation(CBLQuery_Explain(query));
        cerr << string(explanation);

        CHECK(CBLQuery_Parameters(query) == nullptr);
        {
            auto params = MutableDict::newDict();
            params["zip0"] = "30000";
            params["zip1"] = "39999";
            CBLQuery_SetParameters(query, params);
        }

        FLDict params = CBLQuery_Parameters(query);
        CHECK(FLValue_AsString(FLDict_Get(params, "zip0"_sl)) == "30000"_sl);
        CHECK(FLValue_AsString(FLDict_Get(params, "zip1"_sl)) == "39999"_sl);

        results = CBLQuery_Execute(query, &error);
        REQUIRE(results);
        REQUIRE(CBLResultSet_Next(results));
        auto count = FLValue_AsInt( CBLResultSet_ValueAtIndex(results, 0) );
        CHECK(count == 7);
        CHECK(!CBLResultSet_Next(results));

        CBLQuery_Release(query);
        query = nullptr;
        CBLResultSet_Release(results);
        results = nullptr;
    }
}


TEST_CASE_METHOD(QueryTest, "Create and Delete Value Index", "[Query]") {
    CBLError error;
    int errPos;
    
    CBLValueIndexConfiguration index1 = {};
    index1.expressionLanguage = kCBLN1QLLanguage;
    index1.expressions = "name.first"_sl;
    CHECK(CBLCollection_CreateValueIndex(defaultCollection, "index1"_sl, index1, &error));
    
    CBLValueIndexConfiguration index2 = {};
    index2.expressionLanguage = kCBLJSONLanguage;
    index2.expressions = R"([[".name.last"]])"_sl;
    CHECK(CBLCollection_CreateValueIndex(defaultCollection, "index2"_sl, index2, &error));
    
    FLArray indexNames = CBLCollection_GetIndexNames(defaultCollection, &error);
    CHECK(FLArray_Count(indexNames) == 2);
    CHECK(Array(indexNames).toJSONString() == R"(["index1","index2"])");
    FLArray_Release(indexNames);
    
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name.first FROM _ ORDER BY name.first"_sl,
                                    &errPos, &error);
    
    alloc_slice explanation1(CBLQuery_Explain(query));
    CHECK(explanation1.find("USING COVERING INDEX index1"_sl));
    CBLQuery_Release(query);
    
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name.last FROM _ ORDER BY name.last"_sl,
                                    &errPos, &error);
    
    alloc_slice explanation2(CBLQuery_Explain(query));
    CHECK(explanation2.find("USING COVERING INDEX index2"_sl));
    CBLQuery_Release(query);
    query = nullptr;
    
    CHECK(CBLCollection_DeleteIndex(defaultCollection, "index1"_sl, &error));
    CHECK(CBLCollection_DeleteIndex(defaultCollection, "index2"_sl, &error));
    
    indexNames = CBLCollection_GetIndexNames(defaultCollection, &error);
    CHECK(FLArray_Count(indexNames) == 0);
    CHECK(Array(indexNames).toJSONString() == R"([])");
    FLArray_Release(indexNames);
}


TEST_CASE_METHOD(QueryTest, "Create and Delete Full-Text Index", "[Query]") {
    CBLError error;
    int errPos;

    CBLFullTextIndexConfiguration index1 = {};
    index1.expressionLanguage = kCBLN1QLLanguage;
    index1.expressions = "product.description"_sl;
    index1.ignoreAccents = true;
    CHECK(CBLCollection_CreateFullTextIndex(defaultCollection, "index1"_sl, index1, &error));
    
    CBLFullTextIndexConfiguration index2 = {};
    index2.expressionLanguage = kCBLJSONLanguage;
    index2.expressions = R"([[".product.summary"]])"_sl;
    index2.ignoreAccents = false;
    index2.language = "en/english"_sl;
    CHECK(CBLCollection_CreateFullTextIndex(defaultCollection, "index2"_sl, index2, &error));
    
    FLArray indexNames = CBLCollection_GetIndexNames(defaultCollection, &error);
    CHECK(FLArray_Count(indexNames) == 2);
    CHECK(Array(indexNames).toJSONString() == R"(["index1","index2"])");
    FLArray_Release(indexNames);
    
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT product.name FROM _ WHERE match(index1, 'avocado')"_sl,
                                    &errPos, &error);
    
    alloc_slice explanation1(CBLQuery_Explain(query));
    CHECK(explanation1.find("JOIN \"kv_default::index1\" AS \"<idx1>\""));
    CHECK(explanation1.find("SCAN <idx1> VIRTUAL TABLE INDEX"_sl));
    CBLQuery_Release(query);
    
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT product.name FROM _ WHERE match(index2, 'chilli')"_sl,
                                    &errPos, &error);
    
    alloc_slice explanation2(CBLQuery_Explain(query));
    CHECK(explanation2.find("JOIN \"kv_default::index2\" AS \"<idx1>\""));
    CHECK(explanation2.find("SCAN <idx1> VIRTUAL TABLE INDEX"_sl));
    CBLQuery_Release(query);
    query = nullptr;
    
    CHECK(CBLCollection_DeleteIndex(defaultCollection, "index1"_sl, &error));
    CHECK(CBLCollection_DeleteIndex(defaultCollection, "index2"_sl, &error));
    
    indexNames = CBLCollection_GetIndexNames(defaultCollection, &error);
    CHECK(FLArray_Count(indexNames) == 0);
    CHECK(Array(indexNames).toJSONString() == R"([])");
    FLArray_Release(indexNames);
}


TEST_CASE_METHOD(QueryTest, "Query Result As Dict", "[Query]") {
    CBLError error;
    int errPos;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name FROM _ WHERE birthday like '1959-%' ORDER BY birthday"_sl,
                                    &errPos, &error);
    REQUIRE(query);

    CHECK(CBLQuery_ColumnCount(query) == 1);
    CHECK(CBLQuery_ColumnName(query, 0) == "name"_sl);

    alloc_slice explanation(CBLQuery_Explain(query));
    cerr << string(explanation);

    static const slice kExpectedFirst[3] = {"Tyesha",  "Eddie",     "Diedre"};
    static const slice kExpectedLast [3] = {"Loehrer", "Colangelo", "Clinton"};

    int n = 0;
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);
    while (CBLResultSet_Next(results)) {
        FLDict result = CBLResultSet_ResultDict(results);
        FLValue name = FLDict_Get(result, "name"_sl);
        FLDict dict = FLValue_AsDict(name);
        CHECK(dict);
        slice first  = FLValue_AsString(FLDict_Get(dict, "first"_sl));
        slice last   = FLValue_AsString(FLDict_Get(dict, "last"_sl));
        REQUIRE(n < 3);
        CHECK(first == kExpectedFirst[n]);
        CHECK(last == kExpectedLast[n]);
        ++n;
        cerr << first << " " << last << "\n";
    }
    CHECK(n == 3);
}


TEST_CASE_METHOD(QueryTest, "Query Result As Array", "[Query]") {
    CBLError error;
    int errPos;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name, foo FROM _ WHERE birthday like '1959-%' ORDER BY birthday"_sl,
                                    &errPos, &error);
    REQUIRE(query);

    REQUIRE(CBLQuery_ColumnCount(query) == 2);
    CHECK(CBLQuery_ColumnName(query, 0) == "name"_sl);
    CHECK(CBLQuery_ColumnName(query, 1) == "foo"_sl);

    alloc_slice explanation(CBLQuery_Explain(query));
    cerr << string(explanation);

    static const slice kExpectedFirst[3] = {"Tyesha",  "Eddie",     "Diedre"};
    static const slice kExpectedLast [3] = {"Loehrer", "Colangelo", "Clinton"};

    int n = 0;
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);
    while (CBLResultSet_Next(results)) {
        FLArray result = CBLResultSet_ResultArray(results);
        REQUIRE(FLArray_Count(result) == 2);
        FLValue name = FLArray_Get(result, 0);
        FLDict dict = FLValue_AsDict(name);
        CHECK(dict);
        slice first  = FLValue_AsString(FLDict_Get(dict, "first"_sl));
        slice last   = FLValue_AsString(FLDict_Get(dict, "last"_sl));
        REQUIRE(n < 3);
        CHECK(first == kExpectedFirst[n]);
        CHECK(last == kExpectedLast[n]);
        cerr << first << " " << last << "\n";
        FLValue foo = FLArray_Get(result, 1);
        CHECK(FLValue_GetType(foo) == kFLUndefined);
        ++n;
    }
    CHECK(n == 3);
}


TEST_CASE_METHOD(QueryTest, "Query Listener", "[Query][LiveQuery]") {
    CBLError error;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name FROM _ WHERE birthday like '1959-%' ORDER BY birthday"_sl,
                                    nullptr, &error);
    REQUIRE(query);

    CBLResultSet *results = CBLQuery_Execute(query, &error);
    CHECK(countResults(results) == 3);
    CBLResultSet_Release(results);
    
    cerr << "Adding listener\n";
    ListenerState state;
    CBLListenerToken* listenerToken = CBLQuery_AddChangeListener(query, [](void *context, CBLQuery* query, CBLListenerToken* token) {
        ((ListenerState*)context)->receivedCallback(context, query, token);
    }, &state);

    cerr << "Waiting for listener...\n";
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 3);
    
    cerr << "Deleting a doc...\n";
    state.reset();
    const CBLDocument *doc = CBLCollection_GetDocument(defaultCollection, "0000012"_sl, &error);
    REQUIRE(doc);
    CHECK(CBLCollection_DeleteDocument(defaultCollection, doc, &error));
    CBLDocument_Release(doc);

    cerr << "Waiting for listener again...\n";
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 2);
    
    // https://issues.couchbase.com/browse/CBL-2117
    // Remove listener token and sleep to ensure async cleanup in LiteCore's LiveQuerier's _stop()
    // functions is done before checking instance leaking in CBLTest's destructor:
    CBLListener_Remove(listenerToken);
    listenerToken = nullptr;
    cerr << "Sleeping to ensure async cleanup ..." << endl;
    this_thread::sleep_for(500ms);
}


TEST_CASE_METHOD(QueryTest, "Remove Query Listener", "[Query][LiveQuery]") {
    CBLError error;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name FROM _ WHERE birthday like '1959-%' ORDER BY birthday"_sl,
                                    nullptr, &error);
    REQUIRE(query);
    
    cerr << "Adding listener\n";
    ListenerState state;
    CBLListenerToken*  listenerToken = CBLQuery_AddChangeListener(query, [](void *context, CBLQuery* query, CBLListenerToken* token) {
        ((ListenerState*)context)->receivedCallback(context, query, token);
    }, &state);

    cerr << "Waiting for listener...\n";
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 3);
    
    cerr << "Removing the listener...\n";
    CBLListener_Remove(listenerToken);
    
    cerr << "Deleting a doc...\n";
    const CBLDocument *doc = CBLCollection_GetDocument(defaultCollection, "0000012"_sl, &error);
    REQUIRE(doc);
    CHECK(CBLCollection_DeleteDocument(defaultCollection, doc, &error));
    CBLDocument_Release(doc);
    
    cerr << "Sleeping to ensure that the listener callback is not called..." << endl;
    this_thread::sleep_for(1000ms); // Max delay before refreshing result in LiteCore is 500ms
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 3);
    
    // Cleanup:
    listenerToken = nullptr;
    cerr << "Sleeping to ensure async cleanup ..." << endl;
    this_thread::sleep_for(500ms);
}


#ifdef DEBUG
TEST_CASE_METHOD(QueryTest, "Remove Query Listener with Delay Notification", "[Query][LiveQuery][CBL-5661]") {
    CBLError error;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name FROM _ WHERE birthday like '1959-%' ORDER BY birthday"_sl,
                                    nullptr, &error);
    REQUIRE(query);
    
    cerr << "Adding listener\n";
    ListenerState state;
    CBLListenerToken* listenerToken = CBLQuery_AddChangeListener(query, [](void *context, CBLQuery* query, CBLListenerToken* token) {
        ((ListenerState*)context)->receivedCallback(context, query, token);
    }, &state);

    cerr << "Waiting for listener...\n";
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 3);
    
    cerr << "Set a delay in listener callback\n";
    CBLQuery_SetListenerCallbackDelay(2000);
    
    cerr << "Deleting a doc...\n";
    const CBLDocument *doc = CBLCollection_GetDocument(defaultCollection, "0000012"_sl, &error);
    REQUIRE(doc);
    CHECK(CBLCollection_DeleteDocument(defaultCollection, doc, &error));
    CBLDocument_Release(doc);
    
    cerr << "Removing the listener...\n";
    this_thread::sleep_for(1000ms); // Max delay before refreshing result in LiteCore is 500ms
    CBLListener_Remove(listenerToken);
    
    cerr << "Sleeping to ensure that the listener callback is not called..." << endl;
    this_thread::sleep_for(3000ms); 
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 3);
    
    // Cleanup:
    listenerToken = nullptr;
    cerr << "Sleeping to ensure async cleanup ..." << endl;
    this_thread::sleep_for(500ms);
}
#endif


TEST_CASE_METHOD(QueryTest, "Query Listener and Changing parameters", "[Query][LiveQuery]") {
    CBLError error;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name FROM _ WHERE birthday like $dob ORDER BY birthday"_sl,
                                    nullptr, &error);
    REQUIRE(query);
    
    auto params = MutableDict::newDict();
    params["dob"] = "1959-%";
    CBLQuery_SetParameters(query, params);

    cerr << "Adding listener\n";
    ListenerState state;
    CBLListenerToken* listenerToken = CBLQuery_AddChangeListener(query, [](void *context, CBLQuery* query, CBLListenerToken* token) {
        ((ListenerState*)context)->receivedCallback(context, query, token);
    }, &state);

    cerr << "Waiting for listener...\n";
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 3);
    
    cerr << "Changing parameters\n";
    state.reset();
    params = MutableDict::newDict();
    params["dob"] = "1977-%";
    CBLQuery_SetParameters(query, params);

    cerr << "Waiting for listener again...\n";
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 2);
    
    CBLListener_Remove(listenerToken);
    listenerToken = nullptr;
    cerr << "Sleeping to ensure async cleanup ..." << endl;
    this_thread::sleep_for(500ms);
}


TEST_CASE_METHOD(QueryTest, "Multiple Query Listeners", "[Query][LiveQuery]") {
    CBLError error;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name FROM _ WHERE birthday like '1959-%' ORDER BY birthday"_sl,
                                    nullptr, &error);
    REQUIRE(query);
    
    auto callback = [](void *context, CBLQuery* query, CBLListenerToken* token) {
        ((ListenerState*)context)->receivedCallback(context, query, token);
    };
    
    cerr << "Adding listener\n";
    ListenerState state1;
    CBLListenerToken* token1 = CBLQuery_AddChangeListener(query, callback, &state1);
    
    ListenerState state2;
    CBLListenerToken* token2 = CBLQuery_AddChangeListener(query, callback, &state2);

    cerr << "Waiting for listener 1...\n";
    state1.waitForCount(1);
    CHECK(state1.resultCount() == 3);
    
    cerr << "Waiting for listener 2...\n";
    state2.waitForCount(1);
    CHECK(state2.resultCount() == 3);

    cerr << "Deleting a doc...\n";
    state1.reset();
    state2.reset();
    const CBLDocument *doc = CBLCollection_GetDocument(defaultCollection, "0000012"_sl, &error);
    REQUIRE(doc);
    CHECK(CBLCollection_DeleteDocument(defaultCollection, doc, &error));
    CBLDocument_Release(doc);

    cerr << "Waiting for listener 1 again...\n";
    state1.waitForCount(1);
    CHECK(state1.resultCount() == 2);
    
    cerr << "Waiting for listener 2 again...\n";
    state2.waitForCount(1);
    CHECK(state2.resultCount() == 2);
    
    cerr << "Adding another listener\n";
    ListenerState state3;
    CBLListenerToken* token3 = CBLQuery_AddChangeListener(query, callback, &state3);
    
    cerr << "Waiting for the listener 3...\n";
    state3.waitForCount(1);
    CHECK(state3.resultCount() == 2);
    CHECK(state1.count() == 1);
    CHECK(state2.count() == 1);
    
    CBLListener_Remove(token1);
    CBLListener_Remove(token2);
    CBLListener_Remove(token3);
    
    token1 = nullptr;
    token2 = nullptr;
    token3 = nullptr;
    
    cerr << "Sleeping to ensure async cleanup ..." << endl;
    this_thread::sleep_for(500ms);
}


TEST_CASE_METHOD(QueryTest, "Query Listener and Coalescing notification", "[Query][LiveQuery]") {
    CBLError error;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT name FROM _ WHERE birthday like '1959-%' ORDER BY birthday"_sl,
                                    nullptr, &error);
    REQUIRE(query);
    
    cerr << "Adding listener\n";
    ListenerState state;
    CBLListenerToken* listenerToken = CBLQuery_AddChangeListener(query, [](void *context, CBLQuery* query, CBLListenerToken* token) {
        ((ListenerState*)context)->receivedCallback(context, query, token);
    }, &state);

    cerr << "Waiting for listener...\n";
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 3);
    
    cerr << "Deleting a doc...\n";
    state.reset();
    REQUIRE(CBLCollection_DeleteDocumentByID(defaultCollection, "0000012"_sl, &error));
    REQUIRE(CBLCollection_DeleteDocumentByID(defaultCollection, "0000046"_sl, &error));

    cerr << "Sleeping to see if the notification is coalesced ...\n";
    this_thread::sleep_for(2000ms); // Max delay before refreshing result in LiteCore is 500ms
    REQUIRE(state.waitForCount(1));
    CHECK(state.resultCount() == 1);
    
    // https://issues.couchbase.com/browse/CBL-2117
    // Remove listener token and sleep to ensure async cleanup in LiteCore's LiveQuerier's _stop()
    // functions is done before checking instance leaking in CBLTest's destructor:
    CBLListener_Remove(listenerToken);
    listenerToken = nullptr;
    cerr << "Sleeping to ensure async cleanup ..." << endl;
    this_thread::sleep_for(500ms);
}

TEST_CASE_METHOD(QueryTest, "Query Default Collection", "[Query]") {
    string queryString;
    
    SECTION("Use _") {
        queryString = "SELECT name.first FROM _ ORDER BY name.first LIMIT 1";
    }
    
    SECTION("Use _default") {
        queryString = "SELECT name.first FROM _default ORDER BY name.first LIMIT 1";
    }
    
    SECTION("Use _default._default") {
        queryString = "SELECT name.first FROM _default._default ORDER BY name.first LIMIT 1";
    }
    
    SECTION("Use database name") {
        queryString = "SELECT name.first FROM ";
        queryString += slice(CBLDatabase_Name(db)).asString();
        queryString += " ORDER BY name.first LIMIT 1";
    }
    
    CBLError error;
    int errPos;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(query);
    
    int n = 0;
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);
    
    while (CBLResultSet_Next(results)) {
        FLString name = FLValue_AsString(CBLResultSet_ValueForKey(results, "first"_sl));
        REQUIRE(name);
        CHECK(slice(name) == "Abe");
        ++n;
    }
    CHECK(n == 1);
}

TEST_CASE_METHOD(QueryTest, "Query Collection in Default Scope", "[.CBL-3538]") {
    auto col = CreateCollection(db, "colA");
    ImportJSONLines("names_100.json", col);
    CBLCollection_Release(col);
    
    string queryString;
    
    SECTION("Use _default.COLLECTION_NAME") {
        queryString = "SELECT name.first FROM _default.colA ORDER BY name.first LIMIT 1";
    }
    
    SECTION("Use COLLECTION_NAME") {
        queryString = "SELECT name.first FROM colA ORDER BY name.first LIMIT 1";
    }
    
    CBLError error;
    int errPos;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(query);
    
    int n = 0;
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);
    
    while (CBLResultSet_Next(results)) {
        FLString name = FLValue_AsString(CBLResultSet_ValueForKey(results, "first"_sl));
        REQUIRE(name);
        CHECK(slice(name) == "Abe");
        ++n;
    }
    CHECK(n == 1);
}

TEST_CASE_METHOD(QueryTest, "Query Named Collection and Scope", "[Query]") {
    auto col = CreateCollection(db, "colA", "scopeA");
    ImportJSONLines("names_100.json", col);
    CBLCollection_Release(col);
    
    string queryString = "SELECT name.first FROM scopeA.colA ORDER BY name.first LIMIT 1";
    
    CBLError error;
    int errPos;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(query);

    int n = 0;
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);

    while (CBLResultSet_Next(results)) {
        FLString name = FLValue_AsString(CBLResultSet_ValueForKey(results, "first"_sl));
        REQUIRE(name);
        CHECK(slice(name) == "Abe");
        ++n;
    }
    CHECK(n == 1);
}

TEST_CASE_METHOD(QueryTest, "Create Query with Different Collection Name Cases Failed", "[Query]") {
    auto col = CreateCollection(db, "colA", "scopeA");
    ImportJSONLines("names_100.json", col);
    CBLCollection_Release(col);
    
    string queryString = "SELECT name.first FROM ScOpEa.CoLa ORDER BY name.first LIMIT 1";
    
    CBLError error;
    int errPos;
    ExpectingExceptions ex;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(!query);
    CheckError(error, kCBLErrorInvalidQuery);
}

TEST_CASE_METHOD(QueryTest, "Query Non Existing Collection", "[Query]") {
    string queryString = "SELECT name.first FROM scopeA.colA ORDER BY name.first LIMIT 1";
    
    CBLError error;
    int errPos;
    ExpectingExceptions ex;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(!query);
    CheckError(error, kCBLErrorInvalidQuery);
}

TEST_CASE_METHOD(QueryTest, "Test Joins with Collections", "[Query]") {
    auto flowers = CreateCollection(db, "flowers", "test");
    auto colors = CreateCollection(db, "colors", "test");
    
    createDocWithJSON(flowers, "flower1", "{\"name\":\"rose\",\"cid\":\"c1\"}");
    createDocWithJSON(flowers, "flower2", "{\"name\":\"hydrangea\",\"cid\":\"c2\"}");
    
    createDocWithJSON(colors, "color1", "{\"cid\":\"c1\",\"color\":\"red\"}");
    createDocWithJSON(colors, "color2", "{\"cid\":\"c2\",\"color\":\"blue\"}");
    createDocWithJSON(colors, "color3", "{\"cid\":\"c3\",\"color\":\"white\"}");
    
    CBLCollection_Release(flowers);
    CBLCollection_Release(colors);

    string queryString;
    
    SECTION("Use Full Collection Name") {
        queryString = "SELECT flowers.name, colors.color "
                      "FROM test.flowers "
                      "JOIN test.colors "
                      "ON flowers.cid = colors.cid "
                      "ORDER BY flowers.name";
    }
    
    SECTION("Use Alias Name") {
        queryString = "SELECT f.name, c.color "
                      "FROM test.flowers f "
                      "JOIN test.colors c "
                      "ON f.cid = c.cid "
                      "ORDER BY f.name";
    }

    CBLError error;
    int errPos;
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(query);

    static const slice kExpectedNames[2] = {"hydrangea", "rose"};
    static const slice kExpectedColors[2] = {"blue", "red"};

    int n = 0;
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);

    while (CBLResultSet_Next(results)) {
        FLString name = FLValue_AsString(CBLResultSet_ValueForKey(results, "name"_sl));
        REQUIRE(name);
        CHECK(slice(name) == kExpectedNames[n]);

        FLString color = FLValue_AsString(CBLResultSet_ValueForKey(results, "color"_sl));
        REQUIRE(color);
        CHECK(slice(color) == kExpectedColors[n]);

        ++n;
    }
    CHECK(n == 2);
}

TEST_CASE_METHOD(QueryTest, "FTS with FTS Index in Default Collection", "[Query]"){
    CBLError error;
    int errPos;
    int n = 0;
    string queryString;

    CBLFullTextIndexConfiguration index = {};
    index.expressionLanguage = kCBLN1QLLanguage;
    index.expressions = "name.first"_sl;
    index.ignoreAccents = false;
    CHECK(CBLCollection_CreateFullTextIndex(defaultCollection, "index"_sl, index, &error));

    SECTION("name"){
        queryString = "SELECT name "
                      "FROM _default "
                      "WHERE match(index, 'Jasper') "
                      "ORDER BY rank(index) ";
    }
    SECTION("_.name"){
        queryString = "SELECT name "
                      "FROM _ "
                      "WHERE match(_.index, 'Jasper') "
                      "ORDER BY rank(_.index) ";
    }
    SECTION("_default.name"){
        queryString = "SELECT name "
                      "FROM _default "
                      "WHERE match(_default.index, 'Jasper') "
                      "ORDER BY rank(_default.index) ";
    }
    SECTION("db.name"){
        string dbName = slice(CBLDatabase_Name(db)).asString();
        queryString = "SELECT name FROM ";
        queryString += dbName;
        queryString += " WHERE match(";
        queryString += dbName;
        queryString += ".index, 'Jasper') "
                       "ORDER BY rank(";
        queryString += dbName;
        queryString += ".index) ";
    }
    SECTION("alias.name"){
        queryString = "SELECT name "
                      "FROM _default as d "
                      "WHERE match(d.index, 'Jasper') "
                      "ORDER BY rank(d.index) ";
    }

    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(query);
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);

    static const slice kExpectedLast[2] = {"Grebel", "Okorududu"};
    while (CBLResultSet_Next(results))
    {
        FLDict result = CBLResultSet_ResultDict(results);
        FLValue name = FLDict_Get(result, "name"_sl);
        FLDict dict = FLValue_AsDict(name);
        CHECK(dict);
        slice last   = FLValue_AsString(FLDict_Get(dict, "last"_sl));
        CHECK(last == kExpectedLast[n]);
        cerr << last << "\n";
        ++n;
    }
    CHECK(n == 2);
}

TEST_CASE_METHOD(QueryTest, "FTS with FTS Index in Named Collection", "[Query]"){
    CBLError error;
    int errPos;
    int n = 0;
    string queryString;

    auto people = CreateCollection(db, "people", "test");
    createDocWithJSON(people, "person1", "{\"name\": { \"first\": \"Jasper\",\"last\":\"Grebel\"}, \"random\": \"4\"}");
    createDocWithJSON(people, "person2", "{\"name\": { \"first\": \"Jasper\",\"last\":\"Okorududu\"}, \"random\": \"1\"}");
    createDocWithJSON(people, "person3", "{\"name\": { \"first\": \"Monica\",\"last\":\"Polina\"}, \"random\": \"2\"}");

    CBLFullTextIndexConfiguration index = {};
    index.expressionLanguage = kCBLN1QLLanguage;
    index.expressions = "name.first"_sl;
    index.ignoreAccents = false;
    CHECK(CBLCollection_CreateFullTextIndex(people, "index"_sl, index, &error));

    CBLCollection_Release(people);
    
    SECTION("name"){
        queryString = "SELECT name "
                      "FROM test.people "
                      "WHERE match(index, 'Jasper') "
                      "ORDER BY rank(index) ";
    }
    SECTION("collection.name"){
        queryString = "SELECT name "
                      "FROM test.people "
                      "WHERE match(people.index, 'Jasper') "
                      "ORDER BY rank(people.index) ";
    }
    SECTION("alias.name"){
        queryString = "SELECT name "
                      "FROM test.people as p "
                      "WHERE match(p.index, 'Jasper') "
                      "ORDER BY rank(p.index) ";
    }

    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(query);
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);

    static const slice kExpectedLast[2] = {"Grebel", "Okorududu"};
    while (CBLResultSet_Next(results))
    {
        FLDict result = CBLResultSet_ResultDict(results);
        FLValue name = FLDict_Get(result, "name"_sl);
        FLDict dict = FLValue_AsDict(name);
        CHECK(dict);
        slice last   = FLValue_AsString(FLDict_Get(dict, "last"_sl));
        CHECK(last == kExpectedLast[n]);
        cerr << last << "\n";
        ++n;
    }
    CHECK(n == 2);
}

TEST_CASE_METHOD(QueryTest, "FTS Join with Collections", "[Query]"){
    CBLError error;
    int errPos;
    int n = 0;
    
    auto flowers = CreateCollection(db, "flowers", "test");
    auto colors = CreateCollection(db, "colors", "test");

    createDocWithJSON(flowers, "flower1", "{\"name\":\"rose\",\"description\":\"Red flowers\",\"cid\":\"c1\"}");
    createDocWithJSON(flowers, "flower2", "{\"name\":\"hydrangea\",\"description\":\"Blue flowers\",\"cid\":\"c2\"}");
    createDocWithJSON(colors, "color1", "{\"cid\":\"c1\",\"color\":\"red\"}");
    createDocWithJSON(colors, "color2", "{\"cid\":\"c2\",\"color\":\"blue\"}");
    createDocWithJSON(colors, "color3", "{\"cid\":\"c3\",\"color\":\"white\"}");

    CBLFullTextIndexConfiguration descIndex = {};
    descIndex.expressionLanguage = kCBLN1QLLanguage;
    descIndex.expressions = "description"_sl;
    descIndex.ignoreAccents = false;
    CHECK(CBLCollection_CreateFullTextIndex(flowers, "descIndex"_sl, descIndex, &error));
    
    CBLCollection_Release(flowers);
    CBLCollection_Release(colors);

    string queryString = "SELECT f.name, f.description, c.color "
                         "FROM test.flowers as f "
                         "JOIN test.colors as c "
                         "ON f.cid = c.cid "
                         "WHERE match(f.descIndex, 'red') "
                         "ORDER BY f.name ";
    
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), &errPos, &error);
    REQUIRE(query);
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);
    
    while (CBLResultSet_Next(results))
    {
        auto result = CBLResultSet_ResultDict(results);
        CHECK(Dict(result).toJSONString() == "{\"color\":\"red\",\"description\":\"Red flowers\",\"name\":\"rose\"}");
        ++n;
    }
    CHECK(n == 1);
}

TEST_CASE_METHOD(QueryTest, "Test Select All Result Key", "[Query]") {
    CBLError error {};
    auto flowers = CreateCollection(db, "flowers", "test");
    auto defaultCol = CBLDatabase_DefaultCollection(db, &error);
    
    createDocWithJSON(flowers, "flower1", "{\"name\":\"rose\",\"cid\":\"c1\"}");
    createDocWithJSON(defaultCol, "flower1", "{\"name\":\"rose\",\"cid\":\"c1\"}");
    
    CBLCollection_Release(flowers);
    CBLCollection_Release(defaultCol);
    
    const string froms[5] = {
        slice(CBLDatabase_Name(db)).asString(),
        "_",
        "_default._default",
        "test.flowers",
        "test.flowers as f"
    };
    
    const string expectedKeyNames[5] = {
        slice(CBLDatabase_Name(db)).asString(),
        "_",
        "_default",
        "flowers",
        "f"
    };
    
    int i = 0;
    for (string from : froms) {
        error = {};
        int errPos;
        auto query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice("SELECT * FROM " + from),
                                             &errPos, &error);
        REQUIRE(query);
        
        auto results = CBLQuery_Execute(query, &error);
        REQUIRE(results);
        
        CHECK(CBLResultSet_Next(results));
        FLValue value = CBLResultSet_ValueForKey(results, slice(expectedKeyNames[i++]));
        CHECK(value);
        CHECK(FLValue_GetType(value) == kFLDict);
        
        CBLResultSet_Release(results);
        CBLQuery_Release(query);
    }
}

#pragma mark - ArrayIndex / Unnest

// Spec: https://github.com/couchbaselabs/couchbase-lite-api/blob/master/spec/tests/T0004-Unnest-Array-Index.md
// Version: 1.0.1
//
// Notes:
// * 1. TestArrayIndexConfigInvalidExpressions is not applicable for CBL-C as
//      ArrayIndexConfigurationConfiguration is just a pure struct (no validation logic)
//

/**
 2. TestCreateArrayIndexWithPath

 Description
 Test that creating an array index with only path works as expected.

 Steps
 1. Load profiles.json into the collection named "_default.profiles".
 2. Create a ArrayIndexConfiguration object.
     - path: "contacts"
     - expressions:  null
 3. Create an array index named "contacts" in the profiles collection.
 4. Get index names from the profiles collection and check that the index named "contacts" exists.
 5. Get info of the index named "contacts" using an internal API and check that the index has
     path and expressions as configured.
 */
TEST_CASE_METHOD(QueryTest, "TestCreateArrayIndexWithPath", "[Query]") {
    auto collection = CreateCollection(db, "profiles");
    ImportJSONLines("profiles_100.json", collection);
    
    CBLArrayIndexConfiguration config {};
    config.path = "contacts"_sl;
    
    SECTION("SQL++") {
        config.expressionLanguage = kCBLN1QLLanguage;
    }
    
    SECTION("JSON") {
        config.expressionLanguage = kCBLJSONLanguage;
    }
    
    CBLError error {};
    CHECK(CBLCollection_CreateArrayIndex(collection, "contacts"_sl, config, &error));
    CheckNoError(error);
    
    auto indexNames = CBLCollection_GetIndexNames(collection, &error);
    CHECK(FLArray_Count(indexNames) == 1);
    CHECK(Array(indexNames).toJSONString() == R"(["contacts"])");
    FLArray_Release(indexNames);
    
    auto indexesInfo = CBLCollection_GetIndexesInfo(collection, &error);
    CheckNoError(error);
    CHECK(FLArray_Count(indexesInfo) == 1);
    auto info = FLValue_AsDict(FLArray_Get(indexesInfo, 0));
    CHECK(info);
    auto expr = FLValue_AsString(FLDict_Get(info, "expr"_sl));
    if (config.expressionLanguage == kCBLN1QLLanguage) {
        CHECK(expr == kFLSliceNull);
    } else {
        CHECK(expr == "[]"_sl);
    }
    FLArray_Release(indexesInfo);
    
    CBLCollection_Release(collection);
}

/**
 3. TestCreateArrayIndexWithPathAndExpressions

 Description
 Test that creating an array index with path and expressions works as expected.

 Steps
 1. Load profiles.json into the collection named "_default.profiles".
 2. Create a ArrayIndexConfiguration object.
     - path: "contacts"
     - expressions:  ["address.city", "address.state"]
 3. Create an array index named "contacts" in the profiles collection.
 4. Get index names from the profiles collection and check that the index named "contacts" exists.
 5. Get info of the index named "contacts" using an internal API and check that the index has
     path and expressions as configured.
 */
TEST_CASE_METHOD(QueryTest, "TestCreateArrayIndexWithPathAndExpressions", "[Query]") {
    auto collection = CreateCollection(db, "profiles");
    ImportJSONLines("profiles_100.json", collection);
    
    CBLArrayIndexConfiguration config {};
    config.path = "contacts"_sl;
    
    SECTION("SQL++") {
        config.expressionLanguage = kCBLN1QLLanguage;
        config.expressions = "address.city, address.state"_sl;
    }
    
    SECTION("JSON") {
        config.expressionLanguage = kCBLJSONLanguage;
        config.expressions = R"([[".address.city"], [".address.state"]])"_sl;
    }
    
    CBLError error {};
    CHECK(CBLCollection_CreateArrayIndex(collection, "contacts"_sl, config, &error));
    CheckNoError(error);
    
    auto indexNames = CBLCollection_GetIndexNames(collection, &error);
    CHECK(FLArray_Count(indexNames) == 1);
    CHECK(Array(indexNames).toJSONString() == R"(["contacts"])");
    FLArray_Release(indexNames);
    
    auto indexesInfo = CBLCollection_GetIndexesInfo(collection, &error);
    CheckNoError(error);
    CHECK(FLArray_Count(indexesInfo) == 1);
    auto info = FLValue_AsDict(FLArray_Get(indexesInfo, 0));
    CHECK(info);
    auto expr = FLValue_AsString(FLDict_Get(info, "expr"_sl));
    if (config.expressionLanguage == kCBLN1QLLanguage) {
        CHECK(expr == "address.city, address.state"_sl);
    } else {
        CHECK(expr == R"([[".address.city"], [".address.state"]])"_sl);
    }
    FLArray_Release(indexesInfo);
    
    CBLCollection_Release(collection);
}

#pragma mark - Partial Index

// Spec: https://github.com/couchbaselabs/couchbase-lite-api/blob/master/spec/tests/T0007-Partial-Index.md
// Version: 1.0.2

TEST_CASE_METHOD(QueryTest, "TestCreatePartialValueIndex", "[Query]") {
    CBLValueIndexConfiguration config {};
    
    SECTION("SQL++") {
        config.expressionLanguage = kCBLN1QLLanguage;
        config.expressions = "num"_sl;
        config.where = "type = 'number'"_sl;
    }
    
    SECTION("JSON") {
        config.expressionLanguage = kCBLJSONLanguage;
        config.expressions = R"([[".num"]])"_sl;
        config.where = R"(["=",[".type"],"number"])"_sl;
    }
    
    CBLError error {};
    CHECK(CBLCollection_CreateValueIndex(defaultCollection, "num-index"_sl, config, &error));
    CheckNoError(error);
    
    string queryString = "SELECT * FROM _ WHERE type = 'number' AND number > 1000";
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), nullptr, &error);
    REQUIRE(query);
    
    alloc_slice exp = CBLQuery_Explain(query);
    CHECK(exp.find("USING INDEX num-index"_sl));
    
    CBLQuery_Release(query);
    
    queryString = "SELECT * FROM _ WHERE type = 'foo' AND number > 1000";
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), nullptr, &error);
    REQUIRE(query);
    
    exp = CBLQuery_Explain(query);
    CHECK(!exp.find("USING INDEX num-index"_sl));
}

TEST_CASE_METHOD(QueryTest, "TestCreatePartialFullTextIndex", "[Query]") {
    createDocWithJSON(defaultCollection, "doc1", "{\"content\": \"Couchbase Lite is a database.\"}");
    createDocWithJSON(defaultCollection, "doc2", "{\"content\": \"Couchbase Lite is a NoSQL syncable database.\"}");

    CBLFullTextIndexConfiguration config {};
    
    SECTION("SQL++") {
        config.expressionLanguage = kCBLN1QLLanguage;
        config.expressions = "content"_sl;
        config.where = "length(content) > 30"_sl;
    }
    
    SECTION("JSON") {
        config.expressionLanguage = kCBLJSONLanguage;
        config.expressions = R"([[".content"]])"_sl;
        config.where = R"-([">", ["length()", [".content"]], 30])-"_sl;
    }

    CBLError error {};
    CHECK(CBLCollection_CreateFullTextIndex(defaultCollection, "contentIndex"_sl, config, &error));
    CheckNoError(error);
    
    string queryString = "SELECT content FROM _ WHERE match(contentIndex, 'database')";
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(queryString), nullptr, &error);
    REQUIRE(query);
    CheckNoError(error);
    
    results = CBLQuery_Execute(query, &error);
    REQUIRE(results);

    int n = 0;
    while (CBLResultSet_Next(results))
    {
        FLDict result = CBLResultSet_ResultDict(results);
        slice content = FLValue_AsString(FLDict_Get(result, "content"_sl));
        CHECK(content == "Couchbase Lite is a NoSQL syncable database.");
        ++n;
    }
    CHECK(n == 1);
}

#ifdef COUCHBASE_ENTERPRISE

TEST_CASE_METHOD(QueryTest, "Query Encryptable", "[Query]") {
    auto doc = CBLDocument_CreateWithID("doc1"_sl);
    auto props = CBLDocument_MutableProperties(doc);
    
    FLSlot_SetString(FLMutableDict_Set(props, "nosecret"_sl), "No Secret"_sl);
    
    auto secret1 = CBLEncryptable_CreateWithString("Secret 1"_sl);
    FLSlot_SetEncryptableValue(FLMutableDict_Set(props, "secret"_sl), secret1);
    
    auto nestedDict = FLMutableDict_New();
    auto secret2 = CBLEncryptable_CreateWithString("Secret 2"_sl);
    FLSlot_SetEncryptableValue(FLMutableDict_Set(nestedDict, "secret2"_sl), secret2);
    FLSlot_SetDict(FLMutableDict_Set(props, "nested"_sl), nestedDict);
    
    CBLError error;
    REQUIRE(CBLCollection_SaveDocument(defaultCollection, doc, &error));
    CBLEncryptable_Release(secret1);
    CBLEncryptable_Release(secret2);
    FLMutableDict_Release(nestedDict);
    CBLDocument_Release(doc);
    
    query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage,
                                    "SELECT nosecret, secret, secret.value, nested FROM _ WHERE meta().id = 'doc1'"_sl,
                                    nullptr, &error);
    REQUIRE(query);

    results = CBLQuery_Execute(query, &error);
    int n = 0;
    while (CBLResultSet_Next(results)) {
        FLValue value = CBLResultSet_ValueAtIndex(results, 0);
        REQUIRE(value);
        REQUIRE(!FLValue_GetEncryptableValue(value));
        CHECK(FLValue_AsString(value) == "No Secret"_sl);
        
        value = CBLResultSet_ValueAtIndex(results, 1);
        REQUIRE(value);
        auto encValue = FLValue_GetEncryptableValue(value);
        REQUIRE(encValue);
        CHECK(FLValue_AsString(CBLEncryptable_Value(encValue)) == "Secret 1"_sl);
        
        value = CBLResultSet_ValueAtIndex(results, 2);
        REQUIRE(value);
        REQUIRE(!FLValue_GetEncryptableValue(value));
        CHECK(FLValue_AsString(value) == "Secret 1"_sl);
        
        value = FLDict_Get(FLValue_AsDict(CBLResultSet_ValueAtIndex(results, 3)), "secret2"_sl);
        REQUIRE(value);
        encValue = FLValue_GetEncryptableValue(value);
        REQUIRE(encValue);
        CHECK(FLValue_AsString(CBLEncryptable_Value(encValue)) == "Secret 2"_sl);
        
        ++n;
    }
    CHECK(n == 1);
}

#endif
