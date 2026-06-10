//
// LogTest.cc
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

#include "CBLTest.hh"
#include "cbl/CouchbaseLite.h"
#include "fleece/Fleece.hh"
#include <array>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <thread>
#include <vector>

#ifndef _MSC_VER
#include <unistd.h>
#endif

using namespace std;
using namespace fleece;

static const array<CBLLogLevel, 6> kLogLevels = {{
    kCBLLogDebug, kCBLLogVerbose, kCBLLogInfo, kCBLLogWarning, kCBLLogError, kCBLLogNone
}};

static const array<string, 6> kLogLevelNames = {{
    "Debug", "Verbose", "Info", "Warning", "Error", "None"
}};

static const array<string, 5> kLogFileNamePrefixes = {{
    "cbl_debug_", "cbl_verbose_", "cbl_info_", "cbl_warning_", "cbl_error_"
}};

static const array<CBLLogDomainMask, 5> kLogDomainMasks = {{
    kCBLLogDomainMaskDatabase,
    kCBLLogDomainMaskQuery,
    kCBLLogDomainMaskReplicator,
    kCBLLogDomainMaskNetwork,
    kCBLLogDomainMaskListener
}};

static const array<CBLLogDomain, 5> kLogDomains = {{
    kCBLLogDomainDatabase,
    kCBLLogDomainQuery,
    kCBLLogDomainReplicator,
    kCBLLogDomainNetwork,
    kCBLLogDomainListener
}};

// For rotating test log directory:
static unsigned sLogDirCount = 0;

class LogTest : public CBLTest {
public:
    LogTest() {
        prepareLogDir();
        _backupConsoleLogSink = CBLLogSinks_Console();
        reset();
    }
    
    ~LogTest() {
        reset();
        CBLLogSinks_SetConsole(_backupConsoleLogSink);
    }
    
    const string& logDir() { return _logDir; }
    
    void reset() {
        CBLLog_Reset();
        deleteAllLogFiles();
    }
    
    void deleteLogDir() {
        CBLError error {};
        CBL_DeleteDirectoryRecursive(slice(_logDir), &error);
        CheckNoError(error);
    }
    
    void deleteAllLogFiles() {
        auto paths = getAllLogFilePaths();
        for (string path : paths) {
        #ifndef WIN32
            int result = unlink(path.c_str());
        #else
            int result = _unlink(path.c_str());
        #endif
            if (result != 0)
                FAIL("Can't delete file at " << path <<": errno " << errno);
        }
    }
    
    vector<string> getAllLogFilePaths() {
        vector<string> logFiles;
        
        auto dir = opendir(logDir().c_str());
        if (!dir) {
            if (errno != ENOENT) {
                FAIL("Can't open log directory at "<< logDir() << ": errno " << errno);
            }
            return logFiles;
        }
        
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            string filename = string(entry->d_name);
#ifndef WIN32
            string path = logDir() + kPathSeparator + filename;
            struct stat statEntry;
            stat(path.c_str(), &statEntry);
#else
            string path = logDir() + kPathSeparator + filename;
            struct _stat statEntry;
            _stat(path.c_str(), &statEntry);
#endif
            
            if (S_ISDIR(statEntry.st_mode))
                continue;
            
            auto split = splitExtension(filename);
            if (split.second != ".cbllog")
                continue;
            
            logFiles.push_back(path);
        }
        
