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

namespace llvm {
namespace mctoll {

class X86ValueSetAnalysis {
public:
  X86ValueSetAnalysis() = delete;
  X86ValueSetAnalysis(X86MachineInstructionRaiser *);

private:
  X86MachineInstructionRaiser *X86MIRaiser;
  
};


} // end namespace mctoll
} // end namespace llvm

#endif // LLVM_TOOLS_LLVM_MCTOLL_X86_X86VALUESETANALYSIS_H
