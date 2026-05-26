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

// VOLATILE API: Couchbase Lite C++ API is not finalized, and may change in
// future releases.

/** \defgroup cbl_cpp Couchbase Lite C++ API
    @{

    \section error_handling Error handling

    On failure, functions in this API report errors by throwing exceptions derived from
    `std::exception`, so callers should be prepared to catch `std::exception`.

    Couchbase Lite-specific failures are thrown as \ref cbl::Error, a subclass of
    `std::runtime_error` that encapsulates the failure's `domain`, `code`, and message.
    Catch `cbl::Error` when you need that structured information; catch `std::exception`
    for general handling.
    (The raw C-API `CBLError` status struct is wrapped by `cbl::Error` and is never
    thrown directly across this API.)

    `noexcept` is applied only to special member functions (copy/move constructors and
    assignment operators, and destructors), where the no-throw guarantee is structural and
    permanent. It is deliberately omitted from other methods — even ones that cannot throw
    today — so that their implementations remain free to throw in a future release without
    a breaking API change. Do not infer anything from the absence of `noexcept`; assume any
    method may throw `cbl::Error` (or another `std::exception`) on failure. Individual
    functions add an `@throws cbl::Error` note only when the specific trigger, or a
    non-throwing outcome worth contrasting (such as a lookup that returns an empty result
    instead of throwing when an item doesn't exist), is not obvious from the signature.

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
