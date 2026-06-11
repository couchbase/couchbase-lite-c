//
//  CBLDatabase_Internal.hh
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
#include "CBLDatabase.h"
#include "CBLDocument_Internal.hh"
#include "CBLLog_Internal.hh"
#include "CBLPrivate.h"
#include "c4Collection.hh"
#include "c4Database.hh"
#include "c4Observer.hh"
#include "Error.hh"
#include "Internal.hh"
#include "Listener.hh"
#include "access_lock.hh"
#include "fleece/function_ref.hh"
#include "fleece/Mutable.hh"
#include "fleece/RefCounted.hh"
#include <condition_variable>
#include <string>
#include <utility>
#include <unordered_set>

CBL_ASSUME_NONNULL_BEGIN

namespace cbl_internal {
    struct CBLLocalEndpoint;
}


struct CBLDatabase final : public CBLRefCounted {
public:

#ifdef COUCHBASE_ENTERPRISE
    
#pragma mark - Database Extension:
    
    static inline constexpr slice kVectorSearchExtension = "CouchbaseLiteVectorSearch";
    
    static void enableVectorSearch(slice path) {
        CBLLog_Init();
        C4Database::enableExtension(kVectorSearchExtension, path);
    }
    
#endif

#pragma mark - Lifecycle:

    static CBLDatabaseConfiguration defaultConfiguration() {
        CBLDatabaseConfiguration config = {};
        config.directory = effectiveDir(fleece::nullslice);
        return config;
    }

    static bool exists(slice name, slice inDirectory) {
        return C4Database::exists(name, effectiveDir(inDirectory));
    }

    static void copyDatabase(slice fromPath,
                             slice toName,
                             const CBLDatabaseConfiguration* _cbl_nullable config)
    {
        CBLLog_Init();
        C4DatabaseConfig2 c4config = asC4Config(config);
        C4Database::copyNamed(fromPath, toName, c4config);
    }

    static void deleteDatabase(slice name, slice inDirectory) {
        CBLLog_Init();
        (void) C4Database::deleteNamed(name, effectiveDir(inDirectory));
    }

    static Retained<CBLDatabase> open(slice name,
                                      const CBLDatabaseConfiguration* _cbl_nullable config =nullptr)
    {
#ifdef __ANDROID__
        if (!getInitContext()) {
            C4Error::raise(LiteCoreDomain, kC4ErrorUnsupported,
                           "The context hasn't been initialized. Call CBL_Init(CBLInitContext*) to initialize the context");
        }
#endif
        CBLLog_Init();
        C4DatabaseConfig2 c4config = asC4Config(config);
        Retained<C4Database> c4db = C4Database::openNamed(name, c4config);
        return new CBLDatabase(c4db, name, c4config.parentDirectory);
    }

    void performMaintenance(CBLMaintenanceType type) {
        _c4db->useLocked()->maintenance((C4MaintenanceType)type);
    }

#ifdef COUCHBASE_ENTERPRISE
    void changeEncryptionKey(const CBLEncryptionKey* _cbl_nullable newKey) {
        C4EncryptionKey c4key = asC4Key(newKey);
        _c4db->useLocked()->rekey(&c4key);
    }
#endif

    void beginTransaction()                          {_c4db->useLocked()->beginTransaction();}
    void endTransaction(bool commit)                 {_c4db->useLocked()->endTransaction(commit);}
    
    void close();
    void closeAndDelete();


#pragma mark - Accessors:


    slice name() const noexcept                      {return _name;}
    alloc_slice path() const                         {return _c4db->useLocked()->getPath();}
    
    CBLDatabaseConfiguration config() const noexcept {
        auto &c4config = _c4db->useLocked()->getConfiguration();
        CBLDatabaseConfiguration config {};
        config.directory = c4config.parentDirectory;
#ifdef COUCHBASE_ENTERPRISE
        config.encryptionKey = asCBLKey(c4config.encryptionKey);
#endif
        config.fullSync = (c4config.flags & kC4DB_DiskSyncFull) == kC4DB_DiskSyncFull;
        return config;
    }

    alloc_slice publicUUID() const {
        C4UUID uuid = _c4db->useLocked()->getPublicUUID();
        return {uuid.bytes, 16};
    }

