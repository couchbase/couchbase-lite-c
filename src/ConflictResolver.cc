//
// ConflictResolver.cc
//
// Copyright © 2019 Couchbase. All rights reserved.
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

#include "ConflictResolver.hh"
#include "CBLReplicator_Internal.hh"
#include "CBLDocument_Internal.hh"
#include "CBLCollection_Internal.hh"
#include "Internal.hh"
#include "c4DocEnumerator.hh"
#include "StringUtil.hh"
#include "Stopwatch.hh"
#include <string>
#include "betterassert.hh"


static const CBLDocument* defaultConflictResolver(void *context,
                                                  FLString documentID,
                                                  const CBLDocument *localDoc,
                                                  const CBLDocument *remoteDoc)
{
    const CBLDocument* resolved;
    if (remoteDoc == nullptr || localDoc == nullptr)
        resolved = nullptr;
    else if (remoteDoc->generation() > localDoc->generation())
        resolved = remoteDoc;
    else if (localDoc->generation() > remoteDoc->generation())
        resolved = localDoc;
    else if (FLSlice_Compare(localDoc->revisionID(), remoteDoc->revisionID()) > 0)
        resolved = localDoc;
    else
        resolved = remoteDoc;
    return resolved;
}

CBL_PUBLIC const CBLConflictResolver CBLDefaultConflictResolver = &defaultConflictResolver;


namespace cbl_internal {
    using namespace fleece;
    using namespace litecore;

    ConflictResolver::ConflictResolver(CBLCollection *collection,
                                       CBLConflictResolver customResolver,
                                       void* context,
                                       alloc_slice docID)
    :_collection(collection)
    ,_clientResolver(customResolver)
    ,_clientResolverContext(context)
    ,_docID(std::move(docID))
    {
        //SyncLog(Info, "ConflictResolver %p on %.*s", this, _docID.c_str());
    }


    ConflictResolver::ConflictResolver(CBLCollection *collection,
                                       CBLConflictResolver customResolver,
                                       void* context,
                                       const C4DocumentEnded &docEnded)
    :ConflictResolver(collection, customResolver, context, docEnded.docID)
    { }


    void ConflictResolver::runAsync(CompletionHandler completionHandler) noexcept {
        assert(completionHandler);
        _completionHandler = completionHandler;
        SyncLog(Info, "Scheduling async resolution of conflict in doc '%.*s'",
                FMTSLICE(_docID));
        c4_runAsyncTask([](void *context) { ((ConflictResolver*)context)->runNow(); },
                        this);
    }


    // Performs conflict resolution. Returns true on success, false on failure. Sets _error.
    bool ConflictResolver::runNow() {
        bool ok, inConflict = false;
        int retryCount = 0;
        try {
            do {
                // Create a CBLDocument that reflects the conflict revision:
                auto conflict = _collection->getMutableDocument(_docID);
                if (!conflict) {
                    SyncLog(Info, "Doc '%.*s' no longer exists, no conflict to resolve",
                            FMTSLICE(_docID));
                    return true;
                }
                
                ok = conflict->selectNextConflictingRevision();
                if (!ok) {
                    // Revision is gone or not a leaf: Conflict must be resolved, so stop
                    SyncLog(Info, "Conflict in doc '%.*s' already resolved, nothing to do",
                            FMTSLICE(_docID));
                    if (_completionHandler) {
                        _completionHandler(this);       // the handler will most likely delete me
                    }
                    return true;
                }

                // Now resolve the conflict:
                if (_clientResolver)
                    ok = customResolve(conflict);
                else
                    ok = defaultResolve(conflict);

                if (ok) {
                    _flags = conflict->revisionFlags();
                    inConflict = false;
                } else {
                    _error = external(C4Error::make(LiteCoreDomain, kC4ErrorConflict));
                    // If a local revision is saved at the same time we'll fail with a conflict, so retry:
                    inConflict = (++retryCount < 10);
                    if (inConflict) {
                        SyncLog(Warning, "%s conflict resolution of doc '%.*s' conflicted with newer saved"
                                " revision; retrying...",
                                (_clientResolver ? "Custom" : "Default"), FMTSLICE(_docID));
                    }
                }
            } while (inConflict);
        } catch (...) {
            C4Error::fromCurrentException(internal(&_error));
            ok = false;
        }

        if (ok) {
            SyncLog(Info, "Successfully resolved and saved doc '%.*s'", FMTSLICE(_docID));
            _error = {};
        } else {
            SyncLog(Error, "%s conflict resolution of doc '%.*s' failed: %s\n%s",
                    (_clientResolver ? "Custom" : "Default"),
                    FMTSLICE(_docID),
                    internal(_error).description().c_str(),
                    internal(_error).backtrace().c_str());
        }
        
        if (_completionHandler)
            _completionHandler(this);       // the handler will most likely delete me
        return ok;
    }


