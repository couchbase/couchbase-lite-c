//
// CBLReplicatorConfig.hh
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

#include "CBLReplicator.h"
#include "CBLDatabase_Internal.hh"
#include "CBLCollection_Internal.hh"

#ifdef COUCHBASE_ENTERPRISE
#include "CBLTLSIdentity_Internal.hh"
#endif

#include "CBLUserAgent.hh"
#include "Internal.hh"
#include "c4ReplicatorTypes.h"
#include "c4Private.h"
#include "fleece/Fleece.hh"
#include "fleece/Mutable.hh"
#include <climits>
#include <mutex>
#include <string>
#include <vector>

CBL_ASSUME_NONNULL_BEGIN


#pragma mark - ENDPOINT


struct CBLEndpoint {
    virtual ~CBLEndpoint()                                      =default;
    virtual bool valid() const =0;
    const C4Address& remoteAddress() const                      {return _address;}
    virtual C4String remoteDatabaseName() const =0;
    virtual CBLEndpoint* clone() const =0;
    virtual std::string desc() const =0;
#ifdef COUCHBASE_ENTERPRISE
    virtual CBLDatabase* _cbl_nullable otherLocalDB() const     {return nullptr;}
#endif

protected:
    C4Address _address = { };
};


namespace cbl_internal {
    // Concrete Endpoint for remote URLs
    struct CBLURLEndpoint : public CBLEndpoint {
        CBLURLEndpoint(fleece::slice url)
        :_url(url)
        {
            if (!C4Address::fromURL(_url, &_address, (fleece::slice*)&_dbName)) {
                C4Error::raise(LiteCoreDomain, kC4ErrorInvalidParameter,
                               "Invalid URLEndpoint url '%.*s'", FMTSLICE(_url));
            } else if (_address.scheme != kC4Replicator2Scheme &&
                       _address.scheme != kC4Replicator2TLSScheme) {
                C4Error::raise(LiteCoreDomain, kC4ErrorInvalidParameter,
                               "Invalid scheme for URLEndpoint url '%.*s'. It must be either 'ws:' or 'wss:'.",
                               FMTSLICE(_url));
            }
        }

        bool valid() const override                             {return _dbName != fleece::nullslice;}
        C4String remoteDatabaseName() const override            {return _dbName;}
        virtual CBLEndpoint* clone() const override             {return new CBLURLEndpoint(_url);}
        virtual std::string desc() const override               {return _url.asString();}
    private:
        fleece::alloc_slice _url;
        C4String _dbName = { };
    };

#ifdef COUCHBASE_ENTERPRISE
    // Concrete Endpoint for local databases
    struct CBLLocalEndpoint : public CBLEndpoint {
        CBLLocalEndpoint(CBLDatabase *db)
        :_db(db)
        { }

        bool valid() const override                             {return true;}
        virtual C4String remoteDatabaseName() const override    {return fleece::nullslice;}
        virtual CBLDatabase* otherLocalDB() const override      {return _db;}
        virtual CBLEndpoint* clone() const override             {return new CBLLocalEndpoint(_db);}
        virtual std::string desc() const override               {return _db->desc();}
    private:
        fleece::Retained<CBLDatabase> _db;
    };
#endif
}


#pragma mark - AUTHENTICATOR:


struct CBLAuthenticator {
protected:
    using slice = fleece::slice;
    using Encoder = fleece::Encoder;
    using alloc_slice = fleece::alloc_slice;

public:
    virtual ~CBLAuthenticator()                                 =default;
    virtual void writeOptions(Encoder&, C4KeyPair* _cbl_nonnull * _cbl_nullable outExternalKey) =0;
    virtual CBLAuthenticator* clone() const =0;
};


namespace cbl_internal {
    // Concrete Authenticator for HTTP Basic auth:
    struct BasicAuthenticator : public CBLAuthenticator {
        BasicAuthenticator(slice username, slice password)
        :_username(username)
        ,_password(password)
        { }

        virtual void writeOptions(Encoder &enc, C4KeyPair* _cbl_nonnull * _cbl_nullable /*outExternalKey*/) override {
            enc.writeKey(slice(kC4ReplicatorOptionAuthentication));
            enc.beginDict();
            enc[slice(kC4ReplicatorAuthType)] = kC4AuthTypeBasic;
            enc[slice(kC4ReplicatorAuthUserName)] = _username;
            enc[slice(kC4ReplicatorAuthPassword)] = _password;
            enc.endDict();
        }
        
