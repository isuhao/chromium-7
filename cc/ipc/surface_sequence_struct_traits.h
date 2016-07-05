// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_IPC_SURFACE_SEQUENCE_STRUCT_TRAITS_H_
#define CC_IPC_SURFACE_SEQUENCE_STRUCT_TRAITS_H_

#include "cc/ipc/surface_sequence.mojom.h"
#include "cc/surfaces/surface_sequence.h"

namespace mojo {

template <>
struct StructTraits<cc::mojom::SurfaceSequence, cc::SurfaceSequence> {
  static uint32_t id_namespace(const cc::SurfaceSequence& id) {
    return id.id_namespace;
  }

  static uint32_t sequence(const cc::SurfaceSequence& id) {
    return id.sequence;
  }

  static bool Read(cc::mojom::SurfaceSequenceDataView data,
                   cc::SurfaceSequence* out) {
    *out = cc::SurfaceSequence(data.id_namespace(), data.sequence());
    return true;
  }
};

}  // namespace mojo

#endif  // CC_IPC_SURFACE_SEQUENCE_STRUCT_TRAITS_H_