    // Performs default conflict resolution.
    // 1. Deleted wins
    // 2. Higher generation wins
    // 3. Higher revisionID wins
    bool ConflictResolver::defaultResolve(CBLDocument *conflict) {
        CBLDocument *remoteDoc = conflict;
        if (remoteDoc->revisionFlags() & kRevDeleted)
            remoteDoc = nullptr;
        
        auto localDoc = _collection->getDocument(_docID, false);
        if (localDoc && localDoc->revisionFlags() & kRevDeleted)
            localDoc = nullptr;
        
        auto resolved = defaultConflictResolver(_clientResolverContext, _docID, localDoc, remoteDoc);
        
        CBLDocument::Resolution resolution;
        if (resolved == remoteDoc)
            resolution = CBLDocument::Resolution::useRemote;
        else
            resolution = CBLDocument::Resolution::useLocal;
        
        return conflict->resolveConflict(resolution, resolved);
    }


    // Performs custom conflict resolution.
    bool ConflictResolver::customResolve(CBLDocument *conflict) {
        CBLDocument *remoteDoc = conflict;
        if (remoteDoc->revisionFlags() & kRevDeleted)
            remoteDoc = nullptr;
        
        auto localDoc = _collection->getDocument(_docID, false);
        if (localDoc && localDoc->revisionFlags() & kRevDeleted)
            localDoc = nullptr;

        // Call the custom resolver (this could take a long time to return)
        SyncLog(Verbose, "Calling custom conflict resolver for doc '%.*s' ...",
                FMTSLICE(_docID));
        Stopwatch st;
        const CBLDocument* resolved;
        try {
            resolved = _clientResolver(_clientResolverContext, _docID, localDoc, remoteDoc);
        } catch (...) {
            C4Error::raise(LiteCoreDomain, kC4ErrorUnexpectedError,
                           "Custom conflict handler threw an exception");
        }
        SyncLog(Info, "Custom conflict resolver for '%.*s' took %.0fms",
                FMTSLICE(_docID), st.elapsedMS());

        // Determine the resolution type:
        CBLDocument::Resolution resolution;
        if (resolved == localDoc)
            resolution = CBLDocument::Resolution::useLocal;
        else if (resolved == conflict)
            resolution = CBLDocument::Resolution::useRemote;
        else {
            if (resolved) {
                // Sanity check the resolved document:
                if (resolved->collection() && resolved->collection() != _collection) {
                    C4Error::raise(LiteCoreDomain, kC4ErrorInvalidParameter,
                                   "CBLDocument returned from custom conflict resolver belongs to"
                                   " wrong collection");
                }
                if (resolved->docID() != _docID) {
                    SyncLog(Warning, "The document ID '%.*s' of the resolved document is not matching "
                                     "with the document ID '%.*s' of the conflicting document.",
                                     FMTSLICE(resolved->docID()), FMTSLICE(_docID));
                }
            }
            resolution = CBLDocument::Resolution::useMerge;
        }

        // Actually resolve the conflict & save the document:
        bool result = conflict->resolveConflict(resolution, resolved);
        
        // The remoteDoc (conflict) and localDoc are backed by the RetainedConst and will be
        // released by the RetainedConst destructor. For the merged doc created and returned by
        // the custom conflict resolver, an explicit release is needed here.
        if (resolved != localDoc && resolved != remoteDoc)
            CBLDocument_Release(resolved);
        
        return result;
    }

    CBLReplicatedDocument ConflictResolver::result() const {
        CBLReplicatedDocument doc = {};
        auto spec = _collection->spec();
        doc.scope = spec.scope;
        doc.collection = spec.name;
        doc.ID = _docID;
        doc.error = _error;
        if (_flags & kRevDeleted)
            doc.flags |= kCBLDocumentFlagsDeleted;
        if (_flags & kRevPurged)
            doc.flags |= kCBLDocumentFlagsAccessRemoved;
        return doc;
    }

}