        virtual CBLAuthenticator* clone() const override {
            return new BasicAuthenticator(_username, _password);
        }

    private:
        alloc_slice _username, _password;
    };

#ifdef COUCHBASE_ENTERPRISE
    struct CertAuthenticator : public CBLAuthenticator {
        CertAuthenticator(CBLTLSIdentity* identity)
        : _identity(identity)
        { }

        virtual void writeOptions(Encoder &enc, C4KeyPair* _cbl_nullable * _cbl_nullable outExternalKey) override {
            enc.writeKey(slice(kC4ReplicatorOptionAuthentication));
            enc.beginDict();
            enc[slice(kC4ReplicatorAuthType)] = kC4AuthTypeClientCert;
            enc.writeKey(slice(kC4ReplicatorAuthClientCert));
            alloc_slice certData;
            if (_identity->certificates())
                certData = _identity->certificates()->c4Cert()->getData(false);
            enc.writeData(certData);
            alloc_slice privateKeyData;
            C4KeyPair* privateKey = _identity->privateKey() ? _identity->privateKey()->c4KeyPair() : nullptr;
            if (outExternalKey) *outExternalKey = nullptr;
            if ( privateKey ) {
                // The life of privateKey is tied to _identity
                privateKeyData = privateKey->getPrivateKeyData();
                if (privateKeyData) {
                    enc.writeKey(C4STR(kC4ReplicatorAuthClientCertKey));
                    enc.writeData(privateKeyData);
                } else {
                    if (outExternalKey) *outExternalKey = privateKey;
                }
            }

            enc.endDict();
        }

        virtual CBLAuthenticator* clone() const override {
            return new CertAuthenticator(*this);
        }

    private:
        fleece::Retained<CBLTLSIdentity> _identity;
    };
#endif

    // Concrete Authenticator for session-cookie auth:
    struct SessionAuthenticator : public CBLAuthenticator {
        SessionAuthenticator(slice sessionID, slice cookieName)
        :_sessionID(sessionID)
        ,_cookieName(cookieName ? cookieName : kDefaultCookieName)
        { }

        static constexpr const char* kDefaultCookieName = "SyncGatewaySession";

        virtual void writeOptions(Encoder &enc, C4KeyPair* _cbl_nonnull * _cbl_nullable /*outExternalKey*/) override {
            enc.writeKey(slice(kC4ReplicatorOptionCookies));
            enc.writeString(_cookieName + "=" + _sessionID);
        }
        
        virtual CBLAuthenticator* clone() const override {
            return new SessionAuthenticator(slice(_sessionID), slice(_cookieName));
        }

    private:
        std::string _sessionID, _cookieName;
    };
}


#pragma mark - CONFIGURATION:

#define kCBLReplicatorUserAgent "User-Agent"

namespace cbl_internal
{
    // Managed config object that retains/releases its properties.
    struct ReplicatorConfiguration : public CBLReplicatorConfiguration {
        using Encoder = fleece::Encoder;
        using Dict = fleece::Dict;
        using slice = fleece::slice;
        using Array = fleece::Array;
        template <class T> using Retained = fleece::Retained<T>;

    public:
        ReplicatorConfiguration(const CBLReplicatorConfiguration &conf) {
            *(CBLReplicatorConfiguration*)this = conf;
            
            // Throw an exception if the validation failed:
            validate();
            
            if (endpoint)
                endpoint = endpoint->clone();

            authenticator = authenticator ? authenticator->clone() : nullptr;
            headers = FLDict_MutableCopy(headers, kFLDeepCopyImmutables);
            channels = FLArray_MutableCopy(channels, kFLDeepCopyImmutables);
            documentIDs = FLArray_MutableCopy(documentIDs, kFLDeepCopyImmutables);
            pinnedServerCertificate = (_pinnedServerCert = pinnedServerCertificate);
            trustedRootCertificates = (_trustedRootCerts = trustedRootCertificates);
        
        #ifdef __CBL_REPLICATOR_NETWORK_INTERFACE__
            networkInterface = (_networkInterface = networkInterface);
        #endif
            
            if (proxy) {
                _proxy = *proxy;
                proxy = &_proxy;
                _proxy.hostname = copyString(_proxy.hostname, _proxyHostname);
                _proxy.username = copyString(_proxy.username, _proxyUsername);
                _proxy.password = copyString(_proxy.password, _proxyPassword);
            }

            Dict headersDict = Dict(headers);
            fleece::Value userAgent = headersDict[kCBLReplicatorUserAgent];
            _userAgent = userAgent ? userAgent.asstring() : userAgentHeader();
            
            Retained<CBLCollection> defaultCollection = nullptr;
            if (database) {
                defaultCollection = database->getDefaultCollection();
            }

            if (collections) {
                // Copy replication collections, channels, and document ids:
                for (int i = 0; i < collectionCount; i++) {
                    CBLReplicationCollection col = collections[i];
                    col.channels = FLArray_MutableCopy(col.channels, kFLDeepCopyImmutables);
                    col.documentIDs = FLArray_MutableCopy(col.documentIDs, kFLDeepCopyImmutables);
                    _effectiveCollections.push_back(col);
                }
                collections = _effectiveCollections.data();
            } else {
                // Create a replication collection using the default collection:
                assert(defaultCollection);

                CBLReplicationCollection col {};
                col.collection = defaultCollection;
                col.conflictResolver = conflictResolver;
                col.pushFilter = pushFilter;
                col.pullFilter = pullFilter;
                col.channels = FLArray_Retain(channels);        // Already copied
                col.documentIDs = FLArray_Retain(documentIDs);  // Already copied
                _effectiveCollections.push_back(col);
            }

            // Retain the collections and database:
            for (auto& col : _effectiveCollections) {
                _retainedCollections.push_back(col.collection);
                if (!_retainedDatabase) {
                    _retainedDatabase = col.collection->database();
                }
            }
        }

