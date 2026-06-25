//
// CBLLogSinks.cc
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

#include "CBLLogSinks_Internal.hh"
#include "CBLPrivate.h"
#include "CBLUserAgent.hh"
#include "FilePath.hh"
#include "LogDecoder.hh"
#include "ParseDate.hh"

#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <iostream>
#include <mutex>

using namespace std;
using namespace fleece;
using namespace litecore;

CBLLogLevel CBLLogSinks::_sDomainsLogLevel { kCBLLogNone };
CBLLogLevel CBLLogSinks::_sCallbackLogLevel{ kCBLLogNone };

CBLConsoleLogSink CBLLogSinks::_sConsoleSink { kCBLLogWarning };
CBLCustomLogSink CBLLogSinks::_sCustomSink { kCBLLogNone };
CBLFileLogSink CBLLogSinks::_sFileSink { kCBLLogNone, kFLSliceNull };

alloc_slice CBLLogSinks::_sLogFileDir;

std::shared_mutex CBLLogSinks::_sMutex;
std::mutex        CBLLogSinks::_sStreamOutputMutex;

static const char* kC4LogDomains[] = { "DB", "Query", "Sync", "WS", "Listener", "SyncBusy",
                                       "Changes", "BLIPMessages", "TLS", "Zip" };

const std::vector<C4LogDomain>& CBLLogSinks::c4LogDomains() {
    static const std::vector<C4LogDomain> c4Domains = [] {
        std::vector<C4LogDomain> vec;
        vec.reserve(std::size(kC4LogDomains));
        for (const char* name : kC4LogDomains) {
            C4LogDomain domain = c4log_getDomain(name, true);
            if (domain) {
                vec.push_back(domain);
            }
        }
        return vec;
    }();
    return c4Domains;
}

static once_flag initFlag;
void CBLLogSinks::init() {
    call_once(initFlag, [](){
        std::unique_lock<std::shared_mutex> lock(_sMutex);
        updateLogLevelsNoLock();
    });
}

void CBLLogSinks::setConsoleLogSink(CBLConsoleLogSink consoleSink) {
    std::unique_lock<std::shared_mutex> lock(_sMutex);
    _sConsoleSink = consoleSink;
    updateLogLevelsNoLock();
}

CBLConsoleLogSink CBLLogSinks::consoleLogSink(void) {
    std::shared_lock<std::shared_mutex> lock(_sMutex);
    return _sConsoleSink;
}

void CBLLogSinks::setCustomLogSink(CBLCustomLogSink customSink) {
    std::unique_lock<std::shared_mutex> lock(_sMutex);
    _sCustomSink = customSink;
    updateLogLevelsNoLock();
}

CBLCustomLogSink CBLLogSinks::customLogSink(void) {
    std::shared_lock<std::shared_mutex> lock(_sMutex);
    return _sCustomSink;
}

void CBLLogSinks::setFileLogSink(CBLFileLogSink fileSink) {
    std::unique_lock<std::shared_mutex> lock(_sMutex);
    setFileLogSinkNoLock(fileSink);
    updateLogLevelsNoLock();
}

CBLFileLogSink CBLLogSinks::fileLogSink(void) {
    std::shared_lock<std::shared_mutex> lock(_sMutex);
    return _sFileSink;
}

void CBLLogSinks::log(CBLLogDomain domain, CBLLogLevel level, const char *msg) {
    CBLConsoleLogSink consoleSink;
    CBLCustomLogSink customSink;
    {
        std::shared_lock<std::shared_mutex> lock(_sMutex);
        consoleSink = _sConsoleSink;
        customSink  = _sCustomSink;
    }
    
    // To console and custom log:
    logToConsoleLogSink(consoleSink, domain, level, msg);
    logToCustomLogSink(customSink, domain, level, msg);
    
    // To file log:
    C4LogDomain c4LogDomain = toC4LogDomain(domain);
    c4slog(c4LogDomain, C4LogLevel(level), slice(msg));
}

