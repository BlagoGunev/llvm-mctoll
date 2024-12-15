//===-- ReducedIntervalCongruence.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of ReducedIntervalCongruence
// class for use by llvm-mctoll.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCTOLL_REDUCEDINTERVALCONGRUENCE_H 
#define LLVM_TOOLS_LLVM_MCTOLL_REDUCEDINTERVALCONGRUENCE_H

#include <cstdint>

namespace llvm {
namespace mctoll {
    
class ReducedIntervalCongruence {
private:
    enum class BoundState { SET, NEG_INF, INF, UNSURE };

    uint64_t Alignment;
    int64_t IndexLowerBound;
    int64_t IndexUpperBound;
    int64_t Offset;

    BoundState LowerBoundState;
    BoundState UpperBoundState;

public:
    ReducedIntervalCongruence();
    ReducedIntervalCongruence(uint64_t alignment, int64_t indexLowerBound, 
            int64_t indexUpperBound, int64_t offset);
    ReducedIntervalCongruence(uint64_t alignment, int64_t indexLowerBound, 
            int64_t indexUpperBound, int64_t offset, BoundState lowerState,
            BoundState upperState);
    
    bool containsValue(int64_t value);
    bool isSubsetOf(ReducedIntervalCongruence &ric);
    bool intersectionRIC(ReducedIntervalCongruence &ric);
    bool unionRIC(ReducedIntervalCongruence &ric);
    bool widenRIC(ReducedIntervalCongruence &ric);
    bool adjustedRIC(int64_t value);
    bool removeLowerBounds();
    bool removeUpperBounds();
};

} // end namespace mctoll
} // end namespace llvm


#endif // LLVM_TOOLS_LLVM_MCTOLL_REDUCEDINTERVALCONGRUENCE_H
