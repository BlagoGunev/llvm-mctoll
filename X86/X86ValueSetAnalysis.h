//===-- X86ValueSetAnalysis.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of X86ValueSetAnalysis
// class for use by llvm-mctoll.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCTOLL_X86_X86VALUESETANALYSIS_H
#define LLVM_TOOLS_LLVM_MCTOLL_X86_X86VALUESETANALYSIS_H

#include "X86MachineInstructionRaiser.h"
#include "ReducedIntervalCongruence.h"
#include "AlocType.h"
#include "llvm/MC/MCRegisterInfo.h"

namespace llvm {
namespace mctoll {

using MemRgnType = uint32_t;

using RgnRICPair = std::pair<MemRgnType, ReducedIntervalCongruence>;
using ValueSet = std::unordered_set<RgnRICPair>;

// map key : a-loc
// 
// value - (map(value set, RgnRICPair))
using AlocToVSMap = std::unordered_map<AlocType, ValueSet *>;

using FPSetsPair = std::pair<std::unordered_set<AlocType>, std::unordered_set<AlocType>>;

class X86ValueSetAnalysis {
public:
  X86ValueSetAnalysis() = delete;
  X86ValueSetAnalysis(X86MachineInstructionRaiser *);

  bool assignValueToSrc(AlocType dest, AlocType src);
  bool assignValueConst(AlocType dest, Value *val);

  bool containsValue(AlocType aloc, int64_t value);
  bool isSubsetOf(ValueSet *left, ValueSet *right);
  ValueSet *intersectionVS(ValueSet *left, ValueSet *right);
  ValueSet *unionVS(ValueSet *left, ValueSet *right);
  ValueSet *widenVS(ValueSet *left, ValueSet *right);
  bool adjustVS(AlocType dest, int64_t value);
  FPSetsPair fpSets(ValueSet *vs, int64_t size);
  ValueSet *removeLowerBounds(ValueSet *vs);
  ValueSet *removeUpperBounds(ValueSet *vs);

  void dump();
private:
  X86MachineInstructionRaiser *X86MIRaiser;

  AlocToVSMap alocToVSMap;
  
};


} // end namespace mctoll
} // end namespace llvm

namespace std {
  template<>
  struct hash<llvm::mctoll::RgnRICPair>
  {
    size_t operator()(const llvm::mctoll::RgnRICPair& rrp) const noexcept
    {
      llvm::mctoll::ReducedIntervalCongruence ric = rrp.second;
      size_t h1 = ric.getAlignment() << 3;
      size_t h2 = ric.getIndexLowerBound() << 2;
      size_t h3 = ric.getIndexUpperBound() << 1;
      size_t h4 = ric.getOffset();
      size_t h5 = (size_t)ric.getLowerBoundState() << 4;
      size_t h6 = (size_t)ric.getUpperBoundState() << 5;
      return h1 ^ h2 ^ h3 ^ h4 ^ h5 ^ h6 ^ (rrp.first << 7);
    }
  };
  } // end namespace std

#endif // LLVM_TOOLS_LLVM_MCTOLL_X86_X86VALUESETANALYSIS_H
