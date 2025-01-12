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

int64_t getLowerIndexNormalized(int64_t index, BoundState state) {
    switch (state)
    {
    case BoundState::SET:
        return index;
    case BoundState::NEG_INF:
        return std::numeric_limits<int64_t>::min();
    default:
        return std::numeric_limits<int64_t>::max();
    }
}

int64_t getUpperIndexNormalized(int64_t index, BoundState state) {
    switch (state) 
    {
    case BoundState::SET:
        return index;
    case BoundState::INF:
        return std::numeric_limits<int64_t>::max();
    default:
        return std::numeric_limits<int64_t>::min();
    }
}

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
    
bool ReducedIntervalCongruence::containsValue(int64_t value) {
    value -= Offset;
    if (value % Alignment != 0) {
        return false;
    }
    value /= Alignment;
    if ((isLowerBoundSet() && IndexLowerBound > value) || 
        (isUpperBoundSet() && IndexUpperBound < value)) {
        return false;
    }
    return true;
}

bool ReducedIntervalCongruence::isSubsetOf(ReducedIntervalCongruence &ric) {
    if (Alignment < ric.getAlignment()) {
        return false;
    }
    if ((!isLowerBoundSet() && ric.getLowerBoundState() == BoundState::SET) || 
        (!isUpperBoundSet() && ric.getUpperBoundState() == BoundState::SET)) {
        return false;
    }
    int64_t lowerValue = Offset + Alignment * IndexLowerBound;
    int64_t upperValue = Offset + Alignment * IndexUpperBound;
    int64_t lowerValueParent = ric.getOffset() + ric.getAlignment() * ric.getIndexLowerBound();
    int64_t upperValueParent = ric.getOffset() + ric.getAlignment() * ric.getIndexUpperBound();

    // TODO update conditions to handle edge cases
    if (ric.getLowerBoundState() == BoundState::UNSURE || 
        ric.getUpperBoundState() == BoundState::UNSURE) {
        return false;
    }

    bool lowerSideCheck = ric.getLowerBoundState() == BoundState::NEG_INF || 
            (isLowerBoundSet() && lowerValue >= lowerValueParent);
    bool upperSideCheck = ric.getUpperBoundState() == BoundState::INF ||
            (isUpperBoundSet() && upperValue <= upperValueParent);

    return lowerSideCheck && upperSideCheck;
}

