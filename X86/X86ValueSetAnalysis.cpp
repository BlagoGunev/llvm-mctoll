//===-- X86ValueSetAnalysis.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of X86ValueSetAnalysis
// class for use by llvm-mctoll.
//
//===----------------------------------------------------------------------===//

#include "X86ValueSetAnalysis.h"

#define DEBUG_TYPE "mctoll"

using namespace llvm;
using namespace llvm::mctoll;

X86ValueSetAnalysis::X86ValueSetAnalysis(
    X86MachineInstructionRaiser *MIRaiser)
    : X86MIRaiser(MIRaiser) {

  fprintf(stderr, "Created VSA for func: %s\n", MIRaiser->getMF().getName().data());
}

void X86ValueSetAnalysis::assignZeroRic(AlocType dest) {
  alocToVSMap[dest] = new ValueSet;
  pair<MemRgnType, ReducedIntervalCongruence> p;
  p.first = 0;
  p.second = ReducedIntervalCongruence();
  alocToVSMap[dest]->insert(p);
}

bool X86ValueSetAnalysis::tryInsertValue(AlocType dest, int64_t value) {
  if (alocToVSMap.find(dest) != alocToVSMap.end()) {
    printf("Not inserted\n");
    return false;
  }
  printf("Inserted\n");
  alocToVSMap[dest] = new ValueSet;
  pair<MemRgnType, ReducedIntervalCongruence> p;
  p.first = 0;
  p.second = ReducedIntervalCongruence(1, 0, 0, value);
  alocToVSMap[dest]->insert(p);
  return true;
}

bool X86ValueSetAnalysis::assignValueToSrc(AlocType dest, AlocType src) {
  if (alocToVSMap.find(src) == alocToVSMap.end()) {
    assignZeroRic(src);
    assignZeroRic(dest);
    return true;
  }
  alocToVSMap[dest] = alocToVSMap[src];
  return true;
}

bool X86ValueSetAnalysis::assignValueConst(AlocType dest, Value *val) {
  // if (alocToVSMap.find(dest)) {
  //   free(alocToVSMap[dest]);
  // }
  alocToVSMap[dest] = new ValueSet;
  RgnRICPair rrp;
  rrp.first = 0;
  int64_t value = 0;
  // TODO: check what value val is, for now just cast
  if (isa<ConstantInt>(val)) {
    value = (dyn_cast<ConstantInt>(val))->getSExtValue();
  }
  // int64_t value = 3;
  rrp.second = ReducedIntervalCongruence(1, 0, 0, value);
  alocToVSMap[dest]->insert(rrp);
  return true;
}

bool X86ValueSetAnalysis::addValueWithSrc(AlocType dest, AlocType src) {
  return addValueWithSrcTimes(dest, src, 1);
}

ReducedIntervalCongruence addRics(const ReducedIntervalCongruence &a, 
  const ReducedIntervalCongruence &b) {

  ReducedIntervalCongruence result;
  // result.setIndexLowerBound(a.getIndexLowerBound() + b.getIn)
  return result;
}

ValueSet *crossValueSets(ValueSet *dest, ValueSet *src) {
  ValueSet *newSet = new ValueSet;
  for (auto oldRicP : *dest) {
    for (auto srcRicP : *src) {
      RgnRICPair newPair;
      newPair.first = oldRicP.first;
      const ReducedIntervalCongruence oldRic = oldRicP.second;
      newPair.second = ReducedIntervalCongruence(oldRic.getAlignment(), 
        oldRic.getIndexLowerBound(), oldRic.getIndexUpperBound(), 
        oldRic.getOffset() + srcRicP.second.getOffset(),
        oldRic.getLowerBoundState(), oldRic.getUpperBoundState());
      newSet->insert(newPair);
    }
  }
  return newSet;
}
  
