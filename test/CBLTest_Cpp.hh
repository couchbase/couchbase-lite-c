//
// CBLTest_Cpp.hh
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
#include "cbl++/CouchbaseLite.hh"
#include "CBLTest.hh"


namespace cbl {
    // Make Catch write something better than "{?}" when it logs a CBL object:
    #define DEFINE_WRITE_OP(CLASS) \
        static inline std::ostream& operator<< (std::ostream &out, const cbl::CLASS &rc) { \
            return out << "cbl::" #CLASS "[@" << (void*)rc.ref() << "]"; \
        }
    DEFINE_WRITE_OP(Blob)
    DEFINE_WRITE_OP(Database)
    DEFINE_WRITE_OP(Document)
    DEFINE_WRITE_OP(MutableDocument)
    DEFINE_WRITE_OP(Query)
    DEFINE_WRITE_OP(Replicator)
    DEFINE_WRITE_OP(ResultSet)
}


class CBLTest_Cpp {
public:
    static const fleece::slice kDatabaseName;
    
    CBLTest_Cpp();
    ~CBLTest_Cpp();
    
    /** Close test database and create a new database and its default collection instance. */
    void resetDatabase(bool deleteDatabase = false);
    
    void createDocument(std::string docID, std::string property, std::string value);
    
    cbl::Database openDatabaseNamed(fleece::slice name, bool createEmpty = 0);
    
    cbl::Database db;
    cbl::Collection defaultCollection;
};

void createDocWithJSON(cbl::Collection& collection, std::string docID, std::string jsonContent);

void createNumberedDocsWithPrefix(cbl::Collection& collection, unsigned n, const std::string& idprefix, unsigned start = 1);

/** Runs `fn` and returns true if it completes without throwing, or false if it throws a
    cbl::Error. Lets a void, throw-on-error C++ API call be asserted in a Catch CHECK/REQUIRE,
    e.g. `REQUIRE(succeeds([&]{ db.saveBlob(blob); }));`.
    @note Only cbl::Error is treated as failure; other exceptions propagate. */
template <typename Fn>
static inline bool succeeds(Fn&& fn) {
    try {
        std::forward<Fn>(fn)();
        return true;
    } catch (const cbl::Error&) {
        return false;
    }
}

/** Converts a thrown cbl::Error back to the C CBLError struct, for tests that compare against
    expected domain/code (e.g. via CheckError). Usage:
    `catch (const cbl::Error& e) { error = asCBLError(e); }`. */
static inline CBLError asCBLError(const cbl::Error& e) {
    return {e.domain, e.code};
}