        return logFiles;
    }
    
    vector<string> getAllLogFilePaths(CBLLogLevel level) {
        vector<string> paths;
        for (string path : getAllLogFilePaths()) {
            auto split = splitPath(path);
            auto fileName = split.second;
            if (fileName.find(kLogFileNamePrefixes[level]) == 0) {
                paths.push_back(path);
            }
        }
        return paths;
    }
    
    vector<string> readLogFile(CBLLogLevel level) {
        string filePath;
        auto logFilePaths = getAllLogFilePaths();
        for (string path : logFilePaths) {
            auto split = splitPath(path);
            auto fileName = split.second;
            if (fileName.find(kLogFileNamePrefixes[level]) == 0) {
                // Make sure there is only one
                CHECK(filePath.empty());
                filePath = path;
            }
        }
        
        vector<string> lines;
        if (!filePath.empty()) {
            ReadFileByLines(filePath, [&](FLSlice line) {
                lines.push_back(string(line));
                return true;
            });
        }
        return lines;
    }
    
    void writeLog(CBLLogDomain domain, CBLLogLevel level, string msg) {
        CBL_Log(domain, level, "%s", msg.c_str());
    }
    
    void writeC4Log(CBLLogDomain domain, CBLLogLevel level, string msg) {
        CBLLog_LogWithC4Log(domain, level, msg.c_str());
    }
    
    void writeLogs() {
        writeLog(kCBLLogDomainDatabase, kCBLLogDebug, kLogLevelNames[kCBLLogDebug]);
        writeLog(kCBLLogDomainDatabase, kCBLLogVerbose, kLogLevelNames[kCBLLogVerbose]);
        writeLog(kCBLLogDomainDatabase, kCBLLogInfo, kLogLevelNames[kCBLLogInfo]);
        writeLog(kCBLLogDomainDatabase, kCBLLogWarning, kLogLevelNames[kCBLLogWarning]);
        writeLog(kCBLLogDomainDatabase, kCBLLogError, kLogLevelNames[kCBLLogError]);
    }
    
    void writeC4Logs() {
        writeC4Log(kCBLLogDomainDatabase, kCBLLogDebug, kLogLevelNames[kCBLLogDebug]);
        writeC4Log(kCBLLogDomainDatabase, kCBLLogVerbose, kLogLevelNames[kCBLLogVerbose]);
        writeC4Log(kCBLLogDomainDatabase, kCBLLogInfo, kLogLevelNames[kCBLLogInfo]);
        writeC4Log(kCBLLogDomainDatabase, kCBLLogWarning, kLogLevelNames[kCBLLogWarning]);
        writeC4Log(kCBLLogDomainDatabase, kCBLLogError, kLogLevelNames[kCBLLogError]);
    }
    
private:
    CBLConsoleLogSink _backupConsoleLogSink;
    string _logDir;
    
    void prepareLogDir() {
        // Base:
        string dir = string(CBLTest::databaseDir()) + kPathSeparator + "CBLLogTest";
        CreateDir(dir);
        
        // Log dir:
        dir += kPathSeparator + to_string(++sLogDirCount);
        CreateDir(dir);
        
        _logDir = dir;
    }
    
    // File Utils:
    
    pair<string,string> splitExtension(const string &file) {
        auto dot = file.rfind('.');
        auto lastSlash = file.rfind(kPathSeparator);
        if (dot == string::npos || (lastSlash != string::npos && dot < lastSlash))
            return {file, ""};
        else
            return {file.substr(0, dot), file.substr(dot)};
    }
    
    pair<string,string> splitPath(const string &path) {
        string dirname, basename;
        auto slash = path.rfind(kPathSeparator);
        auto backupSlash = path.rfind(kBackupPathSeparator);
        if (slash == string::npos && backupSlash == string::npos) {
            return{ kCurrentDirectory, string(path) };
        }
        
        if (slash == string::npos) {
            slash = backupSlash;
        }
        else if (backupSlash != string::npos) {
            slash = std::max(slash, backupSlash);
        }

        return {string(path.substr(0, slash+1)), string(path.substr(slash+1))};
    }
};

TEST_CASE_METHOD(LogTest, "Legacy - Console Logging : Log Level", "[Log]") {
    for (CBLLogLevel level : kLogLevels) {
        CBLLog_SetConsoleLevel(level);
        CHECK(CBLLog_ConsoleLevel() == level);
    }
}

TEST_CASE_METHOD(LogTest, "Legacy - File Logging : Config", "[Log]") {
    CBLLogFileConfiguration config = {};
    config.directory = slice(logDir());
    config.level = kCBLLogVerbose;
    config.maxRotateCount = 5;
    config.maxSize = 10;
    config.usePlaintext = true;
    
    CBLError error;
    CHECK(CBLLog_SetFileConfig(config, &error));
    
    auto config2 = CBLLog_FileConfig();
    CHECK(config2->level == config.level);
    CHECK(config2->directory == config.directory);
    CHECK(config2->maxRotateCount == config.maxRotateCount);
    CHECK(config2->maxSize == config.maxSize);
    CHECK(config2->usePlaintext == config.usePlaintext);
}

