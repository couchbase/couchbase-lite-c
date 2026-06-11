//
// CBLDatabase_CAPI.cc
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
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

#include "CBLDatabase_Internal.hh"
#include "CBLBlob_Internal.hh"
#include "CBLCollection_Internal.hh"
#include "CBLDatabase.h"
#include "CBLDocument_Internal.hh"


using namespace std;
using namespace fleece;
using namespace cbl_internal;


CBLDatabaseConfiguration CBLDatabaseConfiguration_Default() noexcept {
    try {
        return CBLDatabase::defaultConfiguration();
    } catchAndWarn();
}


bool CBL_DatabaseExists(FLString name, FLString inDirectory) noexcept {
    try {
        return CBLDatabase::exists(name, inDirectory);
    } catchAndWarn();
}


bool CBL_CopyDatabase(FLString fromPath,
                      FLString toName,
                      const CBLDatabaseConfiguration* config,
                      CBLError* outError) noexcept
{
    try {
        CBLDatabase::copyDatabase(fromPath, toName, config);
        return true;
    } catchAndBridge(outError)
}


bool CBL_DeleteDatabase(FLString name,
                        FLString inDirectory,
                        CBLError *outError) noexcept
{
    try {
        CBLDatabase::deleteDatabase(name, inDirectory);
        return true;
    } catchAndBridge(outError)
}

CBLDatabase* CBLDatabase_Open(FLString name,
                              const CBLDatabaseConfiguration *config,
                              CBLError *outError) noexcept
{
    try {
        return CBLDatabase::open(name, config).detach();
    } catchAndBridge(outError)
}


bool CBLDatabase_Close(CBLDatabase* db, CBLError* outError) noexcept {
    try {
        if (db)
            db->close();
        return true;
    } catchAndBridge(outError)
}

bool CBLDatabase_BeginTransaction(CBLDatabase* db, CBLError* outError) noexcept {
    try {
        db->beginTransaction();
        return true;
    } catchAndBridge(outError)
}

bool CBLDatabase_EndTransaction(CBLDatabase* db, bool commit, CBLError* outError) noexcept {
    try {
        db->endTransaction(commit);
        return true;
    } catchAndBridge(outError)
}

bool CBLDatabase_Delete(CBLDatabase* db, CBLError* outError) noexcept {
    try {
        db->closeAndDelete();
        return true;
    } catchAndBridge(outError)
}

#ifdef COUCHBASE_ENTERPRISE
bool CBLDatabase_ChangeEncryptionKey(CBLDatabase *db,
                                     const CBLEncryptionKey *newKey,
                                     CBLError* outError) noexcept
{
    try {
        db->changeEncryptionKey(newKey);
        return true;
    } catchAndBridge(outError)
}
#endif

bool CBLDatabase_PerformMaintenance(CBLDatabase* db,
                                    CBLMaintenanceType type,
                                    CBLError* outError) noexcept
{
    try {
        db->performMaintenance(type);
        return true;
    } catchAndBridge(outError)
}


FLString CBLDatabase_Name(const CBLDatabase* db) noexcept {
    return db->name();
}

FLStringResult CBLDatabase_Path(const CBLDatabase* db) noexcept {
    try {
        return FLStringResult(db->path());
    } catchAndBridgeReturning(nullptr, FLSliceResult(alloc_slice(kFLSliceNull)));
}

const CBLDatabaseConfiguration CBLDatabase_Config(const CBLDatabase* db) noexcept {
    return db->config();
}

uint64_t CBLDatabase_Count(const CBLDatabase* db) noexcept {
    try {
        auto col = const_cast<CBLDatabase*>(db)->getInternalDefaultCollection();
        return col->count();
    } catchAndWarn();
}

/** Private API */
uint64_t CBLDatabase_LastSequence(const CBLDatabase* db) noexcept {
    try {
        auto col = const_cast<CBLDatabase*>(db)->getInternalDefaultCollection();
        return col->lastSequence();
    } catchAndWarn()
}

/** Private API */
FLSliceResult CBLDatabase_PublicUUID(const CBLDatabase* db) noexcept {
    try {
        return FLSliceResult(db->publicUUID());
    } catchAndWarn()
}

#pragma mark - DOCUMENTS:


const CBLDocument* CBLDatabase_GetDocument(const CBLDatabase* db, FLString docID,
                                           CBLError* outError) noexcept
{
    try {
        auto col = const_cast<CBLDatabase*>(db)->getInternalDefaultCollection();
        return CBLCollection_GetDocument(col, docID, outError);
    } catchAndBridge(outError)
}


