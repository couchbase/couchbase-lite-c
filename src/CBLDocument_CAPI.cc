//
// CBLDocument_CAPI.cc
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

#include "CBLDocument_Internal.hh"
#include "CBLPrivate.h"

using namespace fleece;

CBL_PUBLIC const FLSlice kCBLTypeProperty              = C4Document::kObjectTypeProperty;

CBLDocument* CBLDocument_Create() noexcept {
    return CBLDocument_CreateWithID(nullslice);
}

CBLDocument* CBLDocument_CreateWithID(FLString docID) noexcept {
    return make_retained<CBLDocument>(docID, true).detach();
}

CBLDocument* CBLDocument_MutableCopy(const CBLDocument* doc) noexcept {
    return make_retained<CBLDocument>(doc).detach();
}

FLSlice CBLDocument_ID(const CBLDocument* doc) noexcept                 {return doc->docID();}
FLSlice CBLDocument_RevisionID(const CBLDocument* doc) noexcept         {return doc->revisionID();}
uint64_t CBLDocument_Sequence(const CBLDocument* doc) noexcept          {return doc->sequence();}
CBLCollection* CBLDocument_Collection(const CBLDocument* doc) noexcept  {return doc->collection();}
FLDict CBLDocument_Properties(const CBLDocument* doc) noexcept          {return doc->properties();}

/** Private API */
FLSliceResult CBLDocument_CanonicalRevisionID(const CBLDocument* doc) noexcept {
    return FLSliceResult(doc->canonicalRevisionID());
}

/** Private API */
unsigned CBLDocument_Generation(const CBLDocument* doc) noexcept {
    return doc->generation();
}

/** Private API */
FLSliceResult CBLDocument_GetRevisionHistory(const CBLDocument* doc) noexcept {
    return FLSliceResult(doc->getRevisionHistory());
}

FLMutableDict CBLDocument_MutableProperties(CBLDocument* doc) noexcept {
    return doc->mutableProperties();
}

FLSliceResult CBLDocument_CreateJSON(const CBLDocument* doc) noexcept {
    return FLSliceResult(doc->propertiesAsJSON());
}

void CBLDocument_SetProperties(CBLDocument* doc, FLMutableDict properties) noexcept {
    doc->setProperties(properties);
}

bool CBLDocument_SetJSON(CBLDocument* doc, FLSlice json, CBLError* outError) noexcept {
    try {
        doc->setPropertiesAsJSON(json);
        return true;
    } catchAndBridge(outError)
}
