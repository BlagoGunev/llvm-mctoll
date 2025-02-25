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

bool X86ValueSetAnalysis::assignValue(AlocType dest, AlocType src) {
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

void dumpRic(const ReducedIntervalCongruence &ric) {
  char loAddr[30], hiAddr[30];
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

  fprintf(stderr, "[%s, %s]", loAddr, hiAddr);
}

void X86ValueSetAnalysis::dump() {
  if (alocToVSMap.size() == 0) {
    fprintf(stderr, "Empty value set\n");
    return;
  }
  for (auto ME : alocToVSMap) {
    fprintf(stderr, "\t");
    if (ME.first.isRegisterType()) {
      fprintf(stderr, "%s -> ", X86MIRaiser->getModuleRaiser()->getMCRegisterInfo()->getName(ME.first.getRegister()));
    } else {
      fprintf(stderr, "%" PRIu64 "\n", ME.first.getGlobalAddress());
    }
    for (auto ric : *ME.second) {
      dumpRic(ric.second);
    }
    fprintf(stderr, "\n");
  }
}
