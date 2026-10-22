//
// CBLLog.cc
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

#include "CBLLog.h"
#include "CBLLog_Internal.hh"
#include "CBLLogSinks_Internal.hh"
#include "CBLPrivate.h"
#include <cstdlib>

using namespace std;
using namespace fleece;
using namespace litecore;

void CBLLog_Init() {
    CBLLogSinks::init();
}

void CBL_Log(CBLLogDomain domain, CBLLogLevel level, const char *format, ...) CBLAPI {
    char *message = nullptr;
    va_list args;
    va_start(args, format);
    vasprintf(&message, format, args);
    va_end(args);
    CBLLogSinks::log(domain, level, message);
    free(message);
}

void CBL_LogMessage(CBLLogDomain domain, CBLLogLevel level, FLString message) CBLAPI {
    if (message.buf == nullptr)
        return;
    CBL_Log(domain, level, "%.*s", (int) message.size, (char *) message.buf);
}

extern "C" CBL_PUBLIC std::atomic_int gC4ExpectExceptions;

/** Private API */
void CBLLog_BeginExpectingExceptions() CBLAPI {
    ++gC4ExpectExceptions;
    c4log_warnOnErrors(false);
}

/** Private API */
void CBLLog_EndExpectingExceptions() CBLAPI {
    if (--gC4ExpectExceptions == 0)
        c4log_warnOnErrors(true);
}

/** Private API */
void CBLLog_Reset(void) CBLAPI {
    CBLLogSinks::reset();
}

/** Private API */
void CBLLog_LogWithC4Log(CBLLogDomain domain, CBLLogLevel level, const char *message) CBLAPI {
    CBLLogSinks::logWithC4Log(domain, level, message);
}

/** Private API */
bool CBLLog_LogWithC4LogDomain(const char* domainName, CBLLogLevel level, const char *message) CBLAPI {
    C4LogDomain domain = c4log_getDomain(domainName, false);
    if (!domain) {
        return false;
    }
    c4log(domain, C4LogLevel(level), "%s", message);
    return true;
}
