//
// CBLReplicator_CAPI.cc
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

#include "CBLPrivate.h"
#include "CBLReplicator.h"
#include "CBLReplicator_Internal.hh"
#include "CBLTLSIdentity.h"


const FLString kCBLAuthDefaultCookieName = FLSTR("SyncGatewaySession");


CBLEndpoint* CBLEndpoint_CreateWithURL(FLString url, CBLError* _cbl_nullable outError) noexcept {
    try {
        return new CBLURLEndpoint(url);
    } catchAndBridge(outError)
}

#ifdef COUCHBASE_ENTERPRISE
CBLEndpoint* CBLEndpoint_CreateWithLocalDB(CBLDatabase* db) noexcept {
    try {
        return new CBLLocalEndpoint(db);
    } catchAndWarn()
}
#endif

void CBLEndpoint_Free(CBLEndpoint *endpoint) noexcept {
    delete endpoint;
}

CBLAuthenticator* CBLAuth_CreatePassword(FLString username, FLString password) noexcept {
    try {
        return new BasicAuthenticator(username, password);
    } catchAndWarn()
}

CBLAuthenticator* CBLAuth_CreateSession(FLString sessionID, FLString cookieName) noexcept {
    try {
        return new SessionAuthenticator(sessionID, cookieName);
    } catchAndWarn()
}

#ifdef COUCHBASE_ENTERPRISE
CBLAuthenticator* CBLAuth_CreateCertificate(CBLTLSIdentity* identity) noexcept {
    try {
        return new CertAuthenticator(identity);
    } catchAndWarn();
}
#endif

void CBLAuth_Free(CBLAuthenticator *auth) noexcept {
    delete auth;
}

/** Private API*/
FLSlice CBLReplicator_UserAgent(const CBLReplicator* repl) noexcept {
    return repl->getUserAgent();
}

CBLReplicator* CBLReplicator_Create(const CBLReplicatorConfiguration* conf, CBLError *outError) noexcept {
    try {
        return retain(new CBLReplicator(*conf));
    } catchAndBridge(outError)
}

const CBLReplicatorConfiguration* CBLReplicator_Config(CBLReplicator* repl) noexcept {
    return repl->configuration();
}

CBLReplicatorStatus CBLReplicator_Status(CBLReplicator* repl) noexcept {
    return repl->status();
}

FLStringResult CBLReplicator_CorrelationID(CBLReplicator* repl) noexcept {
    return FLStringResult(repl->correlationID());
}

void CBLReplicator_Start(CBLReplicator* repl, bool reset) noexcept        {repl->start(reset);}
void CBLReplicator_Stop(CBLReplicator* repl) noexcept                     {repl->stop();}
void CBLReplicator_SetHostReachable(CBLReplicator* repl, bool r) noexcept {repl->setHostReachable(r);}
void CBLReplicator_SetSuspended(CBLReplicator* repl, bool sus) noexcept   {repl->setSuspended(sus);}

FLDict CBLReplicator_PendingDocumentIDs(CBLReplicator *repl, CBLError *outError) noexcept {
    try {
        auto col = repl->defaultCollection();
        if (!col) {
            C4Error::raise(LiteCoreDomain, kC4ErrorInvalidParameter,
                           "The default collection is not included in the replicator config.");
        }
        return CBLReplicator_PendingDocumentIDs2(repl, col, outError);
    } catchAndBridge(outError)
}

bool CBLReplicator_IsDocumentPending(CBLReplicator *repl, FLString docID, CBLError *outError) noexcept {
    try {
        auto col = repl->defaultCollection();
        if (!col) {
            C4Error::raise(LiteCoreDomain, kC4ErrorInvalidParameter,
                           "The default collection is not included in the replicator config.");
        }
        return CBLReplicator_IsDocumentPending2(repl, docID, col, outError);
    } catchAndBridge(outError)
}

FLDict _cbl_nullable CBLReplicator_PendingDocumentIDs2(CBLReplicator* repl,
                                                       const CBLCollection* collection,
                                                       CBLError* _cbl_nullable outError) noexcept {
    try {
        auto result = FLDict_Retain(repl->pendingDocumentIDs(collection));
        if (!result) {
            result = FLMutableDict_New();
            if (outError) outError->code = 0;
        }
        return result;
    } catchAndBridge(outError)
}

bool CBLReplicator_IsDocumentPending2(CBLReplicator *repl,
                                      FLString docID,
                                      const CBLCollection* collection,
                                      CBLError* _cbl_nullable outError) noexcept {
    try {
        bool result = repl->isDocumentPending(docID, collection);
        if (!result)
            if (outError) outError->code = 0;
        return result;
    } catchAndBridge(outError)
}

CBLListenerToken* CBLReplicator_AddChangeListener(CBLReplicator* repl,
                                                  CBLReplicatorChangeListener listener,
                                                  void *context) noexcept
{
    return retain(repl->addChangeListener(listener, context));
}

CBLListenerToken* CBLReplicator_AddDocumentReplicationListener(CBLReplicator* repl,
                                                    CBLDocumentReplicationListener listener,
                                                    void *context) noexcept
{
    return retain(repl->addDocumentListener(listener, context));
}

#ifdef COUCHBASE_ENTERPRISE
CBLCert* CBLReplicator_ServerCertificate(CBLReplicator* repl) noexcept
{
    try {
        return repl->serverCertificate().detach();
    } catchAndWarn();
}
#endif
