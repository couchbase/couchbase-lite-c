//
// ReplicatorTest.hh
//
// Copyright © 2020 Couchbase. All rights reserved.
//

#pragma once
#include "CBLTest_Cpp.hh"
#include "cbl++/CouchbaseLite.hh"
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include <set>
#include <unordered_map>
#include <vector>

using namespace std;
using namespace fleece;
using namespace cbl;

class ReplicatorTest : public CBLTest_Cpp {
public:
    using clock    = std::chrono::high_resolution_clock;
    using time     = clock::time_point;
    using seconds  = std::chrono::duration<double, std::ratio<1,1>>;
    
    enum class IdleAction {
        kStopReplicator,    ///< Stop Replicator
        kContinueMonitor,   ///< Continue checking status
        kFinishMonitor      ///< Finish checking status
    };

    CBLReplicatorConfiguration config = {};
    
    vector<CBLReplicationCollection> defaultCollectionConfigs = {};
    
    CBLReplicator *repl = nullptr;
    
    bool enableDocReplicationListener = true;
    bool logEveryDocument = true;
    
    struct ReplicatedDoc {
        string scope;
        string collection;
        string docID;
        CBLDocumentFlags flags;
        CBLError error;
    };
    // docID format : <scope>.<collection>.<docID> or <docID> for default collection
    set<string> replicatedDocIDs;
    std::unordered_map<string, ReplicatedDoc> replicatedDocs;
    
    CBLError replError = {};
    IdleAction idleAction = IdleAction::kStopReplicator;
    double timeoutSeconds = 30.0;
    
    std::function<void(const CBLReplicatorStatus& status)> statusWatcher;

    CBLError expectedError = {};
    int64_t expectedDocumentCount = -1;

    ReplicatorTest() {
        resetDefaultReplicatorConfig();
    }
    
    ~ReplicatorTest() {
        if (repl) {
            CHECK(CBLReplicator_Status(repl).activity == kCBLReplicatorStopped);
            CBLReplicator_Release(repl);
        }
        
        CBLEndpoint_Free(config.endpoint);
        
        if (config.authenticator) {
            CBLAuth_Free(config.authenticator);
        }
        
        if (config.headers) {
            FLDict_Release(config.headers);
        }
        
        // For async clean up in Replicator:
        this_thread::sleep_for(500ms);
    }
    
    /** (Re)initializes the default replicator configuration using the default collection,
        configured as a pull replicator. */
    void resetDefaultReplicatorConfig() {
        defaultCollectionConfigs = collectionConfigs({ defaultCollection.ref() });
        config.collections = defaultCollectionConfigs.data();
        config.collectionCount = defaultCollectionConfigs.size();
        config.replicatorType = kCBLReplicatorTypePull;
        config.context = this;
    }
    
    /** A utility function to create a vector of collection configurations. */
    std::vector<CBLReplicationCollection> collectionConfigs(
        const std::vector<CBLCollection*>& collections,
        std::function<void(CBLReplicationCollection&)> configure = nullptr
    ) {
        std::vector<CBLReplicationCollection> colConfigs(collections.size());
        for (size_t i = 0; i < collections.size(); i++) {
            colConfigs[i].collection = collections[i];
            if (configure) {
                configure(colConfigs[i]);  // apply user customization
            }
        }
        return colConfigs;
    }
    
    /** A utility function to (re)configure the current collection configuration. */
    inline void configureCollectionConfigs(
        CBLReplicatorConfiguration& cfg,
        std::function<void(CBLReplicationCollection&)> configure
    ) {
        if (!configure) { return; };
        
        for (size_t i = 0; i < cfg.collectionCount; i++) {
            configure(cfg.collections[i]);
        }
    }
    
    /** Release and set the current replicator used in tests to nullptr. */
    void resetReplicator() {
        if (!repl) {
            return;
        }
        
        REQUIRE(CBLReplicator_Status(repl).activity == kCBLReplicatorStopped);
        CBLReplicator_Release(repl);
        repl = nullptr;
    }
    