TEST_CASE_METHOD(LogTest, "Legacy - File Logging : Set Log Level", "[Log]") {
    CBLLogFileConfiguration config {};
    config.directory = slice(logDir());
    config.usePlaintext = true;
    
    // Set setting different log levels:
    for (CBLLogLevel level : kLogLevels) {
        // Set log level:
        config.level = level;
        
        CBLError error {};
        CHECK(CBLLog_SetFileConfig(config, &error));
        
        // Write messages on each log level:
        writeLogs();
    }
    
    // Verify:
    int lineCount = 2; // 2 header lines
    for (CBLLogLevel level : kLogLevels) {
        if (level == kCBLLogNone)
            continue;
        
        vector<string> lines = readLogFile(level);
        CHECK(lines.size() == ++lineCount);
    }
}

TEST_CASE_METHOD(LogTest, "Legacy - File Logging : Max Size and MaxRotateCount", "[Log]") {
    CBLLogFileConfiguration config {};
    config.directory = slice(logDir());
    config.maxSize = 1024;
    config.maxRotateCount = 2;
    config.usePlaintext = true;
    CBLError error {};
    CHECK(CBLLog_SetFileConfig(config, &error));
    
    // Note: Each log file has ~320 bytes for the header
    for (int i = 0; i < 100; i++) {
        // Workaround for CBL-6291:
        if (i % 10 == 0) this_thread::sleep_for(100ms);
        writeC4Log(kCBLLogDomainDatabase, kCBLLogInfo, "ZZZZZZZZZZZZZZZZZZZZ : " + std::to_string(i)); // ~60 bytes
    }
    
    auto paths = getAllLogFilePaths(kCBLLogInfo);
    CHECK(paths.size() == config.maxRotateCount + 1);
}

TEST_CASE_METHOD(LogTest, "Legacy - File Logging : Binary Format", "[Log]") {
    CBLLogFileConfiguration config {};
    config.directory = slice(logDir());
    config.usePlaintext = false;
    CBLError error {};
    CHECK(CBLLog_SetFileConfig(config, &error));
    
    writeLog(kCBLLogDomainDatabase, kCBLLogInfo, "message");
    
    auto filePaths = getAllLogFilePaths(kCBLLogInfo);
    REQUIRE(filePaths.size() == 1);
    
    std::vector<unsigned char> targetBytes = { 0xcf, 0xb2, 0xab, 0x1b };
    std::fstream file(filePaths[0], std::ios_base::in | std::ios_base::binary);
    std::vector<unsigned char> bytes(4);
    file.read(reinterpret_cast<char*>(bytes.data()), targetBytes.size());
    file.close();
    CHECK(bytes == targetBytes);
}


TEST_CASE_METHOD(LogTest, "Legacy - Custom Logging", "[Log]") {
    // Set log callback:
    static vector<CBLLogLevel>recs;
    CBLLog_SetCallback([](CBLLogDomain domain, CBLLogLevel level, FLString msg) {
        CHECK(level >= CBLLog_CallbackLevel());
        CHECK(string(msg).find(kLogLevelNames[level]) == 0);
        recs.push_back(level);
    });
    
    // Set setting different log levels:
    for (CBLLogLevel callbackLevel : kLogLevels) {
        // Set log level:
        CBLLog_SetCallbackLevel(callbackLevel);
        CHECK(CBLLog_CallbackLevel() == callbackLevel);
        
        // Write messages on each log level:
        recs.clear();
        writeLogs();
        
        // Verify:
        for (CBLLogLevel level : kLogLevels) {
            if (level == kCBLLogNone)
                continue;
            if (level >= callbackLevel)
                CHECK(find(recs.begin(), recs.end(), level) != recs.end());
            else
                CHECK(find(recs.begin(), recs.end(), level) == recs.end());
        }
    }
    
    // Reset log callback:
    CBLLog_SetCallbackLevel(kCBLLogDebug);
    CBLLog_SetCallback(nullptr);
    REQUIRE(CBLLog_Callback() == nullptr);
    recs.clear();
    writeLogs();
    CHECK(recs.empty());
    CBLLog_SetCallbackLevel(kCBLLogNone);
}

TEST_CASE_METHOD(LogTest, "Log Message", "[Log]") {
    static vector<string>recs;
    CBLLogSinkCallback callback = [](CBLLogDomain domain, CBLLogLevel level, FLString msg) {
        recs.push_back(string(msg));
    };
    CBLLogSinks_SetCustom({ kCBLLogDebug, callback, kCBLLogDomainMaskAll});
    
    // Use CBL_Log:
    CBL_Log(kCBLLogDomainDatabase, kCBLLogInfo, "foo %s", "bar");
    
    // Use CBL_LogMessage:
    CBL_LogMessage(kCBLLogDomainDatabase, kCBLLogInfo, "hello world"_sl);
    
    REQUIRE(recs.size() == 2);
    CHECK(recs[0] == "foo bar");
    CHECK(recs[1] == "hello world");
}

