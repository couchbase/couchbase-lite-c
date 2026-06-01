//
// CBLTest.cc
//
// Copyright © 2018 Couchbase. All rights reserved.
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

#define CATCH_CONFIG_CONSOLE_WIDTH 120

#include "CBLTest.hh"
#include "CBLTest_Cpp.hh"
#include "CBLPrivate.h"
#include "fleece/slice.hh"
#include <chrono>
#include <sstream>
#include <functional>
#include <fstream>
#include <thread>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include "Platform_Apple.hh"
#endif

#ifndef _MSC_VER
#include <sys/stat.h>
#else
#include <Windows.h>
#endif

using namespace std;
using namespace fleece;

#ifdef __ANDROID__
    static CBLTestAndroidContext sAndriodContext = {};

    void CBLTest::initAndroidContext(CBLTestAndroidContext context) {
        sAndriodContext.filesDir = strdup(context.filesDir);
        sAndriodContext.tempDir = strdup(context.tempDir);
        sAndriodContext.assetsDir = strdup(context.assetsDir);
        
        if (!CBL_Init({sAndriodContext.filesDir, sAndriodContext.tempDir}, NULL)) {
            FAIL("Failed to init android context");
        }
    }
#endif

#ifdef COUCHBASE_ENTERPRISE

void EnableVectorSearch() {
    auto path = GetExtensionPath();
    if (!path.empty()) {
        CBLError error{};
        REQUIRE(CBL_EnableVectorSearch(slice(path), &error));
    }
}

#endif

static alloc_slice sDatabaseDir;

alloc_slice CBLTest::databaseDir() {
    if (!sDatabaseDir.empty())
        return sDatabaseDir;

#ifdef __APPLE__
    string dir = GetTempDirectory("CBL_C_Tests");
    if (mkdir(dir.c_str(), 0744) != 0 && errno != EEXIST)
        FAIL("Can't create temp directory: errno " << errno);
#else
#ifdef __ANDROID__
    if (!sAndriodContext.filesDir) {
        FAIL("Android context has not been initialized.");
    }
    
    string dir = string(sAndriodContext.filesDir).append("/CBL_C_Tests");
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
        FAIL("Can't create database directory: errno " << errno);
#else
#ifndef WIN32
    string dir = "/tmp/CBL_C_tests";
    if (mkdir(dir.c_str(), 0744) != 0 && errno != EEXIST)
        FAIL("Can't create temp directory: errno " << errno);
#else
    if (_mkdir("C:\\tmp") != 0 && errno != EEXIST)
        FAIL("Can't create C:\\tmp directory: errno " << errno);
    
    string dir = "C:\\tmp\\CBL_C_tests";
    if (_mkdir(dir.c_str()) != 0 && errno != EEXIST)
        FAIL("Can't create temp directory: errno " << errno);
#endif // WIN32
#endif // __ANDROID__
#endif // __ APPLE__
    
    sDatabaseDir = dir;
    return sDatabaseDir;
}

slice const CBLTest::kDatabaseName = "CBLTest";

CBLDatabaseConfiguration CBLTest::databaseConfig() {
    // One-time setup:
    CBLError_SetCaptureBacktraces(true);
    auto dir = databaseDir();
    CBLDatabaseConfiguration config = {dir};
    return config;
}

static constexpr size_t kDocIDBufferSize = 20;
static constexpr size_t kDocContentBufferSize = 100;

static bool WaitUntil(chrono::milliseconds timeout, std::function<bool()> predicate) {
    auto deadline = chrono::steady_clock::now() + timeout;
    do {
        if ( predicate() ) return true;
        this_thread::sleep_for(50ms);
    } while ( chrono::steady_clock::now() < deadline );

    return false;
}

CBLTest::CBLTest() {
    // Check that these have been correctly exported
    CHECK(FLValue_GetType(kFLNullValue) == kFLNull);
    CHECK(FLValue_GetType(kFLUndefinedValue) == kFLUndefined);
    CHECK(FLValue_GetType((FLValue)kFLEmptyArray) == kFLArray);
    CHECK(FLValue_GetType((FLValue)kFLEmptyDict) == kFLDict);

    initTestDatabases(true);
}

