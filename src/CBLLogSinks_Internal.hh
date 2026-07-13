//
// CBLLogSink_Internal.hh
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

#pragma once
#include "CBLLogSinks.h"
#include "Internal.hh"
#include "c4Log.h"

#include <algorithm>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

class CBLLogSinks {
public:
    static void init();
    
    static void setConsoleLogSink(CBLConsoleLogSink consoleSink);
    
    static CBLConsoleLogSink consoleLogSink(void);
    
    static void setCustomLogSink(CBLCustomLogSink customSink);
    
    static CBLCustomLogSink customLogSink(void);

    static void setFileLogSink(CBLFileLogSink fileSink); // May throw
    
    static CBLFileLogSink fileLogSink(void);
    
    static void log(CBLLogDomain domain, CBLLogLevel level, const char *msg);
    
    // For testing purpose
    static void reset(void);
    static void logWithC4Log(CBLLogDomain domain, CBLLogLevel level, const char *msg);
    
private:
    static CBLLogLevel _sDomainsLogLevel;
    static CBLLogLevel _sCallbackLogLevel;
    
    static CBLConsoleLogSink _sConsoleSink;
    static CBLCustomLogSink _sCustomSink;
    static CBLFileLogSink _sFileSink;
    static fleece::alloc_slice _sLogFileDir;
    
    static std::shared_mutex _sMutex;

    // Serializes all writes to std::cout/std::cerr across threads.
    // Two distinct benefits:
    // 1. Production: a log entry spans multiple << writes (timestamp, header, message);
    //    without a lock, concurrent threads interleave these into garbled output.
    // 2. Testing (Catch2): some reporters replace cout/cerr's rdbuf with a non-thread-safe
    //    std::ostringstream to capture output; concurrent writes without a lock cause
    //    heap corruption (e.g. ASan heap-use-after-free during buffer reallocation).
    // Note: Android logging uses __android_log_write (a syscall) and does not need this lock.
    static std::mutex _sStreamOutputMutex;
    
    static const std::vector<C4LogDomain>& c4LogDomains();
    static C4LogDomain toC4LogDomain(CBLLogDomain);
    
    static void setFileLogSinkNoLock(const CBLFileLogSink&);
    static void updateLogLevelsNoLock();
    
    static void c4LogCallback(C4LogDomain c4Domain, C4LogLevel c4Level, const char *msg, va_list args);
    static void logToConsoleLogSink(CBLConsoleLogSink&, CBLLogDomain, CBLLogLevel, const char *);
    static void logToCustomLogSink(CBLCustomLogSink&, CBLLogDomain, CBLLogLevel, const char *);
    
    static CBLLogDomain toCBLLogDomain(C4LogDomain);
    static std::string toLogDomainName(CBLLogDomain);
    static std::string toLogLevelName(CBLLogLevel);
};
