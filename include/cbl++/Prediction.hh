//
// Prediction.hh
//
// Copyright (c) 2024 Couchbase, Inc All rights reserved.
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

// VOLATILE API: Couchbase Lite C++ API is not finalized, and may change in
// future releases.

#ifdef COUCHBASE_ENTERPRISE

#pragma once
#include "cbl++/Base.hh"
#include "cbl/CBLPrediction.h"
#include <string_view>

// VOLATILE API: Couchbase Lite C++ API is not finalized, and may change in
// future releases.

CBL_ASSUME_NONNULL_BEGIN

namespace cbl {
    /** ENTERPRISE EDITION ONLY
     
        The PredictiveModel  that allows to integrate machine learning model
        into queries via invoking query's PREDICTION() function.
     
        @note The predictive index feature is not supported by Couchbase Lite for C.
              The Predictive Model is currently for creating vector indexes using the PREDICTION() function,
              which will call the specified predictive model for computing the vectors. */

    /** A predictive model callable, invoked by queries via the PREDICTION() function.
        Given an input dictionary, return the output dictionary. */
    using PredictiveModel = std::function<fleece::MutableDict(fleece::Dict)>;

    /** Registers/unregisters predictive models by name. */
    class Prediction {
    public:
        /** Registers a predictive model with the given name.
            @param name  The name used to refer to the model in a query's PREDICTION() function.
            @param model  The model implementation. Any matching callable is accepted (lambda, functor, ...). */
        static void registerModel(std::string_view name, PredictiveModel model) {
            auto* holder = new PredictiveModel(std::move(model));
            CBLPredictiveModel config{};
            config.context      = holder;
            config.prediction   = [](void* ctx, FLDict input) -> FLMutableDict {
                auto& fn = *static_cast<PredictiveModel*>(ctx);
                return FLMutableDict_Retain((FLMutableDict)fn(fleece::Dict(input)));
            };
            config.unregistered = [](void* ctx) {
                delete static_cast<PredictiveModel*>(ctx);
            };
            CBL_RegisterPredictiveModel(slice(name), config);
        }

        /** Unregisters the model; LiteCore fires `unregistered` which frees the model. */
        static void unregisterModel(std::string_view name) {
            CBL_UnregisterPredictiveModel(slice(name));
        }
    };
}

CBL_ASSUME_NONNULL_END
    
#endif