bool ReducedIntervalCongruence::intersectRIC(ReducedIntervalCongruence &ric) {

    if (LowerBoundState == BoundState::UNSURE || UpperBoundState == BoundState::UNSURE ||
            ric.getLowerBoundState() == BoundState::UNSURE || 
            ric.getUpperBoundState() == BoundState::UNSURE) {
        return false;
    }

    // TODO make more robust intersection checks
    std::unique_ptr<ReducedIntervalCongruence> intersection;
    intersection->setAlignment(std::max(Alignment, ric.getAlignment()));

    int64_t lowerLimit;
    if (isLowerBoundSet()) {
        lowerLimit = Offset + IndexLowerBound * Alignment;
    } else {
        lowerLimit = std::numeric_limits<int64_t>::min();
    }
    int64_t cmpLowerLimit;
    if (ric.getLowerBoundState() == BoundState::SET) {
        cmpLowerLimit = ric.getOffset() + ric.getIndexLowerBound() * ric.getAlignment();
    } else {
        cmpLowerLimit = std::numeric_limits<int64_t>::min();
    }

    int64_t upperLimit;
    if (isUpperBoundSet()) {
        upperLimit = Offset + IndexUpperBound * Alignment;
    } else {
        upperLimit = std::numeric_limits<int64_t>::max();
    }
    int64_t cmpUpperLimit;
    if (ric.getUpperBoundState() == BoundState::SET) {
        cmpUpperLimit = ric.getOffset() + ric.getIndexUpperBound() * ric.getAlignment();
    } else {
        cmpUpperLimit = std::numeric_limits<int64_t>::max();
    }

    // Lower limit set
    if (lowerLimit >= cmpLowerLimit) {
        intersection->setLowerBoundState(LowerBoundState);
        intersection->setOffset(Offset);
        intersection->setIndexLowerBound(IndexLowerBound);
    } else {
        intersection->setLowerBoundState(ric.getLowerBoundState());
        intersection->setOffset(ric.getOffset());
        intersection->setIndexLowerBound(ric.getIndexLowerBound());
    }

    int64_t actualLowerLimit = std::max(lowerLimit, cmpLowerLimit);

    // Return false(no intersection) if the lower limit exceeds the upper limit
    if (actualLowerLimit > cmpUpperLimit || actualLowerLimit > upperLimit) {
        return false;
    }

    // Upper limit set
    if (intersection->getLowerBoundState() == BoundState::SET) {
        if (upperLimit <= cmpUpperLimit) {
            int64_t alignmentDiff = (upperLimit - actualLowerLimit) / 
                intersection->getAlignment();
            intersection->setUpperBoundState(UpperBoundState);
            intersection->setIndexUpperBound(alignmentDiff + 
                intersection->getIndexLowerBound());
        } else {
            int64_t alignmentDiff = (cmpUpperLimit - actualLowerLimit) / 
                intersection->getAlignment();
            intersection->setUpperBoundState(ric.getUpperBoundState());
            intersection->setIndexUpperBound(alignmentDiff + 
                intersection->getIndexLowerBound());
        }
    } else {
        if (upperLimit <= cmpUpperLimit) {
            int64_t alignmentDiff = (upperLimit - intersection->getOffset()) / 
                intersection->getAlignment();
            intersection->setUpperBoundState(UpperBoundState);
            intersection->setIndexUpperBound(alignmentDiff);
        } else {
            int64_t alignmentDiff = (cmpUpperLimit - intersection->getOffset()) / 
                intersection->getAlignment();
            intersection->setUpperBoundState(ric.getUpperBoundState());
            intersection->setIndexUpperBound(alignmentDiff);
        }
    }

    // if (LowerBoundState < ric.getLowerBoundState()) {
    //     intersection->setLowerBoundState(LowerBoundState);
    // } else {
    //     intersection->setLowerBoundState(ric.getLowerBoundState());
    // }

    // if (UpperBoundState < ric.getUpperBoundState()) {
    //     intersection->setUpperBoundState(UpperBoundState);
    // } else {
    //     intersection->setUpperBoundState(ric.getUpperBoundState());
    // }
    // // ^ could change this part to use std::min

    // intersection->setAlignment(std::max(Alignment, ric.getAlignment()));

    // int64_t lowerIndex = 
    //     getLowerIndexNormalized(IndexLowerBound, LowerBoundState);

    // int64_t cmpLowerIndex = 
    //     getLowerIndexNormalized(ric.getIndexLowerBound(), ric.getLowerBoundState());

    // int64_t upperIndex = 
    //     getUpperIndexNormalized(IndexUpperBound, UpperBoundState);
    
    // int64_t cmpUpperIndex = 
    //     getUpperIndexNormalized(ric.getIndexUpperBound(), ric.getUpperBoundState());
    
    // intersection->setIndexLowerBound(std::max(lowerIndex, cmpLowerIndex));
    // intersection->setIndexUpperBound(std::min(upperIndex, cmpUpperIndex));

    // Probably redundant
    if (intersection->isLowerBoundSet() && intersection->isUpperBoundSet() && 
            intersection->getIndexLowerBound() > intersection->getIndexUpperBound()) {
        return false;
    }

    Alignment = intersection->getAlignment();
    IndexLowerBound = intersection->getIndexLowerBound();
    IndexUpperBound = intersection->getIndexUpperBound();
    Offset = intersection->getOffset();
    LowerBoundState = intersection->getLowerBoundState();
    UpperBoundState = intersection->getUpperBoundState();

    return true;
}

