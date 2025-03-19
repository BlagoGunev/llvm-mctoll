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

bool X86ValueSetAnalysis::assignValueToSrc(AlocType dest, AlocType src) {
  if (alocToVSMap.find(src) == alocToVSMap.end()) {
    alocToVSMap[dest] = new ValueSet;
    pair<MemRgnType, ReducedIntervalCongruence> p;
    p.first = 0;
    p.second = ReducedIntervalCongruence();
    alocToVSMap[dest]->insert(p);
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
    // val->getType()->print(outs());
    value = (dyn_cast<ConstantInt>(val))->getSExtValue();
    // printf("Found an integer type %ld\n", value);
    // fflush(stdout);
  }
  // int64_t value = 3;
  rrp.second = ReducedIntervalCongruence(1, 0, 0, value);
  alocToVSMap[dest]->insert(rrp);
  return true;
}

bool X86ValueSetAnalysis::adjustVS(AlocType dest, int64_t value) {
  if (alocToVSMap.find(dest) == alocToVSMap.end()) {
    alocToVSMap[dest] = new ValueSet;
  }
  ValueSet *updatedVS = new ValueSet;
  for (const RgnRICPair &ric : *(alocToVSMap[dest])) {
    RgnRICPair newP = ric;
    newP.second.adjustRIC(value);
    updatedVS->insert(newP);
  }
  alocToVSMap[dest] = updatedVS;
  return true;
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