CBLTest::~CBLTest() {
    if (defaultCollection) {
        CBLCollection_Release(defaultCollection);
        defaultCollection = nullptr;
    }
    
    if (db){
        ExpectingExceptions x; // Database might have been closed by the test:
        CBLError error;
        if (!CBLDatabase_Close(db, &error))
            WARN("Failed to close database: " << error.domain << "/" << error.code);
        CBLDatabase_Release(db);
        db = nullptr;
    }

    if ( !WaitUntil(10s, [&] { return CBL_InstanceCount() == 0; }) ) {
        CBL_DumpInstances();
        WARN("*** LEAKED OBJECTS: ***");
        CHECK(CBL_InstanceCount() == 0);
    }
}

void CBLTest::initTestDatabases(bool reset) {
    CBLError error {};
    
    if (defaultCollection) {
        CBLCollection_Release(defaultCollection);
    }
    
    if (db) {
        REQUIRE(CBLDatabase_Close(db, &error));
        CheckNoError(error);
        CBLDatabase_Release(db);
    }
    
    auto config = databaseConfig();
    if (reset) {
        if (!CBL_DeleteDatabase(kDatabaseName, config.directory, &error) && error.code != 0) {
            FAIL("Can't delete temp database: " << error.domain << "/" << error.code);
        }
    }
    
    db = CBLDatabase_Open(kDatabaseName, &config, &error);
    REQUIRE(db);

    defaultCollection = CBLDatabase_DefaultCollection(db, &error);
    REQUIRE(defaultCollection);
}

#pragma mark - C++ TEST CLASS:

slice const CBLTest_Cpp::kDatabaseName = CBLTest::kDatabaseName;

CBLTest_Cpp::CBLTest_Cpp() {
    resetDatabase(true);
}

CBLTest_Cpp::~CBLTest_Cpp() {
    defaultCollection = nullptr;
    if (db) {
        db.close();
        db = nullptr;
    }

    if ( !WaitUntil(10s, [&] { return CBL_InstanceCount() == 0; }) ) {
        CBL_DumpInstances();
        WARN("*** LEAKED OBJECTS: ***");
        CHECK(CBL_InstanceCount() == 0);
    }
}

void CBLTest_Cpp::resetDatabase(bool deleteDatabase) {
    if (db) {
        db.close();
        db = nullptr;
    }
    
    db = openDatabaseNamed(kDatabaseName, deleteDatabase);
    REQUIRE(db);
    
    defaultCollection = db.getDefaultCollection();
    REQUIRE(defaultCollection);
}

cbl::Database CBLTest_Cpp::openDatabaseNamed(slice name, bool createEmpty){
    auto config = CBLTest::databaseConfig();
    if (createEmpty == true){
        cbl::Database::deleteDatabase(name, config.directory);
    }
    cbl::Database database = cbl::Database(name, config);
    REQUIRE(database);
    return database;
}

void CBLTest_Cpp::createDocument(std::string docID, std::string property, std::string value) {
    cbl::MutableDocument doc(docID);
    MutableDict newProps = MutableDict::newDict();
    newProps[property] = value;
    doc.setProperties(newProps);
    defaultCollection.saveDocument(doc);
}

#pragma mark - Test Utils C :

string GetAssetFilePath(const std::string &filename) {
    static string sTestFilesPath;
    if (sTestFilesPath.empty()) {
#ifdef __APPLE__
        auto bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.couchbase.CouchbaseLiteTests"));
        if (bundle) {
            auto url = CFBundleCopyResourcesDirectoryURL(bundle);
            CFAutorelease(url);
            url = CFURLCopyAbsoluteURL(url);
            CFAutorelease(url);
            auto path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
            CFAutorelease(path);
            char pathBuf[1000];
            CFStringGetCString(path, pathBuf, sizeof(pathBuf), kCFStringEncodingUTF8);
            strlcat(pathBuf, "/assets/", sizeof(pathBuf));
            sTestFilesPath = pathBuf;
        } else {
            sTestFilesPath = "test/assets/";
        }
#else
#ifdef __ANDROID__
        sTestFilesPath = string(sAndriodContext.assetsDir) + "/";
#else
#ifdef WIN32
        sTestFilesPath = "..\\test\\assets\\";
#else
        sTestFilesPath = "test/assets/";
#endif // WIN32
#endif // __ANDROID
#endif // __APPLE__
    }
    return sTestFilesPath + filename;
}

#ifdef COUCHBASE_ENTERPRISE

