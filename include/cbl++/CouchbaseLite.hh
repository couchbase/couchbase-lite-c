//
//  CouchbaseLite.hh
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


/** \defgroup cbl_cpp Couchbase Lite C++ API
    @{

    \section error_handling Error handling

    Functions in the Couchbase Lite C++ API report Couchbase Lite-specific failures by
    throwing \ref cbl::Error, a subclass of `std::runtime_error` that provides the error
    domain, error code, and a human-readable message from `what()`.

    Catch \ref cbl::Error when you need Couchbase Lite-specific error details. Catch
    `std::exception` for general error handling.

    `noexcept` marks functions that are guaranteed not to throw. All other functions
    should be treated as potentially throwing on failure, unless otherwise documented.

    @} */

#pragma once
#include "Blob.hh"
#include "Collection.hh"
#include "Database.hh"
#include "Document.hh"
#include "Encryptable.hh"
#include "LogSinks.hh"
#include "Prediction.hh"
#include "Query.hh"
#include "QueryIndex.hh"
#include "Replicator.hh"
#include "VectorIndex.hh"