// LogSinks Test starts here

TEST_CASE_METHOD(LogTest, "Default Console Log Sink", "[Log]") {
    CBLConsoleLogSink logSink = CBLLogSinks_Console();
    CHECK(logSink.level == kCBLLogWarning);
    CHECK(logSink.domains == 0);
}

TEST_CASE_METHOD(LogTest, "Default Custom Log Sink", "[Log]") {
    CBLCustomLogSink logSink = CBLLogSinks_CustomSink();
    CHECK(logSink.level == kCBLLogNone);
    CHECK(logSink.domains == 0);
    CHECK(logSink.callback == nullptr);
}

TEST_CASE_METHOD(LogTest, "Default File Log Sink", "[Log]") {
    CBLFileLogSink logSink = CBLLogSinks_File();
    CHECK(logSink.level == kCBLLogNone);
    CHECK(logSink.directory == kFLSliceNull);
}

TEST_CASE_METHOD(LogTest, "Console Log Sink : Set and Get", "[Log]") {
    CBLLogSinks_SetConsole({ kCBLLogVerbose, kCBLLogDomainMaskAll });
    CBLConsoleLogSink logSink = CBLLogSinks_Console();
    CHECK(logSink.level == kCBLLogVerbose);
    CHECK(logSink.domains == kCBLLogDomainMaskAll);
}

TEST_CASE_METHOD(LogTest, "Custom Log Sink : Set and Get", "[Log]") {
    CBLLogSinkCallback callback = [](CBLLogDomain domain, CBLLogLevel level, FLString msg) { };
    CBLLogSinks_SetCustom({ kCBLLogVerbose, callback, kCBLLogDomainMaskAll});
    CBLCustomLogSink logSink = CBLLogSinks_CustomSink();
    CHECK(logSink.level == kCBLLogVerbose);
    CHECK(logSink.domains == kCBLLogDomainMaskAll);
    CHECK(logSink.callback == callback);
}

TEST_CASE_METHOD(LogTest, "File Log Sink : Set and Get", "[Log]") {
    {
        string copiedLogDir = string(logDir()); // For testing that directory is copied properly
        CBLLogSinks_SetFile({ kCBLLogVerbose, slice(copiedLogDir), 5, 1024 * 1024, true});
    }
    
    CBLFileLogSink logSink = CBLLogSinks_File();
    CHECK(logSink.level == kCBLLogVerbose);
    CHECK(FLSlice_Equal(logSink.directory, slice(logDir())));
    CHECK(logSink.maxKeptFiles == 5);
    CHECK(logSink.maxSize == 1024 * 1024);
    CHECK(logSink.usePlaintext);
}

TEST_CASE_METHOD(LogTest, "Custom Log Sink : Null callback", "[Log]") {
    static vector<CBLLogLevel>recs;
    
    CBLCustomLogSink logSink { kCBLLogDebug, nullptr };
    CBLLogSinks_SetCustom(logSink);
    writeLogs();
    CHECK(recs.empty());
}

TEST_CASE_METHOD(LogTest, "Custom Log Sink : Log Level", "[Log]") {
    bool useC4Log {false};
    SECTION("Use c4log function") {
        useC4Log = true;
    }
    
    // The log message doesn't go through the c4log callback:
    SECTION("Use platform log function") {
        useC4Log = false;
    }
   
    static vector<CBLLogLevel>recs;
    CBLCustomLogSink logSink {};
    logSink.callback = [](CBLLogDomain domain, CBLLogLevel level, FLString msg) {
        CHECK(level >= CBLLogSinks_CustomSink().level);
        CHECK(string(msg).find(kLogLevelNames[level]) == 0);
        recs.push_back(level);
    };
    
    // Set and test logging in different log levels:
    for (CBLLogLevel callbackLevel : kLogLevels) {
        // Set log level:
        logSink.level = callbackLevel;
        CBLLogSinks_SetCustom(logSink);
        
        // Write messages on each log level:
        recs.clear();
        
        if (useC4Log) {
            writeC4Logs();
        } else {
            writeLogs();
        }
        
        // Verify:
        if (callbackLevel == kCBLLogNone) {
            CHECK(recs.empty());
        } else {
            for (CBLLogLevel level : kLogLevels) {
                if (level == kCBLLogNone)
                    continue;
                else if (level >= callbackLevel)
                    CHECK(find(recs.begin(), recs.end(), level) != recs.end());
                else
                    CHECK(find(recs.begin(), recs.end(), level) == recs.end());
            }
        }
    }
}

