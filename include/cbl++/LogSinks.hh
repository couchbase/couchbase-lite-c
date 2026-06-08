//
// LogSinks.hh
//
// Copyright (c) 2025 Couchbase, Inc All rights reserved.
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

// VOLATILE API: Couchbase Lite C++ API is not finalized, and may change in
// future releases.

#pragma once
#include "cbl/CBLLogSinks.h"

CBL_ASSUME_NONNULL_BEGIN

namespace cbl {
    using ConsoleLogSink = CBLConsoleLogSink;
    using CustomLogSink  = CBLCustomLogSink;
    using FileLogSink    = CBLFileLogSink;

    /** Controls where Couchbase Lite writes its log messages. There are three independent
        sinks — console, custom (a user callback), and file — each configured separately.
        Disable a sink by setting its log level to \ref kCBLLogNone. */
    class LogSinks {
    public:
        /** Sets the console log sink. To disable it, set the sink's log level to \ref kCBLLogNone.
            @param sink  The console log sink configuration. */
        static void setConsole(const ConsoleLogSink& sink) {
            CBLLogSinks_SetConsole(sink);
        }

        /** Returns the current console log sink. It is enabled at the warning level for all
            domains by default. */
        static ConsoleLogSink console() {
            return CBLLogSinks_Console();
        }

        /** Sets the custom log sink, whose callback receives each log message. To disable it,
            set the sink's log level to \ref kCBLLogNone.
            @param sink  The custom log sink configuration. */
        static void setCustom(const CustomLogSink& sink) {
            CBLLogSinks_SetCustom(sink);
        }

        /** Returns the current custom log sink. It is disabled by default. */
        static CustomLogSink custom() {
            return CBLLogSinks_CustomSink();
        }

        /** Sets the file log sink, which writes log messages to files in a directory. To disable
            it, set the sink's log level to \ref kCBLLogNone.
            @param sink  The file log sink configuration. */
        static void setFile(const FileLogSink& sink) {
            CBLLogSinks_SetFile(sink);
        }

        /** Returns the current file log sink. It is disabled by default. */
        static FileLogSink file() {
            return CBLLogSinks_File();
        }
    };
}

CBL_ASSUME_NONNULL_END