bool ReducedIntervalCongruence::unionRIC(ReducedIntervalCongruence &ric) {

    // For now treat UNSURE as INF

    // TODO more robust checks for union
    std::unique_ptr<ReducedIntervalCongruence> unionr;

    unionr->setAlignment(std::min(Alignment, ric.getAlignment()));

    int64_t lowerLimit;
    if (isLowerBoundSet()) {
        lowerLimit = Offset + IndexLowerBound * Alignment;
    } else {
        lowerLimit = std::numeric_limits<int64_t>::min();
    }
    int64_t cmpLowerLimit;
    if (ric.getLowerBoundState() == BoundState::SET) {
        cmpLowerLimit = ric.getOffset() + ric.getIndexLowerBound() * ric.getAlignment();
    } else {
        cmpLowerLimit = std::numeric_limits<int64_t>::min();
    }

    int64_t upperLimit;
    if (isUpperBoundSet()) {
        upperLimit = Offset + IndexUpperBound * Alignment;
    } else {
        upperLimit = std::numeric_limits<int64_t>::max();
    }
    int64_t cmpUpperLimit;
    if (ric.getUpperBoundState() == BoundState::SET) {
        cmpUpperLimit = ric.getOffset() + ric.getIndexUpperBound() * ric.getAlignment();
    } else {
        cmpUpperLimit = std::numeric_limits<int64_t>::max();
    }

    // Lower limit set
    if (lowerLimit <= cmpLowerLimit) {
        intersection->setLowerBoundState(LowerBoundState);
        intersection->setOffset(Offset);
        intersection->setIndexLowerBound(IndexLowerBound);
    } else {
        intersection->setLowerBoundState(ric.getLowerBoundState());
        intersection->setOffset(ric.getOffset());
        intersection->setIndexLowerBound(ric.getIndexLowerBound());
    }

    int64_t actualLowerLimit = std::min(lowerLimit, cmpLowerLimit);

    // Upper limit set
    if (intersection->getLowerBoundState() == BoundState::SET) {
        if (upperLimit <= cmpUpperLimit) {
            int64_t alignmentDiff = (upperLimit - actualLowerLimit) / 
                intersection->getAlignment();
            intersection->setUpperBoundState(UpperBoundState);
            intersection->setIndexUpperBound(alignmentDiff + 
                intersection->getIndexLowerBound());
        } else {
            int64_t alignmentDiff = (cmpUpperLimit - actualLowerLimit) / 
                intersection->getAlignment();
            intersection->setUpperBoundState(ric.getUpperBoundState());
            intersection->setIndexUpperBound(alignmentDiff + 
                intersection->getIndexLowerBound());
        }
    } else {
        if (upperLimit >= cmpUpperLimit) {
            int64_t alignmentDiff = (upperLimit - intersection->getOffset()) / 
                intersection->getAlignment();
            intersection->setUpperBoundState(UpperBoundState);
            intersection->setIndexUpperBound(alignmentDiff);
        } else {
            int64_t alignmentDiff = (cmpUpperLimit - intersection->getOffset()) / 
                intersection->getAlignment();
            intersection->setUpperBoundState(ric.getUpperBoundState());
            intersection->setIndexUpperBound(alignmentDiff);
        }
    }

    Alignment = intersection->getAlignment();
    IndexLowerBound = intersection->getIndexLowerBound();
    IndexUpperBound = intersection->getIndexUpperBound();
    Offset = intersection->getOffset();
    LowerBoundState = intersection->getLowerBoundState();
    UpperBoundState = intersection->getUpperBoundState();

    return true;
}

bool ReducedIntervalCongruence::widenRIC(ReducedIntervalCongruence &ric) {
    return true;
}

bool ReducedIntervalCongruence::adjustRIC(int64_t value) {
    Offset += value;
    return true;
}

bool ReducedIntervalCongruence::removeLowerBounds() {
    LowerBoundState = BoundState::NEG_INF;
    return true;
}

bool ReducedIntervalCongruence::removeUpperBounds() {
    UpperBoundState = BoundState::INF;
    return true;
}