        ~ReplicatorConfiguration() {
            CBLEndpoint_Free(endpoint);
            CBLAuth_Free(authenticator);
            FLDict_Release(headers);
            FLArray_Release(channels);
            FLArray_Release(documentIDs);

            for (auto& col : _effectiveCollections) {
                FLArray_Release(col.channels);
                FLArray_Release(col.documentIDs);
            }
        }

        // Writes a LiteCore replicator optionsDict
        void writeOptions(Encoder &enc) const {
            fleece::MutableDict mHeaders = headers ? FLDict_AsMutable(headers) : FLMutableDict_New();
            if (!mHeaders[kCBLReplicatorUserAgent]) {
                mHeaders[kCBLReplicatorUserAgent] = _userAgent;
            }
            writeOptionalKey(enc, kC4ReplicatorOptionExtraHeaders, mHeaders);
            
            if (pinnedServerCertificate.buf) {
                enc.writeKey(slice(kC4ReplicatorOptionPinnedServerCert));
                enc.writeData(pinnedServerCertificate);
            }
            if (trustedRootCertificates.buf) {
                enc.writeKey(slice(kC4ReplicatorOptionRootCerts));
                enc.writeData(trustedRootCertificates);
            }
            if (proxy) {
                static constexpr const char* kProxyTypeIDs[] = {kC4ProxyTypeHTTP,
                                                                kC4ProxyTypeHTTPS};
                enc.writeKey(slice(kC4ReplicatorOptionProxyServer));
                enc.beginDict();
                enc[slice(kC4ReplicatorProxyType)] = kProxyTypeIDs[proxy->type];
                enc[slice(kC4ReplicatorProxyHost)] = proxy->hostname;
                enc[slice(kC4ReplicatorProxyPort)] = proxy->port;
                if (proxy->username.size > 0) {
                    enc.writeKey(slice(kC4ReplicatorProxyAuth));
                    enc.beginDict();
                    enc[slice(kC4ReplicatorAuthUserName)] = proxy->username;
                    enc[slice(kC4ReplicatorAuthPassword)] = proxy->password;
                    enc.endDict();
                }
                enc.endDict();
            }

#ifdef COUCHBASE_ENTERPRISE
            if  (acceptOnlySelfSignedServerCertificate) {
                enc.writeKey(C4STR(kC4ReplicatorOptionOnlySelfSignedServerCert));
                enc.writeBool(true);
            }
#endif
            enc.writeKey(slice(kC4ReplicatorOptionAcceptParentDomainCookies));
            enc.writeBool(acceptParentDomainCookies);
            
            enc.writeKey(slice(kC4ReplicatorOptionAutoPurge));
            enc.writeBool(!disableAutoPurge); 
            
            enc.writeKey(slice(kC4ReplicatorOptionMaxRetries));
            if (maxAttempts > 0) {
                enc.writeUInt(maxAttempts - 1);
            } else {
                if (continuous) {
                    enc.writeUInt(kCBLDefaultReplicatorMaxAttemptsContinuous - 1);
                } else {
                    enc.writeUInt(kCBLDefaultReplicatorMaxAttemptsSingleShot - 1);
                }
            }
            
            enc.writeKey(slice(kC4ReplicatorOptionMaxRetryInterval));
            if (maxAttemptWaitTime > 0) {
                enc.writeUInt(maxAttemptWaitTime);
            } else {
                enc.writeUInt(kCBLDefaultReplicatorMaxAttemptsWaitTime);
            }
            
            enc.writeKey(slice(kC4ReplicatorHeartbeatInterval));
            if (heartbeat > 0) {
                enc.writeUInt(heartbeat);
            } else {
                enc.writeUInt(kCBLDefaultReplicatorHeartbeat);
            }
            
        #ifdef __CBL_REPLICATOR_NETWORK_INTERFACE__
            if (networkInterface.buf) {
                enc.writeKey(slice(kC4SocketOptionNetworkInterface));
                enc.writeString(networkInterface);
            }
        #endif
        }
        
