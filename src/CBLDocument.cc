//
// CBLDocument.cc
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

#include "CBLDocument.h"
#include "CBLPrivate.h"
#include "CBLCollection_Internal.hh"
#include "CBLDocument_Internal.hh"
#include "CBLBlob_Internal.hh"
#include "c4BlobStore.hh"
#include "c4Private.h"
#include "betterassert.hh"
#include <mutex>

#ifdef COUCHBASE_ENTERPRISE
#include "CBLEncryptable_Internal.hh"
#else
// Used in saveBlobsAndCheckEncryptables(). As FLDict_IsEncryptableValue() is not available
// in CE, define a function macro to allow the function to compile in CE.
#define FLDict_IsEncryptableValue(d) false
#endif


using namespace std;
using namespace fleece;
using namespace cbl_internal;


// The methods below cannot be in C4Document_Internal.hh because they depend on
// CBLDatabase_Internal.hh, which would create a circular header dependency.


CBLDocument::CBLDocument(slice docID, CBLCollection *collection, C4Document *c4doc, bool isMutable)
:_docID(docID)
,_collection(collection)
,_c4doc(c4doc)
,_mutable(isMutable)
{
    if (c4doc) {
        c4doc->extraInfo() = {this, nullptr};
        _revID = c4doc->selectedRev().revID;
    }
}


CBLDocument::~CBLDocument() {
    auto c4doc = _c4doc.useLocked();
    if (c4doc)
        c4doc->extraInfo() = {nullptr, nullptr};
}


#pragma mark - PROPERTIES:


CBLDatabase* _cbl_nullable CBLDocument::database() const {
    return _collection ? _collection->database() : nullptr;
}


#pragma mark - REVISION HISTORY:


alloc_slice CBLDocument::getRevisionHistory() const {
    if (!_collection) {
        return fleece::nullslice;
    }
    
    auto doc = _collection->getDocument(_docID, false, true);
    if (!doc) {
        return fleece::nullslice;
    }
    
    auto c4doc = doc->_c4doc.useLocked();
    assert(c4doc);
    return c4doc->getRevisionHistory(UINT_MAX, nullptr, 0);
}


#pragma mark - Utils:


void CBLDocument::checkCollectionMatches(CBLCollection* _cbl_nullable myCol, CBLCollection *colParam) {
    if (myCol && (*myCol != *colParam)) {
        C4Error::raise(LiteCoreDomain, kC4ErrorInvalidParameter,
            "The collection for save or delete does not match the document’s collection or belongs to a different database instance.");
    }
}


#pragma mark - SAVING:


bool CBLDocument::save(CBLCollection* collection, const SaveOptions &opt) {
    Retained<C4Document> orignalDoc = nullptr, savingDoc = nullptr;
    bool success = false, retrying = false;
    
    do {
        bool handleConflictNeeded = false;
        RetainedConst<CBLDocument> conflictingDoc = nullptr;
        
        // Note: shared lock b/w database and collection
        collection->useLocked([&](C4Collection* c4col) {
            auto c4db = c4col->getDatabase();
            C4Database::Transaction t(c4db);
            
            auto c4doc = _c4doc.useLocked();
            
            if (!retrying) {
                // validate the precondition :
                // 1. The document should be in the same collection it was passed to the method.
                // 2. The document should be mutable.
                // 3. The document should exist when deleting.
                if (opt.deleting) {
                    if (!c4doc)
                        C4Error::raise(LiteCoreDomain, kC4ErrorNotFound, "Document is not in any database");
                } else {
                    checkMutable();
                }
                checkCollectionMatches(_collection, collection);
                
                orignalDoc = c4doc.get();
                savingDoc = orignalDoc;
            } else {
                // Make sure that the doc hasn't been changed during the save process:
                precondition(c4doc.get() == orignalDoc);
            }
            
            alloc_slice body;
            C4RevisionFlags revFlags;
            if (!opt.deleting) {
                body = encodeBody(collection->database(), c4db, false, revFlags);
            } else {
                revFlags = kRevDeleted;
            }
            
            retrying = false;
            Retained<C4Document> newDoc = nullptr;
            conflictingDoc = nullptr;
            
            if (savingDoc) {
                // Update existing doc:
                newDoc = savingDoc->update(body, revFlags);
            } else {
                // Create new doc:
                C4DocPutRequest rq = {};
                rq.allocedBody = {body.buf, body.size};
                rq.docID = _docID;
                rq.revFlags = revFlags;
                rq.save = true;
                C4Error c4err;
                newDoc = c4col->putDocument(rq, nullptr, &c4err);
                if (!newDoc && c4err != C4Error{LiteCoreDomain, kC4ErrorConflict})
                    C4Error::raise(c4err);
            }
            
            if (newDoc) {
                // Success:
                t.commit();
                _collection = collection;
                // HACK: Replace the inner reference of the c4doc with the one from newDoc.
                c4doc.get() = std::move(newDoc);
                _revID = c4doc->selectedRev().revID;
                success = true;
            } else {
                // Conflict:
                if (opt.concurrency == kCBLConcurrencyControlLastWriteWins) {
                    // Last-write-wins; load current revision and retry:
                    savingDoc = c4col->getDocument(_docID, true, kDocGetCurrentRev);
                    retrying = true;
                } else if (opt.conflictHandler) {
                    // Get the conflicting doc used when calling the conflict handler.
                    // The call to the conflict handler will be done outside the database's lock.
                    conflictingDoc = collection->getDocument(_docID, true);
                    handleConflictNeeded = true;
                }
            }
        });
        
        // Conflict will be handled outside document and database lock:
        if (handleConflictNeeded) {
            // Use conflict handler to solve the conflict; conflictingDoc should be non-null
            // here and we are checking here just for precuation.
            if (_usuallyTrue(conflictingDoc != nullptr) && conflictingDoc->revisionFlags() & kRevDeleted) {
                conflictingDoc = nullptr;
            }
            
            if (opt.conflictHandler(opt.context, this, conflictingDoc)) {
                if (_usuallyTrue(conflictingDoc != nullptr)) {
                    savingDoc = const_cast<C4Document*>(conflictingDoc->_c4doc.useLocked().get());
                    conflictingDoc = nullptr;
                } else {
                    savingDoc = nullptr;
                }
                retrying = true;
            }
        }
    } while (retrying);
    
    return success;
}


