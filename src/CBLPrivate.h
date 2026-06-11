//
// CBLPrivate.h
//
// Copyright (c) 2018 Couchbase, Inc All rights reserved.
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
#include "cbl/CouchbaseLite.h"


CBL_CAPI_BEGIN

    void CBLError_SetCaptureBacktraces(bool) CBLAPI;
    bool CBLError_GetCaptureBacktraces(void) CBLAPI;

    void CBLLog_BeginExpectingExceptions() CBLAPI;
    void CBLLog_EndExpectingExceptions() CBLAPI;

/** Returns the last sequence number assigned in the database (default collection).
    This starts at zero and increments every time a document is saved or deleted. */
    uint64_t CBLDatabase_LastSequence(const CBLDatabase*) CBLAPI;

/** Returns the public UUID of the database.*/
    FLSliceResult CBLDatabase_PublicUUID(const CBLDatabase*) CBLAPI;

/** Returns the last sequence number assigned in the collection.
    This starts at zero and increments every time a document is saved or deleted. */
    uint64_t CBLCollection_LastSequence(const CBLCollection*) CBLAPI;

#ifdef COUCHBASE_ENTERPRISE
/** Returns true if the index is a vector index and the index is trained.
    If the index doesn't exist nor the vector index, an error will be returend. */
    bool CBLCollection_IsIndexTrained(const CBLCollection* collection,
                                      FLString name,
                                      CBLError* _cbl_nullable outError) CBLAPI;

    CBLKeyPair* CBLKeyPair_GenerateRSAKeyPair(FLSlice passwordOrNull,
                                              CBLError* _cbl_nullable outError) CBLAPI;

    CBLKeyPair* CBLKeyPair_PublicKeyFromData(FLSlice data,
                                             CBLError* _cbl_nullable outError) CBLAPI;

    FLSliceResult CBLURLEndpointListener_AnonymousLabel(const CBLURLEndpointListener* listener) CBLAPI;
#endif

FLMutableArray CBLCollection_GetIndexesInfo(const CBLCollection* collection,
                                            CBLError* _cbl_nullable outError) CBLAPI;

/** Deletes a document from the database, given only its ID.
    @note  If no document with that ID exists, this function will return false but the error
            code will be zero.
    @param database  The database.
    @param docID  The document ID to delete.
    @param outError  On failure, the error will be written here.
    @return  True if the document was deleted, false if it doesn't exist or the deletion failed. */
    bool CBLDatabase_DeleteDocumentByID(CBLDatabase* database,
                                        FLString docID,
                                        CBLError* _cbl_nullable outError) CBLAPI;

/** Deletes a document from the collection, given only its ID.
    @note  If no document with that ID exists, this function will return false but the error
            code will be zero.
    @param collection  The collection.
    @param docID  The document ID to delete.
    @param outError  On failure, the error will be written here.
    @return  True if the document was deleted, false if it doesn't exist or the deletion failed. */
    bool CBLCollection_DeleteDocumentByID(CBLCollection* collection,
                                          FLString docID,
                                          CBLError* _cbl_nullable outError) CBLAPI;

    FLSliceResult CBLDocument_CanonicalRevisionID(const CBLDocument* doc) CBLAPI;

    unsigned CBLDocument_Generation(const CBLDocument* doc) CBLAPI;

    FLSliceResult CBLDocument_GetRevisionHistory(const CBLDocument* doc) CBLAPI;
    
    FLSlice CBLReplicator_UserAgent(const CBLReplicator* repl) CBLAPI;

    /** Adding a delay in MS before processing the callback from C4QueryObserver. 
        This is for testing to ensure that the callback has handle the case that the callback
        is called after the query listener token as removed correctly without accessing
        any invalidated objects. */
    void CBLQuery_SetListenerCallbackDelay(int delay) CBLAPI;

    /** Reset log and log sink to the default state. This is for log API testing purpose.  */
    void CBLLog_Reset(void) CBLAPI;

    /** Use c4log to log the message so that the message will be sent to LiteCore's callback and file logging */
    void CBLLog_LogWithC4Log(CBLLogDomain domain, CBLLogLevel level, const char *message) CBLAPI;

    /** A utility for deleting directory recursively used in tests. This function is using LiteCore's FilePath which is cross-platform.
        TODO: When std::filesystem available from C++ 17 can be used, we can get rid of this function. */
    bool CBL_DeleteDirectoryRecursive(FLString dir, CBLError* _cbl_nullable outError) CBLAPI;

CBL_CAPI_END
