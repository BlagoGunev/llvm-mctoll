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
#include <memory>
#include <limits>

namespace llvm {
namespace mctoll {
    
enum class BoundState { SET, NEG_INF, INF, UNSURE };

class ReducedIntervalCongruence {
private:
    uint64_t Alignment;
    int64_t IndexLowerBound;
    int64_t IndexUpperBound;
    int64_t Offset;

    BoundState LowerBoundState;
    BoundState UpperBoundState;

    inline bool isLowerBoundSet() {
        return LowerBoundState == BoundState::SET;
    }
    inline bool isUpperBoundSet() {
        return UpperBoundState == BoundState::SET;
    }

public:
    ReducedIntervalCongruence();
    ReducedIntervalCongruence(uint64_t alignment, int64_t indexLowerBound, 
            int64_t indexUpperBound, int64_t offset);
    ReducedIntervalCongruence(uint64_t alignment, int64_t indexLowerBound, 
            int64_t indexUpperBound, int64_t offset, BoundState lowerState,
            BoundState upperState);
    
    bool containsValue(int64_t value);
    bool isSubsetOf(ReducedIntervalCongruence &ric);
    bool intersectRIC(ReducedIntervalCongruence &ric);
    bool unionRIC(ReducedIntervalCongruence &ric);
    bool widenRIC(ReducedIntervalCongruence &ric);
    bool adjustRIC(int64_t value);
    bool removeLowerBounds();
    bool removeUpperBounds();

    // Getters
    uint64_t getAlignment() const { return Alignment; }
    int64_t getIndexLowerBound() const { return IndexLowerBound; }
    int64_t getIndexUpperBound() const { return IndexUpperBound; }
    int64_t getOffset() const { return Offset; }

    BoundState getLowerBoundState() const { return LowerBoundState; }
    BoundState getUpperBoundState() const { return UpperBoundState; }

    // Setters
    void setAlignment(uint64_t alignment) { Alignment = alignment; }
    void setIndexLowerBound(int64_t index) { IndexLowerBound = index; }
    void setIndexUpperBound(int64_t index) { IndexUpperBound = index; }
    void setOffset(int64_t offset) { Offset = offset; }

    void setLowerBoundState(BoundState state) { LowerBoundState = state; }
    void setUpperBoundState(BoundState state) { UpperBoundState = state; }

    bool operator==(const ReducedIntervalCongruence &ric) const {
        return ric.Alignment == Alignment && ric.IndexLowerBound == IndexLowerBound &&
            ric.IndexUpperBound == IndexUpperBound && ric.Offset == Offset;
    }
};

} // end namespace mctoll
} // end namespace llvm

namespace std {
template<>
struct hash<llvm::mctoll::ReducedIntervalCongruence>
{
    size_t operator()(const llvm::mctoll::ReducedIntervalCongruence& ric) const noexcept
    {
        size_t h1 = ric.getAlignment() << 3;
        size_t h2 = ric.getIndexLowerBound() << 2;
        size_t h3 = ric.getIndexUpperBound() << 1;
        size_t h4 = ric.getOffset();
        size_t h5 = (size_t)ric.getLowerBoundState() << 4;
        size_t h6 = (size_t)ric.getUpperBoundState() << 5;
        return h1 ^ h2 ^ h3 ^ h4 ^ h5 ^ h6;
    }
};
} // end namespace std


#endif // LLVM_TOOLS_LLVM_MCTOLL_REDUCEDINTERVALCONGRUENCE_H