        void writeCollectionOptions(CBLReplicationCollection& collection, Encoder &enc) const {
            writeOptionalKey(enc, kC4ReplicatorOptionDocIDs,        Array(collection.documentIDs));
            writeOptionalKey(enc, kC4ReplicatorOptionChannels,      Array(collection.channels));
        }

        slice getUserAgent() const                                                  { return slice(_userAgent); }
        
        CBLDatabase* effectiveDatabase() const                                      { return _retainedDatabase; }
        const std::vector<CBLReplicationCollection>& effectiveCollections() const   { return _effectiveCollections; }

        ReplicatorConfiguration(const ReplicatorConfiguration&) =delete;
        ReplicatorConfiguration& operator=(const ReplicatorConfiguration&) =delete;

    private:
        using string = std::string;
        using alloc_slice = fleece::alloc_slice;

        static slice copyString(slice str, alloc_slice &allocated) {
            allocated = alloc_slice(str);
            return allocated;
        }
        
        void validate() const {
            const char *problem = nullptr;
            if (!database && !collections)
                problem = "Invalid config: Missing both database and collections";
            else if (database && collections)
                problem = "Invalid config: Both database and collections are set at same time";
            else if (collections && collectionCount == 0)
                problem = "Invalid config: collectionCount is zero";
            else if ((documentIDs || channels || pushFilter || pullFilter) && !database)
                problem = "Invalid config: Cannot use documentIDs, channels, pushFilter or "
                          "pullFilter when collections is set. Set the properties in "
                          "CBLReplicationCollection instead.";
            else if (conflictResolver && !database)
                problem = "Invalid config: Cannot use conflictResolver when collections is set. "
                          "Set the property in CBLReplicationCollection instead.";
        #ifdef COUCHBASE_ENTERPRISE
            else if ((propertyEncryptor || propertyDecryptor ) && !database)
                problem = "Invalid config: Cannot use propertyEncryptor or propertyDecryptor "
                          "when collections is set. Use documentPropertyEncryptor or "
                          "documentPropertyDecryptor instead.";
        #endif
            else if (!endpoint || replicatorType > kCBLReplicatorTypePull)
                problem = "Invalid config: Missing endpoints or bad type";
            else if (!endpoint->valid())
                problem = "Invalid endpoint";
            else if (proxy && (proxy->type > kCBLProxyHTTPS ||
                                                    !proxy->hostname.buf || !proxy->port))
                problem = "Invalid replicator proxy settings";
            
            if (collections) {
                CBLDatabase* db = nullptr;
                for (int i = 0; i < collectionCount; i++) {
                    auto collection = collections[i].collection;
                    if (!collection->isValid()) {
                        problem = "An invalid collection was found in the configuration.";
                        break;
                    }
                    
                    if (!db) {
                        db = collection->database();
                    } else if (db != collection->database()) {
                        problem = "Invalid config: collections are not from the same database instance.";
                        break;
                    }
                }
            }

            if (problem)
                C4Error::raise(LiteCoreDomain, kC4ErrorInvalidParameter, "%s", problem);
        }

        string                                  _userAgent;
        std::vector<CBLReplicationCollection>   _effectiveCollections;
        std::vector<Retained<CBLCollection>>    _retainedCollections;
        Retained<CBLDatabase>                   _retainedDatabase;
        alloc_slice                             _pinnedServerCert, _trustedRootCerts;
        CBLProxySettings                        _proxy;
        alloc_slice                             _proxyHostname, _proxyUsername, _proxyPassword;
    #ifdef __CBL_REPLICATOR_NETWORK_INTERFACE__
        alloc_slice                             _networkInterface;
    #endif
    };
}

CBL_ASSUME_NONNULL_END