TEST_CASE_METHOD(LogTest, "Custom Log Sink : Domains", "[Log]") {
    static vector<CBLLogDomain>recs;
    
    auto writeDomainLogs = [&](bool useC4Log) {
        for (auto domain : kLogDomains) {
            if (useC4Log) {
                writeC4Log(domain, kCBLLogInfo, "message");
            } else {
                writeLog(domain, kCBLLogInfo, "message");
            }
        }
    };
    
    CBLCustomLogSink logSink {};
    logSink.level = kCBLLogVerbose;
    logSink.callback = [](CBLLogDomain domain, CBLLogLevel level, FLString msg) {
        recs.push_back(domain);
    };
    
    SECTION("Filter by each domain") {
        bool useC4Log {false};
        SECTION("Use c4log function") {
            useC4Log = true;
        }
        
        // The log message doesn't go through the c4log callback:
        SECTION("Use platform log function") {
            useC4Log = false;
        }
        
        recs.clear();
        
        for (int i = 0; i < kLogDomainMasks.size(); i++) {
            logSink.domains = kLogDomainMasks[i];
            CBLLogSinks_SetCustom(logSink);
            
            writeDomainLogs(useC4Log);
            
            CHECK(recs.size() == 1);
            CHECK(recs[0] == kLogDomains[i]);
            recs.clear();
        }
    }
    
    SECTION("Filter by combined domains") {
        bool useC4Log {false};
        SECTION("Use c4log function") {
            useC4Log = true;
        }
        
        // The log message doesn't go through the c4log callback:
        SECTION("Use platform log function") {
            useC4Log = false;
        }
        
        recs.clear();
        
        for (int i = 0; i < kLogDomainMasks.size(); i++) {
            logSink.domains = 0;
            for (size_t j = 0; j <= i; j++) {
                logSink.domains |= kLogDomainMasks[j];
            }
            CBLLogSinks_SetCustom(logSink);
            
            writeDomainLogs(useC4Log);
            
            CHECK(recs.size() == i + 1);
            for (int r = 0; r < recs.size(); r++) {
                CHECK(recs[r] == kLogDomains[r]);
            }
            recs.clear();
        }
    }
    
    SECTION("All domains using zero") {
        bool useC4Log {false};
        SECTION("Use c4log function") {
            useC4Log = true;
        }
        
        // The log message doesn't go through the c4log callback:
        SECTION("Use platform log function") {
            useC4Log = false;
        }
        
        recs.clear();
        
        logSink.domains = 0;
        CBLLogSinks_SetCustom(logSink);
        
        writeDomainLogs(useC4Log);
        
        CHECK(recs.size() == kLogDomains.size());
        for (int r = 0; r < recs.size(); r++) {
            CHECK(recs[r] == kLogDomains[r]);
        }
    }
    
    SECTION("All domains using all domain mask") {
        bool useC4Log {false};
        SECTION("Use c4log function") {
            useC4Log = true;
        }
        
        // The log message doesn't go through the c4log callback:
        SECTION("Use platform log function") {
            useC4Log = false;
        }
        
        recs.clear();
        
        logSink.domains = kCBLLogDomainMaskAll;
        CBLLogSinks_SetCustom(logSink);
        
        writeDomainLogs(useC4Log);
        
        CHECK(recs.size() == kLogDomains.size());
        for (int r = 0; r < recs.size(); r++) {
            CHECK(recs[r] == kLogDomains[r]);
        }
    }
}

TEST_CASE_METHOD(LogTest, "File Log Sink : Log Level", "[Log]") {
    CBLFileLogSink logSink {};
    logSink.directory = slice(logDir());
    logSink.usePlaintext = true;
    
    // Set setting different log levels:
    for (CBLLogLevel level : kLogLevels) {
        // Set log level:
        logSink.level = level;
        
        CBLLogSinks_SetFile(logSink);
        
        // Write messages on each log level:
        writeLogs();
    }
    
    // Verify:
    int lineCount = 2; // 2 header lines:
    for (CBLLogLevel level : kLogLevels) {
        if (level == kCBLLogNone)
            continue;
        
        vector<string> lines = readLogFile(level);
        REQUIRE(lines.size() == ++lineCount);
    }
}

