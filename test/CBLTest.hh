//
// CBLTest.hh
//
// Copyright (c) 2019 Couchbase, Inc All rights reserved.
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

#pragma once
#include "cbl/CouchbaseLite.h"
#include "cbl++/Base.hh"
#include "CBLPrivate.h"
#include "fleece/slice.hh"
#include <functional>
#include <iostream>
#include <string>

#ifdef _MSC_VER
    static const char  kPathSeparator = '\\';
    static const char  kBackupPathSeparator = '/';
    static const char* kCurrentDirectory = ".\\";
#else
    static const char  kPathSeparator = '/';
    static const char  kBackupPathSeparator = '\\';
    static const char* kCurrentDirectory = "./";
#endif

#ifdef __APPLE__
#   define CBL_UNUSED __unused
#elif !defined(_MSC_VER)
#   define CBL_UNUSED __attribute__((unused))
#else
#   define CBL_UNUSED
#endif

// Has to be declared before including catch.hpp, so Catch macros can use it
namespace fleece {
    static inline std::ostream& operator << (std::ostream &out, slice s) {
        if (s) {
            out << "slice(\"";
            out.write((const char*)s.buf, s.size);
            out << "\")";
        } else {
            out << "nullslice";
        }
        return out;
    }
}

static inline std::ostream& operator<< (std::ostream &out, CBLError err) {
    out << "{" << err.domain << ", " << err.code << "}";
    return out;
}

#include "catch.hpp"

#ifdef __ANDROID__
struct CBLTestAndroidContext {
    const char* filesDir;
    const char* tempDir;
    const char* assetsDir;
};
#endif

class CBLTest {
public:
    static const fleece::slice kDatabaseName;
    
#ifdef __ANDROID__
    /** Initializing android context is required before running tests on Android platform. */
    static void initAndroidContext(CBLTestAndroidContext context);
#endif
    
    static fleece::alloc_slice databaseDir();
    
    static CBLDatabaseConfiguration databaseConfig();
    
    CBLTest();
    ~CBLTest();
    
    /** Calls to (re)init default test databases and collections.
        The  databases will be closed, and if reset, the databases will be deleted. */
    void initTestDatabases(bool reset);
    
    CBLDatabase *db {nullptr};
    CBLCollection *defaultCollection {nullptr};
};

std::string GetAssetFilePath(const std::string &filename);

#ifdef COUCHBASE_ENTERPRISE

std::string GetExtensionPath();

void EnableVectorSearch();

#endif

void CreateDir(const std::string &dir);

bool ReadFileByLines(const std::string &path, const std::function<bool(FLSlice)> &callback);

unsigned ImportJSONLines(std::string filename, CBLDatabase* database);

unsigned ImportJSONLines(std::string filename, CBLCollection* collection);

void CheckNoError(CBLError& error);

void CheckError(CBLError& error, CBLErrorCode expectedCode, CBLErrorDomain expectedDomain = kCBLDomain);

void CheckNotOpenError(CBLError& error);

std::string CollectionPath(CBLCollection* collection);

CBLCollection* CreateCollection(CBLDatabase* database, std::string collection, std::string scope ="_default");

void createDocWithJSON(CBLCollection *col, std::string docID, std::string jsonContent);

void createDocWithPair(CBLCollection *col, fleece::slice docID, fleece::slice property, fleece::slice value);

void createDocWithPair(CBLDatabase *db, fleece::slice docID, fleece::slice property, fleece::slice value);

void createNumberedDocsWithPrefix(CBLCollection *col, unsigned n, const std::string& idprefix, unsigned start = 1);

void PurgeAllDocs(CBLCollection* collection);

CBLQuery* CreateQuery(CBLDatabase* db, std::string sql);

int CountResults(CBLResultSet *results);

// RAII utility to suppress reporting C++ exceptions (or breaking at them, in the Xcode debugger.)
// Declare an instance when testing something that's expected to throw an exception internally.
struct ExpectingExceptions {
    ExpectingExceptions()   {CBLLog_BeginExpectingExceptions();}
    ~ExpectingExceptions()  {CBLLog_EndExpectingExceptions();}
};