alloc_slice CBLDocument::encodeBody(CBLDatabase* db,
                                    C4Database* c4db,
                                    bool releaseNewBlob,
                                    C4RevisionFlags &outRevFlags) const
{
    auto c4doc = _c4doc.useLocked();
    // Save new blobs and check encryptables in arrays:
    bool hasBlobs = saveBlobsAndCheckEncryptables(db, releaseNewBlob);
    outRevFlags = hasBlobs ? kRevHasAttachments : 0;

    // Now encode the properties to Fleece:
    SharedEncoder enc(c4db->sharedFleeceEncoder());
    enc.writeValue(properties());
    FLError flErr;
    alloc_slice body = enc.finish(&flErr);
    if (!body)
        C4Error::raise(FleeceDomain, flErr);
    return body;
}


#pragma mark - REPLICATOR CONFLICT RESOLUTION:


bool CBLDocument::resolveConflict(Resolution resolution, const CBLDocument * _cbl_nullable resolveDoc) {
    auto c4doc = _c4doc.useLocked();

    if (!c4doc)
        C4Error::raise(LiteCoreDomain, kC4ErrorNotFound, "Document has not been saved to a database");

    _properties = nullptr;
    _fromJSON = nullptr;

    // Remote Revision always win so that the resolved revision will not conflict with the remote:
    // Note: Use alloc_slice to retain the revIDs, as the c4doc's revID storage they point into
    // may be freed while the document is mutated inside c4doc->resolveConflict():
    alloc_slice winner(c4doc->selectedRev().revID), loser(c4doc->revID());

    // Note: shared lock b/w database and collection
    return _collection->useLocked<bool>([&](C4Collection *c4col) {
        auto c4db = c4col->getDatabase();
        C4Database::Transaction t(c4db);

        alloc_slice mergeBody;
        C4RevisionFlags mergeFlags = 0;
        // When useLocal (local wins) or useMerge is true, the new revision will be created
        // under the remote branch which is the winning branch. When useRemote (remote wins)
        // is true, the remote revision will be kept as is and the losing branch will be pruned.
        if (resolution != Resolution::useRemote) {
            if (resolveDoc) {
                mergeBody = resolveDoc->encodeBody(_collection->database(), c4db, true, mergeFlags);
            } else {
                mergeBody = alloc_slice(size_t(0));
                mergeFlags = kRevDeleted;
            }
        }

        try {
            c4doc->resolveConflict(winner, loser, mergeBody, mergeFlags);
        } catch (...) {
            C4Error err = C4Error::fromCurrentException();
            if (err == C4Error{LiteCoreDomain, kC4ErrorConflict})
                return false;
            throw;
        }

        c4doc->save();
        t.commit();
        return true;
    });
}


#pragma mark - BLOBS:


using UnretainedValueToBlobMap = litecore::access_lock<std::unordered_map<slice, CBLNewBlob*>>;

static UnretainedValueToBlobMap& newBlobs() {
    static UnretainedValueToBlobMap sNewBlobs;
    return sNewBlobs;
}


void CBLDocument::registerNewBlob(CBLNewBlob* blob) {
    newBlobs().useLocked()->insert({blob->digest(), blob});
}


void CBLDocument::unregisterNewBlob(CBLNewBlob* blob) {
    newBlobs().useLocked().get().erase(blob->digest());
}


