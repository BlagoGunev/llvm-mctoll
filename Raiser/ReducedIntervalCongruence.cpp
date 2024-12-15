//===-- ReducedIntervalCongruence.cpp ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ReducedIntervalCongruence.h"

#define DEBUG_TYPE "mctoll"

using namespace llvm::mctoll;

ReducedIntervalCongruence::ReducedIntervalCongruence() {
    LowerBoundState = BoundState::SET;
    UpperBoundState = BoundState::SET;
    Alignment = 1;
    IndexLowerBound = 0;
    IndexUpperBound = 0;
    Offset = 0;
}

ReducedIntervalCongruence::ReducedIntervalCongruence(uint64_t alignment, 
        int64_t indexLowerBound, int64_t indexUpperBound, int64_t offset) {
    Alignment = alignment;
    LowerBoundState = BoundState::SET;
    UpperBoundState = BoundState::SET;
    IndexLowerBound = indexLowerBound;
    IndexUpperBound = indexUpperBound;
    Offset = offset;
}
ReducedIntervalCongruence::ReducedIntervalCongruence(uint64_t alignment, 
        int64_t indexLowerBound, int64_t indexUpperBound, int64_t offset, 
        BoundState lowerState, BoundState upperState) {
    Alignment = alignment;
    LowerBoundState = lowerState;
    UpperBoundState = upperState;
    IndexLowerBound = indexLowerBound;
    IndexUpperBound = indexUpperBound;
    Offset = offset;
}
    
bool containsValue(int64_t value) {
    return true;
}