//
//  CBLDocument_Internal.hh
//
// Copyright (c) 2019 Couchbase, Inc All rights reserved.
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
#include "CBLDocument.h"
#include "Internal.hh"
#include "c4Document.hh"
#include "access_lock.hh"
#include "fleece/Expert.hh"
#include "fleece/Fleece.hh"
#include "fleece/Mutable.hh"
#include <climits>
#include <sstream>
#include <unordered_map>

CBL_ASSUME_NONNULL_BEGIN

struct CBLBlob;
struct CBLNewBlob;

#ifdef COUCHBASE_ENTERPRISE
struct CBLEncryptable;
#endif

struct CBLDocument final : public CBLRefCounted {
public:
    // Construct a new document (not in any database yet)
    CBLDocument(slice docID, bool isMutable)
    :CBLDocument(docID ? docID : C4Document::createDocID(), nullptr, nullptr, isMutable)
    { }


    // Mutable copy of another CBLDocument
    CBLDocument(const CBLDocument* otherDoc)
    :CBLDocument(otherDoc->_docID,
                 otherDoc->_collection,
                 const_cast<C4Document*>(otherDoc->_c4doc.useLocked().get()),
                 true)
    {
        if (otherDoc->isMutable()) {
            auto other = otherDoc->_c4doc.useLocked();
            if (otherDoc->_properties)
                _properties = otherDoc->_properties.asDict().mutableCopy(kFLDeepCopyImmutables);
        }
    }


    // Document loaded from db without a C4Document (e.g. a replicator validation callback)
    CBLDocument(CBLCollection *col,
                slice docID,
                slice revID,
                C4RevisionFlags revFlags,
                Dict body)
    :CBLDocument(docID, col, nullptr, false)
    {
        _properties = body;
        _revID = revID;
    }


    static CBLDocument* _cbl_nullable containing(Value value) {
        C4Document* doc = C4Document::containingValue(value);
        return doc ? (CBLDocument*)doc->extraInfo().pointer : nullptr;
    }


#pragma mark - Accessors:

    /** Return the database of the collection, or throw if the database was released. */
    CBLDatabase* _cbl_nullable database() const;
    
    CBLCollection* _cbl_nullable collection() const {return _collection;}
    bool exists() const                         {return _c4doc.useLocked().get() != nullptr;}
    bool isMutable() const                      {return _mutable;}
    slice docID() const                         {return _docID;}
    slice revisionID() const                    {return _revID;}
    unsigned generation() const                 {return C4Document::getRevIDGeneration(_revID);}
    
    uint64_t sequence() const {
        auto c4doc = _c4doc.useLocked();
        return c4doc ? static_cast<uint64_t>(c4doc->selectedRev().sequence) : 0;
    }

    alloc_slice canonicalRevisionID() const {
        auto c4doc = _c4doc.useLocked();
        if (!c4doc)
            return fleece::nullslice;
        const_cast<C4Document*>(c4doc.get())->selectCurrentRevision();
        return c4doc->getSelectedRevIDGlobalForm();
    }

    C4RevisionFlags revisionFlags() const {
        auto c4doc = _c4doc.useLocked();
        return c4doc ? c4doc->selectedRev().flags : (kRevNew | kRevLeaf);
    }
    
    alloc_slice getRevisionHistory() const;
    
#pragma mark - Properties:


    Dict properties() const {
        //TODO: Convert this to use C4Document::getProperties()
        auto c4doc = _c4doc.useLocked();
        if (!_properties) {
            slice storage;
            if (_fromJSON)
                storage = _fromJSON.data();
            else if (c4doc)
                storage = c4doc->getRevisionBody();

            if (storage)
                _properties = ValueFromData(storage);
            if (_mutable) {
                if (_properties)
                    _properties = _properties.asDict().mutableCopy();
                if (!_properties)
                    _properties = MutableDict::newDict();
            } else {
                if (!_properties)
                    _properties = Dict::emptyDict();
            }
        }
        return _properties.asDict();
    }


    MutableDict mutableProperties() {
        checkMutable();
        return properties().asMutable();
    }


    void setProperties(MutableDict d) {
        checkMutable();
        _properties = d;
    }


    alloc_slice propertiesAsJSON() const {
        auto c4doc = _c4doc.useLocked();
        if (!_mutable && c4doc)
            return c4doc->bodyAsJSON(false);        // fast path
        else
            return properties().toJSON();
    }


    void setPropertiesAsJSON(slice json) {
        checkMutable();
        Doc fromJSON = Doc::fromJSON(json);
        if (!fromJSON)
            C4Error::raise(FleeceDomain, kFLJSONError, "Invalid JSON");
        auto c4doc = _c4doc.useLocked(); // lock mutex
        // Store the transcoded Fleece and clear _properties. If app accesses properties(),
        // it'll get a mutable version of this.
        _fromJSON = fromJSON;
        _properties = nullptr;
    }


#pragma mark - Blobs:


    CBLBlob*  _cbl_nullable getBlob(FLDict dict, const C4BlobKey&);

    static void registerNewBlob(CBLNewBlob* blob);

    static void unregisterNewBlob(CBLNewBlob* blob);

