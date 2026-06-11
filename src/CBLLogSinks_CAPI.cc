//
// CBLLogSinks_CAPI.cc
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

void CBLLogSinks_SetConsole(CBLConsoleLogSink logSink) noexcept {
    try { CBLLogSinks::validateAPIUsage(CBLLogSinks::LogAPIStyle::newStyle); } catchAndAbort()
    CBLLogSinks::setConsoleLogSink(logSink);
}

CBLConsoleLogSink CBLLogSinks_Console(void) noexcept {
    try { CBLLogSinks::validateAPIUsage(CBLLogSinks::LogAPIStyle::newStyle); } catchAndAbort()
    return CBLLogSinks::consoleLogSink();
}

void CBLLogSinks_SetCustom(CBLCustomLogSink logSink) noexcept {
    try { CBLLogSinks::validateAPIUsage(CBLLogSinks::LogAPIStyle::newStyle); } catchAndAbort()
    CBLLogSinks::setCustomLogSink(logSink);
}

CBLCustomLogSink CBLLogSinks_CustomSink(void) noexcept {
    try { CBLLogSinks::validateAPIUsage(CBLLogSinks::LogAPIStyle::newStyle); } catchAndAbort()
    return CBLLogSinks::customLogSink();
}

void CBLLogSinks_SetFile(CBLFileLogSink logSink) noexcept {
    try { CBLLogSinks::validateAPIUsage(CBLLogSinks::LogAPIStyle::newStyle); } catchAndAbort()
    try { CBLLogSinks::setFileLogSink(logSink); } catchAndWarnNoReturn();
}

CBLFileLogSink CBLLogSinks_File(void) noexcept {
    try { CBLLogSinks::validateAPIUsage(CBLLogSinks::LogAPIStyle::newStyle); } catchAndAbort()
    return CBLLogSinks::fileLogSink();
}
