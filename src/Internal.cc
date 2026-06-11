//
// Internal.cc
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

#include "Internal.hh"
#include "CBLLog_Internal.hh"
#include "c4Base.hh"
#include "c4Log.h"
#include "fleece/Fleece.h"
#include "fleece/FLExpert.h"
#include "betterassert.hh"
#include "FilePath.hh"
#include <mutex>


using namespace fleece;

namespace cbl_internal {

    void BridgeException(const char *fnName, CBLError *outError) noexcept {
        C4Error error = C4Error::fromCurrentException();
        if (outError)
            *outError = external(error);
        else
            C4WarnError("Function %s() failed: %s", fnName, error.description().c_str());
    }

    alloc_slice convertJSON5(slice json5) {
        FLStringResult errMsg;
        FLError flError;
        alloc_slice json(FLJSON5_ToJSON(json5, &errMsg, nullptr, &flError));
        if (!json) {
            alloc_slice msg(std::move(errMsg));
            C4Error::raise(FleeceDomain, flError, "%.*s", FMTSLICE(msg));
        }
        return json;
    }

#ifdef __ANDROID__

    static CBLInitContext sInitContext;

    void initContext(CBLInitContext context) {
        if (sInitContext.filesDir != nullptr) {
            C4Error::raise(LiteCoreDomain, kC4ErrorUnsupported, "Context cannot be initialized more than once!");
        }
        precondition(context.filesDir != nullptr);
        precondition(context.tempDir != nullptr);
        
        CBLLog_Init();
        
        litecore::FilePath filesDir(context.filesDir, "");
        filesDir.mustExistAsDir();
        
        litecore::FilePath tempDir(context.tempDir, "");
        tempDir.mustExistAsDir();
        
        C4Error c4err;
        if (!c4_setTempDir(FLStr(context.tempDir), &c4err)) {
            C4Error::raise(c4err);
        }
        
        sInitContext = context;
        sInitContext.filesDir = strdup(context.filesDir);
        sInitContext.tempDir = strdup(context.tempDir);
    }

    const CBLInitContext* getInitContext() noexcept {
        if (!sInitContext.filesDir) {
            return nullptr;
        }
        return &sInitContext;
    }
        
#endif
}
