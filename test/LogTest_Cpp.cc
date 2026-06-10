//
// LogTest_Cpp.cc
//
// Copyright © 2025 Couchbase. All rights reserved.
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

using namespace cbl;
using namespace std;

/** Sanity LogSinks C++ API. */
class LogTest_Cpp : public CBLTest_Cpp {
public:
    LogTest_Cpp() {
        _backupConsoleLogSink = LogSinks::console();
    }
    
    ~LogTest_Cpp() {
        LogSinks::setFile({});
        LogSinks::setCustom({});
        LogSinks::setConsole(_backupConsoleLogSink);
    }
private:
    ConsoleLogSink _backupConsoleLogSink;
};

TEST_CASE_METHOD(LogTest_Cpp, "Default Log Sink Cpp", "[Log]") {
    CBLConsoleLogSink console = LogSinks::console();
    CHECK(console.level == kCBLLogWarning);
    CHECK(console.domains == 0);
    
    CBLCustomLogSink custom = LogSinks::custom();
    CHECK(custom.level == kCBLLogNone);
    CHECK(custom.domains == 0);
    CHECK(custom.callback == nullptr);
    
    FileLogSink logSink = LogSinks::file();
    CHECK(logSink.level == kCBLLogNone);
    CHECK(logSink.directory.empty());
}

TEST_CASE_METHOD(LogTest_Cpp, "Console Log Sink Cpp: Set and Get", "[Log]") {
    LogSinks::setConsole({kCBLLogVerbose, kCBLLogDomainMaskAll});
    CBLConsoleLogSink logSink = LogSinks::console();
    CHECK(logSink.level == kCBLLogVerbose);
    CHECK(logSink.domains == kCBLLogDomainMaskAll);
}

TEST_CASE_METHOD(LogTest_Cpp, "Custom Log Sink Cpp: Set and Get", "[Log]") {
    CBLLogSinkCallback callback = [](CBLLogDomain domain, CBLLogLevel level, FLString msg) { };
    LogSinks::setCustom({ kCBLLogVerbose, callback, kCBLLogDomainMaskAll});
    CBLCustomLogSink logSink = LogSinks::custom();
    CHECK(logSink.level == kCBLLogVerbose);
    CHECK(logSink.domains == kCBLLogDomainMaskAll);
    CHECK(logSink.callback == callback);
}

TEST_CASE_METHOD(LogTest_Cpp, "File Log Sink Cpp: Set and Get", "[Log]") {
    string dir = string(CBLTest::databaseDir()) + kPathSeparator + "LogTestCpp";
    CreateDir(dir);
    
    CBLLogSinks_SetFile({ kCBLLogVerbose, slice(dir), 5, 1024 * 1024, true});
    CBLFileLogSink logSink = CBLLogSinks_File();
    CHECK(logSink.level == kCBLLogVerbose);
    CHECK(FLSlice_Equal(logSink.directory, slice(dir)));
    CHECK(logSink.maxKeptFiles == 5);
    CHECK(logSink.maxSize == 1024 * 1024);
    CHECK(logSink.usePlaintext);
}