CBLNewBlob* CBLDocument::findNewBlob(FLDict dict) {
    if (!Dict(dict).asMutable())
        return nullptr;
    return newBlobs().useLocked<CBLNewBlob*>([dict](auto &newBlobs) -> CBLNewBlob* {
        auto digest = Dict(dict)[kCBLBlobDigestProperty].asString();
        assert(digest);
        auto i = newBlobs.find(digest);
        if (i == newBlobs.end()) {
            CBL_Log(kCBLLogDomainDatabase, kCBLLogWarning,
                    "New blob instance looked up with digest '%.*s' was not found; the blob might have already been installed.",
                    FMTSLICE(digest));
            return nullptr;
        }
        return i->second;
    });
}


CBLBlob* CBLDocument::getBlob(FLDict dict, const C4BlobKey &key) {
    auto c4doc = _c4doc.useLocked();
    // Is it already registered by a previous call to getBlob?
    if (auto i = _blobs.find(dict); i != _blobs.end())
        return i->second;

    // Verify it's either a blob or an old-style attachment:
    if (!C4Blob::isBlob(dict) && !C4Blob::isAttachmentIn(dict, properties()))
        return nullptr;
    
    // If database is null, return null:
    auto db = database();
    if (!db)
        return nullptr;

    // Create a new CBLBlob and remember it:
    auto blob = new CBLBlob(db, dict, key);
    _blobs.insert({dict, retained(blob)});
    return blob;
}


#ifdef COUCHBASE_ENTERPRISE

CBLEncryptable* CBLDocument::getEncryptableValue(FLDict dict) {
    if (!CBLEncryptable::isEncryptableValue(dict))
          return nullptr;
          
    auto c4doc = _c4doc.useLocked();
    
    // Is it already registered by a previous call to getEncryptableValue?
    if (auto i = _encryptables.find(dict); i != _encryptables.end())
        return i->second;
    
    // Create a new CBLEncryptable and remember it:
    auto prop = new CBLEncryptable(dict);
    _encryptables.insert({dict, retained(prop)});
    return prop;
}

#endif


bool CBLDocument::saveBlobsAndCheckEncryptables(CBLDatabase *db, bool releaseNewBlob) const {
    // Walk through the Fleece object tree, looking for new mutable blob Dicts to install,
    // and also checking if there are any blobs at all (mutable or not.)
    // Once we've found at least one blob, we can skip checking blobs in immutable collections,
    // because they can't contain new blobs.
    //
    // If the releaseNewBlob is enabled, the new blob will be released after it is installed.
    //
    // (EE Only) While walking through the object tree, check if there are any encryptables in
    // an array and throw an unsupported error if that occurs.
    //
    // Note: If the same new blob is used in multiple places inside the object tree, the
    // blob will be installed only once as it will be unregistered from global sNewBlobs
    // HashTable.
    auto c4doc = _c4doc.useLocked();
    if (!isMutable())
        return C4Blob::dictContainsBlobs(properties());
    
    bool foundBlobs = false;
    
    // In EE, encryptables need to be checked, but this adds a lot of overhead so in CE this will be
    // skipped. By defining a constant bool like this, the compiler should optimize out all of the
    // branches that have a constant `false` inside, and the source can maintain a bit more readability.
#ifdef COUCHBASE_ENTERPRISE
    const bool validateEncryptables = true;
#else
    const bool validateEncryptables = false;
#endif

    for (DeepIterator i(properties()); i; ++i) {
        Dict dict = i.value().asDict();
        if (dict) {
            if (validateEncryptables && FLDict_IsEncryptableValue(dict)) {
                // Encryptables inside of an array are not supported!
                if (i.parent().asArray()) {
                    C4Error::raise(LiteCoreDomain, kC4ErrorUnsupported,
                                   "No support for encryptables in an array");
                }
                i.skipChildren();
            } else if (!dict.asMutable()) {
                // This is an immutable dictionary, so it cannot be a new blob. It might be
                // the first existing blob detected though.
                if (!foundBlobs) {
                    foundBlobs = FLDict_IsBlob(dict);
                    if (foundBlobs) {
                        i.skipChildren();
                    }
                } else if (!validateEncryptables) {
                    // Found at least one blob, and the current dictionary is immutable
                    // Since encryptable validation is disabled, the rest of the keys
                    // are not relevant.
                    i.skipChildren();
                }
            } else if (FLDict_IsBlob(dict)) {
                // This is a mutable dictionary. Check if it's a new blob, so install it
                // if it hasn't been already.
                foundBlobs = true;
                CBLNewBlob *newBlob = findNewBlob(dict);
                if (newBlob) {
                    newBlob->install(db);
                    if (releaseNewBlob) {
                        CBLBlob_Release(newBlob);
                    }
                }
                i.skipChildren();
            }
        } else if (!validateEncryptables && !i.value().asArray().asMutable()) {
            // If one blob has been found already, there is nothing interesting inside of
            // an immutable array. It will only contain previously saved information.
            if (foundBlobs) {
                i.skipChildren();
            }
        }
    }
    return foundBlobs;
}