    std::string desc() const                         {return "CBLDatabase[" + _name.asString() + "]";}

    
#pragma mark - Collections:
    
    
    fleece::MutableArray scopeNames() const {
        auto db = _c4db->useLocked();
        auto names = FLMutableArray_New();
        db->forEachScope([&](slice scope) {
            FLMutableArray_AppendString(names, scope);
        });
        return names;
    }
    
    fleece::MutableArray collectionNames(slice scopeName) const {
        auto db = _c4db->useLocked();
        auto names = FLMutableArray_New();
        db->forEachCollection(scopeName, [&](C4CollectionSpec spec) {
            FLMutableArray_AppendString(names, spec.name);
        });
        return names;
    }
    
    Retained<CBLScope> getScope(slice scopeName);
    
    Retained<CBLCollection> getCollection(slice collectionName, slice scopeName);
    
    Retained<CBLCollection> createCollection(slice collectionName, slice scopeName);
    
    bool deleteCollection(slice collectionName, slice scopeName);
    
    Retained<CBLScope> getDefaultScope();
    
    Retained<CBLCollection> getDefaultCollection();
    
    /**
     * The cached default collection for internal use only.
     */
    Retained<CBLCollection> getInternalDefaultCollection();
    

#pragma mark - Queries & Indexes:


    Retained<CBLQuery> createQuery(CBLQueryLanguage language,
                                   slice queryString,
                                   int* _cbl_nullable outErrPos) const;
    

#pragma mark - Listeners:
    

    void sendNotifications() {
        _notificationQueue.notifyAll();
    }

    void bufferNotifications(CBLNotificationsReadyCallback _cbl_nullable callback, void* _cbl_nullable context) {
        _notificationQueue.setCallback(callback, context);
    }


#pragma mark - Binding Dev Support for Blob:
    
    
    Retained<CBLBlob> getBlob(FLDict properties);
    
    void saveBlob(CBLBlob* blob);
    

#pragma mark - Internals:


protected:
    
    friend struct CBLBlob;
    friend struct CBLNewBlob;
    friend struct CBLBlobWriteStream;
    friend struct CBLCollection;
    friend struct CBLDocument;
    friend struct CBLIndexUpdater;
    friend struct CBLQueryIndex;
    friend struct CBLReplicator;
    friend struct CBLURLEndpointListener;
    friend struct cbl_internal::CBLLocalEndpoint;
    friend struct cbl_internal::ListenerToken<CBLDocumentChangeListener>;
    friend struct cbl_internal::ListenerToken<CBLQueryChangeListener>;
    friend struct cbl_internal::ListenerToken<CBLCollectionDocumentChangeListener>;
    
    /** The C4Database lock that adds a close() function for flagging that the c4database has been closed. It has
        a sentry guard setup tha will throw NotOpen when useLocked() function is called when the closed has been
        flagged. */
    class C4DatabaseAccessLock: public litecore::access_lock<Retained<C4Database>> {
    public:
        C4DatabaseAccessLock(C4Database* db)
        :access_lock(std::move(db))
        {
            _sentry = [this](C4Database* db) {
                if (isClosedNoLock()) {
                    C4Error::raise(LiteCoreDomain, kC4ErrorNotOpen, "Database is closed or deleted");
                }
            };
        }
            
        void close() {
            LOCK_GUARD lock(getMutex());
            _closed = true;
        }
        
        bool isClosedNoLock() {
            return _closed;
        }
        
        template <class CALLBACK>
        /** If the AccessLock is closed, the call will be ignored instead of letting the sentry throws. */
        void useLockedIgnoredWhenClosed(CALLBACK callback) {
            LOCK_GUARD lock(getMutex());
            if (_closed)
                return;
            useLocked(callback);
        }
            
    private:
        bool _closed {false};
    };
    
    using SharedC4DatabaseAccessLock = std::shared_ptr<C4DatabaseAccessLock>;
    
    SharedC4DatabaseAccessLock c4db() const         {return _c4db;}
    
    C4BlobStore* blobStore() const                  {return &(_c4db->useLocked()->getBlobStore());}

    template <class LISTENER, class... Args>
    void notify(ListenerToken<LISTENER>* _cbl_nonnull listener, Args... args) const {
        Retained<ListenerToken<LISTENER>> retained = listener;
        notify([=]() {
            retained->call(args...);
        });
    }

    void notify(Notification n) const   {const_cast<CBLDatabase*>(this)->_notificationQueue.add(n);}