TEST_CASE_METHOD(LogTest, "File Log Sink : MaxSize and MaxKepthFiles", "[Log]") {
    CBLFileLogSink logSink {};
    logSink.level = kCBLLogDebug;
    logSink.directory = slice(logDir());
    logSink.maxSize = 1024;
    logSink.maxKeptFiles = 3;
    logSink.usePlaintext = true;
    CBLLogSinks_SetFile(logSink);
    
    // Note: Each log file has ~320 bytes for the header
    for (int i = 0; i < 100; i++) {
        // Workaround for CBL-6291:
        if (i % 10 == 0) this_thread::sleep_for(100ms);
        writeC4Log(kCBLLogDomainDatabase, kCBLLogInfo, "ZZZZZZZZZZZZZZZZZZZZ : " + std::to_string(i)); // ~60 bytes
    }
    
    auto paths = getAllLogFilePaths(kCBLLogInfo);
    CHECK(paths.size() == logSink.maxKeptFiles);
}

TEST_CASE_METHOD(LogTest, "File Log Sink : Binary Format", "[Log]") {
    CBLFileLogSink logSink {};
    logSink.level = kCBLLogDebug;
    logSink.directory = slice(logDir());
    logSink.usePlaintext = false;
    CBLLogSinks_SetFile(logSink);
    
    writeLog(kCBLLogDomainDatabase, kCBLLogInfo, "message");
    
    auto filePaths = getAllLogFilePaths(kCBLLogInfo);
    REQUIRE(filePaths.size() == 1);
    
    std::vector<unsigned char> targetBytes = { 0xcf, 0xb2, 0xab, 0x1b };
    std::fstream file(filePaths[0], std::ios_base::in | std::ios_base::binary);
    std::vector<unsigned char> bytes(4);
    file.read(reinterpret_cast<char*>(bytes.data()), targetBytes.size());
    file.close();
    CHECK(bytes == targetBytes);
}

TEST_CASE_METHOD(LogTest, "File Log Sink : Create Directory", "[Log]") {
    deleteLogDir();
    
    CBLFileLogSink logSink {};
    logSink.level = kCBLLogInfo;
    logSink.directory = slice(logDir());
    logSink.usePlaintext = true;
    CBLLogSinks_SetFile(logSink);
    
    writeLog(kCBLLogDomainDatabase, kCBLLogInfo, "message");
    vector<string> lines = readLogFile(kCBLLogInfo);
    REQUIRE(lines.size() == 3);
}

// Note: With log level and null or empty directory is not allowed:
TEST_CASE_METHOD(LogTest, "File Log Sink : Disable", "[Log]") {
    CBLFileLogSink logSink {};
    logSink.level = kCBLLogInfo;
    logSink.directory = slice(logDir());
    logSink.usePlaintext = true;
    CBLLogSinks_SetFile(logSink);
    
    writeLog(kCBLLogDomainDatabase, kCBLLogInfo, "message");
    vector<string> lines = readLogFile(kCBLLogInfo);
    REQUIRE(lines.size() == 3);
    
    SECTION("With directory") {
        logSink.level = kCBLLogNone;
        logSink.directory = slice(logDir());
        logSink.usePlaintext = true;
        CBLLogSinks_SetFile(logSink);
    }
    
    SECTION("With null directory") {
        logSink.level = kCBLLogNone;
        logSink.directory = kFLSliceNull;
        logSink.usePlaintext = true;
        CBLLogSinks_SetFile(logSink);
    }
    
    SECTION("With empty directory") {
        logSink.level = kCBLLogNone;
        logSink.directory = slice("");
        logSink.usePlaintext = true;
        CBLLogSinks_SetFile(logSink);
    }
    
    writeLog(kCBLLogDomainDatabase, kCBLLogInfo, "message");
    lines = readLogFile(kCBLLogInfo);
    CHECK(lines.size() >= 3);
    
    // Note: only when setting the directory to null or empty, LiteCore will
    // close the current opened log file and put the ending line to it.
    // It's not clear why but it doesn't impact the log usage:
    if (lines.size() == 4) {
        CHECK(lines[3].find("---- END ----"));
    }
}