    static CBLNewBlob* _cbl_nullable findNewBlob(FLDict dict);
    
    
#ifdef COUCHBASE_ENTERPRISE
    
#pragma mark - EncryptedProperties:
    
    CBLEncryptable*  _cbl_nullable getEncryptableValue(FLDict dict);
    
#endif
    

#pragma mark - Save/delete:


    struct SaveOptions {
        SaveOptions(CBLConcurrencyControl c)         :concurrency(c) { }
        SaveOptions(CBLConflictHandler h, void* _cbl_nullable ctx)
        : conflictHandler(h)
        ,context(ctx)
        ,concurrency(kCBLConcurrencyControlFailOnConflict) { }

        CBLConcurrencyControl concurrency;
        CBLConflictHandler _cbl_nullable conflictHandler = nullptr;
        void* _cbl_nullable context;
        bool deleting = false;
    };
    
    bool save(CBLCollection* collection, const SaveOptions &opt);
    

#pragma mark - Conflict resolution:


    // Select a specific revision. Only works if constructed with allRevisions=true.
    bool selectRevision(slice revID) {
        auto c4doc = _c4doc.useLocked();
        if (!c4doc || !c4doc->selectRevision(revID, true))
            return false;
        _revID = revID;
        _properties = nullptr;
        _fromJSON = nullptr;
        return true;
    }


    // Select a conflicting revision. Only works if constructed with allRevisions=true.
    bool selectNextConflictingRevision() {
        auto c4doc = _c4doc.useLocked();
        if (!c4doc)
            return false;
        _properties = nullptr;
        _fromJSON = nullptr;
        while (c4doc->selectNextLeafRevision(true, true))
            if (c4doc->selectedRev().flags & kRevIsConflict) {
                _revID = c4doc->selectedRev().revID;
                return true;
            }
        return false;
    }


    enum class Resolution {
        useLocal,
        useRemote,
        useMerge
    };

    
    // Resolve conflict for pull replication; The document itself is the conflict (remote) doc.
    // When saving the resolution, the remote branch will always win so that the saved doc
    // will not be conflicted when it is push to the remote server. When the resolution is
    // useRemote, the resolveDoc will be ignore.
    bool resolveConflict(Resolution resolution, const CBLDocument* _cbl_nullable resolveDoc);


#pragma mark - Utils:
    
    static void checkCollectionMatches(CBLCollection* _cbl_nullable myCol, CBLCollection *colParam);

#pragma mark - Internals:


private:
    
    friend struct CBLCollection;
    
    CBLDocument(slice docID, CBLCollection* _cbl_nullable collection,
                C4Document* _cbl_nullable c4doc, bool isMutable);
    
    virtual ~CBLDocument();

    void checkMutable() const {
        if (!_usuallyTrue(_mutable))
            C4Error::raise(LiteCoreDomain, kC4ErrorNotWriteable, "Document object is immutable");
    }

    // Walk through the Fleece object tree, find new mutable blob Dicts to install.
    //
    // The releaseNewBlob flag allows to release the matched CBLNewBlob objects after being
    // installed. The flag is set to true only when saving new blobs while encoding the document
    // returned by the replicator's conflict resolved. The new blobs set to the resolved doc needs
    // to be retained until they are installed here.
    //
    // The method will returns true if there are blobs.
    //
    // (EE Only) Furthermore, while walking through the object tree, check if there are
    // any encryptables in an array and throw an unsupported error if that occurs.
    //
    bool saveBlobsAndCheckEncryptables(CBLDatabase *db, bool releaseNewBlob) const;
    
    // Encode the document body and install new blobs if found into the database.
    //
    // The releaseNewBlob option tells whether the new blob object should be released after
    // being install or not. This option is set to true only when encoding the document
    // returned by the replicator's conflict resolved.
    //
    // The outRevFlags will be updated with kRevHasAttachments if there is a blob found while
    // encoding the document body.
    alloc_slice encodeBody(CBLDatabase* db,
                           C4Database* c4db,
                           bool releaseNewBlob,
                           C4RevisionFlags &outRevFlags) const;

    // Custom object cache:
    using ValueToBlobMap = std::unordered_map<FLDict, Retained<CBLBlob>>;
#ifdef COUCHBASE_ENTERPRISE
    using ValueToEncryptableMap = std::unordered_map<FLDict, Retained<CBLEncryptable>>;
#endif

    Retained<CBLCollection>       _collection;      // Collection (null for new doc)
    litecore::access_lock<Retained<C4Document>>  _c4doc; // LiteCore doc (null for new doc)
    alloc_slice const             _docID;           // Document ID (never empty)
    mutable alloc_slice           _revID;           // Revision ID
    fleece::Doc                   _fromJSON;        // Properties read from JSON
    mutable fleece::RetainedValue _properties;      // Properties, initialized lazily
    ValueToBlobMap                _blobs;           // Maps Dicts in _properties to CBLBlobs
#ifdef COUCHBASE_ENTERPRISE
    ValueToEncryptableMap         _encryptables;    // Maps Dicts in _properties to CBLEncryptables
#endif
    bool const                    _mutable {false}; // True iff I am mutable
};

CBL_ASSUME_NONNULL_END