CBLDocument* CBLDatabase_GetMutableDocument(CBLDatabase* db, FLString docID,
                                            CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_GetMutableDocument(col, docID, outError);
    } catchAndBridge(outError)
}


bool CBLDatabase_SaveDocument(CBLDatabase* db,
                              CBLDocument* doc,
                              CBLError* outError) noexcept
{
    return CBLDatabase_SaveDocumentWithConcurrencyControl
        (db, doc, kCBLConcurrencyControlLastWriteWins, outError);
}

bool CBLDatabase_SaveDocumentWithConcurrencyControl(CBLDatabase* db,
                                                    CBLDocument* doc,
                                                    CBLConcurrencyControl concurrency,
                                                    CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_SaveDocumentWithConcurrencyControl(col, doc, concurrency, outError);
    } catchAndBridge(outError)
}

bool CBLDatabase_SaveDocumentWithConflictHandler(CBLDatabase* db,
                                                 CBLDocument* doc,
                                                 CBLConflictHandler conflictHandler,
                                                 void *context,
                                                 CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_SaveDocumentWithConflictHandler(col, doc, conflictHandler,
                                                             context, outError);
    } catchAndBridge(outError)
}

bool CBLDatabase_DeleteDocument(CBLDatabase *db,
                                const CBLDocument* doc,
                                CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_DeleteDocument(col, doc, outError);
    } catchAndBridge(outError)
}

bool CBLDatabase_DeleteDocumentWithConcurrencyControl(CBLDatabase *db,
                                                      const CBLDocument* doc,
                                                      CBLConcurrencyControl concurrency,
                                                      CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_DeleteDocumentWithConcurrencyControl(col, doc, concurrency, outError);
    } catchAndBridge(outError)
}

/** Private API */
bool CBLDatabase_DeleteDocumentByID(CBLDatabase* db,
                                    FLString docID,
                                    CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_DeleteDocumentByID(col, docID, outError);
    } catchAndBridge(outError)
}

bool CBLDatabase_PurgeDocument(CBLDatabase* db,
                               const CBLDocument* doc,
                               CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        CBLDocument::checkCollectionMatches(doc->collection(), col);
        return CBLCollection_PurgeDocumentByID(col, doc->docID(), outError);
    } catchAndBridge(outError)
}

/** Private API */
bool CBLDatabase_PurgeDocumentByID(CBLDatabase* db,
                                   FLString docID,
                                   CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_PurgeDocumentByID(col, docID, outError);
    } catchAndBridge(outError)
}

CBLTimestamp CBLDatabase_GetDocumentExpiration(CBLDatabase* db,
                                               FLSlice docID,
                                               CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_GetDocumentExpiration(col, docID, outError);
    } catchAndBridge(outError)
}

bool CBLDatabase_SetDocumentExpiration(CBLDatabase* db,
                                       FLSlice docID,
                                       CBLTimestamp expiration,
                                       CBLError* outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_SetDocumentExpiration(col, docID, expiration, outError);
    } catchAndBridge(outError)
}


#pragma mark - QUERIES:


bool CBLDatabase_CreateValueIndex(CBLDatabase *db,
                                  FLString name,
                                  CBLValueIndexConfiguration config,
                                  CBLError *outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_CreateValueIndex(col, name, config, outError);
    } catchAndBridge(outError)
}


bool CBLDatabase_CreateFullTextIndex(CBLDatabase *db,
                                     FLString name,
                                     CBLFullTextIndexConfiguration config,
                                     CBLError *outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_CreateFullTextIndex(col, name, config, outError);
    } catchAndBridge(outError)
}


bool CBLDatabase_DeleteIndex(CBLDatabase *db,
                             FLString name,
                             CBLError *outError) noexcept
{
    try {
        auto col = db->getInternalDefaultCollection();
        return CBLCollection_DeleteIndex(col, name, outError);
    } catchAndBridge(outError)
}


FLArray CBLDatabase_GetIndexNames(CBLDatabase *db) noexcept {
    try {
        auto col = db->getInternalDefaultCollection();
        
        CBLError error;
        auto result = CBLCollection_GetIndexNames(col, &error);
        if (!result) {
            if (error.code != 0) {
                alloc_slice message = CBLError_Message(&error);
                CBL_Log(kCBLLogDomainDatabase, kCBLLogWarning,
                        "Getting index names failed: %d/%d: %.*s",
                        error.domain, error.code, (int)message.size, (char*)message.buf);
            }
            result = FLMutableArray_New();
        }
        return result;
    } catchAndBridgeReturning(nullptr, FLMutableArray_New())
}