string GetExtensionPath() {
    string dir = "";
    
#ifdef __APPLE__
    auto bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.couchbase.CouchbaseLiteTests"));
    if (bundle) {
#if TARGET_OS_SIMULATOR
        dir = "test/extensions/apple/CouchbaseLiteVectorSearch.xcframework/ios-arm64/";
#elif TARGET_OS_IPHONE
        dir = "test/extensions/apple/CouchbaseLiteVectorSearch.xcframework/ios-arm64_x86_64-simulator/";
#else
        dir = "test/extensions/apple/CouchbaseLiteVectorSearch.xcframework/macos-arm64_x86_64/";
#endif
    } else {
        dir = "test/extensions/apple/";
    }
#else
#ifdef __ANDROID__
    dir = "test/extensions/android/arm64-v8a/";
#else
#ifdef WIN32
    char fullPath[32768];
    REQUIRE(GetFullPathName("..\\test\\extensions\\windows\\x86_64\\", 32768, fullPath, nullptr) > 0);
    dir = string(fullPath);
#else
    dir = "test/extensions/linux/x86_64/";
#endif // WIN32
#endif // __ANDROID
#endif // __APPLE__
    
    return dir;
}

#endif

void CreateDir(const string &dir) {
#ifdef WIN32
    if (_mkdir(dir.c_str()) != 0 && errno != EEXIST)
        FAIL("Can't create temp directory: errno " << errno);
#else
    if (mkdir(dir.c_str(), 0744) != 0 && errno != EEXIST)
        FAIL("Can't create temp directory: errno " << errno);
#endif
}

bool ReadFileByLines(const string &path, const function<bool(FLSlice)> &callback) {
    INFO("Reading lines from " << path);
    fstream fd(path.c_str(), ios_base::in);
    REQUIRE(fd);
    vector<char> buf(1000000); // The Wikipedia dumps have verrry long lines
    while (fd.good()) {
        fd.getline(buf.data(), buf.size());
        auto len = fd.gcount();
        if (len <= 0)
            break;
        REQUIRE(buf[len-1] == '\0');
        --len;
        if (!callback({buf.data(), (size_t)len}))
            return false;
    }
    REQUIRE(fd.eof());
    return true;
}

unsigned ImportJSONLines(string filename, CBLDatabase* database) {
    CBLError error {};
    CBLCollection *collection = CBLDatabase_DefaultCollection(database, &error);
    REQUIRE(collection);
    auto result = ImportJSONLines(filename, collection);
    CBLCollection_Release(collection);
    return result;
}

unsigned ImportJSONLines(string filename, CBLCollection* collection) {
    auto path = GetAssetFilePath(filename);
    CBL_Log(kCBLLogDomainDatabase, kCBLLogInfo, "Reading %s ...  ", path.c_str());
    CBLError error {};
    unsigned numDocs = 0;
    
    CBLDatabase* database = CBLCollection_Database(collection);
    REQUIRE(database);
    
    REQUIRE(CBLDatabase_BeginTransaction(database, &error));
    ReadFileByLines(path, [&](FLSlice line) {
        char docID[kDocIDBufferSize];
        snprintf(docID, kDocIDBufferSize, "%07u", numDocs+1);
        auto doc = CBLDocument_CreateWithID(slice(docID));
        REQUIRE(CBLDocument_SetJSON(doc, line, &error));
        CHECK(CBLCollection_SaveDocumentWithConcurrencyControl(collection, doc, kCBLConcurrencyControlFailOnConflict, &error));
        CBLDocument_Release(doc);
        ++numDocs;
        return true;
    });
    CBL_Log(kCBLLogDomainDatabase, kCBLLogInfo, "Committing %u docs...", numDocs);
    REQUIRE(CBLDatabase_EndTransaction(database, true, &error));
    
    return numDocs;
}

void CheckNoError(CBLError& error) {
    CHECK(error.domain == 0);
    CHECK(error.code == 0);
}

void CheckError(CBLError& error, CBLErrorCode expectedCode, CBLErrorDomain expectedDomain) {
    CHECK(error.domain == expectedDomain);
    CHECK(error.code == expectedCode);
}

void CheckNotOpenError(CBLError& error) {
    CheckError(error, kCBLErrorNotOpen);
}

CBLCollection* CreateCollection(CBLDatabase* database, string collection, string scope) {
    CBLError error {};
    auto col = CBLDatabase_CreateCollection(database, slice(collection), slice(scope), &error);
    REQUIRE(col);
    return col;
}

void createDocWithJSON(CBLCollection *col, std::string docID, std::string jsonContent) {
    CBLError error {};
    CBLDocument* doc = docID.empty() ? CBLDocument_Create() : CBLDocument_CreateWithID(slice(docID));
    REQUIRE(CBLDocument_SetJSON(doc, slice(jsonContent), &error));
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc, kCBLConcurrencyControlFailOnConflict, &error));
    CBLDocument_Release(doc);
}

