//
// CBLLogSinks.cc
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

std::atomic<CBLLogSinks::LogAPIStyle> CBLLogSinks::_sAPIStyle {LogAPIStyle::none};

std::atomic<CBLLogLevel> CBLLogSinks::_sDomainsLogLevel{kCBLLogNone};
std::atomic<CBLLogLevel> CBLLogSinks::_sCallbackLogLevel{kCBLLogNone};

CBLConsoleLogSink CBLLogSinks::_sConsoleSink { kCBLLogWarning };
CBLCustomLogSink CBLLogSinks::_sCustomSink { kCBLLogNone };
CBLFileLogSink CBLLogSinks::_sFileSink { kCBLLogNone, kFLSliceNull };

std::shared_mutex CBLLogSinks::_sMutex;

// Note: "TLS" is left at LiteCore's default level; its logs are still mapped to Network.
static const char* kC4ExtraDomains[] = { "SyncBusy", "Changes", "BLIPMessages", "Zip" };

const std::vector<C4LogDomain>& CBLLogSinks::c4LogDomains() {
    static const std::vector<C4LogDomain> c4Domains = [] {
        // MUST match CBLLogDomain order
        std::vector<C4LogDomain> vec = {
            kC4DatabaseLog,
            kC4QueryLog,
            kC4SyncLog,
            kC4WebSocketLog
        };
        C4LogDomain listenerDomain = c4log_getDomain("Listener", true);
        vec.push_back(listenerDomain);
        return vec;
    }();
    return c4Domains;
}

static once_flag initFlag;
void CBLLogSinks::init() {
    call_once(initFlag, [](){
        updateLogLevels();
    });
}

void CBLLogSinks::setConsoleLogSink(CBLConsoleLogSink consoleSink) {
    _setConsoleLogSink(consoleSink);
    updateLogLevels();
}

CBLConsoleLogSink CBLLogSinks::consoleLogSink(void) {
    std::shared_lock<std::shared_mutex> lock(_sMutex);
    return _sConsoleSink;
}

void CBLLogSinks::setCustomLogSink(CBLCustomLogSink customSink) {
    _setCustomLogSink(customSink);
    updateLogLevels();
}

CBLCustomLogSink CBLLogSinks::customLogSink(void) {
    std::shared_lock<std::shared_mutex> lock(_sMutex);
    return _sCustomSink;
}

void CBLLogSinks::setFileLogSink(CBLFileLogSink fileSink) {
    _setFileLogSink(fileSink);
    updateLogLevels();
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
        customSink = _sCustomSink;
    }
    
    // To cosole and custom log:
    logToConsoleLogSink(consoleSink, domain, level, msg);
    logToCustomLogSink(customSink, domain, level, msg);
    
    // To file log:
    C4LogDomain c4LogDomain = toC4LogDomain(domain);
    c4slog(c4LogDomain, C4LogLevel(level), slice(msg));
}

void CBLLogSinks::validateAPIUsage(LogAPIStyle style) {
    LogAPIStyle current = _sAPIStyle.load();
    if (current == LogAPIStyle::none) {
        if (!_sAPIStyle.compare_exchange_strong(current, style)) {
            if (current != style) {
                C4Error::raise(LiteCoreDomain, kC4ErrorUnsupported,
                               "Cannot use both new and old logging API simultaneously");
            }
        }
    } else if (current != style) {
        C4Error::raise(LiteCoreDomain, kC4ErrorUnsupported,
                       "Cannot use both new and old logging API simultaneously");
    }
}

void CBLLogSinks::reset(void) {
    setConsoleLogSink({ kCBLLogWarning });
    setCustomLogSink({ kCBLLogNone });
    setFileLogSink({ kCBLLogNone, kFLSliceNull });
    _sAPIStyle.store(LogAPIStyle::none);
}

void CBLLogSinks::logWithC4Log(CBLLogDomain domain, CBLLogLevel level, const char *msg) {
    C4LogDomain c4Domain = toC4LogDomain(domain);
    c4log(c4Domain, C4LogLevel(level), "%s", msg);
}

