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

void X86ValueSetAnalysis::dump() {
  if (alocToVSMap.size() == 0) {
    fprintf(stderr, "Empty value set\n");
    return;
  }
  for (auto ME : alocToVSMap) {
    fprintf(stderr, "Found an entry: \n");
  }
}