void CBLLogSinks::reset(void) {
    setConsoleLogSink({ kCBLLogWarning });
    setCustomLogSink({ kCBLLogNone });
    setFileLogSink({ kCBLLogNone, kFLSliceNull });
}

void CBLLogSinks::logWithC4Log(CBLLogDomain domain, CBLLogLevel level, const char *msg) {
    C4LogDomain c4Domain = toC4LogDomain(domain);
    c4log(c4Domain, C4LogLevel(level), "%s", msg);
}

// Private Functions

void CBLLogSinks::setFileLogSinkNoLock(const CBLFileLogSink& fileSink) {
    if (fileSink.level != kCBLLogNone) {
        auto directory = slice(fileSink.directory);
        if (!directory.empty()) {
            FilePath path(directory, "");
            if (!path.exists() && !path.mkdir()) {
                C4Error::raise(LiteCoreDomain, kC4ErrorIOError,
                               "Failed to create log directory at path: %s", path.path().c_str());
            }
        }
    }
    
    alloc_slice logFileDir(fileSink.directory); // Make an owned copy of directory
    
    C4LogFileOptions c4opt {};
    c4opt.log_level         = C4LogLevel(fileSink.level);
    c4opt.base_path         = logFileDir;
    c4opt.use_plaintext     = fileSink.usePlaintext;
    c4opt.max_size_bytes    = fileSink.maxSize > 0 ? fileSink.maxSize : kCBLDefaultFileLogSinkMaxSize;
    
    auto maxKeptFiles = fileSink.maxKeptFiles > 0 ? fileSink.maxKeptFiles : kCBLDefaultFileLogSinkMaxKeptFiles;
    c4opt.max_rotate_count  = maxKeptFiles - 1;
    
    string header = "Generated by Couchbase Lite for C / " + userAgentHeader();
    c4opt.header            = slice(header); // Will be copied by c4log_writeToBinaryFile
    
    C4Error err {};
    if (!c4log_writeToBinaryFile(c4opt, &err)) {
        C4Error::raise(err);
    }
    
    _sFileSink = fileSink;
    
    // Point to the owned copy of directory
    _sLogFileDir = std::move(logFileDir);
    _sFileSink.directory = _sLogFileDir;
}

void CBLLogSinks::updateLogLevelsNoLock() {
    CBLLogLevel customLogLevel = _sCustomSink.callback != nullptr ? _sCustomSink.level : kCBLLogNone;
    CBLLogLevel callbackLogLevel = std::min(_sConsoleSink.level, customLogLevel);
    CBLLogLevel domainsLogLevel = std::min(callbackLogLevel, _sFileSink.level);
    C4LogLevel c4LogLevel = C4LogLevel(domainsLogLevel);
    
    if (_sDomainsLogLevel != domainsLogLevel) {
        auto domains = c4LogDomains();
        for (C4LogDomain domain : domains) {
            c4log_setLevel(domain, c4LogLevel);
        }
        _sDomainsLogLevel = domainsLogLevel;
    }
    
    if (_sCallbackLogLevel != callbackLogLevel) {
        c4log_writeToCallback(c4LogLevel, &c4LogCallback, true);
        _sCallbackLogLevel =callbackLogLevel;
    }
}

void CBLLogSinks::c4LogCallback(C4LogDomain c4Domain, C4LogLevel c4Level, const char *msg, va_list args) {
    CBLConsoleLogSink consoleSink;
    CBLCustomLogSink customSink;
    {
        std::shared_lock<std::shared_mutex> lock(_sMutex);
        consoleSink = _sConsoleSink;
        customSink  = _sCustomSink;
    }

    auto domain = toCBLLogDomain(c4Domain);
    CBLLogLevel level = CBLLogLevel(c4Level);
    
    logToConsoleLogSink(consoleSink, domain, level, msg);
    logToCustomLogSink(customSink, domain, level, msg);
}