// Private Functions

void CBLLogSinks::_setConsoleLogSink(const CBLConsoleLogSink& consoleSink) {
    std::unique_lock<std::shared_mutex> lock(_sMutex);
    _sConsoleSink = consoleSink;
}

void CBLLogSinks::_setCustomLogSink(const CBLCustomLogSink& customSink) {
    std::unique_lock<std::shared_mutex> lock(_sMutex);
    _sCustomSink = customSink;
}

void CBLLogSinks::_setFileLogSink(const CBLFileLogSink& fileSink) {
    std::unique_lock<std::shared_mutex> lock(_sMutex);
    
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
    
    C4LogFileOptions c4opt {};
    c4opt.log_level         = C4LogLevel(fileSink.level);
    c4opt.base_path         = fileSink.directory;
    c4opt.use_plaintext     = fileSink.usePlaintext;
    c4opt.max_size_bytes    = fileSink.maxSize > 0 ? fileSink.maxSize : kCBLDefaultFileLogSinkMaxSize;
    
    auto maxKeptFiles = fileSink.maxKeptFiles > 0 ? fileSink.maxKeptFiles : kCBLDefaultFileLogSinkMaxKeptFiles;
    c4opt.max_rotate_count  = maxKeptFiles - 1;
    
    string header = "Generated by Couchbase Lite for C / " + userAgentHeader();
    c4opt.header            = slice(header);
    
    C4Error err {};
    if (!c4log_writeToBinaryFile(c4opt, &err)) {
        C4Error::raise(err);
    }
    
    _sFileSink = fileSink;
}

void CBLLogSinks::updateLogLevels() {
    // A shared-read-lock (allows multiple reads) is used here to ensure that _sConsoleSink,
    // _sCustomSink, and _sFileSink from being modified while this function is executing.
    //
    // The c4LogCallback() also uses shared-read-lock when accessing the _sConsoleSink and
    // _sCustomSink, therefore, the deadlock will not happen if the c4LogCallback() is called
    // at the same time from a different thread while this function is updating to LiteCore's
    // callback log level.
    std::shared_lock<std::shared_mutex> lock(_sMutex);
    
    CBLLogLevel customLogLevel = _sCustomSink.callback != nullptr ? _sCustomSink.level : kCBLLogNone;
    CBLLogLevel callbackLogLevel = std::min(_sConsoleSink.level, customLogLevel);
    CBLLogLevel domainsLogLevel = std::min(callbackLogLevel, _sFileSink.level);
    C4LogLevel c4LogLevel = C4LogLevel(domainsLogLevel);
    
    if (_sDomainsLogLevel.load() != domainsLogLevel) {
        auto domains = c4LogDomains();
        for (C4LogDomain domain : domains) {
            c4log_setLevel(domain, c4LogLevel);
        }

        for(const auto* extraName : kC4ExtraDomains) {
            C4LogDomain domain = c4log_getDomain(extraName, false);
            if (domain) {
                c4log_setLevel(domain, c4LogLevel);
            }
        }
        _sDomainsLogLevel.store(domainsLogLevel);
    }
    
    if (_sCallbackLogLevel.load() != callbackLogLevel) {
        c4log_writeToCallback(c4LogLevel, &c4LogCallback, true);
        _sCallbackLogLevel.store(callbackLogLevel);
    }
}

void CBLLogSinks::c4LogCallback(C4LogDomain c4Domain, C4LogLevel c4Level, const char *msg, va_list args) {
    CBLConsoleLogSink consoleSink;
    CBLCustomLogSink customSink;
    {
        std::shared_lock<std::shared_mutex> lock(_sMutex);
        consoleSink = _sConsoleSink;
        customSink = _sCustomSink;
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
    LogDecoder::writeTimestamp(LogDecoder::now(), os);
    LogDecoder::writeHeader(levelName, domainName.c_str(), os);
    os << msg << '\n';
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
    
    auto domainName = c4log_getDomainName(c4Domain);
    auto it = domainMap.find(domainName);
    if (it != domainMap.end()) {
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