#pragma mark - CHANGE LISTENERS


namespace cbl_internal {
    struct DatabaseChangeContext {
        const CBLDatabase* database;
        const CBLCollection* collection;
        CBLDatabaseChangeListener listener;
        void* context;
    };

    struct DocumentChangeContext {
        const CBLDatabase* database;
        const CBLCollection* collection;
        CBLDocumentChangeListener listener;
        void* context;
    };
}


CBLListenerToken* CBLDatabase_AddChangeListener(const CBLDatabase* db,
                                                CBLDatabaseChangeListener listener,
                                                void *context) noexcept
{
    auto wrappedContext = new DatabaseChangeContext();
    wrappedContext->database = db;
    wrappedContext->listener = listener;
    wrappedContext->context = context;

    auto wrappedListener = [](void* context, const CBLCollectionChange* change) {
        DatabaseChangeContext* ctx = static_cast<DatabaseChangeContext*>(context);
        ctx->listener(ctx->context, ctx->database, change->numDocs, change->docIDs);
    };

    try {
        CBLCollection* col = const_cast<CBLDatabase*>(db)->getInternalDefaultCollection().detach();
        wrappedContext->collection = col;
        
        auto token = CBLCollection_AddChangeListener(col, wrappedListener, wrappedContext);
        token->extraInfo().pointer = wrappedContext;
        token->extraInfo().destructor = [](void* ctx) {
            CBLCollection_Release(static_cast<DatabaseChangeContext*>(ctx)->collection);
            free(ctx);
        };
        return token;
    } catchAndBridgeReturning(nullptr, make_retained<CBLListenerToken>((const void*)listener, nullptr).detach())
}

void CBLDatabase_BufferNotifications(CBLDatabase *db,
                                     CBLNotificationsReadyCallback callback,
                                     void *context) noexcept
{
    db->bufferNotifications(callback, context);
}

void CBLDatabase_SendNotifications(CBLDatabase *db) noexcept {
    db->sendNotifications();
}


CBLListenerToken* CBLDatabase_AddDocumentChangeListener(const CBLDatabase* db,
                                                        FLString docID,
                                                        CBLDocumentChangeListener listener,
                                                        void *context) noexcept
{
    auto wrappedContext = new DocumentChangeContext();
    wrappedContext->database = db;
    wrappedContext->listener = listener;
    wrappedContext->context = context;
    
    auto wrappedListener = [](void* context, const CBLDocumentChange* change) {
        DocumentChangeContext* ctx = static_cast<DocumentChangeContext*>(context);
        ctx->listener(ctx->context, ctx->database, change->docID);
    };
    
    try {
        CBLCollection* col = const_cast<CBLDatabase*>(db)->getInternalDefaultCollection().detach();
        wrappedContext->collection = col;
        
        auto token = CBLCollection_AddDocumentChangeListener(col, docID, wrappedListener, wrappedContext);
        token->extraInfo().pointer = wrappedContext;
        token->extraInfo().destructor = [](void* ctx) {
            CBLCollection_Release(static_cast<DatabaseChangeContext*>(ctx)->collection);
            free(ctx);
        };
        return token;
    } catchAndBridgeReturning(nullptr, make_retained<CBLListenerToken>((const void*)listener, nullptr).detach())
}


#pragma mark - BINDING DEV SUPPORT FOR BLOB:


const CBLBlob* CBLDatabase_GetBlob(CBLDatabase* db, FLDict properties,
                                   CBLError* _cbl_nullable outError) noexcept
{
    try {
        auto blob = db->getBlob(properties).detach();
        if (!blob && outError)
            outError->code = 0;
        return blob;
    } catchAndBridge(outError)
}


bool CBLDatabase_SaveBlob(CBLDatabase* db, CBLBlob* blob,
                          CBLError* _cbl_nullable outError) noexcept
{
    try {
        db->saveBlob(blob);
        return true;
    } catchAndBridge(outError)
}

#pragma mark - EXTENSION:

#ifdef COUCHBASE_ENTERPRISE

bool CBL_EnableVectorSearch(FLString path, CBLError* _cbl_nullable outError) noexcept {
    try {
        CBLDatabase::enableVectorSearch(path);
        return true;
    } catchAndBridge(outError)
}

#endif