void CBLLogSinks::logToConsoleLogSink(CBLConsoleLogSink& consoleSink,
                                      CBLLogDomain domain, CBLLogLevel level, const char *msg)
{
    if (level < consoleSink.level ||
        (consoleSink.domains > 0 && (consoleSink.domains & (1 << domain)) == 0)) {
        return;
    }
    
    auto domainName = toLogDomainName(domain);
    auto levelName = toLogLevelName(level);
#ifdef __ANDROID__
    string tag("CouchbaseLite");
    if (!domainName.empty()) {
        tag += " [" + domainName + "]";
    }
    static const int androidLevels[5] = {ANDROID_LOG_DEBUG, ANDROID_LOG_INFO,
                                         ANDROID_LOG_INFO, ANDROID_LOG_WARN,
                                         ANDROID_LOG_ERROR};
    __android_log_write(androidLevels[(int) level], tag.c_str(), msg);
#else
    ostream& os = level < kCBLLogWarning ? cout : cerr;
    {
        std::scoped_lock lock(_sStreamOutputMutex);
        LogDecoder::writeTimestamp(LogDecoder::now(), os);
        LogDecoder::writeHeader(levelName, domainName.c_str(), os);
        os << msg << '\n';
    }
#endif
}

void CBLLogSinks::logToCustomLogSink(CBLCustomLogSink& customSink,
                                     CBLLogDomain domain, CBLLogLevel level, const char *msg)
{
    if (customSink.callback == nullptr || level < customSink.level ||
        (customSink.domains > 0 && (customSink.domains & (1 << domain)) == 0)) {
        return;
    }
    customSink.callback(domain, level, FLStr(msg));
}

C4LogDomain CBLLogSinks::toC4LogDomain(CBLLogDomain domain) {
    const auto& domains = c4LogDomains();
    size_t index = static_cast<size_t>(domain);
    assert(index < domains.size());
    return domains[index];
}

CBLLogDomain CBLLogSinks::toCBLLogDomain(C4LogDomain c4Domain) {
    const char* domainName = c4log_getDomainName(c4Domain);
    if (!domainName) {
        return kCBLLogDomainDatabase;
    }
    
    static const unordered_map<string_view, CBLLogDomain> domainMap = {
        {"DB", kCBLLogDomainDatabase},
        {"Query", kCBLLogDomainQuery},
        {"Sync", kCBLLogDomainReplicator},
        {"SyncBusy", kCBLLogDomainReplicator},
        {"Changes", kCBLLogDomainDatabase},
        {"BLIP", kCBLLogDomainNetwork},
        {"BLIPMessages", kCBLLogDomainNetwork},
        {"WS", kCBLLogDomainNetwork},
        {"Zip", kCBLLogDomainNetwork},
        {"TLS", kCBLLogDomainNetwork},
        {"Listener", kCBLLogDomainListener}
    };
    
    std::string_view name{domainName};
    if (auto it = domainMap.find(name); it != domainMap.end()) {
        return it->second;
    }
    return kCBLLogDomainDatabase;
}

std::string CBLLogSinks::toLogDomainName(CBLLogDomain domain) {
    switch (domain) {
        case kCBLLogDomainDatabase:
            return "Database";
        case kCBLLogDomainQuery:
            return "Query";
        case kCBLLogDomainReplicator:
            return "Replicator";
        case kCBLLogDomainNetwork:
            return "Network";
        case kCBLLogDomainListener:
            return "Listener";
        default:
            return "Database";
    }
}

std::string CBLLogSinks::toLogLevelName(CBLLogLevel level) {
    switch (level) {
        case kCBLLogDebug:
            return "Debug";
        case kCBLLogVerbose:
            return "Verbose";
        case kCBLLogInfo:
            return "Info";
        case kCBLLogWarning:
            return "Warning";
        case kCBLLogError:
            return "Error";
        default:
            return "Info";
    }
}