bool X86ValueSetAnalysis::addValueWithSrcTimes(AlocType dest, AlocType src, int64_t times) {
  if (alocToVSMap.find(dest) == alocToVSMap.end()) {
    assignZeroRic(dest);
  }

  if (times == 0) {
    return true;
  }

  if (alocToVSMap.find(src) == alocToVSMap.end()) {
    return false;
  }
  
  // This is a very early implementation, assumes that src is const
  if (times == 1) {
    alocToVSMap[dest] = crossValueSets(alocToVSMap[dest], alocToVSMap[src]);
    return true;
  } else {
    ValueSet *mult = new ValueSet;
    for (auto p : *alocToVSMap[src]) {
      RgnRICPair cp;
      cp.first = p.first;
      cp.second = p.second;
      cp.second.multiplyRIC(times);
      mult->insert(cp);
    }
    alocToVSMap[dest] = crossValueSets(alocToVSMap[dest], mult);
    delete mult;
    return true;
  }
  return true;
}

bool X86ValueSetAnalysis::addValueWithImm(AlocType dest, int64_t imm) {
  return addValueWithImmTimes(dest, imm, 1);
}

bool X86ValueSetAnalysis::addValueWithImmTimes(AlocType dest, int64_t imm, int64_t times) {
  
  if (alocToVSMap.find(dest) == alocToVSMap.end()) {
    assignZeroRic(dest);
  }

  if (times == 0) {
    return true;
  }

  bool Success = true;
  ValueSet *updatedVS = new ValueSet;
  for (const RgnRICPair &ric : *(alocToVSMap[dest])) {
    RgnRICPair newP = ric;
    Success &= newP.second.adjustRIC(imm * times);
    updatedVS->insert(newP);
  }
  alocToVSMap[dest] = updatedVS;

  return Success;
}

bool X86ValueSetAnalysis::xorValueWithSrc(AlocType dest, AlocType src) {
  // Currently only implement the XOR with self
  if (dest == src) {
    assignZeroRic(dest);
    return true;
  }
  return false;
}

bool X86ValueSetAnalysis::adjustVS(AlocType dest, int64_t value) {
  // if (alocToVSMap.find(dest) == alocToVSMap.end()) {
  //   assignZeroRic(dest);
  // }
  // ValueSet *updatedVS = new ValueSet;
  // for (const RgnRICPair &ric : *(alocToVSMap[dest])) {
  //   RgnRICPair newP = ric;
  //   newP.second.adjustRIC(value);
  //   updatedVS->insert(newP);
  // }
  // alocToVSMap[dest] = updatedVS;
  // return true;
  return addValueWithImmTimes(dest, value, 1);
}

void dumpRic(const ReducedIntervalCongruence &ric) {
  char loAddr[30], hiAddr[30], offStr[30];
  switch (ric.getLowerBoundState()) {
    case BoundState::NEG_INF: 
      sprintf(loAddr, "-inf");
      break;
    case BoundState::UNSURE:
      sprintf(loAddr, "T");
      break;
    case BoundState::SET:
      sprintf(loAddr, "%ld", ric.getIndexLowerBound());
      break;
    default:
      assert(false && "Bound state incorrect");
  }
  sprintf(hiAddr, "%ld", ric.getIndexUpperBound());
  int64_t offset = ric.getOffset();
  if (offset < 0) {
    sprintf(offStr, " - %ld", -offset);
  } else {
    sprintf(offStr, " + %ld", offset);
  }

  fprintf(stderr, "%lu*[%s, %s]%s", ric.getAlignment(), loAddr, hiAddr, offStr);
}

void X86ValueSetAnalysis::dump() {
  if (alocToVSMap.size() == 0) {
    fprintf(stderr, "Empty value set\n");
    return;
  }
  fprintf(stderr, "Dumping VSA: \n");
  for (auto ME : alocToVSMap) {
    fprintf(stderr, "\t");
    if (ME.first.isRegisterType()) {
      fprintf(stderr, "%s -> ", X86MIRaiser->getModuleRaiser()->getMCRegisterInfo()->getName(ME.first.getRegister()));
    } else if (ME.first.isLocalMemLocType()){
      fprintf(stderr, "mem %lu -> ", ME.first.getLocalAddress());
    } else {
      fprintf(stderr, "global %lu -> ", ME.first.getGlobalAddress());
    }
    for (auto ric : *ME.second) {
      dumpRic(ric.second);
    }
    fprintf(stderr, "\n");
  }
}