    /** Create a new replicator with the current config if the replicator doesn't exist, and start the replicator.
        For a continuous replicator, when the replicator is IDLE, the replicator will be stopped. */
    void replicate(bool reset =false) {
        CBLError error;
        CBLReplicatorStatus status;
        if (!repl) {
            repl = CBLReplicator_Create(&config, &error);
            status = CBLReplicator_Status(repl);
            CHECK(status.activity == kCBLReplicatorStopped);
            CHECK(status.progress.complete == 0.0);
            CHECK(status.progress.documentCount == 0);
            CHECK(status.error.code == 0);
        }
        REQUIRE(repl);

        auto ctoken = CBLReplicator_AddChangeListener(repl, [](void *context, CBLReplicator *r,
                                                               const CBLReplicatorStatus *status) {
            ((ReplicatorTest*)context)->statusChanged(r, *status);
        }, this);

        
        CBLListenerToken* dtoken = nullptr;
        if (enableDocReplicationListener) {
            dtoken = CBLReplicator_AddDocumentReplicationListener(repl, [](void *context, CBLReplicator *r,
                                                                           bool isPush,
                                                                           unsigned numDocuments,
                                                                           const CBLReplicatedDocument* documents) {
                ((ReplicatorTest*)context)->docProgress(r, isPush, numDocuments, documents);
            }, this);
        }
        
        CBLReplicator_Start(repl, reset);

        time start = clock::now();
        cerr << "Waiting...\n";
        while (std::chrono::duration_cast<seconds>(clock::now() - start).count() < timeoutSeconds) {
            status = CBLReplicator_Status(repl);
            if (config.continuous && status.activity == kCBLReplicatorIdle) {
                if (idleAction == IdleAction::kStopReplicator) {
                    cerr << "Stop the continuous replicator...\n";
                    CBLReplicator_Stop(repl);
                } else if (idleAction == IdleAction::kFinishMonitor) {
                    break;
                }
            } else if (status.activity == kCBLReplicatorStopped)
                break;
            this_thread::sleep_for(100ms);
        }
        cerr << "Finished with activity=" << static_cast<int>(status.activity)
             << ", complete=" << status.progress.complete
             << ", documentCount=" << status.progress.documentCount
             << ", error=(" << status.error.domain << "/" << status.error.code << ")\n";
        
        if (config.continuous && idleAction == IdleAction::kFinishMonitor)
            CHECK(status.activity == kCBLReplicatorIdle);
        else
            CHECK(status.activity == kCBLReplicatorStopped);
        
        if (expectedError.code > 0) {
            CHECK(status.error.code == expectedError.code);
        } else {
            CHECK(status.error.code == 0);
            CHECK(status.progress.complete == 1.0);
        }
        
        if (expectedDocumentCount >= 0) {
            CHECK(status.progress.documentCount == expectedDocumentCount);
        }
        
        CBLListener_Remove(ctoken);
        CBLListener_Remove(dtoken);
    }
    
    bool waitForActivityLevel(CBLReplicatorActivityLevel level, double timeout) {
        return waitForActivityLevelAndDocumentCount(level, -1, timeout);
    }
    
    bool waitForActivityLevelAndDocumentCount(CBLReplicatorActivityLevel level,
                                              int64_t documentCount, double timeout)
    {
        time start = clock::now();
        while (std::chrono::duration_cast<seconds>(clock::now() - start).count() < timeout) {
            auto status = CBLReplicator_Status(repl);
            if (status.activity == level && (documentCount < 0 ||
                                             status.progress.documentCount == documentCount)) {
                return true;
            }
            this_thread::sleep_for(100ms);
        }
        return false;
    }

    void statusChanged(CBLReplicator *r, const CBLReplicatorStatus &status) {
        CHECK(r == repl);
        cerr << "--- PROGRESS: status=" << static_cast<int>(status.activity)
             << ", fraction=" << status.progress.complete
             << ", err=" << status.error.domain << "/" << status.error.code << "\n";
        if (status.error.code && !replError.code)
            replError = status.error;
        if (statusWatcher) statusWatcher(status);
    }

    void docProgress(CBLReplicator *r, bool isPush,
                     unsigned numDocuments,
                     const CBLReplicatedDocument* documents) {
        CHECK(r == repl);
        cerr << "--- " << numDocuments << " docs " << (isPush ? "pushed" : "pulled") << ":";
        if (logEveryDocument) {
            for (unsigned i = 0; i < numDocuments; ++i) {
                auto doc = documents[i];
                
                ReplicatedDoc rdoc {};
                rdoc.scope = slice(doc.scope).asString();;
                rdoc.collection = slice(doc.collection).asString();
                rdoc.docID = slice(doc.ID).asString();
                rdoc.flags = doc.flags;
                rdoc.error = doc.error;
                
                string key;
                if (rdoc.scope == "_default" && rdoc.collection == "_default")
                    key = rdoc.docID;
                else
                    key = rdoc.scope + "." + rdoc.collection + "." + rdoc.docID;
                
                replicatedDocs[key] = rdoc;
                replicatedDocIDs.insert(key);
                cerr << " " << key;
            }
        }
        cerr << "\n";
    }
    
    alloc_slice getServerCert() {
        FILE* f = fopen("vendor/couchbase-lite-core/Replicator/tests/data/cert.pem", "r");
        REQUIRE(f);
        char buf[10000];
        auto n = fread(buf, 1, sizeof(buf), f);
        REQUIRE(n > 0);
        REQUIRE(n < sizeof(buf));
        return alloc_slice(buf, n);
    }

    static vector<string> asVector(const set<string> strings) {
        vector<string> out;
        for (const string &s : strings)
            out.push_back(s);
        return out;
    }
    
    static string getDocIDKey(ReplicatedDoc& doc) {
        if (doc.scope == "_default" && doc.collection == "_default") {
            return doc.docID;
        } else {
            return doc.scope + "." + doc.collection + "." + doc.docID;
        }
    }

};