    auto useLocked()                    {return _c4db->useLocked();}
    template <class LAMBDA>
    void useLocked(LAMBDA callback)     {_c4db->useLocked(callback);}
    template <class RESULT, class LAMBDA>
    RESULT useLocked(LAMBDA callback)   {return _c4db->useLocked<RESULT>(callback);}

private:
    
    CBLDatabase(C4Database* _cbl_nonnull db, slice name_, slice dir_);

    virtual ~CBLDatabase();

    // Default location for databases. This is platform-dependent.
    static std::string defaultDirectory();

#ifdef COUCHBASE_ENTERPRISE
    static C4EncryptionKey asC4Key(const CBLEncryptionKey* _cbl_nullable key) {
        C4EncryptionKey c4key;
        if (key) {
            c4key.algorithm = static_cast<C4EncryptionAlgorithm>(key->algorithm);
            memcpy(c4key.bytes, key->bytes, sizeof(CBLEncryptionKey::bytes));
        } else {
            c4key.algorithm = kC4EncryptionNone;
        }
        return c4key;
    }

    static CBLEncryptionKey asCBLKey(const C4EncryptionKey &c4key) {
        CBLEncryptionKey key;
        key.algorithm = static_cast<CBLEncryptionAlgorithm>(c4key.algorithm);
        memcpy(key.bytes, c4key.bytes, sizeof(CBLEncryptionKey::bytes));
        return key;
    }
#endif

    static C4DatabaseConfig2 asC4Config(const CBLDatabaseConfiguration* _cbl_nullable config) {
        CBLDatabaseConfiguration defaultConfig;
        if (!config) {
            defaultConfig = CBLDatabaseConfiguration_Default();
            config = &defaultConfig;
        }
        
        C4DatabaseConfig2 c4Config = {};
        c4Config.parentDirectory = effectiveDir(config->directory);
        c4Config.flags = kC4DB_Create;
        if (config->fullSync) {
            c4Config.flags |= kC4DB_DiskSyncFull;
        }
        
#ifdef COUCHBASE_ENTERPRISE
        c4Config.encryptionKey = asC4Key(&config->encryptionKey);
#endif
        return c4Config;
    }


    static slice effectiveDir(slice inDirectory) {
        if (inDirectory) {
            return inDirectory;
        } else {
            static const std::string kDir = defaultDirectory();
            return slice(kDir);
        }
    }

    void callDocListeners();
    
    void stopActiveService() {
        std::unordered_set<CBLActiveService> services;
        {
            std::lock_guard<std::mutex> lock(_stopMutex);
            if (_stopping)
                return;
            _stopping = true;
            services = _services;
        }

        // Call stop outside lock to prevent deadlock:
        for (const auto& s : services) {
            s.stop();
        }
        
        std::unique_lock<std::mutex> lock(_stopMutex);
        if (!_services.empty()) {
            CBL_Log(kCBLLogDomainDatabase, kCBLLogInfo,
                    "Waiting for %zu active replicators and live queries to stop ...",
                    _services.size());
            _stopCond.wait(lock, [this] { return _services.empty(); });
        }
    }
    
    bool registerService(CBLRefCounted* ref, std::function<void()> stopFn) {
        LOCK(_stopMutex);
        if (_stopping) { return false; }
        _services.emplace(ref, std::move(stopFn));
        return true;
    }
    
    void unregisterService(CBLRefCounted* ref) {
        LOCK(_stopMutex);
        _services.erase(CBLActiveService{ref, nullptr});
        _stopCond.notify_one();
    }

    /** Close the scopes and collections, and access lock. Must call under _c4db lock. */
    void _closed();
    
    template <class T> using Listeners = cbl_internal::Listeners<T>;

    using ScopesMap = std::unordered_map<slice, Retained<CBLScope>>;
    using CollectionsMap = std::unordered_map<C4Database::CollectionSpec, Retained<CBLCollection>>;
    
    SharedC4DatabaseAccessLock                  _c4db;
    
    alloc_slice const                           _name;
    alloc_slice const                           _dir;
    
    Retained<CBLCollection>                     _defaultCollection;     // Internal default collection
    
    // For sending notifications:
    NotificationQueue                           _notificationQueue;
    
    // For Active Services:
    bool                                        _stopping {false};
    mutable std::mutex                          _stopMutex;
    std::condition_variable                     _stopCond;
    std::unordered_set<CBLActiveService>        _services;
};

CBL_ASSUME_NONNULL_END