void createDocWithPair(CBLCollection *col, fleece::slice docID, fleece::slice property, fleece::slice value) {
    CBLDocument* doc = CBLDocument_CreateWithID(docID);
    MutableDict props = CBLDocument_MutableProperties(doc);
    FLSlot_SetString(FLMutableDict_Set(props, property), value);

    CBLError error;
    bool saved = CBLCollection_SaveDocumentWithConcurrencyControl(col, doc, kCBLConcurrencyControlFailOnConflict, &error);
    CBLDocument_Release(doc);
    REQUIRE(saved);
}

void createDocWithPair(CBLDatabase *db, fleece::slice docID, fleece::slice property, fleece::slice value) {
    CBLDocument* doc = CBLDocument_CreateWithID(docID);
    MutableDict props = CBLDocument_MutableProperties(doc);
    FLSlot_SetString(FLMutableDict_Set(props, property), value);

    CBLError error;
    CBLCollection *col = CBLDatabase_DefaultCollection(db, &error);
    bool saved = CBLCollection_SaveDocumentWithConcurrencyControl(col, doc, kCBLConcurrencyControlFailOnConflict, &error);
    CBLDocument_Release(doc);
    REQUIRE(saved);
    CBLCollection_Release(col);
}

void createNumberedDocsWithPrefix(CBLCollection *col, unsigned n, const std::string& idprefix, unsigned start) {
    for (unsigned i = 0; i < n; i++) {
        string docID = idprefix + '-' + to_string(start + i);
        auto doc = CBLDocument_CreateWithID(slice(docID));
        
        MutableDict props = CBLDocument_MutableProperties(doc);
        char content[100];
        snprintf(content, 100, "This is the document #%03u.", start + i);
        FLSlot_SetString(FLMutableDict_Set(props, "content"_sl), slice(content));
        
        CBLError error = {};
        bool saved = CBLCollection_SaveDocument(col, doc, &error);
        CBLDocument_Release(doc);
        REQUIRE(saved);
    }
}

std::string CollectionPath(CBLCollection* collection) {
    CBLScope* scope = CBLCollection_Scope(collection);
    string name = slice(CBLScope_Name(scope)).asString() + "." + slice(CBLCollection_Name(collection)).asString();
    CBLScope_Release(scope);
    return name;
}

void PurgeAllDocs(CBLCollection* collection) {
    std::stringstream ss;
    ss << "SELECT meta().id FROM ";
    ss << CollectionPath(collection);

    CBLDatabase* database = CBLCollection_Database(collection);
    REQUIRE(database);
    
    auto query = CBLDatabase_CreateQuery(database, kCBLN1QLLanguage, slice(ss.str()), NULL, NULL);
    REQUIRE(query);
    
    auto rs = CBLQuery_Execute(query, NULL);
    REQUIRE(rs);
    
    while (CBLResultSet_Next(rs)) {
        FLString id = FLValue_AsString(CBLResultSet_ValueAtIndex(rs, 0));
        REQUIRE(id.buf);
        REQUIRE(CBLCollection_PurgeDocumentByID(collection, id, NULL));
    }
    
    CBLResultSet_Release(rs);
    CBLQuery_Release(query);
}

CBLQuery* CreateQuery(CBLDatabase* db, std::string sql) {
    int errPos;
    CBLError error {};
    auto query = CBLDatabase_CreateQuery(db, kCBLN1QLLanguage, slice(sql), &errPos, &error);
    REQUIRE(query);
    return query;
}

int CountResults(CBLResultSet *results) {
    int n = 0;
    while (CBLResultSet_Next(results))
        ++n;
    return n;
}

#pragma mark - Test Utils C++:

void createNumberedDocsWithPrefix(cbl::Collection& collection, unsigned n, const std::string& idprefix, unsigned start) {
    for (unsigned i = 0; i < n; i++){
        string docID = idprefix + '-' + to_string(start + i);

        char content[kDocContentBufferSize];
        snprintf(content, kDocContentBufferSize, "This is the document #%03u.", i+1);
        cbl::MutableDocument doc(docID);
        doc["content"] = content;
        collection.saveDocument(doc);
    }
}

void createDocWithJSON(cbl::Collection& collection, std::string docID, std::string jsonContent) {
    cbl::MutableDocument doc(docID);
    doc.setPropertiesAsJSON(jsonContent);
    collection.saveDocument(doc);
}
