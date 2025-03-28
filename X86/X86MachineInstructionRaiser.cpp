//===-- X86MachineInstructionRaiser.cpp -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of X86MachineInstructionRaiser class
// for use by llvm-mctoll.
//
//===----------------------------------------------------------------------===//

#include "X86MachineInstructionRaiser.h"
#include "IncludedFileInfo.h"
#include "Raiser/MachineFunctionRaiser.h"
#include "X86InstrBuilder.h"
#include "X86ModuleRaiser.h"
#include "X86RaisedValueTracker.h"
#include "X86RegisterUtils.h"
#include "X86ValueSetAnalysis.h"
#include "llvm-mctoll.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LoopTraversal.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include <X86InstrBuilder.h>
#include <X86Subtarget.h>
#include <set>
#include <vector>

#define DEBUG_TYPE "mctoll"

using namespace llvm;
using namespace llvm::mctoll;
using namespace llvm::mctoll::X86RegisterUtils;

// Constructor

X86MachineInstructionRaiser::X86MachineInstructionRaiser(MachineFunction &MF,
                                                         const ModuleRaiser *MR,
                                                         MCInstRaiser *MIR)
    : MachineInstructionRaiser(MF, MR, MIR), machineRegInfo(MF.getRegInfo()),
      x86TargetInfo(MF.getSubtarget<X86Subtarget>()) {
  x86InstrInfo = x86TargetInfo.getInstrInfo();
  x86RegisterInfo = x86TargetInfo.getRegisterInfo();

  FPUStack.TOP = 0;
  for (int Idx = 0; Idx < FPUSTACK_SZ; Idx++)
    FPUStack.Regs[Idx] = nullptr;

  raisedValues = nullptr;
  valueSetAnalysis = nullptr;
}

bool X86MachineInstructionRaiser::raisePushInstruction(const MachineInstr &MI) {
  const MCInstrDesc &MCIDesc = MI.getDesc();
  uint64_t MCIDTSFlags = MCIDesc.TSFlags;

  assert(((MCIDTSFlags & X86II::FormMask) == X86II::AddRegFrm) &&
         "Unhandled PUSH instruction with non-AddrRegFrm source operand");
  assert((MI.getOperand(0).isReg()) &&
         "Unhandled PUSH instruction with a non-register operand");

  assert(MCIDesc.getNumImplicitUses() == 1 &&
         MCIDesc.getNumImplicitDefs() == 1 &&
         "Unexpected number of implicit uses and defs in push instruction");
  assert((find64BitSuperReg(MCIDesc.ImplicitUses[0]) == X86::RSP) &&
         (find64BitSuperReg(MCIDesc.ImplicitDefs[0]) == X86::RSP) &&
         (MI.getNumExplicitOperands() == 1) &&
         "Unexpected implicit or explicit registers in push instruction");

  X86AddressMode MemRef;
  MemRef.Base.Reg = MCIDesc.ImplicitUses[0]; // SP
  MemRef.BaseType = X86AddressMode::RegBase;
  // Displacement of the store location of MIDesc.ImplicitUses[0] 0 off SP
  // i.e., on top of stack.
  MemRef.Disp = 0;
  MemRef.IndexReg = X86::NoRegister;
  MemRef.Scale = 1;
  Value *StackRef = getStackAllocatedValue(MI, MemRef, false);
  assert(StackRef != nullptr && "Failed to allocate stack slot for push");
  assert(StackRef->getType()->isPointerTy() &&
         "Unexpected non-pointer stack slot type while raising push");
  raisedValues->setPhysRegSSAValue(X86::RSP, MI.getParent()->getNumber(),
                                   StackRef);
  // Get operand value
  Value *RegValue = getRegOperandValue(MI, 0);
  // If the value of push operand register is known, then it does not
  // have a manifestation in the function code. So, a known garbage value is
  // used to affect the push operation.
  Type *StackRefElemTy = getPointerElementType(StackRef);
  if (RegValue == nullptr) {
    RegValue =
        ConstantInt::get(StackRefElemTy, 0xdeadbeef, false /* isSigned */);
  }
  // Ensure types of RegValue matches that of StackRef
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());
  RegValue = getRaisedValues()->castValue(RegValue, StackRefElemTy, RaisedBB);

  // Store operand at stack top (StackRef)
  new StoreInst(RegValue, StackRef, RaisedBB);

  // add to vsa
  unsigned addrSpace = MR->getModule()->getDataLayout().getAllocaAddrSpace();
  valueSetAnalysis->assignValueToSrc(AlocType(AlocType::AlocTypeID::LocalMemLocTy, addrSpace), AlocType(MI.getOperand(0).getReg()));


  return true;
}

bool X86MachineInstructionRaiser::raisePopInstruction(const MachineInstr &MI) {
  // TODO : Need to handle pop instructions other than those that restore bp
  // from stack.
  const MCInstrDesc &MCIDesc = MI.getDesc();
  uint64_t MCIDTSFlags = MCIDesc.TSFlags;

  if ((MCIDTSFlags & X86II::FormMask) == X86II::AddRegFrm) {
#if 0
    // This is a register POP. If the source is base pointer,
    // not need to raise the instruction.
    if (MI.definesRegister(X86::RBP) || MI.definesRegister(X86::EBP)) {
      return true;
    } else {
      // assert(false && "Unhandled POP instruction that restores a register
      // "
      //                "other than frame pointer");
      return true;
    }
#else
    // add to vsa

    return true;
#endif
  } else {
    if (getInstructionKind(MI.getOpcode()) == InstructionKind::LEAVE_OP) {
      return true;
    }
    assert(false && "Unhandled POP instruction with source operand other "
                    "than AddrRegFrm");
  }
  return false;
}

bool X86MachineInstructionRaiser::raiseConvertBWWDDQMachineInstr(
    const MachineInstr &MI) {
  const MCInstrDesc &MIDesc = MI.getDesc();
  unsigned int Opcode = MI.getOpcode();
  LLVMContext &Ctx(MF.getFunction().getContext());

  assert(MIDesc.getNumImplicitUses() == 1 && MIDesc.getNumImplicitDefs() == 1 &&
         "Unexpected number of implicit uses and defs in cbw/cwde/cdqe "
         "instruction");
  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  MCPhysReg UseReg = MIDesc.ImplicitUses[0];
  MCPhysReg DefReg = MIDesc.ImplicitDefs[0];
  Type *TargetTy = nullptr;

  if (Opcode == X86::CDQE) {
    assert(is32BitPhysReg(UseReg) &&
           "Unexpected non-32-bit register in cdqe instruction");
    assert(is64BitPhysReg(DefReg) &&
           "Unexpected non-64-bit register in cdqe instruction");
    TargetTy = Type::getInt64Ty(Ctx);
  } else if (Opcode == X86::CBW) {
    assert(is8BitPhysReg(UseReg) &&
           "Unexpected non-32-bit register in cbw instruction");
    assert(is16BitPhysReg(DefReg) &&
           "Unexpected non-64-bit register in cbw instruction");
    TargetTy = Type::getInt16Ty(Ctx);
  } else if (Opcode == X86::CWDE) {
    assert(is16BitPhysReg(UseReg) &&
           "Unexpected non-32-bit register in cwde instruction");
    assert(is32BitPhysReg(DefReg) &&
           "Unexpected non-64-bit register in cwde instruction");
    TargetTy = Type::getInt32Ty(Ctx);
  }
  assert(TargetTy != nullptr &&
         "Target type not set for cbw/cwde/cdqe instruction");
  Value *UseValue = getRegOperandValue(
      MI, MI.findRegisterUseOperandIdx(UseReg, false, nullptr));

  // Generate sign-extend instruction
  SExtInst *SextInst = new SExtInst(UseValue, TargetTy);
  RaisedBB->getInstList().push_back(SextInst);

  // Update the value mapping of DefReg
  raisedValues->setPhysRegSSAValue(DefReg, MI.getParent()->getNumber(),
                                   SextInst);
  return true;
}

bool X86MachineInstructionRaiser::raiseConvertWDDQQOMachineInstr(
    const MachineInstr &MI) {
  const MCInstrDesc &MIDesc = MI.getDesc();
  unsigned int Opcode = MI.getOpcode();
  LLVMContext &Ctx(MF.getFunction().getContext());

  assert(MIDesc.getNumImplicitUses() == 1 && MIDesc.getNumImplicitDefs() == 2 &&
         "Unexpected number of implicit uses and defs in cwd/cdq/cqo "
         "instruction");
  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  MCPhysReg UseReg = MIDesc.ImplicitUses[0];
  MCPhysReg DefReg0 = MIDesc.ImplicitDefs[0];
  MCPhysReg DefReg1 = MIDesc.ImplicitDefs[1];
  Type *TargetTy = nullptr;
  Type *UseRegTy = nullptr;

  if (Opcode == X86::CWD) {
    assert(is16BitPhysReg(UseReg) && is16BitPhysReg(DefReg0) &&
           is16BitPhysReg(DefReg1) && (UseReg == DefReg0) &&
           "Unexpected characteristics of use/def registers in cwd "
           "instruction");
    TargetTy = Type::getInt32Ty(Ctx);
    UseRegTy = Type::getInt16Ty(Ctx);
  } else if (Opcode == X86::CDQ) {
    assert(is32BitPhysReg(UseReg) && is32BitPhysReg(DefReg0) &&
           is32BitPhysReg(DefReg1) && (UseReg == DefReg0) &&
           "Unexpected characteristics of use/def registers in cdq "
           "instruction");
    TargetTy = Type::getInt64Ty(Ctx);
    UseRegTy = Type::getInt32Ty(Ctx);
  } else if (Opcode == X86::CQO) {
    assert(is64BitPhysReg(UseReg) && is64BitPhysReg(DefReg0) &&
           is64BitPhysReg(DefReg1) && (UseReg == DefReg0) &&
           "Unexpected characteristics of use/def registers in cqo "
           "instruction");
    TargetTy = Type::getInt128Ty(Ctx);
    UseRegTy = Type::getInt64Ty(Ctx);
  }

  assert((TargetTy != nullptr) && (UseRegTy != nullptr) &&
         "Target type not set for cwd/cdq/cqo instruction");
  // Value *UseValue = getRegOrArgValue(UseReg, MI.getParent()->getNumber());
  Value *UseValue = getPhysRegValue(MI, UseReg);

  // Check if UseReg is a use-before-define register
  if (UseValue == nullptr)
    return false;

  // Generate sign-extend instruction
  SExtInst *TargetSextInst = new SExtInst(UseValue, TargetTy);
  assert(UseValue->getType()->getScalarSizeInBits() ==
             UseRegTy->getScalarSizeInBits() &&
         "Mismatched types in cwd/cdq/cqo instruction");
  RaisedBB->getInstList().push_back(TargetSextInst);

  // Logical Shift TargetSextInst by n-bits (where n is the size of
  // UserRegTy) to get the high bytes and set DefReg_1 to the resulting
  // value.
  Value *ShiftAmount = ConstantInt::get(
      TargetTy, UseRegTy->getScalarSizeInBits(), false /* isSigned */);
  Instruction *LShrInst =
      BinaryOperator::CreateLShr(TargetSextInst, ShiftAmount);
  RaisedBB->getInstList().push_back(LShrInst);
  // Truncate LShrInst to get the high bytes
  Instruction *HighBytesInst =
      CastInst::Create(Instruction::Trunc, LShrInst, UseRegTy);
  RaisedBB->getInstList().push_back(HighBytesInst);
  // Update the value mapping of DefReg_1
  raisedValues->setPhysRegSSAValue(DefReg1, MI.getParent()->getNumber(),
                                   HighBytesInst);

  return true;
}

bool X86MachineInstructionRaiser::raiseMoveImmToRegMachineInstr(
    const MachineInstr &MI) {
  unsigned int Opcode = MI.getOpcode();
  bool Success = false;

  switch (Opcode) {
  case X86::MOV8ri:
  case X86::MOV16ri:
  case X86::MOV32ri:
  case X86::MOV64ri:
  case X86::MOV64ri32: {
    unsigned DestOpIndex = 0, SrcOpIndex = 1;
    const MachineOperand &DestOp = MI.getOperand(DestOpIndex);
    const MachineOperand &SrcOp = MI.getOperand(SrcOpIndex);
    assert(MI.getNumExplicitOperands() == 2 && DestOp.isReg() &&
           SrcOp.isImm() &&
           "Expecting exactly two operands for move imm-to-reg instructions");

    Register DstPReg = DestOp.getReg();
    int64_t SrcImm = SrcOp.getImm();
    Type *ImmTy = getImmOperandType(MI, 1);
    Value *SrcValue = ConstantInt::get(ImmTy, SrcImm);

    bool SignExtend = (Opcode == X86::MOV64ri32);

    SrcValue = getRaisedValues()->castValue(SrcValue, getPhysRegType(DstPReg),
                                            getRaisedBasicBlock(MI.getParent()),
                                            SignExtend);

    if (SrcImm > 0) {
      // Check if the immediate value corresponds to a global variable.
      Value *GV = getGlobalVariableValueAt(MI, SrcImm);
      if (GV != nullptr) {
        SrcValue = GV;
      }
    }
    assert(SrcValue != nullptr &&
           "Failed to get source value in the mov immediate instruction");
    // Update the value mapping of DstReg
    raisedValues->setPhysRegSSAValue(DstPReg, MI.getParent()->getNumber(),
                                     SrcValue);
    Success = true;
  } break;
  default:
    assert(false && "Unhandled move imm-to-reg instruction");
    break;
  }
  return Success;
}

bool X86MachineInstructionRaiser::raiseMoveRegToRegMachineInstr(
    const MachineInstr &MI) {
  unsigned int Opcode = MI.getOpcode();
  int MBBNo = MI.getParent()->getNumber();
  LLVMContext &Ctx(MF.getFunction().getContext());

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  bool Success = false;
  unsigned DstIndex = 0, Src1Index = 1, Src2Index = 2;
  assert(
      (MI.getNumExplicitOperands() == 2 || MI.getNumExplicitOperands() == 4) &&
      MI.getOperand(DstIndex).isReg() &&
      (MI.getOperand(Src1Index).isReg() || MI.getOperand(Src2Index).isReg()) &&
      "Expecting exactly two or four operands for move reg-to-reg "
      "instructions");

  Register DstPReg = MI.getOperand(DstIndex).getReg();

  // Get source operand value
  Value *SrcValue = nullptr;
  if (MI.getNumExplicitOperands() == 2)
    SrcValue = getRegOperandValue(MI, Src1Index);
  else if (MI.getNumExplicitOperands() == 4)
    SrcValue = getRegOperandValue(MI, Src2Index);
  else
    assert(false &&
           "Unexpected operand numbers for move reg-to-reg instruction");

  switch (Opcode) {
  case X86::MOVSX16rr8:
  case X86::MOVSX32rr8:
  case X86::MOVSX32rr16:
  case X86::MOVSX64rr8:
  case X86::MOVSX64rr16:
  case X86::MOVSX64rr32:
  case X86::MOVZX16rr8:
  case X86::MOVZX32rr8:
  case X86::MOVZX32rr16:
  case X86::MOVZX64rr8:
  case X86::MOVZX64rr16: {
    Type *Ty = nullptr;
    Instruction::CastOps Cast;
    // Check for sanity of source value
    assert(SrcValue &&
           "Encountered instruction with undefined source register");

    switch (Opcode) {
    case X86::MOVSX16rr8: {
      assert(is16BitPhysReg(DstPReg) &&
             "Not found expected 16-bit destination register - movsx "
             "instruction");
      Ty = Type::getInt16Ty(Ctx);
      Cast = Instruction::SExt;
    } break;
    case X86::MOVSX32rr8:
    case X86::MOVSX32rr16: {
      assert(is32BitPhysReg(DstPReg) &&
             "Not found expected 32-bit destination register - movsx "
             "instruction");
      Ty = Type::getInt32Ty(Ctx);
      Cast = Instruction::SExt;
    } break;
    case X86::MOVSX64rr8:
    case X86::MOVSX64rr16:
    case X86::MOVSX64rr32: {
      assert(is64BitPhysReg(DstPReg) &&
             "Not found expected 64-bit destination register - movsx "
             "instruction");
      Ty = Type::getInt64Ty(Ctx);
      Cast = Instruction::SExt;
    } break;
    case X86::MOVZX16rr8: {
      assert(is16BitPhysReg(DstPReg) &&
             "Not found expected 16-bit destination register - movsx "
             "instruction");
      Ty = Type::getInt16Ty(Ctx);
      Cast = Instruction::ZExt;
    } break;
    case X86::MOVZX32rr8:
    case X86::MOVZX32rr16: {
      assert(is32BitPhysReg(DstPReg) &&
             "Not found expected 32-bit destination register - movzx "
             "instruction");
      Ty = Type::getInt32Ty(Ctx);
      Cast = Instruction::ZExt;
    } break;
    case X86::MOVZX64rr8:
    case X86::MOVZX64rr16: {
      assert(is64BitPhysReg(DstPReg) &&
             "Not found expected 64-bit destination register - movzx "
             "instruction");
      Ty = Type::getInt64Ty(Ctx);
      Cast = Instruction::ZExt;
    } break;
    default:
      assert(false &&
             "Should not reach here! - mov with extension instruction");
    }
    assert(Ty != nullptr &&
           "Failed to set type - mov with extension instruction");
    // Now create the cast instruction corresponding to the instruction.
    CastInst *CInst = CastInst::Create(Cast, SrcValue, Ty);
    RaisedBB->getInstList().push_back(CInst);

    // Update the value mapping of DstPReg
    raisedValues->setPhysRegSSAValue(DstPReg, MBBNo, CInst);
    Success = true;
  } break;
  case X86::MOV64rr:
  case X86::MOV32rr:
  case X86::MOV16rr:
  case X86::MOV8rr: {
    unsigned int DstPRegSize = getPhysRegOperandSize(MI, DstIndex);
    unsigned int SrcPRegSize = getPhysRegOperandSize(MI, Src1Index);

    // Verify sanity of the instruction.
    assert(DstPRegSize != 0 && DstPRegSize == SrcPRegSize &&
           "Unexpected sizes of source and destination registers size differ "
           "in mov instruction");
    assert(SrcValue &&
           "Encountered mov instruction with undefined source register");
    assert(SrcValue->getType()->isSized() &&
           "Unsized source value in move instruction");
    MachineOperand MO = MI.getOperand(Src1Index);
    assert(MO.isReg() && "Unexpected non-register operand");
    // Check for undefined use
    Success = (SrcValue != nullptr);
    if (Success)
      // Update the value mapping of DstPReg
      raisedValues->setPhysRegSSAValue(DstPReg, MBBNo, SrcValue);
    
    // here
    Register SrcPReg = MI.getOperand(Src1Index).getReg();
    valueSetAnalysis->assignValueToSrc(AlocType(find64BitSuperReg(DstPReg)),
         AlocType(find64BitSuperReg(SrcPReg)));

  } break;
  case X86::CMOV16rr:
  case X86::CMOV32rr:
  case X86::CMOV64rr: {
    unsigned int DstPRegSize = getPhysRegOperandSize(MI, DstIndex);
    unsigned int SrcPRegSize = getPhysRegOperandSize(MI, Src2Index);

    // Verify sanity of the instruction.
    assert(DstPRegSize != 0 && DstPRegSize == SrcPRegSize &&
           "Unexpected sizes of source and destination registers size differ "
           "in cmovcc instruction");
    assert(SrcValue &&
           "Encountered cmovcc instruction with undefined source register");
    assert(SrcValue->getType()->isSized() &&
           "Unsized source value in cmovcc instruction");
    SrcValue = getRegOperandValue(MI, Src2Index);
    // Check for undefined use
    Success = (SrcValue != nullptr);
    if (Success) {
      // Get destination operand value
      Value *DstValue = getRegOrArgValue(DstPReg, MBBNo);
      Value *TrueValue = ConstantInt::getTrue(Ctx);
      Value *FalseValue = ConstantInt::getFalse(Ctx);
      CmpInst::Predicate Pred = CmpInst::Predicate::BAD_ICMP_PREDICATE;
      Value *CMOVCond = nullptr;

      switch (X86::getCondFromCMov(MI)) {
      case X86::COND_NE: {
        // Check if ZF == 0
        Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
        assert(ZFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVNE!");
        Pred = CmpInst::Predicate::ICMP_EQ;
        // Construct a compare instruction
        CMOVCond = new ICmpInst(Pred, ZFValue, FalseValue, "Cond_CMOVNE");
      } break;
      case X86::COND_E: {
        // Check if ZF == 1
        Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
        assert(ZFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVE!");
        Pred = CmpInst::Predicate::ICMP_EQ;
        // Construct a compare instruction
        CMOVCond = new ICmpInst(Pred, ZFValue, TrueValue, "Cond_CMOVE");
      } break;
      case X86::COND_A: {
        // Check CF == 0 and ZF == 0
        Value *CFValue = getRegOrArgValue(EFLAGS::CF, MBBNo);
        Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
        assert((CFValue != nullptr) && (ZFValue != nullptr) &&
               "Failed to get EFLAGS value while raising CMOVA!");
        Pred = CmpInst::Predicate::ICMP_EQ;
        // CF or ZF
        BinaryOperator *CFZFOrCond =
            BinaryOperator::CreateOr(CFValue, ZFValue, "CFZFOR_CMOVA");
        RaisedBB->getInstList().push_back(CFZFOrCond);
        // Test CF == 0 and ZF == 0
        CMOVCond = new ICmpInst(Pred, CFZFOrCond, FalseValue, "Cond_CMOVA");
      } break;
      case X86::COND_L: {
        // Check SF != OF
        Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
        Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
        assert((SFValue != nullptr) && (OFValue != nullptr) &&
               "Failed to get EFLAGS value while raising CMOVL!");
        Pred = CmpInst::Predicate::ICMP_NE;
        // Test SF != OF
        CMOVCond = new ICmpInst(Pred, SFValue, OFValue, "Cond_CMOVL");
      } break;
      case X86::COND_G: {
        // Check ZF == 0 and SF == OF
        Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
        Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
        Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
        assert((ZFValue != nullptr) && (SFValue != nullptr) &&
               (OFValue != nullptr) &&
               "Failed to get EFLAGS value while raising CMOVG!");
        Pred = CmpInst::Predicate::ICMP_EQ;
        // Compare ZF and 0
        CmpInst *ZFCond =
            new ICmpInst(Pred, ZFValue, FalseValue, "ZFCmp_CMOVG");
        RaisedBB->getInstList().push_back(ZFCond);
        // Test SF == OF
        CmpInst *SFOFCond =
            new ICmpInst(Pred, SFValue, OFValue, "SFOFCmp_CMOVG");
        RaisedBB->getInstList().push_back(SFOFCond);
        CMOVCond = BinaryOperator::CreateAnd(ZFCond, SFOFCond, "Cond_CMOVG");
      } break;
      case X86::COND_LE: {
        // Check ZF == 1 or SF != OF
        Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
        Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
        Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
        assert((ZFValue != nullptr) && (SFValue != nullptr) &&
               (OFValue != nullptr) &&
               "Failed to get EFLAGS value while raising CMOVLE!");

        // Check ZF == 1
        CmpInst *ZFCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, ZFValue,
                                       TrueValue, "ZFCmp_CMOVLE");
        RaisedBB->getInstList().push_back(ZFCond);

        // Test SF != OF
        CmpInst *SFOFCond = new ICmpInst(CmpInst::Predicate::ICMP_NE, SFValue,
                                         OFValue, "SFOFCmp_CMOVLE");
        RaisedBB->getInstList().push_back(SFOFCond);

        CMOVCond = BinaryOperator::CreateOr(ZFCond, SFOFCond, "Cond_CMOVLE");
      } break;
      case X86::COND_NS: {
        // Test SF == 0
        Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
        assert(SFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVNS");
        // Construct a compare instruction
        CMOVCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, SFValue,
                                FalseValue, "Cond_CMOVNS");
      } break;
      case X86::COND_AE: {
        // Test CF == 0
        Value *CFValue = getRegOrArgValue(EFLAGS::CF, MBBNo);
        assert(CFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVAE");
        // Construct a compare instruction
        CMOVCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, CFValue,
                                FalseValue, "Cond_CMOVAE");
      } break;
      case X86::COND_B: {
        // Check if CF == 1
        Value *CFValue = getRegOrArgValue(EFLAGS::CF, MBBNo);
        assert(CFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVB!");
        Pred = CmpInst::Predicate::ICMP_EQ;
        // Construct a compare instruction
        CMOVCond = new ICmpInst(Pred, CFValue, TrueValue, "Cond_CMOVB");
      } break;
      case X86::COND_NO: {
        // Test OF == 0
        Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
        assert(OFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVNO");
        // Construct a compare instruction
        CMOVCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, OFValue,
                                FalseValue, "Cond_CMOVNO");
      } break;
      case X86::COND_O: {
        // Check if OF == 1
        Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
        assert(OFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVO!");
        Pred = CmpInst::Predicate::ICMP_EQ;
        // Construct a compare instruction
        CMOVCond = new ICmpInst(Pred, OFValue, TrueValue, "Cond_CMOVO");
      } break;
      case X86::COND_S: {
        // Check if SF == 1
        Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
        assert(SFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVS!");
        Pred = CmpInst::Predicate::ICMP_EQ;
        // Construct a compare instruction
        CMOVCond = new ICmpInst(Pred, SFValue, TrueValue, "Cond_CMOVS");
      } break;
      case X86::COND_GE: {
        // Check SF == OF
        Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
        Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
        assert((SFValue != nullptr) && (OFValue != nullptr) &&
               "Failed to get EFLAGS value while raising CMOVGE!");
        // Test SF == OF
        CMOVCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, SFValue, OFValue,
                                "Cond_CMOVGE");
      } break;
      case X86::COND_BE: {
        // Check CF == 1 OR ZF == 1
        Value *CFValue = getRegOrArgValue(EFLAGS::CF, MBBNo);
        Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
        assert(CFValue != nullptr && ZFValue != nullptr &&
               "Failed to get EFLAGS value while raising CMOVBE");
        auto *CFCmp = new ICmpInst(CmpInst::Predicate::ICMP_EQ, CFValue,
                                   TrueValue, "Cond_CMOVBE_CF");
        auto *ZFCmp = new ICmpInst(CmpInst::Predicate::ICMP_EQ, ZFValue,
                                   TrueValue, "Cond_CMOVBE_ZF");
        RaisedBB->getInstList().push_back(CFCmp);
        RaisedBB->getInstList().push_back(ZFCmp);
        CMOVCond = BinaryOperator::CreateOr(CFCmp, ZFCmp, "Cond_CMOVBE");
      } break;
      case X86::COND_INVALID:
        assert(false && "CMOV instruction with invalid condition found");
        break;
      default:
        assert(false && "CMOV instruction with unhandled condition found");
        break;
      }
      RaisedBB->getInstList().push_back(dyn_cast<Instruction>(CMOVCond));

      // Ensure that the types of SrcValue and DstValue match.
      DstValue =
          getRaisedValues()->castValue(DstValue, SrcValue->getType(), RaisedBB);

      // Generate SelectInst for CMOV instruction
      SelectInst *SI =
          SelectInst::Create(CMOVCond, SrcValue, DstValue, "CMOV", RaisedBB);

      // Update the value mapping of DstPReg
      raisedValues->setPhysRegSSAValue(DstPReg, MBBNo, SI);
    }
  } break;
  default:
    assert(false && "Unhandled move reg-to-reg instruction");
    break;
  }
  return Success;
}

bool X86MachineInstructionRaiser::raiseLEAMachineInstr(const MachineInstr &MI) {
  unsigned int Opcode = MI.getOpcode();

  assert(MI.getNumExplicitOperands() == 6 &&
         "Unexpected number of arguments of lea instruction");
  // Get dest operand
  MachineOperand DestOp = MI.getOperand(0);
  assert(DestOp.isReg() &&
         "Unhandled non-register destination operand in lea instruction");
  Register DestReg = DestOp.getReg();

  int OpIndex = X86II::getMemoryOperandNo(MI.getDesc().TSFlags);
  assert(OpIndex >= 0 && "Failed to get first operand of addressing-mode "
                         "expression in lea instruction");

  MachineOperand BaseRegOp = MI.getOperand(OpIndex + X86::AddrBaseReg);
  assert(BaseRegOp.isReg() &&
         "Unhandled non-register BaseReg operand in lea instruction");
  Register BaseReg = BaseRegOp.getReg();
  Value *EffectiveAddrValue = nullptr;

  // If the basereg refers stack, get the stack allocated object value
  uint64_t BaseSupReg = find64BitSuperReg(BaseReg);
  if ((BaseSupReg == x86RegisterInfo->getStackRegister()) ||
      (BaseSupReg == x86RegisterInfo->getFramePtr())) {
    // Get index of memory reference in the instruction.
    int MemRefOpIndex = getMemoryRefOpIndex(MI);
    // Should have found the index of the memory reference operand
    assert(MemRefOpIndex != -1 && "Unable to find memory reference "
                                  "operand of a load/store instruction");
    X86AddressMode MemRef = llvm::getAddressFromInstr(&MI, MemRefOpIndex);
    EffectiveAddrValue = getStackAllocatedValue(MI, MemRef, false);
  } else {
    MachineOperand ScaleAmtOp = MI.getOperand(OpIndex + X86::AddrScaleAmt);
    assert(ScaleAmtOp.isImm() &&
           "Unhandled non-immediate ScaleAmt operand in lea instruction");

    MachineOperand IndexRegOp = MI.getOperand(OpIndex + X86::AddrIndexReg);
    assert(IndexRegOp.isReg() &&
           "Unhandled non-register IndexReg operand in lea instruction");

    Register IndexReg = IndexRegOp.getReg();

    MachineOperand SegmentRegOp = MI.getOperand(OpIndex + X86::AddrSegmentReg);
    assert(SegmentRegOp.getReg() == X86::NoRegister &&
           "Unhandled vaule of SegmentReg operand in lea instruction");

    MachineOperand Disp = MI.getOperand(OpIndex + X86::AddrDisp);
    assert(Disp.isImm() &&
           "Unhandled non-immediate Disp operand in lea instruction");

    // Check the sanity of register sizes
    if ((Opcode == X86::LEA64r) || (Opcode == X86::LEA64_32r)) {
      // lea64mem (see LEA64 and LEA64_32r description in
      // X86InstrArithmetic.td)
      assert((is64BitPhysReg(BaseReg) || BaseReg == X86::NoRegister) &&
             "Unexpected non-64 bit base register in lea instruction");
      assert(((IndexReg == X86::NoRegister) || is64BitPhysReg(IndexReg)) &&
             "Unexpected index register type in lea instruction");
      assert(IndexReg != x86RegisterInfo->getStackRegister() &&
             "Unexpected stack pointer register as indexReg operand of lea "
             "instruction");
      if (Opcode == X86::LEA64_32r) {
        assert(is32BitPhysReg(DestReg) &&
               "Unexpected non-32 bit destination register in lea "
               "instruction");
      } else {
        assert(is64BitPhysReg(DestReg) &&
               "Unexpected non-32 bit dest register in lea instruction");
      }
    } else if (Opcode == X86::LEA32r) {
      assert((is32BitPhysReg(BaseReg) || BaseReg == X86::NoRegister) &&
             "Unexpected non-32 bit base register in lea instruction");
      assert(((IndexReg == X86::NoRegister) || is32BitPhysReg(IndexReg)) &&
             "Unexpected indext register type in lea instruction");
      assert(is32BitPhysReg(DestReg) &&
             "Unexpected non-32 bit dest register in lea instruction");
    } else if (Opcode == X86::LEA16r) {
      assert((is16BitPhysReg(BaseReg) || BaseReg == X86::NoRegister) &&
             "Unexpected non-16 bit source register in lea instruction");
      assert(((IndexReg == X86::NoRegister) || is16BitPhysReg(IndexReg)) &&
             "Unexpected indext register type in lea instruction");
      assert(is16BitPhysReg(DestReg) &&
             "Unexpected non-16 bit dest register in lea instruction");
    }
    if (BaseReg == X86::RIP)
      EffectiveAddrValue = createPCRelativeAccesssValue(MI);
    else
      EffectiveAddrValue = getMemoryAddressExprValue(MI);
  }

  assert((EffectiveAddrValue != nullptr) &&
         "Failed to get effective address value");

  unsigned DestRegSize = getPhysRegSizeInBits(DestReg);
  Type *DstTy = Type::getIntNTy(MF.getFunction().getContext(), DestRegSize);
  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());
  // Cast the result as needed
  EffectiveAddrValue =
      getRaisedValues()->castValue(EffectiveAddrValue, DstTy, RaisedBB);

  // Update the value mapping of DestReg
  raisedValues->setPhysRegSSAValue(DestReg, MI.getParent()->getNumber(),
                                   EffectiveAddrValue);
  return true;
}

bool X86MachineInstructionRaiser::raiseBinaryOpRegToRegMachineInstr(
    const MachineInstr &MI) {

  auto MCID = MI.getDesc();
  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  // Convenience variables for instructions with a dest and one or two
  // operands
  const unsigned DestOpIndex = 0, UseOp1Index = 1, UseOp2Index = 2;
  std::vector<Value *> ExplicitSrcValues;
  int MBBNo = MI.getParent()->getNumber();
  bool Success = true;
  unsigned Opc = MI.getOpcode();

  // Check if this instruction is a xor reg1, reg1 instruction, and does not
  // need to look up the value of the operand values
  bool IsXorSetZeroInstruction =
      (Opc == X86::XOR64rr || Opc == X86::XOR32rr || Opc == X86::XOR16rr ||
       Opc == X86::XOR8rr || Opc == X86::XORPSrr || Opc == X86::XORPDrr ||
       Opc == X86::PXORrr) &&
      (MI.findTiedOperandIdx(1) == 0 &&
       MI.getOperand(DestOpIndex).getReg() ==
           MI.getOperand(UseOp2Index).getReg());

  // If we are raising a xor instruction that's just zeroing-out a register,
  // we don't need to look up the register operand values
  if (!IsXorSetZeroInstruction) {
    for (const MachineOperand &MO : MI.explicit_uses()) {
      assert(MO.isReg() &&
             "Unexpected non-register operand in binary op instruction");
      auto UseOpIndex =
          MI.findRegisterUseOperandIdx(MO.getReg(), false, nullptr);
      Value *SrcValue = getRegOperandValue(MI, UseOpIndex);

      ExplicitSrcValues.push_back(SrcValue);
    }

    // Verify the instruction has 1 or 2 use operands
    assert(
        (ExplicitSrcValues.size() == 1 || ((ExplicitSrcValues.size() == 2))) &&
        "Unexpected number of operands in register binary op instruction");

    // If the instruction has two use operands, ensure that their values are
    // of the same type and non-pointer type.
    if (ExplicitSrcValues.size() == 2) {
      Value *Src1Value = ExplicitSrcValues.at(0);
      Value *Src2Value = ExplicitSrcValues.at(1);
      // The user operand values can be null if the instruction is 'xor op
      // op'. See below.
      if ((Src1Value != nullptr) && (Src2Value != nullptr)) {
        // If this is a pointer type, convert it to int type
        while (Src1Value->getType()->isPointerTy()) {
          PtrToIntInst *ConvPtrToInst =
              new PtrToIntInst(Src1Value, getPointerElementType(Src1Value));
          RaisedBB->getInstList().push_back(ConvPtrToInst);
          Src1Value = ConvPtrToInst;
        }

        // If this is a pointer type, convert it to int type
        while (Src2Value->getType()->isPointerTy()) {
          PtrToIntInst *ConvPtrToInst =
              new PtrToIntInst(Src2Value, getPointerElementType(Src2Value));
          RaisedBB->getInstList().push_back(ConvPtrToInst);
          Src2Value = ConvPtrToInst;
        }
        assert(((Src1Value->getType()->isIntegerTy() &&
                 Src2Value->getType()->isIntegerTy()) ||
                (Src1Value->getType()->isFloatingPointTy() &&
                 Src2Value->getType()->isFloatingPointTy()) ||
                (Src1Value->getType()->isVectorTy() &&
                 Src2Value->getType()->isVectorTy())) &&
               "Unhandled operand value types in reg-to-reg binary op "
               "instruction");
        if (Src1Value->getType() != Src2Value->getType()) {
          // Cast the second operand to the type of second.
          // NOTE : The choice of target cast type is rather arbitrary. May
          // need a closer look.
          Type *DestValueTy = Src1Value->getType();
          Instruction *CInst = CastInst::Create(
              CastInst::getCastOpcode(Src2Value, false, DestValueTy, false),
              Src2Value, DestValueTy);
          RaisedBB->getInstList().push_back(CInst);
          Src2Value = CInst;
        }
        ExplicitSrcValues[0] = Src1Value;
        ExplicitSrcValues[1] = Src2Value;
      }
    }
  }

  // Figure out the destination register, corresponding value and the
  // binary operator.
  unsigned int DstReg = X86::NoRegister;
  Value *DstValue = nullptr;
  // Construct the appropriate binary operation instruction
  switch (Opc) {
  case X86::ADD8rr:
  case X86::ADD16rr:
  case X86::ADD32rr:
  case X86::ADD64rr: {
    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);
    // Verify the def operand is a register.
    assert(MI.getOperand(DestOpIndex).isReg() &&
           "Expecting destination of add instruction to be a register "
           "operand");
    assert((MCID.getNumDefs() == 1) &&
           "Unexpected number of defines in an add instruction");
    assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
           "Unhandled situation: register is used before initialization in "
           "add");
    DstReg = MI.getOperand(DestOpIndex).getReg();
    // Create add instruction
    Instruction *BinOpInst = BinaryOperator::CreateNSWAdd(Src1Value, Src2Value);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, BinOpInst);
    raisedValues->setInstMetadataRODataIndex(Src2Value, BinOpInst);
    RaisedBB->getInstList().push_back(BinOpInst);
    DstValue = BinOpInst;
    // Set SF and ZF based on DstValue; technically OF, AF, CF and PF also
    // needs to be set but ignoring for now.
    raisedValues->testAndSetEflagSSAValue(EFLAGS::SF, MI, DstValue);
    raisedValues->testAndSetEflagSSAValue(EFLAGS::ZF, MI, DstValue);

    // Update the value of DstReg
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
  } break;
  case X86::IMUL16rr:
  case X86::IMUL32rr:
  case X86::IMUL64rr: {
    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);
    // Verify the def operand is a register.
    assert(MI.getOperand(DestOpIndex).isReg() &&
           "Expecting destination of mul instruction to be a register "
           "operand");
    assert((MCID.getNumDefs() == 1) &&
           "Unexpected number of defines in a mul instruction");
    assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
           "Unhandled situation: register is used before initialization in "
           "mul");
    DstReg = MI.getOperand(DestOpIndex).getReg();
    Instruction *BinOpInst = BinaryOperator::CreateNSWMul(Src1Value, Src2Value);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, BinOpInst);
    raisedValues->setInstMetadataRODataIndex(Src2Value, BinOpInst);
    RaisedBB->getInstList().push_back(BinOpInst);

    DstValue = BinOpInst;
    // Setting EFLAG bits does not seem to matter, so not setting
    // Set the DstReg value
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
  } break;
  case X86::IMUL16r:
  case X86::IMUL32r:
  case X86::IMUL64r:
  case X86::MUL16r:
  case X86::MUL32r:
  case X86::MUL64r: {
    bool IsSigned = instrNameStartsWith(MI, "IMUL");

    assert(MCID.getNumDefs() == 0 && MCID.getNumImplicitDefs() == 3 &&
           MCID.getNumImplicitUses() == 1 &&
           "Unexpected operands in imul instruction");
    // Find first source operand - this is the implicit operand AL/AX/EAX/RAX
    const MCPhysReg Src1Reg = MCID.ImplicitUses[0];
    assert(find64BitSuperReg(Src1Reg) == X86::RAX &&
           "Unexpected implicit register in imul instruction");
    // Find second operand - this is the explicit operand of the instruction
    std::vector<MCPhysReg> SrcRegs;
    for (const MachineOperand &MO : MI.explicit_uses()) {
      assert(MO.isReg() &&
             "Unexpected non-register operand in binary op instruction");
      SrcRegs.push_back(MO.getReg());
    }
    // Ensure that there is only one explicit source operand
    assert(SrcRegs.size() == 1 &&
           "Unexpected number of source register operands in imul instruction");
    // Check the sizes of source operands are the same
    const MCPhysReg Src2Reg = SrcRegs[0];
    unsigned int SrcOpSize = getPhysRegSizeInBits(Src1Reg);
    assert(getPhysRegSizeInBits(Src1Reg) == getPhysRegSizeInBits(Src2Reg) &&
           "Mismatched size of implicit source register and explicit source "
           "register");
    // Get the value of Src1Reg and Src2Reg
    Value *Src1Value = getRegOrArgValue(Src1Reg, MBBNo);
    Value *Src2Value = getRegOrArgValue(Src2Reg, MBBNo);
    assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
           "Unexpected null source operand value in imul instruction");
    assert(Src1Value->getType()->isIntegerTy() &&
           Src2Value->getType()->isIntegerTy() &&
           "Unexpected non-integer type source operands in imul instruction");
    LLVMContext &Ctx(MF.getFunction().getContext());
    // Widen the source values since the result of the multiplication
    Type *WideTy = Type::getIntNTy(Ctx, SrcOpSize * 2);
    CastInst *Src1ValueDT = CastInst::Create(
        CastInst::getCastOpcode(Src1Value, IsSigned, WideTy, IsSigned),
        Src1Value, WideTy);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, Src1ValueDT);
    RaisedBB->getInstList().push_back(Src1ValueDT);

    CastInst *Src2ValueDT = CastInst::Create(
        CastInst::getCastOpcode(Src2Value, IsSigned, WideTy, IsSigned),
        Src2Value, WideTy);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src2Value, Src2ValueDT);
    RaisedBB->getInstList().push_back(Src2ValueDT);
    // Multiply the values
    Instruction *FullProductValue =
        BinaryOperator::CreateNSWMul(Src1ValueDT, Src2ValueDT);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1ValueDT, FullProductValue);
    raisedValues->setInstMetadataRODataIndex(Src2ValueDT, FullProductValue);
    RaisedBB->getInstList().push_back(FullProductValue);
    // Shift amount equal to size of source operand
    Value *ShiftAmountVal =
        ConstantInt::get(FullProductValue->getType(), SrcOpSize);

    // Split the value into ImplicitDefs[0]:ImplicitDefs[1]
    // Compute shr of FullProductValue
    Instruction *ShrDT =
        BinaryOperator::CreateLShr(FullProductValue, ShiftAmountVal);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(FullProductValue, ShrDT);
    RaisedBB->getInstList().push_back(ShrDT);
    // Truncate ShrDT to SrcOpSize
    Type *SrcValTy = Type::getIntNTy(Ctx, SrcOpSize);
    CastInst *ProductUpperValue =
        CastInst::Create(CastInst::Trunc, ShrDT, SrcValTy);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(ShrDT, ProductUpperValue);
    RaisedBB->getInstList().push_back(ProductUpperValue);
    // Set the value of ImplicitDef[1] as ProductLowreHalfValue
    raisedValues->setPhysRegSSAValue(MCID.ImplicitDefs[1], MBBNo,
                                     ProductUpperValue);

    // Truncate the multiplication result to get the lower half value
    CastInst *ProductLowerHalfValue =
        CastInst::Create(CastInst::Trunc, FullProductValue, SrcValTy);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(FullProductValue,
                                             ProductLowerHalfValue);
    RaisedBB->getInstList().push_back(ProductLowerHalfValue);
    // Set the value of ImplicitDef[0] as ProductLowerHalfValue
    raisedValues->setPhysRegSSAValue(MCID.ImplicitDefs[0], MBBNo,
                                     ProductLowerHalfValue);

    if (IsSigned) {
      // For IMUL instruction, we check if the full result is equal to the
      // sign-extended lower half If they are equal, set OF and CF to 0, 1
      // otherwise
      auto *LowerSExtended = CastInst::Create(
          CastInst::SExt, ProductLowerHalfValue, WideTy, "", RaisedBB);
      auto *EqualToFullValue = new ICmpInst(CmpInst::Predicate::ICMP_NE,
                                            LowerSExtended, FullProductValue);
      RaisedBB->getInstList().push_back(EqualToFullValue);

      raisedValues->setPhysRegSSAValue(X86RegisterUtils::EFLAGS::OF, MBBNo,
                                       EqualToFullValue);
      raisedValues->setPhysRegSSAValue(X86RegisterUtils::EFLAGS::CF, MBBNo,
                                       EqualToFullValue);
    } else {
      // Set OF and CF flags to 0 if upper half of the result is 0; else to 1.
      Value *ZeroValue = ConstantInt::get(SrcValTy, 0, IsSigned);

      Instruction *ZFTest =
          new ICmpInst(CmpInst::Predicate::ICMP_NE, ProductUpperValue,
                       ZeroValue, "Test_Not_Zero");

      RaisedBB->getInstList().push_back(ZFTest);
      raisedValues->setPhysRegSSAValue(X86RegisterUtils::EFLAGS::OF, MBBNo,
                                       ZFTest);
      raisedValues->setPhysRegSSAValue(X86RegisterUtils::EFLAGS::CF, MBBNo,
                                       ZFTest);
    }
  } break;
  case X86::AND8rr:
  case X86::AND16rr:
  case X86::AND32rr:
  case X86::AND64rr:
  case X86::OR8rr:
  case X86::OR16rr:
  case X86::OR32rr:
  case X86::OR64rr:
  case X86::XOR8rr:
  case X86::XOR16rr:
  case X86::XOR32rr:
  case X86::XOR64rr: {
    // Verify the def operand is a register.
    const MachineOperand &DestOp = MI.getOperand(DestOpIndex);
    assert(DestOp.isReg() && "Expecting destination of xor instruction to "
                             "be a register operand");
    assert((MCID.getNumDefs() == 1) &&
           MCID.hasImplicitDefOfPhysReg(X86::EFLAGS) &&
           "Unexpected defines in a xor instruction");
    DstReg = DestOp.getReg();
    // Generate an or instruction to set the zero flag if the
    // operands are the same. An instruction such as 'xor $ecx, ecx' is
    // generated to set the register value to 0.
    if (IsXorSetZeroInstruction) {
      // No instruction to generate. Just set destReg value to 0.
      Type *DestTy = getPhysRegOperandType(MI, 0);
      Value *Val = ConstantInt::get(DestTy, 0, false /* isSigned */);
      DstValue = Val;
      // Set SF and ZF knowing that the value is 0
      raisedValues->setEflagBoolean(EFLAGS::SF, MBBNo, false);
      raisedValues->setEflagBoolean(EFLAGS::ZF, MBBNo, true);
      // Set PF knowing that the value is 0, since 0 has
      // an even number of bits set, namely, zero
      raisedValues->setEflagBoolean(EFLAGS::PF, MBBNo, true);
    } else {
      Value *Src1Value = ExplicitSrcValues.at(0);
      Value *Src2Value = ExplicitSrcValues.at(1);
      assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
             "Unhandled situation: register used before initialization in "
             "xor");
      Instruction *BinOpInst = nullptr;
      switch (Opc) {
      case X86::AND8rr:
      case X86::AND16rr:
      case X86::AND32rr:
      case X86::AND64rr:
        BinOpInst = BinaryOperator::CreateAnd(Src1Value, Src2Value);
        break;
      case X86::OR8rr:
      case X86::OR16rr:
      case X86::OR32rr:
      case X86::OR64rr:
        BinOpInst = BinaryOperator::CreateOr(Src1Value, Src2Value);
        break;
      case X86::XOR8rr:
      case X86::XOR16rr:
      case X86::XOR32rr:
      case X86::XOR64rr:
        BinOpInst = BinaryOperator::CreateXor(Src1Value, Src2Value);
        break;
      default:
        assert(false && "Reached unexpected location");
      }
      // Copy any necessary rodata related metadata
      raisedValues->setInstMetadataRODataIndex(Src1Value, BinOpInst);
      raisedValues->setInstMetadataRODataIndex(Src2Value, BinOpInst);

      RaisedBB->getInstList().push_back(BinOpInst);
      DstValue = BinOpInst;
      // Set SF, PF, and ZF based on DstValue.
      raisedValues->testAndSetEflagSSAValue(EFLAGS::SF, MI, DstValue);
      raisedValues->testAndSetEflagSSAValue(EFLAGS::ZF, MI, DstValue);
      raisedValues->testAndSetEflagSSAValue(EFLAGS::PF, MI, DstValue);
    }
    // Clear OF and CF
    raisedValues->setEflagBoolean(EFLAGS::OF, MBBNo, false);
    raisedValues->setEflagBoolean(EFLAGS::CF, MBBNo, false);
    // Update the value of DstReg
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
  } break;
  case X86::TEST8rr:
  case X86::TEST16rr:
  case X86::TEST32rr:
  case X86::TEST64rr: {
    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);
    assert((MCID.getNumDefs() == 0) &&
           MCID.hasImplicitDefOfPhysReg(X86::EFLAGS) &&
           "Unexpected defines in a test instruction");
    assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
           "Unhandled situation: register is used before initialization in "
           "test");
    DstReg = X86::EFLAGS;
    Instruction *BinOpInst = BinaryOperator::CreateAnd(Src1Value, Src2Value);
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, BinOpInst);
    if (Src1Value != Src2Value)
      raisedValues->setInstMetadataRODataIndex(Src2Value, BinOpInst);
    RaisedBB->getInstList().push_back(BinOpInst);
    DstValue = BinOpInst;
    // Clear OF and CF
    raisedValues->setEflagBoolean(EFLAGS::OF, MBBNo, false);
    raisedValues->setEflagBoolean(EFLAGS::CF, MBBNo, false);
    // Set SF, PF, and ZF based on DstValue.
    raisedValues->testAndSetEflagSSAValue(EFLAGS::SF, MI, DstValue);
    raisedValues->testAndSetEflagSSAValue(EFLAGS::ZF, MI, DstValue);
    raisedValues->testAndSetEflagSSAValue(EFLAGS::PF, MI, DstValue);
  } break;
  case X86::NEG8r:
  case X86::NEG16r:
  case X86::NEG32r:
  case X86::NEG64r: {
    // Verify source and dest are tied and are registers
    const MachineOperand &DestOp = MI.getOperand(DestOpIndex);
    assert(DestOp.isTied() &&
           (MI.findTiedOperandIdx(DestOpIndex) == UseOp1Index) &&
           "Expect tied operand in neg instruction");
    assert(DestOp.isReg() && "Expect reg operand in neg instruction");
    assert((MCID.getNumDefs() == 1) &&
           MCID.hasImplicitDefOfPhysReg(X86::EFLAGS) &&
           "Unexpected defines in a neg instruction");
    DstReg = DestOp.getReg();
    Value *Src1Value = ExplicitSrcValues.at(0);
    Instruction *BinOpInst = BinaryOperator::CreateNeg(Src1Value);
    DstValue = BinOpInst;
    // Set CF to 0 if source operand is 0
    // Note: Add this instruction _before_ adding the result of neg
    raisedValues->testAndSetEflagSSAValue(EFLAGS::CF, MI, DstValue);
    // Now add the neg instruction
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, BinOpInst);
    RaisedBB->getInstList().push_back(BinOpInst);
    // Now set up the flags according to the result
    // Set SF, PF, and ZF based on DstValue.
    raisedValues->testAndSetEflagSSAValue(EFLAGS::ZF, MI, DstValue);
    raisedValues->testAndSetEflagSSAValue(EFLAGS::SF, MI, DstValue);
    raisedValues->testAndSetEflagSSAValue(EFLAGS::PF, MI, DstValue);

    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
  } break;
  case X86::NOT8r:
  case X86::NOT16r:
  case X86::NOT32r:
  case X86::NOT64r: {
    // Verify source and dest are tied and are registers
    const MachineOperand &DestOp = MI.getOperand(DestOpIndex);
    assert(DestOp.isTied() &&
           (MI.findTiedOperandIdx(DestOpIndex) == UseOp1Index) &&
           "Expect tied operand in not instruction");
    assert(DestOp.isReg() && "Expect reg operand in not instruction");
    assert((MCID.getNumDefs() == 1) &&
           "Unexpected defines in a not instruction");
    DstReg = DestOp.getReg();
    Value *Src1Value = ExplicitSrcValues.at(0);
    Instruction *BinOpInst = BinaryOperator::CreateNot(Src1Value);
    // No EFLAGS are effected
    // Add the not instruction
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, BinOpInst);
    RaisedBB->getInstList().push_back(BinOpInst);
    DstValue = BinOpInst;

    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
  } break;
  case X86::SAR8rCL:
  case X86::SAR16rCL:
  case X86::SAR32rCL:
  case X86::SAR64rCL:
  case X86::SHL8rCL:
  case X86::SHL16rCL:
  case X86::SHL32rCL:
  case X86::SHL64rCL:
  case X86::SHR8rCL:
  case X86::SHR16rCL:
  case X86::SHR32rCL:
  case X86::SHR64rCL: {
    // Verify source and dest are tied and are registers
    const MachineOperand &DestOp = MI.getOperand(DestOpIndex);
    assert(DestOp.isTied() &&
           (MI.findTiedOperandIdx(DestOpIndex) == UseOp1Index) &&
           "Expect tied operand in shl instruction");
    assert(DestOp.isReg() && "Expect reg operand in shl instruction");
    assert((MCID.getNumDefs() == 1) &&
           "Unexpected defines in a shl instruction");
    DstReg = DestOp.getReg();
    Value *SrcOpValue = ExplicitSrcValues.at(0);
    assert((MCID.getNumImplicitUses() == 1) &&
           "Expect one implicit use in shl instruction");
    assert((MCID.ImplicitUses[0] == X86::CL) &&
           "Expect implicit CL regsiter operand in shl instruction");
    Value *CountValue = getPhysRegValue(MI, X86::CL);
    // Check for undefined use
    if (CountValue == nullptr)
      Success = false;
    else {
      // cast CountValue as needed
      Type *SrcOpValueTy = SrcOpValue->getType();
      CountValue =
          getRaisedValues()->castValue(CountValue, SrcOpValueTy, RaisedBB);
      // Count is masked to 5 bits (6 bits if 64-bit register)
      bool Is64Bit = (SrcOpValue->getType()->getPrimitiveSizeInBits() == 64);
      Value *CountMask = Is64Bit ? ConstantInt::get(SrcOpValueTy, 0x1f)
                                 : ConstantInt::get(SrcOpValueTy, 0x3f);
      // Generate mask
      CountValue = BinaryOperator::CreateAnd(CountValue, CountMask,
                                             "shift-cnt-msk", RaisedBB);

      Instruction *BinOpInst;
      if (instrNameStartsWith(MI, "SAR")) {
        BinOpInst = BinaryOperator::CreateAShr(SrcOpValue, CountValue);
      } else if (instrNameStartsWith(MI, "SHL")) {
        BinOpInst = BinaryOperator::CreateShl(SrcOpValue, CountValue);
      } else if (instrNameStartsWith(MI, "SHR")) {
        BinOpInst = BinaryOperator::CreateLShr(SrcOpValue, CountValue);
      } else {
        llvm_unreachable("unhandled shift instruction");
      }

      // Copy any necessary rodata related metadata
      raisedValues->setInstMetadataRODataIndex(SrcOpValue, BinOpInst);
      raisedValues->setInstMetadataRODataIndex(CountValue, BinOpInst);
      // Add the shl instruction
      RaisedBB->getInstList().push_back(BinOpInst);
      DstValue = BinOpInst;

      // Affected EFLAGS
      raisedValues->testAndSetEflagSSAValue(EFLAGS::CF, MI, DstValue);
      raisedValues->testAndSetEflagSSAValue(EFLAGS::ZF, MI, DstValue);
      raisedValues->testAndSetEflagSSAValue(EFLAGS::SF, MI, DstValue);

      raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
    }
  } break;
  case X86::POPCNT16rr:
  case X86::POPCNT32rr:
  case X86::POPCNT64rr: {
    const MachineOperand &DestOp = MI.getOperand(DestOpIndex);
    assert(DestOp.isReg() && "Expecting destination of popcnt instruction to "
                             "be a register operand");
    assert((MCID.getNumDefs() == 1) &&
           MCID.hasImplicitDefOfPhysReg(X86::EFLAGS) &&
           "Unexpected defines in a popcnt instruction");
    DstReg = DestOp.getReg();
    Value *SrcValue = ExplicitSrcValues.at(0);

    Module *M = MR->getModule();
    Function *IntrinsicFunc =
        Intrinsic::getDeclaration(M, Intrinsic::ctpop, SrcValue->getType());
    Value *IntrinsicCallArgs[] = {SrcValue};
    Instruction *BinOpInst =
        CallInst::Create(IntrinsicFunc, ArrayRef<Value *>(IntrinsicCallArgs));

    // Add the intrinsic call instruction
    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(SrcValue, BinOpInst);
    RaisedBB->getInstList().push_back(BinOpInst);
    DstValue = BinOpInst;

    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);

    // OF, SF, ZF, AF, CF, PF are all cleared. ZF is set if SRC = 0, otherwise
    // ZF is cleared.
    raisedValues->setEflagBoolean(EFLAGS::OF, MBBNo, false);
    raisedValues->setEflagBoolean(EFLAGS::SF, MBBNo, false);
    raisedValues->setEflagBoolean(EFLAGS::AF, MBBNo, false);
    raisedValues->setEflagBoolean(EFLAGS::CF, MBBNo, false);
    raisedValues->setEflagBoolean(EFLAGS::PF, MBBNo, false);

    // Test if SrcValue is Zero
    Value *ZeroValue =
        ConstantInt::get(SrcValue->getType(), 0, false /* isSigned */);
    Instruction *ZFTest = new ICmpInst(CmpInst::Predicate::ICMP_EQ, SrcValue,
                                       ZeroValue, "ZeroFlag");
    RaisedBB->getInstList().push_back(ZFTest);
    // ZF = (SrcValue==0).
    raisedValues->setPhysRegSSAValue(X86RegisterUtils::EFLAGS::ZF, MBBNo,
                                     ZFTest);

  } break;
  case X86::SUBSSrr_Int:
  case X86::SUBSDrr_Int:
  case X86::ADDSSrr_Int:
  case X86::ADDSDrr_Int:
  case X86::MULSSrr_Int:
  case X86::MULSDrr_Int:
  case X86::DIVSSrr_Int:
  case X86::DIVSDrr_Int:
  case X86::SUBPSrr:
  case X86::SUBPDrr:
  case X86::ADDPSrr:
  case X86::ADDPDrr:
  case X86::MULPSrr:
  case X86::MULPDrr:
  case X86::DIVPSrr:
  case X86::DIVPDrr: {
    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);
    // Verify the def operand is a register.
    assert(MI.getOperand(DestOpIndex).isReg() &&
           "Expecting destination of fp op instruction to be a register "
           "operand");
    assert((MCID.getNumDefs() == 1) &&
           "Unexpected number of defines in fp op instruction");
    assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
           "Unhandled situation: register is used before initialization in "
           "fp op");
    DstReg = MI.getOperand(DestOpIndex).getReg();

    Instruction *BinOpInst = nullptr;
    switch (Opc) {
    case X86::ADDSSrr_Int:
    case X86::ADDSDrr_Int:
    case X86::ADDPSrr:
    case X86::ADDPDrr:
      BinOpInst = BinaryOperator::CreateFAdd(Src1Value, Src2Value);
      break;
    case X86::SUBSSrr_Int:
    case X86::SUBSDrr_Int:
    case X86::SUBPSrr:
    case X86::SUBPDrr:
      BinOpInst = BinaryOperator::CreateFSub(Src1Value, Src2Value);
      break;
    case X86::MULSSrr_Int:
    case X86::MULSDrr_Int:
    case X86::MULPSrr:
    case X86::MULPDrr:
      BinOpInst = BinaryOperator::CreateFMul(Src1Value, Src2Value);
      break;
    case X86::DIVSSrr_Int:
    case X86::DIVSDrr_Int:
    case X86::DIVPSrr:
    case X86::DIVPDrr:
      BinOpInst = BinaryOperator::CreateFDiv(Src1Value, Src2Value);
      break;
    default:
      llvm_unreachable("Unhandled fp instruction");
    }

    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, BinOpInst);
    // raisedValues->setInstMetadataRODataIndex(Src2Value, BinOpInst);
    RaisedBB->getInstList().push_back(BinOpInst);
    DstValue = BinOpInst;

    // Update the value of DstReg
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
  } break;
  case X86::PANDrr:
  case X86::PANDNrr:
  case X86::ANDPDrr:
  case X86::ANDPSrr:
  case X86::ANDNPDrr:
  case X86::ANDNPSrr:
  case X86::PORrr:
  case X86::ORPDrr:
  case X86::ORPSrr:
  case X86::PXORrr:
  case X86::XORPDrr:
  case X86::XORPSrr: {
    // bitwise operations on fp values do not exist in LLVM.
    // To emulate the behavior, we
    // - bitcast values to int
    // - perform the operation
    // - bitcast back to original type
    DstReg = MI.getOperand(DestOpIndex).getReg();

    if (IsXorSetZeroInstruction) {
      // No instruction to generate. Just set destReg value to 0.
      Type *DestTy = getPhysRegOperandType(MI, 0);
      if (DestTy->isFPOrFPVectorTy()) {
        DstValue = ConstantFP::get(DestTy, 0);
      } else {
        DstValue = ConstantInt::get(DestTy, 0);
      }
    } else {
      LLVMContext &Ctx(MF.getFunction().getContext());

      Value *Src1Value = ExplicitSrcValues.at(0);
      Value *Src2Value = ExplicitSrcValues.at(1);
      // Verify the def operand is a register.
      assert(MI.getOperand(DestOpIndex).isReg() &&
             "Expecting destination of fp op instruction to be a register "
             "operand");
      assert((MCID.getNumDefs() == 1) &&
             "Unexpected number of defines in fp op instruction");
      assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
             "Unhandled situation: register is used before initialization in "
             "fp op");

      assert(Src1Value->getType()->getPrimitiveSizeInBits() ==
                 Src2Value->getType()->getPrimitiveSizeInBits() &&
             "Expected operand types to have same size");

      auto BitSize = std::max(Src1Value->getType()->getPrimitiveSizeInBits(),
                              Src2Value->getType()->getPrimitiveSizeInBits());

      Instruction *BitCastToInt1 =
          new BitCastInst(Src1Value, Type::getIntNTy(Ctx, BitSize),
                          "bitwise_operand", RaisedBB);
      Instruction *BitCastToInt2 =
          new BitCastInst(Src2Value, Type::getIntNTy(Ctx, BitSize),
                          "bitwise_operand", RaisedBB);

      Instruction *Result;

      switch (Opc) {
      case X86::PANDrr:
      case X86::ANDPDrr:
      case X86::ANDPSrr:
        Result = BinaryOperator::CreateAnd(BitCastToInt1, BitCastToInt2,
                                           "and_result", RaisedBB);
        break;
      case X86::PANDNrr:
      case X86::ANDNPDrr:
      case X86::ANDNPSrr: {
        auto *NotVal = BinaryOperator::CreateNot(BitCastToInt1, "", RaisedBB);
        Result = BinaryOperator::CreateAnd(NotVal, BitCastToInt2, "andn_result",
                                           RaisedBB);
      } break;
      case X86::PORrr:
      case X86::ORPDrr:
      case X86::ORPSrr:
        Result = BinaryOperator::CreateOr(BitCastToInt1, BitCastToInt2,
                                          "or_result", RaisedBB);
        break;
      case X86::PXORrr:
      case X86::XORPDrr:
      case X86::XORPSrr:
        Result = BinaryOperator::CreateXor(BitCastToInt1, BitCastToInt2,
                                           "xor_result", RaisedBB);
        break;
      default:
        llvm_unreachable("unhandled bitwise instruction");
      }

      Instruction *CastBackResult = new BitCastInst(
          Result, Src1Value->getType(), "bitcast_result", RaisedBB);

      DstValue = CastBackResult;
    }

    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
  } break;
  case X86::MAXSDrr_Int:
  case X86::MAXSSrr_Int:
  case X86::MINSDrr_Int:
  case X86::MINSSrr_Int: {
    bool IsMax = instrNameStartsWith(MI, "MAX");
    std::string NameString = IsMax ? "max" : "min";

    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);

    auto CmpType = IsMax ? CmpInst::FCMP_OGT : CmpInst::FCMP_OLT;
    Instruction *CmpInst =
        new FCmpInst(*RaisedBB, CmpType, Src1Value, Src2Value, "cmp");

    Instruction *SelectInst =
        SelectInst::Create(CmpInst, Src1Value, Src2Value, NameString, RaisedBB);

    DstReg = MI.getOperand(DestOpIndex).getReg();
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, SelectInst);
  } break;
  case X86::SBB16rr:
  case X86::SBB32rr:
  case X86::SBB64rr:
  case X86::SBB8rr: {
    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);
    Value *CFValue = getRegOrArgValue(EFLAGS::CF, MBBNo);
    assert(CFValue && "Expected CF to be set for sbb instruction");

    auto *CFExtended = CastInst::Create(Instruction::ZExt, CFValue,
                                        Src2Value->getType(), "", RaisedBB);

    auto *Src2AndCFValue =
        BinaryOperator::CreateAdd(Src2Value, CFExtended, "", RaisedBB);

    auto *Result =
        BinaryOperator::CreateSub(Src1Value, Src2AndCFValue, "", RaisedBB);

    DstReg = MI.getOperand(DestOpIndex).getReg();
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, Result);
    raisedValues->testAndSetEflagSSAValue(EFLAGS::OF, MI, Result);
    raisedValues->testAndSetEflagSSAValue(EFLAGS::CF, MI, Result);
  } break;
  case X86::SQRTSDr:
  case X86::SQRTSDr_Int:
  case X86::SQRTSSr:
  case X86::SQRTSSr_Int: {
    LLVMContext &Ctx(MF.getFunction().getContext());

    // Destination operand is an SSE register
    auto PReg = MI.getOperand(DestOpIndex).getReg();
    auto SSERegSzInBits =
        getRegisterInfo()->getRegSizeInBits(PReg, machineRegInfo);
    Type *InstrTy =
        getRaisedValues()->getSSEInstructionType(MI, SSERegSzInBits, Ctx);
    Value *SrcValue = getRaisedValues()->reinterpretSSERegValue(
        ExplicitSrcValues.at(0), InstrTy, RaisedBB);

    Module *M = MR->getModule();
    auto *IntrinsicFunc =
        Intrinsic::getDeclaration(M, Intrinsic::sqrt, InstrTy);

    Value *IntrinsicCallArgs[] = {SrcValue};
    auto *Result = CallInst::Create(
        IntrinsicFunc, ArrayRef<Value *>(IntrinsicCallArgs), "", RaisedBB);
    raisedValues->setInstMetadataRODataIndex(SrcValue, Result);

    DstReg = MI.getOperand(DestOpIndex).getReg();
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, Result);
  } break;
  case X86::PADDBrr:
  case X86::PADDDrr:
  case X86::PADDQrr:
  case X86::PADDWrr:
  case X86::PSUBBrr:
  case X86::PSUBDrr:
  case X86::PSUBQrr:
  case X86::PSUBWrr: {
    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);
    // Verify the def operand is a register.
    assert(MI.getOperand(DestOpIndex).isReg() &&
           isSSE2Reg(MI.getOperand(DestOpIndex).getReg()) &&
           "Expecting destination of sse op instruction to be a register "
           "operand");
    assert((MCID.getNumDefs() == 1) &&
           "Unexpected number of defines in sse op instruction");
    assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
           "Unhandled situation: register is used before initialization in "
           "sse op");
    DstReg = MI.getOperand(DestOpIndex).getReg();

    Type *SrcTy;
    LLVMContext &Ctx(MF.getFunction().getContext());
    switch (MI.getOpcode()) {
    case X86::PADDBrr:
    case X86::PSUBBrr:
      SrcTy = VectorType::get(Type::getInt8Ty(Ctx), 16, false);
      break;
    case X86::PADDWrr:
    case X86::PSUBWrr:
      SrcTy = VectorType::get(Type::getInt16Ty(Ctx), 8, false);
      break;
    case X86::PADDDrr:
    case X86::PSUBDrr:
      SrcTy = VectorType::get(Type::getInt32Ty(Ctx), 4, false);
      break;
    case X86::PADDQrr:
    case X86::PSUBQrr:
      SrcTy = VectorType::get(Type::getInt64Ty(Ctx), 2, false);
      break;
    default:
      MI.dump();
      llvm_unreachable("Unhandled sse packed instruction");
    }

    Src1Value = new BitCastInst(Src1Value, SrcTy, "", RaisedBB);
    Src2Value = new BitCastInst(Src2Value, SrcTy, "", RaisedBB);

    bool IsAdd = instrNameStartsWith(MI, "PADD");
    bool IsSub = instrNameStartsWith(MI, "PSUB");

    Instruction *BinOpInst = nullptr;
    if (IsAdd) {
      BinOpInst = BinaryOperator::CreateAdd(Src1Value, Src2Value);
    } else if (IsSub) {
      BinOpInst = BinaryOperator::CreateSub(Src1Value, Src2Value);
    } else {
      MI.dump();
      llvm_unreachable("Unhandled instruction");
    }

    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, BinOpInst);
    // raisedValues->setInstMetadataRODataIndex(Src2Value, BinOpInst);
    RaisedBB->getInstList().push_back(BinOpInst);
    DstValue = BinOpInst;

    // Update the value of DstReg
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, DstValue);
  } break;
  case X86::PMAXSBrr:
  case X86::PMAXSDrr:
  case X86::PMAXSWrr:
  case X86::PMAXUBrr:
  case X86::PMAXUDrr:
  case X86::PMAXUWrr:
  case X86::PMINSBrr:
  case X86::PMINSDrr:
  case X86::PMINSWrr:
  case X86::PMINUBrr:
  case X86::PMINUDrr:
  case X86::PMINUWrr: {
    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);
    // Verify the def operand is a register.
    assert(MI.getOperand(DestOpIndex).isReg() &&
           isSSE2Reg(MI.getOperand(DestOpIndex).getReg()) &&
           "Expecting destination of sse op instruction to be a register "
           "operand");
    assert((MCID.getNumDefs() == 1) &&
           "Unexpected number of defines in sse op instruction");
    assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
           "Unhandled situation: register is used before initialization in "
           "sse op");
    DstReg = MI.getOperand(DestOpIndex).getReg();

    bool IsSigned =
        instrNameStartsWith(MI, "PMAXS") || instrNameStartsWith(MI, "PMINS");
    bool IsMax = instrNameStartsWith(MI, "PMAX");
    CmpInst::Predicate CmpPred;
    if (IsMax) {
      CmpPred = IsSigned ? CmpInst::Predicate::ICMP_SGT
                         : CmpInst::Predicate::ICMP_UGT;
    } else {
      CmpPred = IsSigned ? CmpInst::Predicate::ICMP_SLT
                         : CmpInst::Predicate::ICMP_ULT;
    }

    unsigned int SegmentSz;
    switch (MI.getOpcode()) {
    case X86::PMAXSBrr:
    case X86::PMAXUBrr:
    case X86::PMINSBrr:
    case X86::PMINUBrr:
      SegmentSz = 8;
      break;
    case X86::PMAXSWrr:
    case X86::PMAXUWrr:
    case X86::PMINSWrr:
    case X86::PMINUWrr:
      SegmentSz = 16;
      break;
    case X86::PMAXSDrr:
    case X86::PMAXUDrr:
    case X86::PMINSDrr:
    case X86::PMINUDrr:
      SegmentSz = 32;
      break;
    default:
      llvm_unreachable("Unhandled packed min/max instruction");
    }

    LLVMContext &Ctx(MF.getFunction().getContext());
    FixedVectorType *VecTy =
        FixedVectorType::get(Type::getIntNTy(Ctx, SegmentSz), 128 / SegmentSz);

    Src1Value = new BitCastInst(Src1Value, VecTy, "", RaisedBB);
    Src2Value = new BitCastInst(Src2Value, VecTy, "", RaisedBB);

    Value *Result = ConstantInt::get(VecTy, 0);
    for (unsigned int Idx = 0; Idx < VecTy->getNumElements(); ++Idx) {
      auto *Index = ConstantInt::get(VecTy->getElementType(), Idx);
      auto *CmpSegment1 =
          ExtractElementInst::Create(Src1Value, Index, "", RaisedBB);
      auto *CmpSegment2 =
          ExtractElementInst::Create(Src2Value, Index, "", RaisedBB);
      auto *CmpInst = new ICmpInst(*RaisedBB, CmpPred, CmpSegment1, CmpSegment2,
                                   "cmp_segment");
      auto *SelectInst = SelectInst::Create(CmpInst, CmpSegment1, CmpSegment2,
                                            "min_max_segment", RaisedBB);
      Result =
          InsertElementInst::Create(Result, SelectInst, Index, "", RaisedBB);
    }

    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, (Instruction *)Result);
    // Update the value of DstReg
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, Result);
  } break;
  case X86::UNPCKLPDrr:
  case X86::UNPCKLPSrr: {
    Value *Src1Value = ExplicitSrcValues.at(0);
    Value *Src2Value = ExplicitSrcValues.at(1);
    // Verify the def operand is a register.
    assert(MI.getOperand(DestOpIndex).isReg() &&
           "Expecting destination of sse op instruction to be a register "
           "operand");
    assert((MCID.getNumDefs() == 1) &&
           "Unexpected number of defines in sse op instruction");
    assert((Src1Value != nullptr) && (Src2Value != nullptr) &&
           "Unhandled situation: register is used before initialization in "
           "sse op");
    DstReg = MI.getOperand(DestOpIndex).getReg();

    unsigned int SegmentSz;
    if (MI.getOpcode() == X86::UNPCKLPDrr) {
      SegmentSz = 64;
    } else {
      SegmentSz = 32;
    }

    LLVMContext &Ctx(MF.getFunction().getContext());
    FixedVectorType *VecTy =
        FixedVectorType::get(Type::getIntNTy(Ctx, SegmentSz), 128 / SegmentSz);

    Src1Value = new BitCastInst(Src1Value, VecTy, "", RaisedBB);
    Src2Value = new BitCastInst(Src2Value, VecTy, "", RaisedBB);

    Value *Result = ConstantInt::get(VecTy, 0);
    for (unsigned int Idx = 0; Idx < VecTy->getNumElements(); ++Idx) {
      auto *DstIndex = ConstantInt::get(VecTy->getElementType(), Idx);
      auto *SrcIndex = ConstantInt::get(VecTy->getElementType(), Idx >> 1);

      Value *SrcVal;
      if (Idx % 2 == 0) {
        SrcVal = Src1Value;
      } else {
        SrcVal = Src2Value;
      }

      auto *ExtractInst =
          ExtractElementInst::Create(SrcVal, SrcIndex, "", RaisedBB);
      Result = InsertElementInst::Create(Result, ExtractInst, DstIndex, "",
                                         RaisedBB);
    }

    // Copy any necessary rodata related metadata
    raisedValues->setInstMetadataRODataIndex(Src1Value, (Instruction *)Result);
    // Update the value of DstReg
    raisedValues->setPhysRegSSAValue(DstReg, MBBNo, Result);
  } break;
  default:
    MI.dump();
    assert(false && "Unhandled binary instruction");
  }

  return Success;
}

bool X86MachineInstructionRaiser::raiseBinaryOpMemToRegInstr(
    const MachineInstr &MI, Value *MemRefValue) {
  unsigned int Opcode = MI.getOpcode();
  const MCInstrDesc &MIDesc = MI.getDesc();
  std::set<unsigned> AffectedEFlags;
  std::set<unsigned> ClearedEFlags;

  assert((MIDesc.getNumDefs() == 1) &&
         "Encountered memory load instruction with more than 1 defs");
  unsigned int DestIndex = 0;
  const MachineOperand &DestOp = MI.getOperand(DestIndex);
  assert(DestOp.isReg() &&
         "Expect destination register operand in binary reg/mem instruction");

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  Register DestPReg = DestOp.getReg();
  unsigned int MemAlignment = getInstructionMemOpSize(Opcode);
  Type *DestopTy = getPhysRegOperandType(MI, DestIndex);
  Value *DestValue = getRegOrArgValue(DestPReg, MI.getParent()->getNumber());

  // Verify sanity of the instruction.
  // SSE2 registers are always of a fixed size and might not be equal to
  // MemAlignment
  assert((isSSE2Reg(DestOp.getReg()) ||
          getPhysRegOperandSize(MI, DestIndex) == MemAlignment) &&
         "Mismatched destination register size and instruction size of binary "
         "op instruction");

  unsigned int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
  Value *LoadValue =
      loadMemoryRefValue(MI, MemRefValue, MemoryRefOpIndex, DestopTy);
  if (DestValue != nullptr) {
    // Cast DestValue to the DestopTy, as for single-precision FP ops
    // DestValue type and DestopTy might be different.
    if (isSSE2Reg(DestPReg)) {
      DestValue = getRaisedValues()->reinterpretSSERegValue(DestValue, DestopTy,
                                                            RaisedBB);
    } else {
      DestValue = getRaisedValues()->castValue(DestValue, DestopTy, RaisedBB);
    }
  }
  Instruction *BinOpInst = nullptr;

  switch (Opcode) {
  case X86::ADD64rm:
  case X86::ADD32rm:
  case X86::ADD16rm:
  case X86::ADD8rm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    // Create add instruction
    BinOpInst = BinaryOperator::CreateAdd(DestValue, LoadValue);
    AffectedEFlags.insert(EFLAGS::PF);
  } break;
  case X86::AND64rm:
  case X86::AND32rm:
  case X86::AND16rm:
  case X86::AND8rm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    // Create and instruction
    BinOpInst = BinaryOperator::CreateAnd(DestValue, LoadValue);
    AffectedEFlags.insert(EFLAGS::PF);
  } break;
  case X86::OR32rm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    // Create or instruction
    BinOpInst = BinaryOperator::CreateOr(DestValue, LoadValue);
    AffectedEFlags.insert(EFLAGS::PF);
  } break;
  case X86::IMUL16rm:
  case X86::IMUL32rm:
  case X86::IMUL64rm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    // One-operand form of IMUL
    // Create mul instruction
    BinOpInst = BinaryOperator::CreateMul(DestValue, LoadValue);
  } break;
  case X86::IMUL16rmi:
  case X86::IMUL16rmi8:
  case X86::IMUL32rmi:
  case X86::IMUL32rmi8:
  case X86::IMUL64rmi8:
  case X86::IMUL64rmi32: {
    // We don't read from DestValue, so we don't need to check if it is null
    // Three-operand form of IMUL
    // Get index of memory reference in the instruction.
    int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
    // The index of the memory reference operand should be 1
    assert(MemoryRefOpIndex == 1 &&
           "Unexpected memory reference operand index in imul instruction");
    const MachineOperand &SecondSourceOp =
        MI.getOperand(MemoryRefOpIndex + X86::AddrNumOperands);
    // Second source should be an immediate.
    assert(SecondSourceOp.isImm() &&
           "Expect immediate operand in imul instruction");
    // Construct the value corresponding to immediate operand
    Value *SecondSourceVal =
        ConstantInt::get(LoadValue->getType(), SecondSourceOp.getImm());
    // Create mul instruction
    BinOpInst = BinaryOperator::CreateMul(SecondSourceVal, LoadValue);
  } break;
  case X86::XOR8rm:
  case X86::XOR16rm:
  case X86::XOR32rm:
  case X86::XOR64rm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    // Create xor instruction
    BinOpInst = BinaryOperator::CreateXor(DestValue, LoadValue);
    ClearedEFlags.insert(EFLAGS::OF);
    ClearedEFlags.insert(EFLAGS::CF);
    AffectedEFlags.insert(EFLAGS::SF);
    AffectedEFlags.insert(EFLAGS::ZF);
    AffectedEFlags.insert(EFLAGS::PF);
  } break;
  case X86::ADDSSrm_Int:
  case X86::ADDSDrm_Int:
  case X86::ADDPSrm:
  case X86::ADDPDrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    BinOpInst = BinaryOperator::CreateFAdd(DestValue, LoadValue);
  } break;
  case X86::SUBSSrm_Int:
  case X86::SUBSDrm_Int:
  case X86::SUBPSrm:
  case X86::SUBPDrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    BinOpInst = BinaryOperator::CreateFSub(DestValue, LoadValue);
  } break;
  case X86::MULSSrm_Int:
  case X86::MULSDrm_Int:
  case X86::MULPSrm:
  case X86::MULPDrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    BinOpInst = BinaryOperator::CreateFMul(DestValue, LoadValue);
  } break;
  case X86::DIVSSrm_Int:
  case X86::DIVSDrm_Int:
  case X86::DIVPSrm:
  case X86::DIVPDrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    BinOpInst = BinaryOperator::CreateFDiv(DestValue, LoadValue);
  } break;
  case X86::MAXSDrm_Int:
  case X86::MAXSSrm_Int:
  case X86::MINSDrm_Int:
  case X86::MINSSrm_Int: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    bool IsMax = instrNameStartsWith(MI, "MAX");
    std::string NameString = IsMax ? "max" : "min";

    auto CmpType = IsMax ? CmpInst::FCMP_OGT : CmpInst::FCMP_OLT;
    Instruction *CmpInst =
        new FCmpInst(*RaisedBB, CmpType, DestValue, LoadValue, "cmp");

    BinOpInst = SelectInst::Create(CmpInst, DestValue, LoadValue, NameString);
  } break;
  case X86::ANDPDrm:
  case X86::ANDPSrm:
  case X86::ANDNPDrm:
  case X86::ANDNPSrm:
  case X86::ORPDrm:
  case X86::ORPSrm:
  case X86::XORPDrm:
  case X86::XORPSrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    LLVMContext &Ctx(MF.getFunction().getContext());
    auto *Int128Ty = Type::getInt128Ty(Ctx);
    // BitCast operands to integer types to perform and/or/xor operation
    auto *DestValueInt = new BitCastInst(DestValue, Int128Ty, "", RaisedBB);
    auto *LoadValueInt = new BitCastInst(LoadValue, Int128Ty, "", RaisedBB);

    Value *Result;
    switch (Opcode) {
    case X86::ANDPDrm:
    case X86::ANDPSrm:
      Result =
          BinaryOperator::CreateAnd(DestValueInt, LoadValueInt, "", RaisedBB);
      break;
    case X86::ANDNPDrm:
    case X86::ANDNPSrm: {
      auto *NotVal = BinaryOperator::CreateNot(DestValueInt, "", RaisedBB);
      Result = BinaryOperator::CreateAnd(NotVal, LoadValueInt, "", RaisedBB);
    } break;
    case X86::ORPDrm:
    case X86::ORPSrm:
      Result =
          BinaryOperator::CreateOr(DestValueInt, LoadValueInt, "", RaisedBB);
      break;
    case X86::XORPDrm:
    case X86::XORPSrm:
      Result =
          BinaryOperator::CreateXor(DestValueInt, LoadValueInt, "", RaisedBB);
      break;
    default:
      llvm_unreachable("Unhandled opcode for packed bitwise instruction");
    }
    // Cast back to operand type
    BinOpInst = new BitCastInst(Result, DestopTy);
  } break;
  case X86::PANDrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    BinOpInst = BinaryOperator::CreateAnd(DestValue, LoadValue);
  } break;
  case X86::PANDNrm: {
    DestValue = BinaryOperator::CreateNot(DestValue, "", RaisedBB);
    BinOpInst = BinaryOperator::CreateAnd(DestValue, LoadValue);
  } break;
  case X86::PORrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    BinOpInst = BinaryOperator::CreateOr(DestValue, LoadValue);
  } break;
  case X86::PXORrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    BinOpInst = BinaryOperator::CreateXor(DestValue, LoadValue);
  } break;
  case X86::SBB16rm:
  case X86::SBB32rm:
  case X86::SBB64rm:
  case X86::SBB8rm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    auto *CFValue = getRegOrArgValue(EFLAGS::CF, MI.getParent()->getNumber());
    assert(CFValue && "Expected CF to be set for sbb instruction");

    auto *CFExtended = CastInst::Create(Instruction::ZExt, CFValue,
                                        LoadValue->getType(), "", RaisedBB);

    auto *LoadAndCFValue =
        BinaryOperator::CreateAdd(LoadValue, CFExtended, "", RaisedBB);

    BinOpInst = BinaryOperator::CreateSub(DestValue, LoadAndCFValue);

    AffectedEFlags.insert(EFLAGS::OF);
    AffectedEFlags.insert(EFLAGS::CF);
  } break;
  case X86::SQRTSDm:
  case X86::SQRTSDm_Int:
  case X86::SQRTSSm:
  case X86::SQRTSSm_Int: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    LLVMContext &Ctx(MF.getFunction().getContext());

    auto PReg = MI.getOperand(DestIndex).getReg();
    auto SSERegSzInBits =
        getRegisterInfo()->getRegSizeInBits(PReg, machineRegInfo);
    Type *InstrTy =
        getRaisedValues()->getSSEInstructionType(MI, SSERegSzInBits, Ctx);
    assert(LoadValue->getType() == InstrTy &&
           "Unexpected value type for sqrtsd/sqrtss instruction");

    Module *M = MR->getModule();
    auto *IntrinsicFunc =
        Intrinsic::getDeclaration(M, Intrinsic::sqrt, InstrTy);

    Value *IntrinsicCallArgs[] = {LoadValue};
    BinOpInst =
        CallInst::Create(IntrinsicFunc, ArrayRef<Value *>(IntrinsicCallArgs));
  } break;
  case X86::PADDBrm:
  case X86::PADDDrm:
  case X86::PADDQrm:
  case X86::PADDWrm:
  case X86::PSUBBrm:
  case X86::PSUBDrm:
  case X86::PSUBQrm:
  case X86::PSUBWrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    Type *SrcTy;
    LLVMContext &Ctx(MF.getFunction().getContext());
    switch (MI.getOpcode()) {
    case X86::PADDBrm:
    case X86::PSUBBrm:
      SrcTy = VectorType::get(Type::getInt8Ty(Ctx), 16, false);
      break;
    case X86::PADDWrm:
    case X86::PSUBWrm:
      SrcTy = VectorType::get(Type::getInt16Ty(Ctx), 8, false);
      break;
    case X86::PADDDrm:
    case X86::PSUBDrm:
      SrcTy = VectorType::get(Type::getInt32Ty(Ctx), 4, false);
      break;
    case X86::PADDQrm:
    case X86::PSUBQrm:
      SrcTy = VectorType::get(Type::getInt64Ty(Ctx), 2, false);
      break;
    default:
      MI.dump();
      llvm_unreachable("Unhandled sse packed instruction");
    }

    DestValue = new BitCastInst(DestValue, SrcTy, "", RaisedBB);
    LoadValue = new BitCastInst(LoadValue, SrcTy, "", RaisedBB);

    bool IsAdd = instrNameStartsWith(MI, "PADD");
    bool IsSub = instrNameStartsWith(MI, "PSUB");

    if (IsAdd) {
      BinOpInst = BinaryOperator::CreateAdd(DestValue, LoadValue);
    } else if (IsSub) {
      BinOpInst = BinaryOperator::CreateSub(DestValue, LoadValue);
    } else {
      MI.dump();
      llvm_unreachable("Unhandled instruction");
    }
  } break;
  case X86::PMAXSBrm:
  case X86::PMAXSDrm:
  case X86::PMAXSWrm:
  case X86::PMAXUBrm:
  case X86::PMAXUDrm:
  case X86::PMAXUWrm:
  case X86::PMINSBrm:
  case X86::PMINSDrm:
  case X86::PMINSWrm:
  case X86::PMINUBrm:
  case X86::PMINUDrm:
  case X86::PMINUWrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");
    Value *Src1Value = DestValue;
    Value *Src2Value = LoadValue;

    bool IsSigned =
        instrNameStartsWith(MI, "PMAXS") || instrNameStartsWith(MI, "PMINS");
    bool IsMax = instrNameStartsWith(MI, "PMAX");
    CmpInst::Predicate CmpPred;
    if (IsMax) {
      CmpPred = IsSigned ? CmpInst::Predicate::ICMP_SGT
                         : CmpInst::Predicate::ICMP_UGT;
    } else {
      CmpPred = IsSigned ? CmpInst::Predicate::ICMP_SLT
                         : CmpInst::Predicate::ICMP_ULT;
    }

    unsigned int SegmentSz;
    switch (MI.getOpcode()) {
    case X86::PMAXSBrm:
    case X86::PMAXUBrm:
    case X86::PMINSBrm:
    case X86::PMINUBrm:
      SegmentSz = 8;
      break;
    case X86::PMAXSWrm:
    case X86::PMAXUWrm:
    case X86::PMINSWrm:
    case X86::PMINUWrm:
      SegmentSz = 16;
      break;
    case X86::PMAXSDrm:
    case X86::PMAXUDrm:
    case X86::PMINSDrm:
    case X86::PMINUDrm:
      SegmentSz = 32;
      break;
    default:
      llvm_unreachable("Unhandled packed min/max instruction");
    }

    LLVMContext &Ctx(MF.getFunction().getContext());
    FixedVectorType *VecTy =
        FixedVectorType::get(Type::getIntNTy(Ctx, SegmentSz), 128 / SegmentSz);

    Src1Value = new BitCastInst(Src1Value, VecTy, "", RaisedBB);
    Src2Value = new BitCastInst(Src2Value, VecTy, "", RaisedBB);

    Value *Result = ConstantInt::get(VecTy, 0);
    for (unsigned int Idx = 0; Idx < VecTy->getNumElements(); ++Idx) {
      auto *Index = ConstantInt::get(VecTy->getElementType(), Idx);
      auto *CmpSegment1 =
          ExtractElementInst::Create(Src1Value, Index, "", RaisedBB);
      auto *CmpSegment2 =
          ExtractElementInst::Create(Src2Value, Index, "", RaisedBB);
      auto *CmpInst = new ICmpInst(*RaisedBB, CmpPred, CmpSegment1, CmpSegment2,
                                   "cmp_segment");
      auto *SelectInst = SelectInst::Create(CmpInst, CmpSegment1, CmpSegment2,
                                            "min_max_segment", RaisedBB);
      // don't insert last instruction as that will be done after the switch
      // statement
      if (Idx != VecTy->getNumElements() - 1) {
        Result =
            InsertElementInst::Create(Result, SelectInst, Index, "", RaisedBB);
      } else {
        Result = InsertElementInst::Create(Result, SelectInst, Index);
      }
    }

    BinOpInst = (Instruction *)Result;
  } break;
  case X86::UNPCKLPDrm:
  case X86::UNPCKLPSrm: {
    assert(DestValue != nullptr &&
           "Encountered instruction with undefined register");

    unsigned int SegmentSz;
    if (MI.getOpcode() == X86::UNPCKLPDrm) {
      SegmentSz = 64;
    } else {
      SegmentSz = 32;
    }

    LLVMContext &Ctx(MF.getFunction().getContext());
    FixedVectorType *VecTy =
        FixedVectorType::get(Type::getIntNTy(Ctx, SegmentSz), 128 / SegmentSz);

    Value *Src1Value = new BitCastInst(DestValue, VecTy, "", RaisedBB);
    Value *Src2Value = new BitCastInst(LoadValue, VecTy, "", RaisedBB);

    Value *Result = ConstantInt::get(VecTy, 0);
    for (unsigned int Idx = 0; Idx < VecTy->getNumElements(); ++Idx) {
      auto *DstIndex = ConstantInt::get(VecTy->getElementType(), Idx);
      auto *SrcIndex = ConstantInt::get(VecTy->getElementType(), Idx >> 1);

      Value *SrcVal;
      if (Idx % 2 == 0) {
        SrcVal = Src1Value;
      } else {
        SrcVal = Src2Value;
      }

      auto *ExtractInst =
          ExtractElementInst::Create(SrcVal, SrcIndex, "", RaisedBB);
      // don't insert last instruction as that will be done after the switch
      // statement
      if (Idx != VecTy->getNumElements() - 1) {
        Result = InsertElementInst::Create(Result, ExtractInst, DstIndex, "",
                                           RaisedBB);
      } else {
        Result = InsertElementInst::Create(Result, ExtractInst, DstIndex);
      }
    }

    BinOpInst = (Instruction *)Result;
  } break;
  case X86::PSHUFDmi: {
    // Get index of memory reference in the instruction.
    int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
    // The index of the memory reference operand should be 1
    assert(MemoryRefOpIndex == 1 &&
           "Unexpected memory reference operand index in imul instruction");
    const MachineOperand &SecondSourceOp =
        MI.getOperand(MemoryRefOpIndex + X86::AddrNumOperands);
    // Second source should be an immediate.
    assert(SecondSourceOp.isImm() &&
           "Expect immediate operand in imul instruction");

    LLVMContext &Ctx(MF.getFunction().getContext());
    FixedVectorType *VecTy = FixedVectorType::get(Type::getInt32Ty(Ctx), 4);
    Value *SrcOpValue =
        getRaisedValues()->reinterpretSSERegValue(LoadValue, VecTy, RaisedBB);

    uint8_t ImmValue = (uint8_t)SecondSourceOp.getImm();
    Value *Result = ConstantInt::get(VecTy, 0);
    for (unsigned Idx = 0; Idx < VecTy->getNumElements(); ++Idx) {
      auto *DstIndex = ConstantInt::get(Type::getInt32Ty(Ctx), Idx);
      auto *SrcIndex = ConstantInt::get(Type::getInt32Ty(Ctx),
                                        (ImmValue >> (2 * Idx) & 0b11));

      auto *ExtractInst =
          ExtractElementInst::Create(SrcOpValue, SrcIndex, "", RaisedBB);
      if (Idx != VecTy->getNumElements() - 1) {
        Result = InsertElementInst::Create(Result, ExtractInst, DstIndex, "",
                                           RaisedBB);
      } else {
        Result = InsertElementInst::Create(Result, ExtractInst, DstIndex);
      }
    }

    BinOpInst = dyn_cast<Instruction>(Result);
  } break;
  default:
    assert(false && "Unhandled binary op mem to reg instruction ");
  }
  // Add instruction to block
  getRaisedValues()->setInstMetadataRODataIndex(LoadValue, BinOpInst);
  RaisedBB->getInstList().push_back(BinOpInst);

  // Clear EFLAGS if any
  int MMBNo = MI.getParent()->getNumber();
  for (auto F : ClearedEFlags)
    raisedValues->setEflagBoolean(F, MMBNo, false);

  // Test and set affected flags
  for (auto Flag : AffectedEFlags)
    raisedValues->testAndSetEflagSSAValue(Flag, MI, BinOpInst);

  // Update PhysReg to Value map
  raisedValues->setPhysRegSSAValue(DestPReg, MI.getParent()->getNumber(),
                                   BinOpInst);
  return true;
}

bool X86MachineInstructionRaiser::raiseLoadIntToFloatRegInstr(
    const MachineInstr &MI, Value *MemRefValue) {

  const unsigned int Opcode = MI.getOpcode();
  const MCInstrDesc &MIDesc = MI.getDesc();
  // Get index of memory reference in the instruction.
  int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
  assert(MemoryRefOpIndex == 0 && "Expect memory operand of floating-point "
                                  "load instruction at index 0");
  assert(MIDesc.getNumDefs() == 0 &&
         "Expect no defs in floating-point load instruction");

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  X86AddressMode MemRef = llvm::getAddressFromInstr(&MI, MemoryRefOpIndex);
  uint64_t BaseSupReg = find64BitSuperReg(MemRef.Base.Reg);
  bool IsPCRelMemRef = (BaseSupReg == X86::RIP);

  // Load the value from memory location of memRefValue.
  // memRefVal is either an AllocaInst (stack access) or GlobalValue (global
  // data access) or an effective address value.
  assert((isa<AllocaInst>(MemRefValue) || isEffectiveAddrValue(MemRefValue) ||
          isa<GlobalValue>(MemRefValue) || isa<ConstantExpr>(MemRefValue) ||
          MemRefValue->getType()->isPointerTy()) &&
         "Unexpected type of memory reference in FPU load op instruction");

  LLVMContext &Ctx(MF.getFunction().getContext());
  if (IsPCRelMemRef) {
    // If it is a PC-relative mem ref, memRefValue is a
    // global value loaded from PC-relative memory location. If it is a
    // derived type value, get its element pointer.
    Type *MemRefValueTy = MemRefValue->getType();
    if (!MemRefValueTy->isFloatingPointTy()) {
      assert(MemRefValueTy->isPointerTy() &&
             "Unhandled non-pointer type found while attempting to push value "
             "to FPU register stack.");
      Type *MemRefValPtrElementTy = getPointerElementType(MemRefValue);
      switch (MemRefValPtrElementTy->getTypeID()) {
      case Type::ArrayTyID: {
        // Make sure the array element type is integer or floating point type.
        Type *ArrElemTy = MemRefValPtrElementTy->getArrayElementType();
        assert((ArrElemTy->isIntegerTy() || ArrElemTy->isFloatingPointTy()) &&
               "Unexpected type of data referenced in FPU register stack "
               "load instruction");
        // Get the element
        Value *IndexOne = ConstantInt::get(Ctx, APInt(32, 1));
        Instruction *GetElem = GetElementPtrInst::CreateInBounds(
            ArrElemTy, MemRefValue, {IndexOne, IndexOne}, "", RaisedBB);
        MemRefValue = GetElem;
      } break;
      // Primitive types that need not be reached into.
      case Type::IntegerTyID:
        break;
      default: {
        assert(false && "Encountered value with type whose cast to float is "
                        "not yet handled");
      } break;
      }
    }
  }
  // If it is non-pointer value, convert it to a pointer type.
  if (!MemRefValue->getType()->isPointerTy()) {
    PointerType *PtrTy = MemRefValue->getType()->getPointerTo(0);
    Instruction *CInst = CastInst::Create(
        CastInst::getCastOpcode(MemRefValue, true, PtrTy, true), MemRefValue,
        PtrTy);
    RaisedBB->getInstList().push_back(CInst);
    MemRefValue = CInst;
  }
  assert(MemRefValue->getType()->isPointerTy() &&
         "Pointer type expected in load instruction");
  // Load the value from memory location
  auto MemAlignment =
      Align(getPointerElementType(MemRefValue)->getPrimitiveSizeInBits() / 8);
  LoadInst *LdInst =
      new LoadInst(getPointerElementType(MemRefValue), MemRefValue, "", false,
                   MemAlignment, RaisedBB);

  switch (Opcode) {
  default: {
    assert(false && "Unhandled load floating-point register instruction");
  } break;
  case X86::ILD_F32m:
  case X86::ILD_F64m: {
    Type *FloatTy = Type::getFloatTy(Ctx);
    assert(LdInst->getType()->isIntegerTy() &&
           "Unexpected non-integter type of source in fild instruction");
    // Cast source to float
    Instruction *CInst = CastInst::Create(
        CastInst::getCastOpcode(LdInst, true, FloatTy, true), LdInst, FloatTy);
    RaisedBB->getInstList().push_back(CInst);
    // Push value to top of FPU register stack
    pushFPURegisterStack(CInst);
  } break;
  case X86::LD_F32m:
  case X86::LD_F64m: {
    Type *FloatTy = Type::getFloatTy(Ctx);
    // Cast source to float
    Instruction *CInst = CastInst::Create(
        CastInst::getCastOpcode(LdInst, true, FloatTy, true), LdInst, FloatTy);
    RaisedBB->getInstList().push_back(CInst);
    // Push value to top of FPU register stack
    pushFPURegisterStack(CInst);
  }
  }
  return true;
}

bool X86MachineInstructionRaiser::raiseStoreIntToFloatRegInstr(
    const MachineInstr &MI, Value *MemRefValue) {

  const unsigned int Opcode = MI.getOpcode();
  const MCInstrDesc &MIDesc = MI.getDesc();
  // Get index of memory reference in the instruction.
  int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
  assert(MemoryRefOpIndex == 0 && "Expect memory operand of floating-point "
                                  "load instruction at index 0");
  assert(MIDesc.getNumDefs() == 0 &&
         "Expect no defs in floating-point load instruction");

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  X86AddressMode MemRef = llvm::getAddressFromInstr(&MI, MemoryRefOpIndex);
  uint64_t BaseSupReg = find64BitSuperReg(MemRef.Base.Reg);
  bool IsPCRelMemRef = (BaseSupReg == X86::RIP);

  // Load the value from memory location of memRefValue.
  // memRefVal is either an AllocaInst (stack access) or GlobalValue (global
  // data access) or an effective address value.
  assert((isa<AllocaInst>(MemRefValue) || isEffectiveAddrValue(MemRefValue) ||
          isa<GlobalValue>(MemRefValue) ||
          MemRefValue->getType()->isPointerTy()) &&
         "Unexpected type of memory reference in FPU store op instruction");

  LLVMContext &Ctx(MF.getFunction().getContext());
  if (IsPCRelMemRef) {
    // If it is a PC-relative mem ref, MemRefValue is a global value loaded
    // from PC-relative memory location. If it is a derived type value, get
    // its element pointer.
    assert(isa<GlobalValue>(MemRefValue) && "MemRefValue is not GlobalValue");
    Type *MemRefValueTy = MemRefValue->getType();
    if (!MemRefValueTy->isFloatingPointTy()) {
      assert(MemRefValueTy->isPointerTy() &&
             "Unhandled non-pointer type found while attempting to load value "
             "from FPU register stack.");
      Type *MemRefValPtrElementTy = getPointerElementType(MemRefValue);
      switch (MemRefValPtrElementTy->getTypeID()) {
      case Type::ArrayTyID: {
        assert(MemRefValPtrElementTy->getArrayNumElements() == 1 &&
               "Unexpected number of array elements in value being cast to "
               "float");
        // Make sure the array element type is integer or floating point
        // type.
        Type *ArrElemTy = MemRefValPtrElementTy->getArrayElementType();
        assert((ArrElemTy->isIntegerTy() || ArrElemTy->isFloatingPointTy()) &&
               "Unexpected type of data referenced in FPU register stack "
               "store instruction");
        // Get the element
        Value *IndexOne = ConstantInt::get(Ctx, APInt(32, 1));
        Instruction *GetElem = GetElementPtrInst::CreateInBounds(
            ArrElemTy, MemRefValue, {IndexOne, IndexOne}, "", RaisedBB);
        MemRefValue = GetElem;
      } break;
      // Primitive types that need not be reached into.
      case Type::IntegerTyID:
        break;
      default: {
        assert(false && "Encountered value with type whose cast to float is "
                        "not yet handled");
      } break;
      }
    }
  }
  // Ensure MemRefValue is of pointer type
  Type *MRTy = MemRefValue->getType();
  if (!MRTy->isPointerTy()) {
    PointerType *PtrTy = MRTy->getPointerTo(0);
    Instruction *CInst = CastInst::Create(
        CastInst::getCastOpcode(MemRefValue, true, PtrTy, true), MemRefValue,
        PtrTy);
    RaisedBB->getInstList().push_back(CInst);
    MemRefValue = CInst;
  }

  switch (Opcode) {
  default: {
    assert(false && "Unhandled store floating-point register instruction");
  } break;
  case X86::ST_FP32m:
  case X86::ST_FP64m: {
    Value *ST0Val = topFPURegisterStack();
    Type *SrcTy = ST0Val->getType();
    // The value in ST0 is converted to single-precision or double precision
    // floating-point format. So, cast the MemRefValue to the PointerType of
    // SrcTy.
    Type *DestElemTy = getPointerElementType(MemRefValue);
    if (DestElemTy != SrcTy) {
      PointerType *SrcPtrTy = SrcTy->getPointerTo(0);
      Instruction *CInst = CastInst::Create(
          CastInst::getCastOpcode(MemRefValue, true, SrcPtrTy, true),
          MemRefValue, SrcPtrTy);
      RaisedBB->getInstList().push_back(CInst);
      MemRefValue = CInst;
    }
    // Create the store
    new StoreInst(ST0Val, MemRefValue, RaisedBB);

    // Pop value to top of FPU register stack
    popFPURegisterStack();
  }
  }
  return true;
}

bool X86MachineInstructionRaiser::raiseMoveFromMemInstr(const MachineInstr &MI,
                                                        Value *MemRefValue) {
  const unsigned int Opcode = MI.getOpcode();
  const MCInstrDesc &MIDesc = MI.getDesc();
  unsigned LoadOpIndex = 0;
  // Get index of memory reference in the instruction.
  int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
  assert(MemoryRefOpIndex == 1 &&
         "Expect memory operand of a mem move instruction at index 1");
  assert(MIDesc.getNumDefs() == 1 && MI.getOperand(LoadOpIndex).isReg() &&
         "Expect store operand register target");

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  X86AddressMode MemRef = llvm::getAddressFromInstr(&MI, MemoryRefOpIndex);
  uint64_t BaseSupReg = find64BitSuperReg(MemRef.Base.Reg);
  bool IsPCRelMemRef = (BaseSupReg == X86::RIP);
  const MachineOperand &LoadOp = MI.getOperand(LoadOpIndex);
  Register LoadPReg = LoadOp.getReg();
  assert(Register::isPhysicalRegister(LoadPReg) &&
         "Expect destination to be a physical register in move from mem "
         "instruction");

  // Load the value from memory location of memRefValue.
  // memRefVal is either an AllocaInst (stack access), GlobalValue (global
  // data access), an effective address value, element pointer or select
  // instruction.
  assert((isa<AllocaInst>(MemRefValue) || isEffectiveAddrValue(MemRefValue) ||
          isa<GlobalValue>(MemRefValue) || isa<SelectInst>(MemRefValue) ||
          isa<GetElementPtrInst>(MemRefValue) ||
          MemRefValue->getType()->isPointerTy()) &&
         "Unexpected type of memory reference in binary mem op instruction");

  // Assume that MemRefValue represents a memory reference location and hence
  // needs to be loaded from.
  bool LoadFromMemrefValue = true;
  // Following are the exceptions when MemRefValue needs to be considered as
  // memory content and not as memory reference.
  if (IsPCRelMemRef) {
    // If it is a PC-relative global variable with an initializer, it is memory
    // content and should not be loaded from.
    if (auto *GV = dyn_cast<GlobalVariable>(MemRefValue))
      LoadFromMemrefValue = !(GV->hasInitializer());
    // If it is not a PC-relative constant expression accessed using
    // GetElementPtrInst, it is memory content and should not be loaded from.
    else {
      const ConstantExpr *CExpr = dyn_cast<ConstantExpr>(MemRefValue);
      if (CExpr != nullptr) {
        LoadFromMemrefValue =
            (CExpr->getOpcode() == Instruction::GetElementPtr);
      }
    }
  }

  if (LoadFromMemrefValue) {
    // If it is an effective address value or a select instruction, convert it
    // to a pointer to load register type.
    Type *LdTy = getPhysRegOperandType(MI, LoadOpIndex);
    PointerType *PtrTy = PointerType::get(LdTy, 0);
    if ((isEffectiveAddrValue(MemRefValue)) || isa<SelectInst>(MemRefValue)) {
      IntToPtrInst *ConvIntToPtr = new IntToPtrInst(MemRefValue, PtrTy);
      // Set or copy rodata metadata, if any
      getRaisedValues()->setInstMetadataRODataIndex(MemRefValue, ConvIntToPtr);
      RaisedBB->getInstList().push_back(ConvIntToPtr);
      MemRefValue = ConvIntToPtr;
    }
    assert(MemRefValue->getType()->isPointerTy() &&
           "Pointer type expected in load instruction");
    // Cast the pointer to match the size of memory being accessed by the
    // instruction, as needed.
    MemRefValue = getRaisedValues()->castValue(MemRefValue, PtrTy, RaisedBB);
    // Load the value from memory location
    LoadInst *LdInst =
        new LoadInst(LdTy, MemRefValue, "memload", false, Align());
    LdInst = getRaisedValues()->setInstMetadataRODataContent(LdInst);
    RaisedBB->getInstList().push_back(LdInst);

    LLVMContext &Ctx(MF.getFunction().getContext());
    Type *MemTy = nullptr;
    Type *ExtTy = nullptr;
    switch (Opcode) {
    default:
      raisedValues->setPhysRegSSAValue(LoadPReg, MI.getParent()->getNumber(),
                                       LdInst);
      break;
    case X86::MOVSX64rm32:
      ExtTy = Type::getInt64Ty(Ctx);
      MemTy = Type::getInt32Ty(Ctx);
      break;
    case X86::MOVZX64rm16:
    case X86::MOVSX64rm16:
      ExtTy = Type::getInt64Ty(Ctx);
      MemTy = Type::getInt16Ty(Ctx);
      break;
    case X86::MOVZX64rm8:
    case X86::MOVSX64rm8:
      ExtTy = Type::getInt64Ty(Ctx);
      MemTy = Type::getInt8Ty(Ctx);
      break;
    case X86::MOVZX32rm8:
    case X86::MOVZX32rm8_NOREX:
    case X86::MOVSX32rm8:
      ExtTy = Type::getInt32Ty(Ctx);
      MemTy = Type::getInt8Ty(Ctx);
      break;
    case X86::MOVZX32rm16:
    case X86::MOVSX32rm16:
      ExtTy = Type::getInt32Ty(Ctx);
      MemTy = Type::getInt16Ty(Ctx);
      break;
    case X86::MOVZX16rm8:
    case X86::MOVSX16rm8:
      ExtTy = Type::getInt16Ty(Ctx);
      MemTy = Type::getInt8Ty(Ctx);
      break;
    case X86::MOVZX16rm16:
    case X86::MOVSX16rm16:
      ExtTy = Type::getInt16Ty(Ctx);
      MemTy = Type::getInt16Ty(Ctx);
      break;
    }
    // Decide based on opcode value and not opcode name??
    bool IsSextInst = instrNameStartsWith(MI, "MOVSX");
    bool IsZextInst = instrNameStartsWith(MI, "MOVZX");

    if (IsSextInst || IsZextInst) {
      assert(((ExtTy != nullptr) && (MemTy != nullptr)) &&
             "Unhandled move from memory instruction");

      // Load value of type memTy
      Value *CInstValue = getRaisedValues()->castValue(LdInst, MemTy, RaisedBB);

      Instruction *ExtInst = nullptr;

      // Now extend the value accordingly
      if (IsSextInst) {
        // Sign extend
        ExtInst = new SExtInst(CInstValue, ExtTy);
      } else {
        // Zero extend
        ExtInst = new ZExtInst(CInstValue, ExtTy);
      }
      Instruction *CInst = dyn_cast<Instruction>(CInstValue);
      assert(CInst != nullptr && "Expect cast value to be and instruction");
      ExtInst->copyMetadata(*CInst);

      RaisedBB->getInstList().push_back(ExtInst);
      // Update PhysReg to Value map
      raisedValues->setPhysRegSSAValue(LoadPReg, MI.getParent()->getNumber(),
                                       ExtInst);
    } // else PhysReg is already updated in the default case of the above switch
      // statement
  } else {
    // memRefValue already represents the global value loaded from
    // PC-relative memory location. It is incorrect to generate an
    // additional load of this value. It should be directly used.
    raisedValues->setPhysRegSSAValue(LoadPReg, MI.getParent()->getNumber(),
                                     MemRefValue);
  }

  AlocType srcAloc;

  if (isa<AllocaInst>(MemRefValue)) {
    // printf("alloca\n");
    int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
    X86AddressMode MemRef = llvm::getAddressFromInstr(&MI, MemoryRefOpIndex);
    srcAloc = AlocType(AlocType::LocalMemLocTy, MemRef.Disp);
  } else if (isEffectiveAddrValue(MemRefValue)) {

  } else if (isa<GlobalValue>(MemRefValue)) {

  } else if (isa<SelectInst>(MemRefValue)) {

  } else if (isa<GetElementPtrInst>(MemRefValue)) {

  } else if (MemRefValue->getType()->isPointerTy()) {

  }     
  valueSetAnalysis->assignValueToSrc(AlocType(find64BitSuperReg(LoadPReg.id())), srcAloc);

  return true;
}

bool X86MachineInstructionRaiser::raiseMoveToMemInstr(const MachineInstr &MI,
                                                      Value *MemRefVal) {
  unsigned int SrcOpIndex = getMemoryRefOpIndex(MI) + X86::AddrNumOperands;

  // If the source operand is neither an immediate nor a register, check for an
  // implicit register use
  if (!MI.getOperand(SrcOpIndex).isImm() &&
      !MI.getOperand(SrcOpIndex).isReg()) {
    MCInstrDesc Desc = MI.getDesc();
    assert(Desc.getNumImplicitUses() == 1 && "Expected one implicit use");
    for (unsigned int CurIdx = SrcOpIndex + 1; CurIdx < MI.getNumOperands();
         ++CurIdx) {
      if (MI.getOperand(CurIdx).isUse() && MI.getOperand(CurIdx).isReg()) {
        SrcOpIndex = CurIdx;
        break;
      }
    }
  }

  const MachineOperand &SrcOp = MI.getOperand(SrcOpIndex);

  // Is this a mov instruction?
  bool IsMovInst = instrNameStartsWith(MI, "MOV");

  assert((SrcOp.isImm() || SrcOp.isReg()) &&
         "Register or immediate value source expected in a move to mem "
         "instruction");
  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  Value *SrcValue = nullptr;
  Type *SrcOpTy = nullptr;

  // If Source op is immediate, create a constant int value
  // of type memory location.
  if (SrcOp.isImm()) {
    SrcOpTy = getImmOperandType(MI, SrcOpIndex);
    int64_t SrcImm = SrcOp.getImm();
    if (IsMovInst) {
      if (SrcImm > 0) {
        // Check if the immediate value corresponds to a global variable.
        Value *GV = getGlobalVariableValueAt(MI, SrcImm);
        if (GV != nullptr)
          SrcValue = GV;
        else {
          SrcValue = ConstantInt::get(SrcOpTy, SrcImm);
        }
      } else
        SrcValue = ConstantInt::get(SrcOpTy, SrcImm);
    } else
      SrcValue = ConstantInt::get(SrcOpTy, SrcImm);
  } else {
    // If it is not an immediate value, get source value
    SrcValue = getRegOperandValue(MI, SrcOpIndex);
    SrcOpTy = getPhysRegOperandType(MI, SrcOpIndex);
  }
  assert(SrcValue != nullptr &&
         "Unable to get source value while raising move to mem instruction");
  // Load the value from memory location of memRefValue.
  // memRefVal is either an AllocaInst (stack access) or GlobalValue (global
  // data access) or an effective address value.
  assert((isa<AllocaInst>(MemRefVal) || isEffectiveAddrValue(MemRefVal) ||
          isa<GlobalValue>(MemRefVal) || isa<GetElementPtrInst>(MemRefVal) ||
          MemRefVal->getType()->isPointerTy()) &&
         "Unexpected type of memory reference in mem-to-reg instruction");

  // If memory reference is not a pointer type, cast it to a pointer
  Type *DstMemTy = MemRefVal->getType();
  LLVMContext &Ctx(MF.getFunction().getContext());
  auto Opc = MI.getOpcode();
  unsigned int MemSzInBits = getInstructionMemOpSize(Opc) * 8;
  if (!DstMemTy->isPointerTy()) {
    // Cast it as pointer to SrcOpTy
    PointerType *PtrTy = nullptr;
    if (isSSE2Instruction(Opc)) {
      assert(MI.getOperand(SrcOpIndex).isReg() &&
             "Unexpected non-SSE register operand");
      auto SSERegSzInBits = getRegisterInfo()->getRegSizeInBits(
          MI.getOperand(SrcOpIndex).getReg(), machineRegInfo);
      PtrTy = getRaisedValues()
                  ->getSSEInstructionType(MI, SSERegSzInBits, Ctx)
                  ->getPointerTo();
    } else {
      assert(MemSzInBits > 0 && "Unexpected memory access size of instruction");
      PtrTy = Type::getIntNPtrTy(Ctx, MemSzInBits);
    }
    IntToPtrInst *ConvIntToPtr = new IntToPtrInst(MemRefVal, PtrTy);
    RaisedBB->getInstList().push_back(ConvIntToPtr);
    MemRefVal = ConvIntToPtr;
  }

  // This instruction moves a source value to memory. So, if the types of
  // the source value and that of the memory pointer element are not the
  // same as that of the store size of the instruction, cast them as needed.
  // Consider store value type to be the same as source value type, by default.
  Type *StoreTy = SrcValue->getType();
  if (SrcValue->getType()->isIntegerTy())
    StoreTy = Type::getIntNTy(Ctx, MemSzInBits);
  else if (SrcValue->getType()->isFloatingPointTy()) {
    if (MemSzInBits == 32)
      StoreTy = Type::getFloatTy(Ctx);
    else if (MemSzInBits == 64)
      StoreTy = Type::getDoubleTy(Ctx);
  }

  bool SignExtend = MI.getOpcode() == X86::MOV64mi32;
  // Cast SrcValue and MemRefVal as needed.
  MemRefVal = getRaisedValues()->castValue(MemRefVal, StoreTy->getPointerTo(),
                                           RaisedBB);
  SrcValue =
      getRaisedValues()->castValue(SrcValue, StoreTy, RaisedBB, SignExtend);

  if (!IsMovInst) {
    // If this is not an instruction that just moves SrcValue, load from memory,
    // generate the instruction that performs the appropriate operation.

    // Load the value from memory location
    auto PtrAlign =
        MemRefVal->getPointerAlignment(MR->getModule()->getDataLayout());
    LoadInst *LdInst =
        new LoadInst(StoreTy, MemRefVal, "", false, PtrAlign, RaisedBB);
    assert((LdInst != nullptr) && "Memory value expected to be loaded while "
                                  "raising binary mem op instruction");
    assert((SrcValue != nullptr) && "Source value expected to be loaded while "
                                    "raising binary mem op instruction");

    std::set<unsigned> AffectedEFlags;

    Instruction *BinOpInst = nullptr;

    // Generate instruction to perform appropriate operation to produce the
    // value be stored.
    switch (MI.getOpcode()) {
    case X86::ADD8mi:
    case X86::ADD8mi8:
    case X86::ADD8mr:
    case X86::ADD16mi:
    case X86::ADD16mi8:
    case X86::ADD16mr:
    case X86::ADD32mi:
    case X86::ADD32mi8:
    case X86::ADD32mr:
    case X86::ADD64mi8:
    case X86::ADD64mi32:
    case X86::ADD64i32:
    case X86::ADD64mr:
    case X86::INC8m:
    case X86::INC16m:
    case X86::INC32m:
    case X86::INC64m: {
      // Generate Add instruction
      BinOpInst = BinaryOperator::CreateAdd(LdInst, SrcValue);

      AffectedEFlags.insert(EFLAGS::PF);
    } break;
    case X86::AND8mi:
    case X86::AND8mi8:
    case X86::AND8mr:
    case X86::AND16mi:
    case X86::AND16mi8:
    case X86::AND16mr:
    case X86::AND32mi:
    case X86::AND32mi8:
    case X86::AND32mr:
    case X86::AND64mi8:
    case X86::AND64mi32:
    case X86::AND64mr: {
      BinOpInst = BinaryOperator::CreateAnd(LdInst, SrcValue);

      AffectedEFlags.insert(EFLAGS::PF);
    } break;
    case X86::OR8mi:
    case X86::OR8mi8:
    case X86::OR8mr:
    case X86::OR16mi:
    case X86::OR16mi8:
    case X86::OR16mr:
    case X86::OR32mi:
    case X86::OR32mi8:
    case X86::OR32mr:
    case X86::OR64mi8:
    case X86::OR64mi32:
    case X86::OR64mr: {
      BinOpInst = BinaryOperator::CreateOr(LdInst, SrcValue);

      AffectedEFlags.insert(EFLAGS::PF);
    } break;
    case X86::XOR8mi:
    case X86::XOR8mi8:
    case X86::XOR8mr:
    case X86::XOR16mi:
    case X86::XOR16mi8:
    case X86::XOR16mr:
    case X86::XOR32mi:
    case X86::XOR32mi8:
    case X86::XOR32mr:
    case X86::XOR64mi8:
    case X86::XOR64mi32:
    case X86::XOR64mr: {
      BinOpInst = BinaryOperator::CreateXor(LdInst, SrcValue);

      AffectedEFlags.insert(EFLAGS::PF);
    } break;
    case X86::DEC8m:
    case X86::DEC16m:
    case X86::DEC32m:
    case X86::DEC64m: {
      BinOpInst = BinaryOperator::CreateSub(LdInst, SrcValue);

      AffectedEFlags.insert(EFLAGS::PF);
    } break;
    case X86::SAR8mCL:
    case X86::SAR16mCL:
    case X86::SAR32mCL:
    case X86::SAR64mCL:
    case X86::SHL8mCL:
    case X86::SHL16mCL:
    case X86::SHL32mCL:
    case X86::SHL64mCL:
    case X86::SHR8mCL:
    case X86::SHR16mCL:
    case X86::SHR32mCL:
    case X86::SHR64mCL: {
      Type *LdInstTy = LdInst->getType();
      // SrcValue is masked to 5 bits (6 bits if 64-bit register)
      bool Is64Bit = (LdInstTy->getPrimitiveSizeInBits() == 64);
      Value *CountMask = Is64Bit ? ConstantInt::get(LdInstTy, 0x1f)
                                 : ConstantInt::get(LdInstTy, 0x3f);
      // Generate mask
      auto *CountValue = BinaryOperator::CreateAnd(SrcValue, CountMask,
                                                   "shift-cnt-msk", RaisedBB);

      if (instrNameStartsWith(MI, "SAR")) {
        BinOpInst = BinaryOperator::CreateAShr(LdInst, CountValue);
      } else if (instrNameStartsWith(MI, "SHL")) {
        BinOpInst = BinaryOperator::CreateShl(LdInst, CountValue);
      } else if (instrNameStartsWith(MI, "SHR")) {
        BinOpInst = BinaryOperator::CreateLShr(LdInst, CountValue);
      } else {
        llvm_unreachable("unhandled shift instruction");
      }
    } break;
    case X86::SAR8mi:
    case X86::SAR16mi:
    case X86::SAR32mi:
    case X86::SAR64mi: {
      BinOpInst = BinaryOperator::CreateLShr(LdInst, SrcValue);
    } break;
    case X86::SHL8mi:
    case X86::SHL16mi:
    case X86::SHL32mi:
    case X86::SHL64mi: {
      BinOpInst = BinaryOperator::CreateShl(LdInst, SrcValue);
    } break;
    case X86::SHR8mi:
    case X86::SHR16mi:
    case X86::SHR32mi:
    case X86::SHR64mi: {
      BinOpInst = BinaryOperator::CreateLShr(LdInst, SrcValue);
    } break;
    default:
      assert(false && "Unhandled non-move mem op instruction");
    }

    RaisedBB->getInstList().push_back(BinOpInst);

    SrcValue = BinOpInst;

    // Test and set affected flags
    for (auto Flag : AffectedEFlags)
      raisedValues->testAndSetEflagSSAValue(Flag, MI, BinOpInst);
  }

  assert((SrcValue != nullptr) && "Unexpected null value to be stored while "
                                  "raising binary mem op instruction");
  // Store resultant value
  new StoreInst(SrcValue, MemRefVal, false, Align(), RaisedBB);

  // vsa
  // TODO: See if using the type of SrcValue can be used to determine
  // if it is const assignment or register
  // assignToConst, dest is memRefVal, src is SrcValue
  // isa<AllocaInst>(MemRefVal) || isEffectiveAddrValue(MemRefVal) ||
  //         isa<GlobalValue>(MemRefVal) || isa<GetElementPtrInst>(MemRefVal) ||
  //         MemRefVal->getType()->isPointerTy()
  AlocType destAloc = AlocType();
  if (isa<AllocaInst>(MemRefVal)) {
    // local
    int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
    X86AddressMode MemRef = llvm::getAddressFromInstr(&MI, MemoryRefOpIndex);
    destAloc = AlocType(AlocType::LocalMemLocTy, MemRef.Disp);

  } else if (isEffectiveAddrValue(MemRefVal)) {
    // local
  } else if (isa<GlobalValue>(MemRefVal) || isa<GetElementPtrInst>(MemRefVal) ||
          MemRefVal->getType()->isPointerTy()) {
    // atid = AlocType::AlocTypeID::GlobalMemLocTy;
  } else {
    // atid = AlocType::AlocTypeID::RegisterTy;
  }

  if (isa<Constant>(SrcValue)) {
    // printf("%s\n", x86InstrInfo->getName(MI.getOpcode()));
    valueSetAnalysis->assignValueConst(destAloc, SrcValue);
  } else if (isa<Instruction>(SrcValue)) {
    AlocType reg = AlocType(find64BitSuperReg(MI.getOperand(SrcOpIndex).getReg()));
    // printf("Found a register MOV\n");
    // printf("aloc type %d, aloc addr space %s\n", reg.getAlocTypeID(), x86RegisterInfo->getName(MI.getOperand(0).getReg()));
    SrcValue->dump();
    valueSetAnalysis->
      assignValueToSrc(destAloc, reg);
  }

  return true;
}

// load from memory, apply operation, store back to the same memory
bool X86MachineInstructionRaiser::raiseInplaceMemOpInstr(const MachineInstr &MI,
                                                         Value *MemRefVal) {
  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());
  LLVMContext &Ctx(MF.getFunction().getContext());

  unsigned int MemOpSize = getInstructionMemOpSize(MI.getOpcode());

  // Note that not instruction with memory operand loads from MemrefVal,
  // computes not operation on the loaded value and stores it back in the
  // location MemRegVal

  // Load the value from memory location of memRefValue.
  Type *SrcTy = MemRefVal->getType();
  Type *DataTy = Type::getIntNTy(Ctx, MemOpSize * 8);
  // Get the pointer type of data stored by the instruction
  Type *MemPtrTy = DataTy->getPointerTo();
  // If Cast the value to pointer type of size memAlignment
  if (!SrcTy->isPointerTy() || (SrcTy != DataTy)) {
    CastInst *CInst = CastInst::Create(
        CastInst::getCastOpcode(MemRefVal, false, MemPtrTy, false), MemRefVal,
        MemPtrTy);
    RaisedBB->getInstList().push_back(CInst);
    MemRefVal = CInst;
    SrcTy = MemRefVal->getType();
  }

  // Make sure the value is of pointer type.
  assert(SrcTy->isPointerTy() &&
         "Expect value of load instruction to be of pointer type");
  // Load the value from memory location
  // OpaquePointer hack
  Instruction *SrcValue =
      new LoadInst(DataTy, MemRefVal, "memload", false, Align(), RaisedBB);

  switch (MI.getOpcode()) {
  case X86::NOT16m:
  case X86::NOT32m:
  case X86::NOT64m:
  case X86::NOT8m:
    SrcValue = BinaryOperator::CreateNot(SrcValue);
    break;
  case X86::INC8m:
  case X86::INC16m:
  case X86::INC32m:
  case X86::INC64m:
    SrcValue = BinaryOperator::CreateAdd(
        SrcValue, ConstantInt::get(SrcValue->getType(), 1));
    break;
  default:
    assert(false && "Unhandled instruction type");
  }

  RaisedBB->getInstList().push_back(SrcValue);

  // Store the result back in MemRefVal
  new StoreInst(SrcValue, MemRefVal, false, Align(MemOpSize), RaisedBB);
  return true;
}

// Raise signed or unsigned division instruction with memory reference value
bool X86MachineInstructionRaiser::raiseDivideFromMemInstr(
    const MachineInstr &MI, Value *MemRefValue) {
  LLVMContext &Ctx(MF.getFunction().getContext());
  unsigned int MemoryRefOpIndex = getMemoryRefOpIndex(MI);
  Type *DestopTy =
      Type::getIntNTy(Ctx, getInstructionMemOpSize(MI.getOpcode()) * 8);

  Value *SrcValue =
      loadMemoryRefValue(MI, MemRefValue, MemoryRefOpIndex, DestopTy);
  return raiseDivideInstr(MI, SrcValue);
}

// Raise signed or unsigned division instruction with source operand with value
// SrcValue.
bool X86MachineInstructionRaiser::raiseDivideInstr(const MachineInstr &MI,
                                                   Value *SrcValue) {
  const MCInstrDesc &MIDesc = MI.getDesc();
  LLVMContext &Ctx(MF.getFunction().getContext());

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  // div uses AX or DX:AX or EDX:EAX or RDX:RAX registers as dividend and
  // stores the result in AX(AH:AL) or DX:AX or EDX:EAX or RDX:RAX.
  // Additionally, EFLAGS is an implicit def.
  bool Is8BOperand =
      (instrNameStartsWith(MI, "IDIV8") || instrNameStartsWith(MI, "DIV8"));

  // Determine if the instruction is a signed division or not
  bool IsSigned = instrNameStartsWith(MI, "IDIV");

  // Handle 8-bit division differentrly as it does not use register pairs.
  if (Is8BOperand) {
    assert(MIDesc.getNumImplicitUses() == 1 &&
           MIDesc.getNumImplicitDefs() == 3 &&
           MIDesc.hasImplicitDefOfPhysReg(X86::EFLAGS) &&
           "Unexpected number of implicit uses and defs in div instruction");
    // Ensure AX is the implicit use operand
    MCPhysReg UseDefReg = MIDesc.ImplicitUses[0];
    assert((UseDefReg == X86::AX) &&
           "Expected AX operand of div instruction not found");
    LLVMContext &Context(MF.getFunction().getContext());
    // This is the instruction result type
    Type *ResultType = Type::getInt16Ty(Context);
    Value *DividendValue = getPhysRegValue(MI, UseDefReg);
    // Cast SrcValue and DividendValue to ResultType
    Value *SrcValue16b =
        getRaisedValues()->castValue(SrcValue, ResultType, RaisedBB, IsSigned);
    Value *DividendValue16b = getRaisedValues()->castValue(
        DividendValue, ResultType, RaisedBB, IsSigned);

    // quotient
    auto DivOp = (IsSigned) ? Instruction::SDiv : Instruction::UDiv;
    Instruction *Quotient = BinaryOperator::Create(
        DivOp, DividendValue16b, SrcValue16b, "div8_q", RaisedBB);

    // remainder
    auto RemOp = (IsSigned) ? Instruction::SRem : Instruction::URem;
    Instruction *Remainder = BinaryOperator::Create(
        RemOp, DividendValue16b, SrcValue16b, "div8_r", RaisedBB);

    // Construct a value of ResultType holding quotient in AL and remainder in
    // AH. Clear high-byte of Quotient
    Instruction *ALValue = BinaryOperator::CreateAnd(
        Quotient, ConstantInt::get(ResultType, 0xff), "div8_al", RaisedBB);
    // Shift Remainder by 8
    Instruction *AHValue = BinaryOperator::CreateShl(
        Remainder, ConstantInt::get(ResultType, 8), "div8_ah", RaisedBB);
    // Construct AX value
    Instruction *AXValue =
        BinaryOperator::CreateOr(ALValue, AHValue, "div8_ax", RaisedBB);
    // CF, OF, SF, ZF, AF and PF flags are undefined. So, no need to generate
    // code to compute any of the status flags. Update value of UseDefReg
    raisedValues->setPhysRegSSAValue(UseDefReg, MI.getParent()->getNumber(),
                                     AXValue);

  } else {
    assert(MIDesc.getNumImplicitUses() == 2 &&
           MIDesc.getNumImplicitDefs() == 3 &&
           MIDesc.hasImplicitDefOfPhysReg(X86::EFLAGS) &&
           "Unexpected number of implicit uses and defs in div instruction");
    MCPhysReg UseDefReg0 = MIDesc.ImplicitUses[0];
    MCPhysReg UseDefReg1 = MIDesc.ImplicitUses[1];
    assert((UseDefReg0 == MIDesc.ImplicitDefs[0]) &&
           (UseDefReg1 == MIDesc.ImplicitDefs[1]) &&
           "Unexpected use/def registers in div instruction");

    Value *DividendLowBytes = getPhysRegValue(MI, UseDefReg0);
    Value *DividendHighBytes = getPhysRegValue(MI, UseDefReg1);
    if ((DividendLowBytes == nullptr) || (DividendHighBytes == nullptr))
      return false;

    // Divisor is srcValue.
    // Create a Value representing the dividend.
    assert((DividendLowBytes->getType() == DividendHighBytes->getType()) &&
           "Unexpected types of dividend registers in div instruction");
    unsigned int UseDefRegSize =
        DividendLowBytes->getType()->getScalarSizeInBits();
    // Generate the following code
    // %h = lshl DividendHighBytes, UseDefRegSize
    // %f = or %h, DividendLowBytes
    // %quo = (s/u)div %f, srcValue
    // %rem = (s/u)rem %f, srcValue
    // UseDef_0 = %quo
    // UseDef_1 = %rem

    // Logical Shift left DividendHighBytes by n-bits (where n is the size of
    // UseDefRegSize) to get the high bytes and set DefReg_1 to the resulting
    // value.
    // DoubleTy type is of type twice the use reg size
    Type *DoubleTy = Type::getIntNTy(Ctx, UseDefRegSize * 2);
    Value *ShiftAmountVal =
        ConstantInt::get(DoubleTy, UseDefRegSize, false /* isSigned */);
    // Cast DividendHighBytes and DividendLowBytes to types with double the
    // size.
    Value *DividendLowBytesDT = getRaisedValues()->castValue(
        DividendLowBytes, DoubleTy, RaisedBB, IsSigned);

    Value *DividendHighBytesDT = getRaisedValues()->castValue(
        DividendHighBytes, DoubleTy, RaisedBB, IsSigned);

    Instruction *LShlInst = BinaryOperator::CreateNUWShl(
        DividendHighBytesDT, ShiftAmountVal, "div_hb_ls", RaisedBB);

    // Combine the dividend values to get full dividend.
    // or instruction
    Instruction *FullDividend = BinaryOperator::CreateOr(
        LShlInst, DividendLowBytesDT, "dividend", RaisedBB);

    // Cast divisor (srcValue) to double type
    Value *SrcValueDT =
        getRaisedValues()->castValue(SrcValue, DoubleTy, RaisedBB, IsSigned);
    // quotient
    auto DivOp = (IsSigned) ? Instruction::SDiv : Instruction::UDiv;
    Instruction *QuotientDT = BinaryOperator::Create(
        DivOp, FullDividend, SrcValueDT, "div_q", RaisedBB);

    // Cast Quotient back to UseDef reg value type
    Value *Quotient = getRaisedValues()->castValue(
        QuotientDT, DividendLowBytes->getType(), RaisedBB, IsSigned);

    // Update ssa val of UseDefReg_0
    raisedValues->setPhysRegSSAValue(UseDefReg0, MI.getParent()->getNumber(),
                                     Quotient);

    // remainder
    auto RemOp = (IsSigned) ? Instruction::SRem : Instruction::URem;
    Instruction *RemainderDT = BinaryOperator::Create(
        RemOp, FullDividend, SrcValueDT, "div_r", RaisedBB);

    // Cast RemainderDT back to UseDef reg value type
    CastInst *Remainder = CastInst::Create(
        CastInst::getCastOpcode(RemainderDT, IsSigned,
                                DividendHighBytes->getType(), IsSigned),
        RemainderDT, DividendHighBytes->getType(), "", RaisedBB);

    // CF, OF, SF, ZF, AF and PF flags are undefined. So, no need to generate
    // code to compute any of the status flags. Update value of UseDefReg_1
    raisedValues->setPhysRegSSAValue(UseDefReg1, MI.getParent()->getNumber(),
                                     Remainder);
  }

  return true;
}

bool X86MachineInstructionRaiser::raiseBitTestMachineInstr(
    const MachineInstr &MI, Value *MemRefValue, bool IsMemBT) {
  // Get index of memory reference in the instruction.
  int MemRefOpIndex = getMemoryRefOpIndex(MI);
  int MBBNo = MI.getParent()->getNumber();
  assert((((MemRefOpIndex != -1) && IsMemBT) ||
          ((MemRefOpIndex == -1) && !IsMemBT)) &&
         "Inconsistent memory reference operand information specified for "
         "compare instruction");
  MCInstrDesc MCIDesc = MI.getDesc();
  unsigned NumImplicitUses = MCIDesc.getNumImplicitUses();
  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  SmallVector<Value *, 2> OpValues{nullptr, nullptr};

  bool IsBTSInstr = instrNameStartsWith(MI, "BTS");
  bool IsBTRInstr = instrNameStartsWith(MI, "BTR");
  bool IsBTCInstr = instrNameStartsWith(MI, "BTC");

  unsigned Op2Index;
  if (IsMemBT) {
    assert(MemRefValue != nullptr && "Null memory reference value encountered "
                                     "while raising compare instruction");
    assert(MemRefOpIndex == 0 &&
           "Unexpected memory operand index in compare instruction");

    LLVMContext &Ctx(MF.getFunction().getContext());
    auto MemOpSize = getInstructionMemOpSize(MI.getOpcode());
    assert(MemOpSize != 0 && "Expected mem op size to be > 0");

    Type *OpTy = Type::getIntNTy(Ctx, MemOpSize * 8);

    auto *LoadedVal = loadMemoryRefValue(MI, MemRefValue, MemRefOpIndex, OpTy);

    OpValues[0] = LoadedVal;
    Op2Index = X86::AddrNumOperands;
  } else {
    // The instruction operands do not reference memory
    unsigned Op1Index;

    if (NumImplicitUses == 1) {
      // If an implicit operand is used, that is op1.
      MCPhysReg UseReg = MCIDesc.ImplicitUses[0];
      Op1Index = MI.findRegisterUseOperandIdx(UseReg, false, nullptr);
      Op2Index = MCIDesc.getNumDefs() == 0 ? 0 : 1;
    } else {
      // Explicit operands are used
      Op1Index = MCIDesc.getNumDefs() == 0 ? 0 : 1;
      Op2Index = Op1Index + 1;
    }

    MachineOperand CmpOp1 = MI.getOperand(Op1Index);

    assert((CmpOp1.isReg()) &&
           "Unhandled first operand type in bt instruction");

    OpValues[0] = getRegOrArgValue(CmpOp1.getReg(), MBBNo);
  }

  MachineOperand Op2 = MI.getOperand(Op2Index);
  Value *Op2Val = nullptr;
  if (Op2.isReg()) {
    Op2Val = raisedValues->castValue(getRegOrArgValue(Op2.getReg(), MBBNo),
                                     OpValues[0]->getType(), RaisedBB);
  } else if (Op2.isImm()) {
    Op2Val = ConstantInt::get(OpValues[0]->getType(), Op2.getImm());
  } else {
    LLVM_DEBUG(MI.dump());
    assert(false && "Unhandled operand type in compare instruction");
  }
  OpValues[1] = Op2Val;

  assert(OpValues[0] != nullptr && OpValues[1] != nullptr &&
         "Unable to materialize compare operand values");

  assert((OpValues[0]->getType() == OpValues[1]->getType()) &&
         "Mis-matched operand types encountered while raising bt instruction");

  // An index greater than the width of the first operand should wrap around
  auto BitWidth = OpValues[0]->getType()->getPrimitiveSizeInBits();
  auto *ModuloOperand = ConstantInt::get(OpValues[1]->getType(), BitWidth);
  auto *PositionModulo =
      BinaryOperator::CreateURem(OpValues[1], ModuloOperand, "", RaisedBB);

  // Create a bitmask to select the bit we want to check
  auto *ConstOne = ConstantInt::get(OpValues[0]->getType(), 1);
  auto *BitMask =
      BinaryOperator::CreateShl(ConstOne, PositionModulo, "", RaisedBB);

  // Check if the bit is set by checking if logical and with the bitmask is not
  // zero
  auto *BitValue =
      BinaryOperator::CreateAnd(OpValues[0], BitMask, "", RaisedBB);
  auto *ConstZero = ConstantInt::get(OpValues[0]->getType(), 0);
  auto *CFValue = ICmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_NE,
                                   BitValue, ConstZero, "", RaisedBB);

  raisedValues->setEflagValue(EFLAGS::CF, MI.getParent()->getNumber(), CFValue);

  // For BTS, BTR and BTC the bit at the specified index needs to be changed
  Value *WriteBackVal = nullptr;
  if (IsBTSInstr) {
    // Set the bit to 1
    WriteBackVal = BinaryOperator::CreateOr(OpValues[0], BitMask, "", RaisedBB);
  } else if (IsBTRInstr) {
    // Set the bit to 0
    BitMask = BinaryOperator::CreateNot(BitMask, "", RaisedBB);
    WriteBackVal =
        BinaryOperator::CreateAnd(OpValues[0], BitMask, "", RaisedBB);
  } else if (IsBTCInstr) {
    // Flip the bit
    WriteBackVal =
        BinaryOperator::CreateXor(OpValues[0], BitMask, "", RaisedBB);
  }

  if (WriteBackVal != nullptr) {
    if (IsMemBT) {
      assert(MemRefValue->getType()->isPointerTy() &&
             "Expected MemRefVal to be of pointer type for bt instruction");
      if (MemRefValue->getType() != WriteBackVal->getType()->getPointerTo()) {
        MemRefValue = new BitCastInst(
            MemRefValue, WriteBackVal->getType()->getPointerTo(), "", RaisedBB);
      }
      new StoreInst(WriteBackVal, MemRefValue, RaisedBB);
    } else {
      assert(MCIDesc.getNumDefs() == 1 &&
             "Expected one def for bt instruction");
      auto DestOp = MI.getOperand(0);
      assert(DestOp.isReg() &&
             "Expected dest to be register for bt instruction");
      raisedValues->setPhysRegSSAValue(
          DestOp.getReg(), MI.getParent()->getNumber(), WriteBackVal);
    }
  }

  return true;
}

// Raise compare instruction. If the instruction is a memory compare, it
// is expected that this function is called from raiseMemRefMachineInstr
// after verifying the accessibility of memory location and with
// isMemCompare set true.If IsMemCompare is true, MemRefValue needs to be
// the non-null memory reference value representing the memory reference the
// instruction uses.

bool X86MachineInstructionRaiser::raiseCompareMachineInstr(
    const MachineInstr &MI, bool IsMemCompare, Value *MemRefValue) {
  // Ensure this is a compare instruction
  assert(MI.isCompare() && "Compare instruction expected");
  // Get index of memory reference in the instruction.
  int MemRefOpIndex = getMemoryRefOpIndex(MI);
  int MBBNo = MI.getParent()->getNumber();
  unsigned int DestReg = X86::NoRegister;
  assert((((MemRefOpIndex != -1) && IsMemCompare) ||
          ((MemRefOpIndex == -1) && !IsMemCompare)) &&
         "Inconsistent memory reference operand information specified for "
         "compare instruction");
  MCInstrDesc MCIDesc = MI.getDesc();
  unsigned NumImplicitUses = MCIDesc.getNumImplicitUses();
  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  // Three instructions viz., CMP*, SUB* and TEST* are classified by LLVM as
  // compare instructions Is this a sub instruction?
  bool IsCMPInst = instrNameStartsWith(MI, "CMP");
  bool IsSUBInst = instrNameStartsWith(MI, "SUB");
  bool IsTESTInst = instrNameStartsWith(MI, "TEST");

  assert((IsCMPInst || IsSUBInst || IsTESTInst) &&
         "Unhandled memory referencing compare instruction");
  SmallVector<Value *, 2> OpValues{nullptr, nullptr};

  // Get operand indices
  if (IsMemCompare) {
    // This is a memory referencing instruction.
    Type *NonMemRefOpTy;
    const MachineOperand *NonMemRefOp;
    assert(MemRefOpIndex >= 0 &&
           "Unexpected memory operand index in compare instruction");
    unsigned NonMemRefOpIndex = (MemRefOpIndex == 0) ? X86::AddrNumOperands : 0;
    NonMemRefOp = &(MI.getOperand(NonMemRefOpIndex));
    if (NonMemRefOp->isReg()) {
      NonMemRefOpTy = getPhysRegOperandType(MI, NonMemRefOpIndex);
    } else if (NonMemRefOp->isImm()) {
      LLVMContext &Ctx(MF.getFunction().getContext());
      auto MemOpSize = getInstructionMemOpSize(MI.getOpcode());
      assert(MemOpSize != 0 && "Expected mem op size to be > 0");
      NonMemRefOpTy = Type::getIntNTy(Ctx, MemOpSize * 8);
    } else {
      LLVM_DEBUG(MI.dump());
      assert(false && "Unhandled second operand type in compare instruction");
    }

    assert(MemRefValue != nullptr && "Null memory reference value encountered "
                                     "while raising compare instruction");
    // Convert it to a pointer of type of non-memory operand
    if (isEffectiveAddrValue(MemRefValue)) {
      PointerType *PtrTy = PointerType::get(NonMemRefOpTy, 0);
      IntToPtrInst *ConvIntToPtr = new IntToPtrInst(MemRefValue, PtrTy);
      RaisedBB->getInstList().push_back(ConvIntToPtr);
      MemRefValue = ConvIntToPtr;
    }
    // Load the value from memory location
    auto PtrAlign =
        MemRefValue->getPointerAlignment(MR->getModule()->getDataLayout());
    LoadInst *LoadInstr =
        new LoadInst(NonMemRefOpTy, MemRefValue, "", false, PtrAlign, RaisedBB);
    // save it at the appropriate index of operand value array
    if (MemRefOpIndex == 0) {
      OpValues[0] = LoadInstr;
    } else {
      OpValues[1] = LoadInstr;
    }

    // Get value for non-memory operand of compare.
    Value *NonMemRefVal = nullptr;
    if (NonMemRefOp->isReg()) {
      NonMemRefVal = getRegOrArgValue(NonMemRefOp->getReg(), MBBNo);
    } else if (NonMemRefOp->isImm()) {
      NonMemRefVal = ConstantInt::get(NonMemRefOpTy, NonMemRefOp->getImm());
    } else {
      LLVM_DEBUG(MI.dump());
      assert(false && "Unhandled first operand type in compare instruction");
    }
    // save non-memory reference value at the appropriate index of operand
    // value array
    if (MemRefOpIndex == 0) {
      OpValues[1] = NonMemRefVal;
    } else {
      OpValues[0] = NonMemRefVal;
    }
  } else {
    // The instruction operands do not reference memory
    unsigned Op1Index, Op2Index;

    // Determine the appropriate operand indices of the instruction based on the
    // usage of implicit registers. Note that a cmp instruction is translated as
    // sub op1, op2 (i.e., op1 - op2).
    if (NumImplicitUses == 1) {
      // If an implicit operand is used, that is op1.
      MCPhysReg UseReg = MCIDesc.ImplicitUses[0];
      Op1Index = MI.findRegisterUseOperandIdx(UseReg, false, nullptr);
      Op2Index = MCIDesc.getNumDefs() == 0 ? 0 : 1;
    } else {
      // Explicit operands are used
      Op1Index = MCIDesc.getNumDefs() == 0 ? 0 : 1;
      Op2Index = Op1Index + 1;
    }

    MachineOperand CmpOp1 = MI.getOperand(Op1Index);
    MachineOperand CmpOp2 = MI.getOperand(Op2Index);

    assert((CmpOp1.isReg() || CmpOp1.isImm()) &&
           "Unhandled first operand type in compare instruction");

    assert((CmpOp2.isReg() || CmpOp2.isImm()) &&
           "Unhandled second operand type in compare instruction");

    if (CmpOp1.isReg()) {
      OpValues[0] = getRegOrArgValue(CmpOp1.getReg(), MBBNo);
    }

    if (CmpOp2.isReg()) {
      OpValues[1] = getRegOrArgValue(CmpOp2.getReg(), MBBNo);
    }

    // Construct value if either of the operands is an immediate
    if (CmpOp1.isImm()) {
      assert((OpValues[1] != nullptr) &&
             "At least one value expected while raising compare instruction");
      OpValues[0] = ConstantInt::get(OpValues[1]->getType(), CmpOp1.getImm());
    }

    if (CmpOp2.isImm()) {
      assert((OpValues[0] != nullptr) &&
             "At least one value expected while raising compare instruction");
      OpValues[1] = ConstantInt::get(OpValues[0]->getType(), CmpOp2.getImm());
    }
  }
  assert(OpValues[0] != nullptr && OpValues[1] != nullptr &&
         "Unable to materialize compare operand values");

  // If the first operand is register, make sure the source operand value types
  // are the same as destination register type.
  if (MI.getOperand(0).isReg()) {
    DestReg = MI.getOperand(0).getReg();
    if (DestReg != X86::NoRegister) {
      Type *DestTy = getPhysRegOperandType(MI, 0);
      for (int Idx = 0; Idx < 2; Idx++) {
        if (OpValues[Idx]->getType() != DestTy) {
          CastInst *CInst = CastInst::Create(
              CastInst::getCastOpcode(OpValues[Idx], false, DestTy, false),
              OpValues[Idx], DestTy);
          RaisedBB->getInstList().push_back(CInst);
          OpValues[Idx] = CInst;
        }
      }
    }
  }

  // If the number of implicit use operand is one, make sure the source operand
  // value type is the same as the implicit use operand value type.
  if (NumImplicitUses == 1) {
    if (OpValues[0]->getType() != OpValues[1]->getType()) {
      CastInst *CInst = CastInst::Create(
          CastInst::getCastOpcode(OpValues[0], false, OpValues[1]->getType(),
                                  false),
          OpValues[0], OpValues[1]->getType());
      RaisedBB->getInstList().push_back(CInst);
      OpValues[0] = CInst;
    }
  }

  assert((OpValues[0]->getType() == OpValues[1]->getType()) &&
         "Mis-matched operand types encountered while raising compare "
         "instruction");

  raisedValues->setEflagBoolean(EFLAGS::OF, MBBNo, false);
  raisedValues->setEflagBoolean(EFLAGS::CF, MBBNo, false);
  // CmpInst is of type Value * to allow for a potential need to pass it to
  // castValue(), if needed.
  // If MI is a test instruction, the compare instruction should be an and
  // instruction.
  Value *CmpInst = (IsTESTInst)
                       ? BinaryOperator::CreateAnd(OpValues[0], OpValues[1])
                       : BinaryOperator::CreateSub(OpValues[0], OpValues[1]);
  // Casting CmpInst to instruction to be added to the raised basic
  // block is correct since it is known to be specifically of type Instruction.
  RaisedBB->getInstList().push_back(dyn_cast<Instruction>(CmpInst));

  if (IsSUBInst) {
    switch (MI.getOpcode()) {
    case X86::SUB8mi:
    case X86::SUB8mi8:
    case X86::SUB8mr:
    case X86::SUB16mi:
    case X86::SUB16mi8:
    case X86::SUB16mr:
    case X86::SUB32mi:
    case X86::SUB32mi8:
    case X86::SUB32mr:
    case X86::SUB64mi8:
    case X86::SUB64mi32:
    case X86::SUB64mr: {
      // This instruction moves a source value to memory. So, if the types of
      // the source value and that of the memory pointer content are not the
      // same, it is the source value that needs to be cast to match the type of
      // destination (i.e., memory). It needs to be sign extended as needed.

      Type *MatchTy = getPointerElementType(MemRefValue);
      if (!MatchTy->isArrayTy()) {
        CmpInst = getRaisedValues()->castValue(CmpInst, MatchTy, RaisedBB);
      }

      // Store CmpInst to MemRefValue only if this is a sub MI or MR
      // instruction. Do not update if this is a cmp instruction.
      new StoreInst(CmpInst, MemRefValue, RaisedBB);
    } break;
    case X86::SUB32rr:
    case X86::SUB64rr:
    case X86::SUB8rm:
    case X86::SUB32rm:
    case X86::SUB64rm: {
      assert(MCIDesc.getNumDefs() == 1 &&
             "Unexpected number of def operands of sub instruction");
      // Update the DestReg only if this is a sub instruction. Do not update
      // if this is a cmp instruction
      raisedValues->setPhysRegSSAValue(DestReg, MBBNo, CmpInst);
    } break;
    default:
      assert(false && "Unhandled sub instruction found");
    }
  }
  // Now update EFLAGS
  assert(MCIDesc.getNumImplicitDefs() == 1 &&
         "Compare instruction does not have exactly one implicit def");
  MCPhysReg ImpDefReg = MCIDesc.ImplicitDefs[0];
  assert(ImpDefReg == X86::EFLAGS &&
         "Expected implicit EFLAGS def in compare instruction");
  // Create instructions to set CF, ZF, SF, PF, and OF flags according to the
  // result CmpInst. NOTE: Support for tracking AF not yet implemented.
  raisedValues->testAndSetEflagSSAValue(EFLAGS::CF, MI, CmpInst);
  raisedValues->testAndSetEflagSSAValue(EFLAGS::ZF, MI, CmpInst);
  raisedValues->testAndSetEflagSSAValue(EFLAGS::SF, MI, CmpInst);
  raisedValues->testAndSetEflagSSAValue(EFLAGS::OF, MI, CmpInst);
  raisedValues->testAndSetEflagSSAValue(EFLAGS::PF, MI, CmpInst);
  return true;
}

// Raise a load/store instruction.
// Current implementation only raises instructions that load and store to
// stack.
bool X86MachineInstructionRaiser::raiseMemRefMachineInstr(
    const MachineInstr &MI) {

  // Handle the push instruction that is marked as a memory store
  // instruction
  if (isPushToStack(MI)) {
    return raisePushInstruction(MI);
  }

  if (isPopFromStack(MI)) {
    return raisePopInstruction(MI);
  }

  Value *MemoryRefValue = getMemoryRefValue(MI);

  // Raise a memory compare instruction
  if (MI.isCompare())
    return raiseCompareMachineInstr(MI, true /* isMemRef */, MemoryRefValue);

  // Now that we have all necessary information about memory reference and
  // the load/store operand, we can raise the memory referencing instruction
  // according to the opcode.

  switch (getInstructionKind(MI.getOpcode())) {
    // Move register or immediate to memory
  case InstructionKind::MOV_TO_MEM:
  case InstructionKind::SSE_MOV_TO_MEM:
    return raiseMoveToMemInstr(MI, MemoryRefValue);
  case InstructionKind::INPLACE_MEM_OP:
    return raiseInplaceMemOpInstr(MI, MemoryRefValue);
  // Move register from memory
  case InstructionKind::MOV_FROM_MEM:
  case InstructionKind::SSE_MOV_FROM_MEM:
    return raiseMoveFromMemInstr(MI, MemoryRefValue);
  case InstructionKind::BINARY_OP_RM:
    return raiseBinaryOpMemToRegInstr(MI, MemoryRefValue);
  case InstructionKind::DIVIDE_MEM_OP:
    return raiseDivideFromMemInstr(MI, MemoryRefValue);
  case InstructionKind::LOAD_FPU_REG:
    return raiseLoadIntToFloatRegInstr(MI, MemoryRefValue);
  case InstructionKind::STORE_FPU_REG:
    return raiseStoreIntToFloatRegInstr(MI, MemoryRefValue);
  case InstructionKind::SSE_COMPARE_RM:
    return raiseSSECompareFromMemMachineInstr(MI, MemoryRefValue);
  case InstructionKind::SSE_CONVERT_RM:
    return raiseSSEConvertPrecisionFromMemMachineInstr(MI, MemoryRefValue);
  case InstructionKind::BIT_TEST_OP:
    return raiseBitTestMachineInstr(MI, MemoryRefValue, true);
  case InstructionKind::SETCC:
    return raiseSetCCMachineInstr(MI, MemoryRefValue);
  default:
    LLVM_DEBUG(MI.dump());
    assert(false && "Unhandled memory referencing instruction");
  }
  return false;
}

bool X86MachineInstructionRaiser::raiseSetCCMachineInstr(
    const MachineInstr &MI, Value *MemoryRefValue = nullptr) {
  const MCInstrDesc &MIDesc = MI.getDesc();
  int MBBNo = MI.getParent()->getNumber();
  LLVMContext &Ctx(MF.getFunction().getContext());
  Value *FalseValue = ConstantInt::getFalse(Ctx);
  Value *TrueValue = ConstantInt::getTrue(Ctx);

  assert(((MIDesc.getNumDefs() == 1 && MemoryRefValue == nullptr) ||
          (MIDesc.getNumDefs() == 0 && MemoryRefValue != nullptr)) &&
         "Not found expected one destination operand of set instruction");
  assert(MIDesc.getNumImplicitUses() == 1 &&
         MIDesc.hasImplicitUseOfPhysReg(X86::EFLAGS) &&
         "Not found expected implicit use of eflags in set instruction.");

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());
  Value *Result;

  CmpInst::Predicate Pred = CmpInst::Predicate::BAD_ICMP_PREDICATE;
  switch (X86::getCondFromSETCC(MI)) {
  case X86::COND_NE: {
    // Check if ZF == 0
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, ZFValue, FalseValue);
  } break;
  case X86::COND_E: {
    // Check if ZF == 1
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, ZFValue, TrueValue);
  } break;
  case X86::COND_G: {
    // Check ZF == 0 and SF == OF
    Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
    Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
    Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
    assert((ZFValue != nullptr) && (SFValue != nullptr) &&
           (OFValue != nullptr) &&
           "Failed to get EFLAGS value while raising CMOVG!");
    Pred = CmpInst::Predicate::ICMP_EQ;

    // Compare ZF and 0
    CmpInst *ZFCond = new ICmpInst(Pred, ZFValue, FalseValue, "ZFCmp_CMOVG");
    RaisedBB->getInstList().push_back(ZFCond);

    // Test SF == OF
    CmpInst *SFOFCond = new ICmpInst(Pred, SFValue, OFValue, "SFOFCmp_CMOVG");
    RaisedBB->getInstList().push_back(SFOFCond);
    Result =
        BinaryOperator::CreateAnd(ZFCond, SFOFCond, "Cond_CMOVG", RaisedBB);
  } break;
  case X86::COND_O: {
    // Check if OF = 1
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, OFValue, TrueValue);
  } break;
  case X86::COND_B: {
    // Check if CF = 1
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *CFValue = getRegOrArgValue(EFLAGS::CF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, CFValue, TrueValue);
  } break;
  case X86::COND_AE: {
    // Check if CF = 0
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *CFValue = getRegOrArgValue(EFLAGS::CF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, CFValue, FalseValue);
  } break;
  case X86::COND_A: {
    // Check CF == 0 and ZF == 0
    Value *CFValue = getRegOrArgValue(EFLAGS::CF, MBBNo);
    Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
    assert((CFValue != nullptr) && (ZFValue != nullptr) &&
           "Failed to get EFLAGS value while raising CMOVA!");
    Pred = CmpInst::Predicate::ICMP_EQ;

    // Compare CF and 0
    CmpInst *CFCond = new ICmpInst(Pred, CFValue, FalseValue, "CFCmp_CMOVA");
    RaisedBB->getInstList().push_back(CFCond);

    // Compare ZF and 0
    CmpInst *ZFCond = new ICmpInst(Pred, ZFValue, FalseValue, "ZFCmp_CMOVA");
    RaisedBB->getInstList().push_back(ZFCond);

    Result =
        BinaryOperator::CreateAnd(CFCond, ZFCond, "CFAndZF_CMOVA", RaisedBB);
  } break;
  case X86::COND_NS: {
    // Check if SF == 0
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, SFValue, FalseValue);
  } break;
  case X86::COND_S: {
    // Check if SF == 1
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, SFValue, TrueValue);
  } break;
  case X86::COND_LE: {
    // Check ZF == 1 or SF != OF
    Value *ZFValue = getRegOrArgValue(EFLAGS::ZF, MBBNo);
    Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
    Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
    assert((ZFValue != nullptr) && (SFValue != nullptr) &&
           (OFValue != nullptr) &&
           "Failed to get EFLAGS value while raising SETLE!");
    // Check ZF == 1
    Pred = CmpInst::Predicate::ICMP_EQ;
    CmpInst *ZFCond = new ICmpInst(Pred, ZFValue, TrueValue, "ZFCmp_SETLE");
    RaisedBB->getInstList().push_back(ZFCond);
    // Test SF != OF
    CmpInst *SFOFCond = new ICmpInst(CmpInst::Predicate::ICMP_NE, SFValue,
                                     OFValue, "SFOFCmp_SETLE");
    RaisedBB->getInstList().push_back(SFOFCond);

    Result = BinaryOperator::CreateOr(ZFCond, SFOFCond, "Cond_SETLE", RaisedBB);
  } break;
  case X86::COND_GE: {
    // SF == OF
    Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
    Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
    assert(SFValue != nullptr && OFValue != nullptr &&
           "Failed to get EFLAGS value while raising SETGE");
    Pred = CmpInst::Predicate::ICMP_EQ;
    // Compare SF and OF
    Result = new ICmpInst(*RaisedBB, Pred, SFValue, OFValue, "Cond_SETGE");
  } break;
  case X86::COND_NP: {
    // Check if PF == 0
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *PFValue = getRegOrArgValue(EFLAGS::PF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, PFValue, FalseValue);
  } break;
  case X86::COND_P: {
    // Check if PF == 1
    Pred = CmpInst::Predicate::ICMP_EQ;
    Value *PFValue = getRegOrArgValue(EFLAGS::PF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, PFValue, TrueValue);
  } break;
  case X86::COND_L: {
    // Check if SF != OF
    Pred = CmpInst::Predicate::ICMP_NE;
    Value *SFValue = getRegOrArgValue(EFLAGS::SF, MBBNo);
    Value *OFValue = getRegOrArgValue(EFLAGS::OF, MBBNo);
    Result = new ICmpInst(*RaisedBB, Pred, SFValue, OFValue);
  } break;
  case X86::COND_INVALID:
    assert(false && "Set instruction with invalid condition found");
    break;
  default:
    assert(false && "Set instruction with unhandled condition found");
    break;
  }

  if (Pred == CmpInst::Predicate::BAD_ICMP_PREDICATE || Result == nullptr) {
    LLVM_DEBUG(MI.dump());
    assert(false && "Unhandled set instruction");
    return false;
  }

  const MachineOperand &DestOp = MI.getOperand(0);
  if (MemoryRefValue == nullptr) {
    // store result in register
    raisedValues->setPhysRegSSAValue(DestOp.getReg(),
                                     MI.getParent()->getNumber(), Result);
  } else {
    // store result in memory
    unsigned int MemSzInBits = getInstructionMemOpSize(MI.getOpcode()) * 8;
    Type *StoreTy = Type::getIntNTy(Ctx, MemSzInBits);
    auto *StoreVal = new ZExtInst(Result, StoreTy, "", RaisedBB);

    new StoreInst(StoreVal, MemoryRefValue, false, Align(), RaisedBB);
  }

  return true;
}

// Raise a binary operation instruction with operand encoding MRI or MRC
// TODO: The current implementation handles only instructions with first operand
// as register operand. Need to expand to add support for instructions with
// first operand as memory operand.
bool X86MachineInstructionRaiser::raiseBinaryOpMRIOrMRCEncodedMachineInstr(
    const MachineInstr &MI) {
  bool Success = true;
  unsigned int DstIndex = 0, SrcOp1Index = 1, SrcOp2Index = 2, SrcOp3Index = 3;
  const MCInstrDesc &MIDesc = MI.getDesc();

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  // A binary operation instruction with encoding MRI or MRC specifies three
  // operands - the first operand is memory or register and the second is a
  // register and the third is an immediate value or CL register. As noted
  // above, support is not yet implemented if for first operand being a memory
  // operand.
  //
  // X86::EFLAGS is the implicit def operand.
  unsigned NumOperands = MI.getNumExplicitOperands() +
                         MIDesc.getNumImplicitUses() +
                         MIDesc.getNumImplicitDefs();

  assert((NumOperands == 5) && "Unexpected number of operands of BinOp "
                               "instruction with MRI/MRC operand format");

  // Ensure that the instruction defines EFLAGS as implicit define register.
  assert(MIDesc.hasImplicitDefOfPhysReg(X86::EFLAGS) &&
         "Expected implicit def operand EFLAGS not found");

  // TODO: Memory accessing instructions not yet supported.
  assert(!MIDesc.mayLoad() && !MIDesc.mayStore() &&
         "Unsupported MRI/MRC instruction");

  MachineOperand DstOp = MI.getOperand(DstIndex);
  MachineOperand SrcOp1 = MI.getOperand(SrcOp1Index);
  MachineOperand SrcOp2 = MI.getOperand(SrcOp2Index);
  // Check the validity of operands.
  // The first operand is also as the destination operand.
  // Verify source and dest are tied and are registers.
  assert(DstOp.isTied() && (MI.findTiedOperandIdx(DstIndex) == SrcOp1Index) &&
         "Expect tied operand in MRI/MRC encoded instruction");
  assert(SrcOp1.isReg() && SrcOp2.isReg() &&
         "Unexpected operands of an MRC/MRI encoded instruction");
  // Values need to be discovered to form the appropriate instruction.
  // Note that DstOp is both source and dest.
  Register DstPReg = DstOp.getReg();
  Value *SrcOp1Value = getRegOperandValue(MI, SrcOp1Index);
  Value *SrcOp2Value = getRegOperandValue(MI, SrcOp2Index);
  // Return if either of the operand values are not found. Possibly use of
  // undefined register.
  if ((SrcOp1Value == nullptr) || (SrcOp2Value == nullptr)) {
    return false;
  }

  assert(SrcOp1Value->getType() == SrcOp2Value->getType() &&
         "Mismatched types of MRI/MRC encoded instructions");
  Instruction *BinOpInstr = nullptr;
  // EFLAGS that are affected by the result of the binary operation
  std::set<unsigned> AffectedEFlags;
  Value *CountValue = nullptr;

  switch (MI.getOpcode()) {
  case X86::SHLD16rri8:
  case X86::SHLD32rri8:
  case X86::SHLD64rri8:
  case X86::SHRD16rri8:
  case X86::SHRD32rri8:
  case X86::SHRD64rri8: {
    MachineOperand SrcOp3 = MI.getOperand(SrcOp3Index);
    assert(SrcOp3.isImm() &&
           "Expect immediate operand in an MRI encoded instruction");
    CountValue =
        ConstantInt::get(getImmOperandType(MI, SrcOp3Index), SrcOp3.getImm());
    // cast CountValue as needed
    CountValue = getRaisedValues()->castValue(CountValue,
                                              SrcOp1Value->getType(), RaisedBB);
  } break;
  case X86::SHLD16rrCL:
  case X86::SHLD32rrCL:
  case X86::SHLD64rrCL:
  case X86::SHRD16rrCL:
  case X86::SHRD32rrCL:
  case X86::SHRD64rrCL: {
    assert((MIDesc.getNumImplicitUses() == 1) &&
           "Expect one implicit use in MCR encoded instruction");
    assert((MIDesc.ImplicitUses[0] == X86::CL) &&
           "Expect implicit CL regsiter operand in MCR encoded instruction");
    CountValue = getPhysRegValue(MI, X86::CL);
    // Check for undefined use
    if (CountValue != nullptr)
      // cast CountValue as needed
      CountValue = getRaisedValues()->castValue(
          CountValue, SrcOp1Value->getType(), RaisedBB);
    else
      Success = false;
  } break;
  default:
    llvm_unreachable("Unhandled MRI/MRC encoded instruction");
  }

  if (!Success)
    return false;

  // Now generate the call to instrinsic
  // Types of all operands are already asserted to be the same
  auto IntrinsicKind = Intrinsic::not_intrinsic;
  if (instrNameStartsWith(MI, "SHLD")) {
    IntrinsicKind = Intrinsic::fshl;
  } else if (instrNameStartsWith(MI, "SHRD")) {
    IntrinsicKind = Intrinsic::fshr;
    // Swap the argument order
    Value *Tmp = SrcOp1Value;
    SrcOp1Value = SrcOp2Value;
    SrcOp2Value = Tmp;
  } else
    llvm_unreachable("Unhandled MCR/MCI encoded instruction");
  assert((IntrinsicKind != Intrinsic::not_intrinsic) &&
         "Failed to set appropriate intrinsic kind");
  Module *M = MR->getModule();
  Function *IntrinsicFunc =
      Intrinsic::getDeclaration(M, IntrinsicKind, SrcOp1Value->getType());
  Value *IntrinsicCallArgs[] = {SrcOp1Value, SrcOp2Value, CountValue};
  BinOpInstr =
      CallInst::Create(IntrinsicFunc, ArrayRef<Value *>(IntrinsicCallArgs));
  // Test and set EFLAGs
  AffectedEFlags.insert(EFLAGS::CF);
  // Insert the binary operation instruction
  RaisedBB->getInstList().push_back(BinOpInstr);
  // Test and set affected flags
  for (auto Flag : AffectedEFlags)
    raisedValues->testAndSetEflagSSAValue(Flag, MI, BinOpInstr);

  // Update PhysReg to Value map
  if (DstPReg != X86::NoRegister)
    raisedValues->setPhysRegSSAValue(DstPReg, MI.getParent()->getNumber(),
                                     BinOpInstr);
  return Success;
}

// Raise a binary operation instruction with operand encoding I or RI
bool X86MachineInstructionRaiser::raiseBinaryOpImmToRegMachineInstr(
    const MachineInstr &MI) {
  unsigned int DstIndex = 0, SrcOp1Index = 1, SrcOp2Index = 2;
  const MCInstrDesc &MIDesc = MI.getDesc();
  int MBBNo = MI.getParent()->getNumber();

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  // A binary operation instruction with encoding I specifies one operand -
  // using AL/AX/EAX/RAX as implicit register operand.
  // A binary operation instruction with encoding RI specifies two operands
  // - the first operand is a register and the second the immediate value
  //
  // The first operand is also as the destination operand.
  // X86::EFLAGS is the implicit def operand.
  unsigned NumOperands = MI.getNumExplicitOperands() +
                         MIDesc.getNumImplicitUses() +
                         MIDesc.getNumImplicitDefs();
  assert(((NumOperands == 3) || (NumOperands == 4)) &&
         "Unexpected number of operands of BinOp instruction with RI/I "
         "operand format");

  // Create a stack alloc slot corresponding to the adjusted sp value, if the
  // operands reference SP.
  if ((MIDesc.getNumDefs() == 1) &&
      (find64BitSuperReg(MI.getOperand(DstIndex).getReg()) == X86::RSP) &&
      (find64BitSuperReg(MI.getOperand(SrcOp1Index).getReg()) == X86::RSP) &&
      MI.getOperand(SrcOp2Index).isImm() &&
      MIDesc.hasImplicitDefOfPhysReg(X86::EFLAGS)) {

    // Find the stack allocation, if any, associated with the stack index
    // being changed to.
    X86AddressMode AdjSPRef;
    AdjSPRef.Base.Reg = X86::RSP;
    uint64_t Imm = MI.getOperand(SrcOp2Index).getImm();

    switch (MI.getOpcode()) {
    case X86::ADD8i8:
    case X86::ADD16i16:
    case X86::ADD32i32:
    case X86::ADD64i32:
    case X86::ADD8ri:
    case X86::ADD16ri:
    case X86::ADD16ri8:
    case X86::ADD32ri:
    case X86::ADD32ri8:
    case X86::ADD64ri8:
    case X86::ADD64ri32:
      AdjSPRef.Disp = Imm;
      break;
    case X86::SUB32i32:
    case X86::SUB32ri:
    case X86::SUB32ri8:
    case X86::SUB64ri8:
    case X86::SUB64ri32:
    case X86::SUB64i32:
      AdjSPRef.Disp = -Imm;
      break;
    default:
      assert(false && "SP computation - unhandled binary opcode instruction");
    }

    Value *StackRefVal = getStackAllocatedValue(MI, AdjSPRef, true);
    assert((StackRefVal != nullptr) && "Reference to unallocated stack slot");
    raisedValues->setPhysRegSSAValue(X86::RSP, MI.getParent()->getNumber(),
                                     StackRefVal);
  } else {
    // Values need to be discovered to form the appropriate instruction.
    Value *SrcOp1Value = nullptr;
    Value *SrcOp2Value = nullptr;
    unsigned int DstPReg = X86::NoRegister;

    // A vector holding source operand values.
    SmallVector<Value *, 2> OpValues = {nullptr, nullptr};
    unsigned NumImplicitDefs = MIDesc.getNumImplicitDefs();
    assert((NumImplicitDefs == 0 || NumImplicitDefs == 1 ||
            NumImplicitDefs == 2) &&
           "Encountered instruction unexpected number of implicit defs");
    // Index of the instruction operand being read.
    unsigned CurExplicitOpIndex = 0;
    // Keep a count of the number of instruction operands evaluated. A count of
    // NumOperands need to be evaluated. The value is 1 because we have already
    // checked that EFLAGS is an implicit def.
    unsigned NumOperandsEval =
        MIDesc.hasImplicitDefOfPhysReg(X86::EFLAGS) ? 1 : 0;
    // Find destination register of the instruction
    // If the instruction has an explicit dest operand, get the DstPreg from
    // dest operand.
    if (MIDesc.getNumDefs() != 0) {
      // Get destination reg
      const MachineOperand &DstOp = MI.getOperand(CurExplicitOpIndex);
      assert(DstOp.isReg() && "Not found expected register to be the "
                              "destination operand of BinOp instruction with "
                              "RI/I operand format");
      DstPReg = DstOp.getReg();
      // Go to next explicit operand index
      CurExplicitOpIndex++;
      // Increment the number of operands evaluated
      NumOperandsEval++;
    }
    // If there is no explicit dst register in the instruction, find if there is
    // an implicit physical register defined by the instruction.
    if ((NumImplicitDefs == 2) && (DstPReg == X86::NoRegister)) {
      // Find the implicit dest reg. Register at index 0 is the implicit def
      // physical register. That at index 1 is EFLAGS.
      DstPReg = MIDesc.ImplicitDefs[0];
      assert(((DstPReg == X86::AL) || (DstPReg == X86::AX) ||
              (DstPReg == X86::EAX) || (DstPReg == X86::RAX)) &&
             "Expected implicit use of operand AL/AX/EAX/RAX not found");
      // Increment the number of operands evaluated
      NumOperandsEval++;
    }

    // Now, find source operand values.
    // First check if there are any implicit use operands of the instruction.
    unsigned NumImplicitUses = MIDesc.getNumImplicitUses();
    assert((NumImplicitUses < 3) &&
           "More than two implicit use operands found in BinOp instruction "
           "with RI/I format operands");
    unsigned SrcValIdx = 0;
    for (; SrcValIdx < NumImplicitUses; SrcValIdx++) {
      OpValues[SrcValIdx] = getPhysRegValue(MI, MIDesc.ImplicitUses[SrcValIdx]);
      // Check for undefined use
      if (OpValues[SrcValIdx] == nullptr)
        return false;
      NumOperandsEval++;
    }

    // Get the explicit source operand values.
    while (NumOperandsEval < NumOperands) {
      assert((SrcValIdx < 2) && "Unexpected operand index while raising BinOp "
                                "instruction with RI/I operand format");
      const MachineOperand &SrcOp = MI.getOperand(CurExplicitOpIndex);
      if (SrcValIdx == 0) {
        assert(SrcOp.isReg() &&
               "Not found expected register to be the first "
               "operand of BinOp instruction with RI/I operand format");

        // Get value of SrcOp appropriately sized.
        OpValues[0] = getRegOperandValue(MI, CurExplicitOpIndex);
        // Check for undefined use
        if (OpValues[0] == nullptr)
          return false;
        CurExplicitOpIndex++;
        NumOperandsEval++;
      }

      // Get the second source operand value if the instruction has at least two
      // operands.
      if (SrcValIdx == 1) {
        // If the instruction has an explicit second operand
        // Get value of SrcOp
        assert(SrcOp.isImm() && "Expect immediate operand in a BinOp "
                                "instruction with RI/I operand format");
        assert(OpValues[0] != nullptr &&
               "Undefined first source value encountered in BinOp instruction "
               "with RI/I operand format");
        // Create constant of type that matches that of the dest register
        // If the instruction has no dest operand (such as TEST) set the type of
        // immediate value to be that of first operand value.
        Type *Ty = (DstPReg == X86::NoRegister) ? OpValues[0]->getType()
                                                : getPhysRegType(DstPReg);
        OpValues[1] = ConstantInt::get(Ty, SrcOp.getImm());
        CurExplicitOpIndex++;
        NumOperandsEval++;
      }
      SrcValIdx++;
    }

    assert((NumOperandsEval == NumOperands) &&
           "Failed to evaluate operands of BinOp instruction correctly");

    // Set up the source values to be used by BinOp instruction.

    SrcOp1Value = OpValues[0];
    SrcOp2Value = OpValues[1];

    // Check validity of source operand values. Both source operands need to be
    // non-null values. The only exception is when the instruction has 3
    // operands indicating that there is an implicit constant value encoded by
    // the instruction such as SHR81. Such operands are constructed in an
    // instruction-specific way before the generating the appropriate IR
    // instruction.
    assert((SrcOp1Value != nullptr) &&
           ((SrcOp2Value != nullptr) ||
            ((NumOperands == 3) && (SrcOp2Value == nullptr))) &&
           "Unexpected source values encountered in BinOp instruction with "
           "RI/I operand format");

    Instruction *BinOpInstr = nullptr;
    // EFLAGS that are affected by the result of the binary operation
    std::set<unsigned> AffectedEFlags;

    switch (MI.getOpcode()) {
    case X86::ADD8i8:
    case X86::ADD16i16:
    case X86::ADD8ri:
    case X86::ADD16ri:
    case X86::ADD16ri8:
    case X86::ADD32ri:
    case X86::ADD32ri8:
    case X86::ADD32i32:
    case X86::ADD64ri8:
    case X86::ADD64ri32:
    case X86::ADD64i32: {
      // Generate add instruction
      BinOpInstr = BinaryOperator::CreateAdd(SrcOp1Value, SrcOp2Value);
      // Clear OF and CF
      AffectedEFlags.insert(EFLAGS::CF);
      AffectedEFlags.insert(EFLAGS::OF);
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      AffectedEFlags.insert(EFLAGS::PF);

      valueSetAnalysis->adjustVS(AlocType(find64BitSuperReg(DstPReg)), 
        dyn_cast<ConstantInt>(SrcOp2Value)->getSExtValue());
    } break;
    case X86::SUB32i32:
    case X86::SUB32ri:
    case X86::SUB32ri8:
    case X86::SUB64ri8:
    case X86::SUB64ri32:
    case X86::SUB64i32:
      // Generate sub instruction
      BinOpInstr = BinaryOperator::CreateSub(SrcOp1Value, SrcOp2Value);
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      AffectedEFlags.insert(EFLAGS::CF);
      AffectedEFlags.insert(EFLAGS::OF);
      AffectedEFlags.insert(EFLAGS::PF);
      break;
    case X86::AND8i8:
    case X86::AND8ri:
    case X86::AND16i16:
    case X86::AND16ri:
    case X86::AND16ri8:
    case X86::AND32i32:
    case X86::AND32ri:
    case X86::AND32ri8:
    case X86::AND64i32:
    case X86::AND64ri8:
    case X86::AND64ri32:
      // Generate and instruction
      BinOpInstr = BinaryOperator::CreateAnd(SrcOp1Value, SrcOp2Value);
      // Clear OF and CF
      raisedValues->setEflagBoolean(EFLAGS::OF, MBBNo, false);
      raisedValues->setEflagBoolean(EFLAGS::CF, MBBNo, false);
      // Test and set EFLAGs
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      AffectedEFlags.insert(EFLAGS::PF);
      break;
    case X86::OR8i8:
    case X86::OR8ri:
    case X86::OR16ri8:
    case X86::OR16i16:
    case X86::OR16ri:
    case X86::OR32i32:
    case X86::OR32ri:
    case X86::OR32ri8:
    case X86::OR64i32:
    case X86::OR64ri32:
    case X86::OR64ri8:
      // Generate or instruction
      BinOpInstr = BinaryOperator::CreateOr(SrcOp1Value, SrcOp2Value);
      // Clear OF and CF
      raisedValues->setEflagBoolean(EFLAGS::OF, MBBNo, false);
      raisedValues->setEflagBoolean(EFLAGS::CF, MBBNo, false);
      // Test and set EFLAGs
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      AffectedEFlags.insert(EFLAGS::PF);
      break;
    case X86::ROL8r1:
    case X86::ROL16r1:
    case X86::ROL32r1:
    case X86::ROL64r1:
      SrcOp2Value = ConstantInt::get(SrcOp1Value->getType(), 1);
      // Mark affected EFLAGs. Note OF is affected only for 1-bit rotates.
      AffectedEFlags.insert(EFLAGS::OF);
      LLVM_FALLTHROUGH;
    case X86::ROL8ri:
    case X86::ROL16ri:
    case X86::ROL32ri:
    case X86::ROL64ri: {
      // Generate the call to instrinsic
      auto IntrinsicKind = Intrinsic::fshl;
      Module *M = MR->getModule();
      Function *IntrinsicFunc =
          Intrinsic::getDeclaration(M, IntrinsicKind, SrcOp1Value->getType());
      Value *IntrinsicCallArgs[] = {SrcOp1Value, SrcOp1Value, SrcOp2Value};
      BinOpInstr =
          CallInst::Create(IntrinsicFunc, ArrayRef<Value *>(IntrinsicCallArgs));
      // Mark affected EFLAGs
      AffectedEFlags.insert(EFLAGS::CF);
      // The SF, ZF, AF, and PF flags are not affected.
    } break;
    case X86::ROR8r1:
    case X86::ROR16r1:
    case X86::ROR32r1:
    case X86::ROR64r1:
      SrcOp2Value = ConstantInt::get(SrcOp1Value->getType(), 1);
      // Mark affected EFLAGs. Note OF is affected only for 1-bit rotates.
      AffectedEFlags.insert(EFLAGS::OF);
      // The SF, ZF, AF, and PF flags are not affected.
      LLVM_FALLTHROUGH;
    case X86::ROR8ri:
    case X86::ROR16ri:
    case X86::ROR32ri:
    case X86::ROR64ri: {
      // Generate the call to instrinsic
      auto IntrinsicKind = Intrinsic::fshr;
      Module *M = MR->getModule();
      Function *IntrinsicFunc =
          Intrinsic::getDeclaration(M, IntrinsicKind, SrcOp1Value->getType());
      Value *IntrinsicCallArgs[] = {SrcOp1Value, SrcOp1Value, SrcOp2Value};
      BinOpInstr =
          CallInst::Create(IntrinsicFunc, ArrayRef<Value *>(IntrinsicCallArgs));
      // Mark affected EFLAGs
      AffectedEFlags.insert(EFLAGS::CF);
      // The SF, ZF, AF, and PF flags are not affected.
    } break;
    case X86::XOR8ri:
    case X86::XOR16ri:
    case X86::XOR32ri:
    case X86::XOR8i8:
    case X86::XOR16i16:
    case X86::XOR32i32:
    case X86::XOR8ri8:
    case X86::XOR16ri8:
    case X86::XOR32ri8:
    case X86::XOR64ri8:
    case X86::XOR64ri32:
      // Generate xor instruction
      BinOpInstr = BinaryOperator::CreateXor(SrcOp1Value, SrcOp2Value);
      // Clear OF and CF
      raisedValues->setEflagBoolean(EFLAGS::OF, MBBNo, false);
      raisedValues->setEflagBoolean(EFLAGS::CF, MBBNo, false);
      // Test and set EFLAGs
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      AffectedEFlags.insert(EFLAGS::PF);
      break;
    case X86::IMUL16rri:
    case X86::IMUL32rri:
    case X86::IMUL32rri8:
    case X86::IMUL64rri8:
    case X86::IMUL64rri32:
      BinOpInstr = BinaryOperator::CreateMul(SrcOp1Value, SrcOp2Value);
      // OF is also affected, but is set to be the same as CF. Setting of OF for
      // IMUL is handled along with setting of CF. So, there is no need to add
      // OF as affected flag.
      AffectedEFlags.insert(EFLAGS::CF);
      break;
    case X86::SHR8r1:
    case X86::SHR16r1:
    case X86::SHR32r1:
    case X86::SHR64r1:
      SrcOp2Value = ConstantInt::get(SrcOp1Value->getType(), 1);
      LLVM_FALLTHROUGH;
    case X86::SHR8ri:
    case X86::SHR16ri:
    case X86::SHR32ri:
    case X86::SHR64ri:
      // Generate shr instruction
      BinOpInstr = BinaryOperator::CreateLShr(SrcOp1Value, SrcOp2Value);
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      break;
    case X86::SHL8r1:
    case X86::SHL16r1:
    case X86::SHL32r1:
    case X86::SHL64r1:
      // Create SrcOp2 value of constant int value of 1
      SrcOp2Value = ConstantInt::get(SrcOp1Value->getType(), 1);
      LLVM_FALLTHROUGH;
    case X86::SHL8ri:
    case X86::SHL16ri:
    case X86::SHL32ri:
    case X86::SHL64ri:
      // Generate shl instruction
      BinOpInstr = BinaryOperator::CreateShl(SrcOp1Value, SrcOp2Value);
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      break;
    case X86::SAR8r1:
    case X86::SAR16r1:
    case X86::SAR32r1:
    case X86::SAR64r1:
      // Create SrcOp2 value of constant int value of 1
      SrcOp2Value = ConstantInt::get(SrcOp1Value->getType(), 1);
      LLVM_FALLTHROUGH;
    case X86::SAR8ri:
    case X86::SAR16ri:
    case X86::SAR32ri:
    case X86::SAR64ri:
      // Generate sar instruction
      BinOpInstr = BinaryOperator::CreateLShr(SrcOp1Value, SrcOp2Value);
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      break;
    case X86::TEST8i8:
    case X86::TEST16i16:
    case X86::TEST32i32:
    case X86::TEST64i32:
    case X86::TEST8ri:
    case X86::TEST16ri:
    case X86::TEST32ri:
      BinOpInstr = BinaryOperator::CreateAnd(SrcOp1Value, SrcOp2Value);
      // Clear OF and CF
      raisedValues->setEflagBoolean(EFLAGS::OF, MBBNo, false);
      raisedValues->setEflagBoolean(EFLAGS::CF, MBBNo, false);
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      AffectedEFlags.insert(EFLAGS::PF);
      break;
    case X86::INC8r:
    case X86::INC16r:
    case X86::INC16r_alt:
    case X86::INC32r:
    case X86::INC32r_alt:
    case X86::INC64r:
      SrcOp2Value = ConstantInt::get(SrcOp1Value->getType(), 1);
      BinOpInstr = BinaryOperator::CreateAdd(SrcOp1Value, SrcOp2Value);
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      AffectedEFlags.insert(EFLAGS::PF);
      break;
    case X86::DEC8r:
    case X86::DEC16r:
    case X86::DEC16r_alt:
    case X86::DEC32r:
    case X86::DEC32r_alt:
    case X86::DEC64r:
      SrcOp2Value = ConstantInt::get(SrcOp1Value->getType(), 1);
      BinOpInstr = BinaryOperator::CreateSub(SrcOp1Value, SrcOp2Value);
      AffectedEFlags.insert(EFLAGS::SF);
      AffectedEFlags.insert(EFLAGS::ZF);
      AffectedEFlags.insert(EFLAGS::PF);
      break;
    case X86::PSHUFDri: {
      ConstantInt *Imm = dyn_cast<ConstantInt>(SrcOp2Value);
      assert(Imm && "Expected immediate for pshufd to be defined");

      LLVMContext &Ctx(MF.getFunction().getContext());
      FixedVectorType *VecTy = FixedVectorType::get(Type::getInt32Ty(Ctx), 4);
      SrcOp1Value = getRaisedValues()->reinterpretSSERegValue(SrcOp1Value,
                                                              VecTy, RaisedBB);

      uint8_t ImmValue = (uint8_t)Imm->getZExtValue();
      Value *Result = ConstantInt::get(VecTy, 0);
      for (unsigned Idx = 0; Idx < VecTy->getNumElements(); ++Idx) {
        auto *DstIndex = ConstantInt::get(Type::getInt32Ty(Ctx), Idx);
        auto *SrcIndex = ConstantInt::get(Type::getInt32Ty(Ctx),
                                          (ImmValue >> (2 * Idx) & 0b11));

        auto *ExtractInst =
            ExtractElementInst::Create(SrcOp1Value, SrcIndex, "", RaisedBB);
        if (Idx != VecTy->getNumElements() - 1) {
          Result = InsertElementInst::Create(Result, ExtractInst, DstIndex, "",
                                             RaisedBB);
        } else {
          Result = InsertElementInst::Create(Result, ExtractInst, DstIndex);
        }
      }

      BinOpInstr = dyn_cast<Instruction>(Result);
    } break;
    default:
      LLVM_DEBUG(MI.dump());
      assert(false && "Unhandled reg to imm binary operator instruction");
      break;
    }

    // propagate rodata metadata
    raisedValues->setInstMetadataRODataIndex(SrcOp1Value, BinOpInstr);
    raisedValues->setInstMetadataRODataIndex(SrcOp2Value, BinOpInstr);
    // Insert the binary operation instruction
    RaisedBB->getInstList().push_back(BinOpInstr);
    // Test and set affected flags
    for (auto Flag : AffectedEFlags)
      raisedValues->testAndSetEflagSSAValue(Flag, MI, BinOpInstr);

    // Update PhysReg to Value map
    if (DstPReg != X86::NoRegister)
      raisedValues->setPhysRegSSAValue(DstPReg, MI.getParent()->getNumber(),
                                       BinOpInstr);
  }
  return true;
}

// Raise indirect branch instruction.
bool X86MachineInstructionRaiser::raiseIndirectBranchMachineInstr(
    ControlTransferInfo *CTRec) {
  const MachineInstr *MI = CTRec->CandidateMachineInstr;
  BasicBlock *CandBB = CTRec->CandidateBlock;

  const MCInstrDesc &MCID = MI->getDesc();

  // Make sure this function was called on a direct branch instruction.
  assert((MCID.TSFlags & X86II::ImmMask) == 0 &&
         "PC-Relative control transfer not expected");

  // Raise indirect branch instruction to jump table
  if (MI->getOperand(0).isJTI()) {
    unsigned JTIndex = MI->getOperand(0).getIndex();
    std::vector<JumpTableBlock> JTCases;
    const MachineJumpTableInfo *MJT = MF.getJumpTableInfo();

    // Get the case value
    MachineBasicBlock *CondMBB = JTList[JTIndex].ConditionMBB;
    Value *CondVal = getSwitchCompareValue(*CondMBB);
    assert(CondVal != nullptr && "Failed to get switch compare value.");
    Type *CaseValTy = CondVal->getType();

    std::vector<MachineJumpTableEntry> JumpTables = MJT->getJumpTables();
    for (unsigned Idx = 0, Sz = JumpTables[JTIndex].MBBs.size(); Idx != Sz;
         ++Idx) {
      ConstantInt *CaseVal =
          cast<ConstantInt>(ConstantInt::get(CaseValTy, Idx, true));
      MachineBasicBlock *Succ = JumpTables[JTIndex].MBBs[Idx];
      JTCases.push_back(std::make_pair(CaseVal, Succ));
    }

    // Create the Switch Instruction
    unsigned int NumCases = JTCases.size();
    auto IntrDf = mbbToBBMap.find(JTList[JTIndex].DefaultMBB->getNumber());

    BasicBlock *DfBB = IntrDf->second;
    SwitchInst *Inst = SwitchInst::Create(CondVal, DfBB, NumCases);

    for (unsigned Idx = 0, Count = NumCases; Idx != Count; ++Idx) {
      MachineBasicBlock *Mbb = JTCases[Idx].second;
      auto Intr = mbbToBBMap.find(Mbb->getNumber());
      BasicBlock *BB = Intr->second;
      Inst->addCase(JTCases[Idx].first, BB);
    }

    CandBB->getInstList().push_back(Inst);
    CTRec->Raised = true;
  } else {
    assert(false && "Support to raise indirect branches to non-jumptable "
                    "location not yet implemented");
  }
  return true;
}

// Raise direct branch instruction.
bool X86MachineInstructionRaiser::raiseDirectBranchMachineInstr(
    ControlTransferInfo *CTRec) {
  const MachineInstr *MI = CTRec->CandidateMachineInstr;
  BasicBlock *CandBB = CTRec->CandidateBlock;

  const MCInstrDesc &MCID = MI->getDesc();

  // Make sure this function was called on a direct branch instruction.
  assert(X86II::isImmPCRel(MCID.TSFlags) &&
         "PC-Relative control transfer expected");

  // Get branch offset of the branch instruction
  const MachineOperand &MO = MI->getOperand(0);
  assert(MO.isImm() && "Expected immediate operand not found");
  int64_t BranchOffset = MO.getImm();
  MCInstRaiser *MCIR = getMCInstRaiser();
  // Get MCInst offset - the offset of machine instruction in the binary
  uint64_t MCInstOffset = MCIR->getMCInstIndex(*MI);

  assert(MCIR != nullptr && "MCInstRaiser not initialized");
  int64_t BranchTargetOffset =
      MCInstOffset + MCIR->getMCInstSize(MCInstOffset) + BranchOffset;
  const int64_t TgtMBBNo =
      MCIR->getMBBNumberOfMCInstOffset(BranchTargetOffset, MF);
  assert((TgtMBBNo != -1) && "No branch target found");
  auto Iter = mbbToBBMap.find(TgtMBBNo);
  assert(Iter != mbbToBBMap.end() &&
         "BasicBlock corresponding to MachineInstr branch not found");
  BasicBlock *TgtBB = (*Iter).second;
  if (MI->isUnconditionalBranch()) {
    // Just create a branch instruction targeting TgtBB
    BranchInst *UncondBr = BranchInst::Create(TgtBB);
    CandBB->getInstList().push_back(UncondBr);
    CTRec->Raised = true;
  } else if (MI->isConditionalBranch()) {
    // Find the fall through basic block
    MCInstRaiser::const_mcinst_iter MCIter = MCIR->getMCInstAt(MCInstOffset);
    LLVMContext &Ctx(MF.getFunction().getContext());
    // Go to next non-nop instruction on the fall-through path.
    bool IsNop = true;
    while (IsNop) {
      MCIter++;
      IsNop = isNoop(MCIter->second.getMCInst().getOpcode());
      assert(MCIter != MCIR->const_mcinstr_end() &&
             "Attempt to go past MCInstr stream");
    }
    // Get MBB number whose lead instruction is at the offset of fall-through
    // non-nop instruction. This is the fall-through MBB.
    int64_t FTMBBNum = MCIR->getMBBNumberOfMCInstOffset((*MCIter).first, MF);
    assert((FTMBBNum != -1) && "No fall-through target found");
    if (MF.getBlockNumbered(FTMBBNum)->empty())
      assert(false && "Fall-through empty");
    // Find raised BasicBlock corresponding to fall-through MBB
    auto MapIter = mbbToBBMap.find(FTMBBNum);
    assert(MapIter != mbbToBBMap.end() &&
           "Fall-through BasicBlock corresponding to MachineInstr branch not "
           "found");
    BasicBlock *FTBB = (*MapIter).second;
    // Get the condition value
    assert(CTRec->RegValues.size() == EFlagBits.size() &&
           "Unexpected number of EFLAGS bit values in conditional branch not "
           "handled");

    // Branch condition value
    Value *BranchCond = nullptr;
    // Predicate operation to be performed
    Value *TrueValue = ConstantInt::getTrue(Ctx);
    Value *FalseValue = ConstantInt::getFalse(Ctx);
    auto Opcode = MI->getOpcode();
    assert(((Opcode == X86::JCC_1) || (Opcode == X86::JCC_2) ||
            (Opcode == X86::JCC_4)) &&
           "Conditional branch instruction expected");
    X86::CondCode CC = X86::COND_INVALID;

    // Unfortunately X86::getCondFromBranch(MI) only looks at JCC_1. We need
    // to handle JCC_2 and JCC_4 as well.
    switch (MI->getOpcode()) {
    default:
      CC = X86::COND_INVALID;
      break;
    case X86::JCC_1:
    case X86::JCC_2:
    case X86::JCC_4:
      CC = static_cast<X86::CondCode>(
          MI->getOperand(MI->getDesc().getNumOperands() - 1).getImm());
    }

    switch (CC) {
    case X86::COND_B: {
      // Test CF == 1
      int CFIndex = getEflagBitIndex(EFLAGS::CF);
      Value *CFValue = CTRec->RegValues[CFIndex];
      assert(CFValue != nullptr &&
             "Failed to get EFLAGS value while raising JB");
      // Construct a compare instruction
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, CFValue, TrueValue,
                                "CmpCF_JB");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_E: {
      // Test ZF == 1
      int ZFIndex = getEflagBitIndex(EFLAGS::ZF);
      Value *ZFValue = CTRec->RegValues[ZFIndex];
      assert(ZFValue != nullptr &&
             "Failed to get EFLAGS value while raising JE");
      // Construct a compare instruction
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, ZFValue, TrueValue,
                                "CmpZF_JE");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_NE: {
      // Test ZF == 0
      int ZFIndex = getEflagBitIndex(EFLAGS::ZF);
      Value *ZFValue = CTRec->RegValues[ZFIndex];
      assert(ZFValue != nullptr &&
             "Failed to get EFLAGS value while raising JNE");
      // Construct a compare instruction
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, ZFValue,
                                FalseValue, "CmpZF_JNE");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_S: {
      // Test SF == 1
      int SFIndex = getEflagBitIndex(EFLAGS::SF);
      Value *SFValue = CTRec->RegValues[SFIndex];
      assert(SFValue != nullptr &&
             "Failed to get EFLAGS value while raising JS");
      // Construct a compare instruction
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, SFValue, TrueValue,
                                "CmpSF_JS");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_NS: {
      // Test SF == 0
      int SFIndex = getEflagBitIndex(EFLAGS::SF);
      Value *SFValue = CTRec->RegValues[SFIndex];
      assert(SFValue != nullptr &&
             "Failed to get EFLAGS value while raising JNS");
      // Construct a compare instruction
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, SFValue,
                                FalseValue, "CmpSF_JNS");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_A: {
      // CF == 0 and ZF == 0
      int CFIndex = getEflagBitIndex(EFLAGS::CF);
      int ZFIndex = getEflagBitIndex(EFLAGS::ZF);
      Value *CFValue = CTRec->RegValues[CFIndex];
      Value *ZFValue = CTRec->RegValues[ZFIndex];

      assert((CFValue != nullptr) && (ZFValue != nullptr) &&
             "Failed to get EFLAGS value while raising JA");
      // Test CF == 0
      Instruction *CFCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, CFValue,
                                         FalseValue, "CFCmp_JA");
      CandBB->getInstList().push_back(CFCond);
      // Test ZF == 0
      Instruction *ZFCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, ZFValue,
                                         FalseValue, "ZFCmp_JA");
      CandBB->getInstList().push_back(ZFCond);
      BranchCond = BinaryOperator::CreateAnd(ZFCond, CFCond, "CFAndZF_JA");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_AE: {
      // CF == 0
      int CFIndex = getEflagBitIndex(EFLAGS::CF);
      Value *CFValue = CTRec->RegValues[CFIndex];
      assert(CFValue != nullptr &&
             "Failed to get EFLAGS value while raising JAE");
      // Compare CF == 0
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, CFValue,
                                FalseValue, "CFCmp_JAE");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_BE: {
      // CF == 1 or ZF == 1
      int CFIndex = getEflagBitIndex(EFLAGS::CF);
      int ZFIndex = getEflagBitIndex(EFLAGS::ZF);
      Value *CFValue = CTRec->RegValues[CFIndex];
      Value *ZFValue = CTRec->RegValues[ZFIndex];
      assert((CFValue != nullptr) && (ZFValue != nullptr) &&
             "Failed to get EFLAGS value while raising JBE");
      // Compare CF == 1
      Instruction *CFCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, CFValue,
                                         TrueValue, "CFCmp_JBE");
      CandBB->getInstList().push_back(CFCond);
      // Compare ZF == 1
      Instruction *ZFCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, ZFValue,
                                         TrueValue, "ZFCmp_JBE");
      CandBB->getInstList().push_back(ZFCond);
      BranchCond = BinaryOperator::CreateOr(ZFCond, CFCond, "CFAndZF_JBE");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_G: {
      // ZF == 0 and (SF == OF)
      int ZFIndex = getEflagBitIndex(EFLAGS::ZF);
      int SFIndex = getEflagBitIndex(EFLAGS::SF);
      int OFIndex = getEflagBitIndex(EFLAGS::OF);
      Value *ZFValue = CTRec->RegValues[ZFIndex];
      Value *SFValue = CTRec->RegValues[SFIndex];
      Value *OFValue = CTRec->RegValues[OFIndex];
      Instruction *ZFCond = nullptr;
      Instruction *SFOFCond = nullptr;
      assert(((ZFValue != nullptr) && (SFValue != nullptr) &&
              (OFValue != nullptr)) &&
             "Failed to get EFLAGS value while raising JG");
      // Compare ZF and 0
      ZFCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, ZFValue, FalseValue,
                            "ZFCmp_JG");
      CandBB->getInstList().push_back(ZFCond);
      // Test SF == OF
      SFOFCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, SFValue, OFValue,
                              "SFOFCmp_JG");
      CandBB->getInstList().push_back(SFOFCond);
      BranchCond = BinaryOperator::CreateAnd(ZFCond, SFOFCond, "ZFAndSFOF_JG");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_GE: {
      // SF == OF
      int SFIndex = getEflagBitIndex(EFLAGS::SF);
      int OFIndex = getEflagBitIndex(EFLAGS::OF);
      Value *SFValue = CTRec->RegValues[SFIndex];
      Value *OFValue = CTRec->RegValues[OFIndex];
      assert(SFValue != nullptr && OFValue != nullptr &&
             "Failed to get EFLAGS value while raising JGE");
      // Compare SF and OF
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, SFValue, OFValue,
                                "CmpSFOF_JGE");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_L: {
      // SF != OF
      int SFIndex = getEflagBitIndex(EFLAGS::SF);
      int OFIndex = getEflagBitIndex(EFLAGS::OF);
      Value *SFValue = CTRec->RegValues[SFIndex];
      Value *OFValue = CTRec->RegValues[OFIndex];
      assert(((SFValue != nullptr) && (OFValue != nullptr)) &&
             "Failed to get EFLAGS value while raising JL");
      // Test SF != OF
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_NE, SFValue, OFValue,
                                "SFAndOF_JL");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_LE: {
      // ZF == 1 or (SF != OF)
      int ZFIndex = getEflagBitIndex(EFLAGS::ZF);
      int SFIndex = getEflagBitIndex(EFLAGS::SF);
      int OFIndex = getEflagBitIndex(EFLAGS::OF);
      Value *ZFValue = CTRec->RegValues[ZFIndex];
      Value *SFValue = CTRec->RegValues[SFIndex];
      Value *OFValue = CTRec->RegValues[OFIndex];
      Instruction *ZFCond = nullptr;
      Instruction *SFOFCond = nullptr;
      assert(((ZFValue != nullptr) && (SFValue != nullptr) &&
              (OFValue != nullptr)) &&
             "Failed to get EFLAGS value while raising JLE");
      // Compare ZF and 1
      ZFCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, ZFValue, TrueValue,
                            "CmpZF_JLE");
      CandBB->getInstList().push_back(ZFCond);
      // Test SF != OF
      SFOFCond = new ICmpInst(CmpInst::Predicate::ICMP_NE, SFValue, OFValue,
                              "CmpOF_JLE");
      CandBB->getInstList().push_back(SFOFCond);
      BranchCond = BinaryOperator::CreateOr(ZFCond, SFOFCond, "ZFOrSF_JLE");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    // Parity flag is set by instructions that abstract unordered
    // result of SSE compare instructions.
    // NOTE: Setting of PF is not modeled while abstracting non-SSE2
    // instructions
    case X86::COND_P: {
      // Test PF == 1
      int PFIndex = getEflagBitIndex(EFLAGS::PF);
      Value *PFValue = CTRec->RegValues[PFIndex];
      assert(PFValue != nullptr &&
             "Failed to get EFLAGS value while raising JP");
      // Construct a compare instruction
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, PFValue, TrueValue,
                                "CmpPF_JP");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_NP: {
      // Test PF == 0
      int PFIndex = getEflagBitIndex(EFLAGS::PF);
      Value *PFValue = CTRec->RegValues[PFIndex];
      assert(PFValue != nullptr &&
             "Failed to get EFLAGS value while raising JNP");
      // Construct a compare instruction
      BranchCond = new ICmpInst(CmpInst::Predicate::ICMP_EQ, PFValue,
                                FalseValue, "CmpPF_JNP");
      CandBB->getInstList().push_back(dyn_cast<Instruction>(BranchCond));
    } break;
    case X86::COND_INVALID:
      assert(false && "Invalid condition on branch");
      break;
    default:
      LLVM_DEBUG(MI->dump());
      assert(false && "Unhandled conditional branch");
    }

    // Create branch instruction
    BranchInst *CondBr = BranchInst::Create(TgtBB, FTBB, BranchCond);
    CandBB->getInstList().push_back(CondBr);
    CTRec->Raised = true;
  } else {
    assert(false && "Unhandled type of branch instruction");
  }
  return true;
}

// Raise a generic instruction. This is the catch all MachineInstr raiser
bool X86MachineInstructionRaiser::raiseGenericMachineInstr(
    const MachineInstr &MI) {
  unsigned int Opcode = MI.getOpcode();
  bool Success = false;

  // Now raise the instruction according to the opcode kind
  switch (getInstructionKind(Opcode)) {
  case InstructionKind::BINARY_OP_WITH_IMM:
    Success = raiseBinaryOpImmToRegMachineInstr(MI);
    break;
  case InstructionKind::BINARY_OP_MRI_OR_MRC:
    Success = raiseBinaryOpMRIOrMRCEncodedMachineInstr(MI);
    break;
  case InstructionKind::CONVERT_BWWDDQ:
    Success = raiseConvertBWWDDQMachineInstr(MI);
    break;
  case InstructionKind::CONVERT_WDDQQO:
    Success = raiseConvertWDDQQOMachineInstr(MI);
    break;
  case InstructionKind::LEA_OP:
    Success = raiseLEAMachineInstr(MI);
    break;
  case InstructionKind::MOV_RR:
    Success = raiseMoveRegToRegMachineInstr(MI);
    break;
  case InstructionKind::MOV_RI:
    Success = raiseMoveImmToRegMachineInstr(MI);
    break;
  case InstructionKind::BINARY_OP_RR:
    Success = raiseBinaryOpRegToRegMachineInstr(MI);
    break;
  case InstructionKind::SETCC:
    Success = raiseSetCCMachineInstr(MI);
    break;
  case InstructionKind::COMPARE:
    Success = raiseCompareMachineInstr(MI, false, nullptr);
    break;
  case InstructionKind::FPU_REG_OP:
    Success = raiseFPURegisterOpInstr(MI);
    break;
  case InstructionKind::DIVIDE_REG_OP: {
    const MachineOperand &SrcOp = MI.getOperand(0);
    assert(SrcOp.isReg() &&
           "Expect register source operand of a div instruction");
    Value *SrcVal =
        getRegOrArgValue(SrcOp.getReg(), MI.getParent()->getNumber());
    Success = raiseDivideInstr(MI, SrcVal);
  } break;
  case InstructionKind::BIT_TEST_OP:
    Success = raiseBitTestMachineInstr(MI, nullptr, false);
    break;
  case InstructionKind::SSE_MOV_RR:
    Success = raiseSSEMoveRegToRegMachineInstr(MI);
    break;
  case InstructionKind::SSE_COMPARE_RR:
    Success = raiseSSECompareMachineInstr(MI);
    break;
  case InstructionKind::SSE_CONVERT_RR:
    Success = raiseSSEConvertPrecisionMachineInstr(MI);
    break;
  default: {
    dbgs() << "*** Generic instruction not raised : " << MF.getName().data()
           << "\n\t";
    MI.print(dbgs());
    Success = false;
  }
  }
  return Success;
}

// Raise a return instruction.
bool X86MachineInstructionRaiser::raiseReturnMachineInstr(
    const MachineInstr &MI) {
  Type *RetType = RaisedFunction->getReturnType();
  Value *RetValue = nullptr;

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  unsigned int RetReg = X86::NoRegister;
  if (!RetType->isVoidTy()) {
    if (RetType->isPointerTy())
      RetReg = X86::RAX;
    else if (RetType->isIntegerTy()) {
      switch (RetType->getPrimitiveSizeInBits()) {
      case 64:
        RetReg = X86::RAX;
        break;
      case 32:
        RetReg = X86::EAX;
        break;
      case 16:
        RetReg = X86::AX;
        break;
      case 8:
        RetReg = X86::AL;
        break;
      default:
        llvm_unreachable("Unhandled return type");
      }
    } else if (RetType->isFloatingPointTy() || RetType->isVectorTy()) {
      switch (RetType->getPrimitiveSizeInBits()) {
      case 128:
      case 64:
      case 32:
        RetReg = X86::XMM0;
        break;
      default:
        llvm_unreachable("Unhandled return type");
      }
    } else {
      llvm_unreachable("Unhandled return type");
    }
    RetValue =
        raisedValues->getReachingDef(RetReg, MI.getParent()->getNumber(), true);
  }

  // If RetType is a pointer type and RetValue type is 64-bit, cast RetValue
  // appropriately.
  if ((RetValue != nullptr) && RetType->isPointerTy() && (RetReg == X86::RAX))
    RetValue = getRaisedValues()->castValue(RetValue, RetType, RaisedBB);

  // Ensure RetValue type match RetType
  if (RetValue != nullptr) {
    if (RetReg == X86::XMM0) {
      RetValue = getRaisedValues()->reinterpretSSERegValue(RetValue, RetType,
                                                           RaisedBB);
    } else {
      RetValue = getRaisedValues()->castValue(RetValue, RetType, RaisedBB);
    }
  }

  // Create return instruction
  Instruction *RetInstr =
      ReturnInst::Create(MF.getFunction().getContext(), RetValue);
  RaisedBB->getInstList().push_back(RetInstr);

  // Make sure that the return type of raisedFunction is void. Else change it to
  // void type as reaching definition computation is more accurate than that
  // deduced earlier just looking at the per-basic block definitions.
  Type *RaisedFuncReturnTy = RaisedFunction->getReturnType();
  if (RetValue == nullptr) {
    if (!RaisedFuncReturnTy->isVoidTy()) {
      ModuleRaiser *NonConstMR = const_cast<ModuleRaiser *>(MR);
      NonConstMR->changeRaisedFunctionReturnType(
          RaisedFunction, Type::getVoidTy(MF.getFunction().getContext()));
    }
  }

  return true;
}

bool X86MachineInstructionRaiser::raiseBranchMachineInstrs() {
  LLVM_DEBUG(dbgs() << "CFG : Before Raising Terminator Instructions\n");
  LLVM_DEBUG(RaisedFunction->dump());

  // Raise branch instructions with control transfer records
  bool Success = true;
  for (ControlTransferInfo *CTRec : CTInfo) {
    if (CTRec->CandidateMachineInstr->isBranch()) {
      const MachineInstr *MI = CTRec->CandidateMachineInstr;
      const MCInstrDesc &MCID = MI->getDesc();
      uint64_t Imm = MCID.TSFlags & X86II::ImmMask;

      if ((Imm == X86II::Imm8PCRel) || (Imm == X86II::Imm16PCRel) ||
          (Imm == X86II::Imm32PCRel)) {
        Success &= raiseDirectBranchMachineInstr(CTRec);
        assert(Success && "Failed to raise direct branch instruction");
      } else {
        Success &= raiseIndirectBranchMachineInstr(CTRec);
        assert(Success && "Failed to raise indirect branch instruction");
      }
    }
  }

  // Delete all ControlTransferInfo records of branch instructions
  // that were raised.
  if (!CTInfo.empty()) {
    CTInfo.erase(std::remove_if(CTInfo.begin(), CTInfo.end(),
                                [](const ControlTransferInfo *CTI) {
                                  return CTI->Raised;
                                }),
                 CTInfo.end());
  }
  assert(CTInfo.empty() && "Unhandled branch instructions exist");

  // Note that for basic blocks that fall-through and have no terminator,
  // no control transfer record is created. Insert branch instructions
  // at the end of all such blocks.

  // Walk basic blocks of the MachineFunction.
  for (MachineFunction::iterator MFBegIter = MF.begin(), MFEndIter = MF.end();
       MFBegIter != MFEndIter; MFBegIter++) {
    MachineBasicBlock &MBB = *MFBegIter;
    // Get the number of MachineBasicBlock being looked at.
    // If MBB has no terminators, insert a branch to the fall through edge.
    if (MBB.getFirstTerminator() == MBB.end()) {
      if (MBB.succ_size() > 0) {
        // Find the BasicBlock corresponding to MBB
        auto Iter = mbbToBBMap.find(MBB.getNumber());
        assert(Iter != mbbToBBMap.end() &&
               "Unable to find BasicBlock to insert unconditional branch");
        BasicBlock *BB = Iter->second;
        // skip basic blocks that already contain an 'unreachable' terminator
        if (BB->getTerminator() != nullptr) {
          assert(isa<UnreachableInst>(BB->getTerminator()) &&
                 "Expecting unreachable instruction");
          continue;
        }

        // Find the BasicBlock corresponding to the successor of MBB
        MachineBasicBlock *SuccMBB = *(MBB.succ_begin());
        Iter = mbbToBBMap.find(SuccMBB->getNumber());
        assert(Iter != mbbToBBMap.end() &&
               "Unable to find successor BasicBlock");
        BasicBlock *SuccBB = Iter->second;

        // Create a branch instruction targeting SuccBB
        BranchInst *UncondBr = BranchInst::Create(SuccBB);
        BB->getInstList().push_back(UncondBr);
      }
    }
  }
  LLVM_DEBUG(dbgs() << "CFG : After Raising Terminator Instructions\n");
  LLVM_DEBUG(RaisedFunction->dump());

  return true;
}

// Raise FPU instructions
bool X86MachineInstructionRaiser::raiseFPURegisterOpInstr(
    const MachineInstr &MI) {

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  // Construct the appropriate instruction
  unsigned Opcode = MI.getOpcode();
  switch (Opcode) {
  case X86::ADD_FPrST0:
  case X86::MUL_FPrST0:
  case X86::DIV_FPrST0: {
    Value *St0Val = getFPURegisterStackValueAt(0);
    assert((St0Val != nullptr) && "Failed to get ST(0) value");
    Type *St0ValTy = St0Val->getType();
    assert(St0ValTy->isFloatingPointTy() &&
           "Unexpected non-FP value on FPU register stack");
    assert((MI.getNumExplicitOperands() == 1) &&
           "Unexpected number of operands in FP register op instruction "
           "format");
    const MachineOperand &StRegOp = MI.getOperand(0);
    assert(StRegOp.isReg() &&
           "Unexpected non-register operand of FP register op instruction");
    int8_t FPRegIndex = StRegOp.getReg() - X86::ST0;
    assert((FPRegIndex >= 0) && (FPRegIndex < FPUSTACK_SZ) &&
           "Unexpected FPU register stack index computed");
    Value *StVal = getFPURegisterStackValueAt(FPRegIndex);
    assert((StVal != nullptr) && "Failed to get value of FPU register");
    if (StVal->getType() != St0ValTy) {
      CastInst *CInst = CastInst::Create(
          CastInst::getCastOpcode(StVal, false, St0ValTy, false), StVal,
          St0ValTy);
      RaisedBB->getInstList().push_back(CInst);
      StVal = CInst;
    }
    // Create fp operation
    Instruction *FPRegOpInstr = nullptr;
    if (Opcode == X86::MUL_FPrST0) {
      FPRegOpInstr = BinaryOperator::CreateFMul(StVal, St0Val);
    } else if (Opcode == X86::DIV_FPrST0) {
      FPRegOpInstr = BinaryOperator::CreateFDiv(StVal, St0Val);
    } else if (Opcode == X86::ADD_FPrST0) {
      FPRegOpInstr = BinaryOperator::CreateFAdd(StVal, St0Val);
    }
    RaisedBB->getInstList().push_back(FPRegOpInstr);
    // Update the FP register FPRegIndex with FPRegOpInstr
    setFPURegisterStackValueAt(FPRegIndex, FPRegOpInstr);
    // Pop FPU register stack
    popFPURegisterStack();
  } break;
  default: {
    assert(false && "Unhandled FPU instruction");
  } break;
  }

  return true;
}

// Raise Call instruction
bool X86MachineInstructionRaiser::raiseCallMachineInstr(
    const MachineInstr &MI) {
  unsigned int Opcode = MI.getOpcode();

  // Get the BasicBlock corresponding to MachineBasicBlock of MI.
  // Raised instruction is added to this BasicBlock.
  BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());

  bool Success = false;
  switch (Opcode) {
    // case X86::CALLpcrel16   :
    // case X86::CALLpcrel32   :
  case X86::CALL64pcrel32:
  case X86::JMP_1:
  case X86::JMP_4: {
    Function *CalledFunc = getCalledFunction(MI);
    LLVMContext &Ctx(MF.getFunction().getContext());

    assert(CalledFunc != nullptr && "Failed to detect call target");
    std::vector<Value *> CallInstFuncArgs;
    unsigned NumGPArgs = CalledFunc->arg_size();
    Argument *CalledFuncArgs = CalledFunc->arg_begin();

    // check number of floating point args
    unsigned NumSSEArgs = 0;
    for (unsigned Idx = 0; Idx < NumGPArgs; ++Idx) {
      if (CalledFunc->getArg(Idx)->getType()->isFloatingPointTy()) {
        ++NumSSEArgs;
      }
    }
    // NumGPArgs refers to just general purpose registers
    NumGPArgs -= NumSSEArgs;

    if (CalledFunc->isVarArg()) {
      // Discover argument registers that are live just before the CallMI.
      // Liveness of the blocks is already computed in
      // getRaisedFunctionPrototype(). So no need to run it again since no
      // MBB would be modified.
      // MachineBasicBlock::const_reverse_iterator CallInstIter(MI);
      // Find the highest argument register that is defined in the block
      // before the CallMI. NOTE : We assume that all arguments are setup
      // prior to the call. This argument setup manifests as defines in the
      // block or a combination of argument registers that are live-in and
      // defines in the block. Additionally, if the block has more than one
      // calls, it is assumed that call setup for all calls other than the
      // first is done entirely in the block after the preceding call. In
      // such a situation, there is no need to look for argument registers
      // in the live-ins of the block.

      // Bit mask to keep track of argument register positions already
      // discovered.
      uint8_t PositionMask = 0;

      const MachineBasicBlock *CurMBB = MI.getParent();
      // If an argument register does not have a definition in a block that
      // has a call instruction between block entry and MI, there is no need
      // (and is not correct) to look for a reaching definition in its
      // predecessors.
      bool HasCallInst = false;
      unsigned int ArgNo = 1;
      // Find if CurMBB has call between block entry and MI

      for (auto ArgReg : GPR64ArgRegs64Bit) {
        if (getPhysRegDefiningInstInBlock(ArgReg, &MI, CurMBB, MCID::Call,
                                          HasCallInst) != nullptr)
          PositionMask |= (1 << ArgNo);
        else if (!HasCallInst) {
          // Look to see if the argument register has a reaching definition in
          // the predecessors of CurMBB.
          unsigned int ReachDefPredEdgeCount = 0;

          for (auto *P : CurMBB->predecessors()) {
            SmallVector<MachineBasicBlock *, 8> WorkList;
            // No blocks visited in this walk up the predecessor P
            BitVector BlockVisited(MF.getNumBlockIDs(), false);

            // CurMBB has already been visited. Mark it so.
            BlockVisited.set(CurMBB->getNumber());
            // Start at predecessor P
            WorkList.push_back(P);

            while (!WorkList.empty()) {
              MachineBasicBlock *PredMBB = WorkList.pop_back_val();
              if (!BlockVisited[PredMBB->getNumber()]) {
                // Mark block as visited
                BlockVisited.set(PredMBB->getNumber());
                // Need to consider definitions after any call instructions in
                // the block. This is the reason we can not use
                // getReachingDefs() which does not consider the position
                // where the register is defined.
                bool Ignored;
                if (getPhysRegDefiningInstInBlock(ArgReg, nullptr, PredMBB,
                                                  MCID::Call,
                                                  Ignored) != nullptr)
                  ReachDefPredEdgeCount++;
                else {
                  // Reach info not found, continue walking the predecessors
                  // of CurBB.
                  for (auto *Block : PredMBB->predecessors()) {
                    // push_back the block which was not visited.
                    if (!BlockVisited[Block->getNumber()])
                      WorkList.push_back(Block);
                  }
                }
              }
            }
          }
          // If there is a reaching def on all predecessor edges then consider
          // it as an argument used by the variadic function.
          if ((ReachDefPredEdgeCount > (unsigned)0) &&
              (ReachDefPredEdgeCount == CurMBB->pred_size()))
            PositionMask |= (1 << ArgNo);
        }
        ArgNo++;
      }

      // Find the number of arguments
      // NOTE: Handling register arguments - 6 in number. Need to handle
      // arguments passed on stack make sure bit 8 and bit 0 are not set
      assert(!(PositionMask & 1) && !(PositionMask & (1 << 7)) &&
             "Invalid number of arguments discovered");
      uint8_t ShftPositionMask = PositionMask >> 1;
      uint8_t NumGPArgsDiscovered = 0;
      // Consider only consecutive argument registers.
      while (ShftPositionMask & 1) {
        ShftPositionMask = ShftPositionMask >> 1;
        NumGPArgsDiscovered++;
      }
      // If number of arguments discovered is greater than CalledFunc
      // arguments use that as the number of arguments of the called
      // function.
      if (NumGPArgsDiscovered > NumGPArgs) {
        NumGPArgs = NumGPArgsDiscovered;
      }

      uint8_t NumSSEArgsDiscovered = 0;
      // Get the number of vector registers used
      // When using the x86_64 System V ABI, RAX holds the number of vector
      // registers used
      bool Ignored;
      auto *Instr = getPhysRegDefiningInstInBlock(X86::AL, &MI, CurMBB,
                                                  MCID::Call, Ignored);
      if (Instr != nullptr && Instr->getNumOperands() > 0) {
        // With the System V X86_64 ABI the compiler generates an instruction
        // like mov al, 1 with the number of vector arguments for the varargs
        // call
        const MachineOperand &SrcOp = Instr->getOperand(1);
        if (SrcOp.isImm()) {
          NumSSEArgsDiscovered = (uint8_t)SrcOp.getImm();
        }
      }
      // If number of vector args discovered is greater than CalledFunc
      // arguments, but still in the range of allowed number of vector
      // argument registers, use that as the number of vector args
      if (NumSSEArgsDiscovered > NumSSEArgs &&
          NumGPArgsDiscovered <= SSEArgRegs64Bit.size()) {
        NumSSEArgs = NumSSEArgsDiscovered;
      }
    }
    // Construct the argument list with values to be used to construct a new
    // CallInst. These values are those of the physical registers as defined
    // in C calling convention (the calling convention currently supported).
    for (unsigned Idx = 0; Idx < NumGPArgs + NumSSEArgs; Idx++) {
      // First check all GP registers, then FP registers
      MCPhysReg ArgReg = Idx < NumGPArgs ? GPR64ArgRegs64Bit[Idx]
                                         : SSEArgRegs64Bit[Idx - NumGPArgs];
      // Get the values of argument registers
      // Do not match types since we are explicitly using 64-bit GPR array.
      // Any necessary casting will be done later in this function.
      Value *ArgVal = getRegOrArgValue(ArgReg, MI.getParent()->getNumber());
      // This condition will not be true for varargs of a variadic function.
      // In that case just add the value.
      if (Idx < CalledFunc->arg_size()) {
        // If the ConstantInt value is being treated as a pointer (i.e., is
        // an address, try to construct the associated global read-only data
        // value.
        Argument &FuncArg = CalledFuncArgs[Idx];
        if (ArgVal == nullptr) {
          // Most likely the argument register corresponds to an argument value
          // that is not used in the function body. Just initialize it to 0.
          if (FuncArg.getType()->isIntOrPtrTy()) {
            ArgVal =
                ConstantInt::get(FuncArg.getType(), 0, false /* isSigned */);
          } else if (FuncArg.getType()->isFloatingPointTy()) {
            ArgVal = ConstantFP::get(FuncArg.getType(), 0.0);
          } else {
            FuncArg.getType()->dump();
            llvm_unreachable("Unsupported argument type");
          }
        } else if (isa<ConstantInt>(ArgVal)) {
          ConstantInt *Address = dyn_cast<ConstantInt>(ArgVal);
          if (!Address->isNegative()) {
            Value *RefVal = getOrCreateGlobalRODataValueAtOffset(
                Address->getSExtValue(), RaisedBB);
            if (RefVal != nullptr) {
              assert(RefVal->getType()->isPointerTy() &&
                     "Non-pointer type of global value abstracted from "
                     "address");
              ArgVal = RefVal;
            }
          }
        }
        if (FuncArg.getType()->isFloatingPointTy() ||
            FuncArg.getType()->isVectorTy()) {
          ArgVal = getRaisedValues()->reinterpretSSERegValue(
              ArgVal, FuncArg.getType(), RaisedBB);
        } else {
          ArgVal =
              getRaisedValues()->castValue(ArgVal, FuncArg.getType(), RaisedBB);
        }
      } else {
        // for varadic arguments, reinterpret vectors as doubles
        if (ArgVal->getType()->isVectorTy()) {
          ArgVal = getRaisedValues()->reinterpretSSERegValue(
              ArgVal, Type::getDoubleTy(Ctx), RaisedBB);
        }
      }
      assert(ArgVal != nullptr && "Unexpected null argument value");
      CallInstFuncArgs.push_back(ArgVal);
    }

    // Construct call inst.
    Instruction *CInst =
        CallInst::Create(CalledFunc, ArrayRef<Value *>(CallInstFuncArgs));

    // If this is a branch being turned to a tail call set the flag
    // accordingly.
    if (MI.isBranch())
      dyn_cast<CallInst>(CInst)->setTailCall(true);

    RaisedBB->getInstList().push_back(CInst);
    // A function call with a non-void return will modify
    // RAX (or its sub-register).
    Type *RetType = CalledFunc->getReturnType();
    if (!RetType->isVoidTy()) {
      unsigned int RetReg = X86::NoRegister;
      if (RetType->isPointerTy()) {
        // Cast pointer return type to 64-bit type
        Type *CastTy = Type::getInt64Ty(Ctx);
        Instruction *CstInst = CastInst::Create(
            CastInst::getCastOpcode(CInst, false, CastTy, false), CInst, CastTy,
            "", RaisedBB);
        CInst = CstInst;
        RetReg = X86::RAX;
      } else if (RetType->isIntegerTy()) {
        switch (RetType->getScalarSizeInBits()) {
        case 64:
          RetReg = X86::RAX;
          break;
        case 32:
          RetReg = X86::EAX;
          break;
        case 16:
          RetReg = X86::AX;
          break;
        case 8:
          RetReg = X86::AL;
          break;
        default:
          assert(false && "Unhandled return value size");
        }
      } else if (RetType->isFloatingPointTy() || RetType->isVectorTy()) {
        switch (RetType->getPrimitiveSizeInBits()) {
        case 128:
        case 64:
        case 32:
          RetReg = X86::XMM0;
          break;
        default:
          llvm_unreachable("Unhandled return value size");
        }
      } else {
        llvm_unreachable_internal("Unhandled return type");
      }
      raisedValues->setPhysRegSSAValue(RetReg, MI.getParent()->getNumber(),
                                       CInst);
    }
    if (MI.isBranch()) {
      // Emit appropriate ret instruction. There will be no ret instruction
      // in the binary since this is a tail call.
      ReturnInst *RetInstr;
      if (RetType->isVoidTy())
        RetInstr = ReturnInst::Create(Ctx);
      else {
        RetInstr = ReturnInst::Create(Ctx, CInst);
        ModuleRaiser *NonConstMR = const_cast<ModuleRaiser *>(MR);
        NonConstMR->changeRaisedFunctionReturnType(RaisedFunction,
                                                   CInst->getType());
      }
      RaisedBB->getInstList().push_back(RetInstr);
    }
    // Add 'unreachable' instruction after CallInstr if it is a call to glibc
    // function 'void exit(int)'
    // 'void __assert_fail(char *, char *, unsigned int, char *)'
    if (CalledFunc->getName().equals("exit")) {
      FunctionType *FT = CalledFunc->getFunctionType();
      if (FT->getReturnType()->isVoidTy() && (FT->getNumParams() == 1) &&
          FT->getParamType(0)->isIntegerTy(32)) {
        Instruction *UR = new UnreachableInst(Ctx);
        RaisedBB->getInstList().push_back(UR);
        // Mark callInst as tail call
        dyn_cast<CallInst>(CInst)->setTailCall(true);
      }
    } else if (CalledFunc->getName().equals("__assert_fail")) {
      FunctionType *FT = CalledFunc->getFunctionType();
      if (FT->getReturnType()->isVoidTy() && FT->getNumParams() == 4 &&
          FT->getParamType(0)->isPointerTy() &&
          FT->getParamType(1)->isPointerTy() &&
          FT->getParamType(2)->isIntegerTy() &&
          FT->getParamType(3)->isPointerTy()) {
        Instruction *UR = new UnreachableInst(Ctx);
        RaisedBB->getInstList().push_back(UR);
        // Mark CInst as tail call
        dyn_cast<CallInst>(CInst)->setTailCall(true);
      }
    }
    Success = true;
  } break;
  case X86::CALL64m:
  case X86::CALL64r: {
    LLVMContext &Ctxt(MF.getFunction().getContext());
    BasicBlock *RaisedBB = getRaisedBasicBlock(MI.getParent());
    const MachineBasicBlock *MBB = MI.getParent();
    int MBBNo = MBB->getNumber();

    std::vector<Type *> ArgTypeVector;
    std::vector<Value *> ArgValueVector;

    // Find all sequentially reachable argument register defintions at call site
    for (auto Reg : GPR64ArgRegs64Bit) {
      Value *RD = // getRegOrArgValue(Reg, MBBNo);
          raisedValues->getReachingDef(Reg, MBBNo, true /* on all preds */,
                                       true /* any subreg */);
      if (RD == nullptr) {
        break;
      }
      ArgTypeVector.push_back(RD->getType());
      ArgValueVector.push_back(RD);
    }
    for (auto Reg : SSEArgRegs64Bit) {
      Value *RD = raisedValues->getReachingDef(Reg, MBBNo, true, true);
      if (RD == nullptr) {
        break;
      }
      if (RD->getType()->isVectorTy()) {
        RD = raisedValues->reinterpretSSERegValue(RD, Type::getDoubleTy(Ctxt),
                                                  RaisedBB);
      }
      ArgTypeVector.push_back(RD->getType());
      ArgValueVector.push_back(RD);
    }

    // Find if return register is used before the end of the block with call
    // instruction. If so, consider that to indicate the return value of the
    // called function.
    bool BlockHasCall;
    Type *ReturnType = getReturnTypeFromMBB(*MBB, BlockHasCall /* ignored*/);

    // If return type not found, consider it to be void type
    if (ReturnType == nullptr) {
      ReturnType = Type::getVoidTy(MF.getFunction().getContext());
    }

    // Build Function type.
    auto *FT = FunctionType::get(ReturnType, ArgTypeVector, false);

    Value *Func;
    // Get function pointer address.
    if (Opcode == X86::CALL64r) {
      Register CallReg = MI.getOperand(0).getReg();
      Func = getRegOrArgValue(CallReg, MBBNo);
    } else {
      Value *MemRefValue = getMemoryRefValue(MI);

      unsigned LoadOpIndex = 0;
      // Get index of memory reference in the instruction.

      // Load the value from memory location of memRefValue.
      // memRefVal is either an AllocaInst (stack access), GlobalValue (global
      // data access), an effective address value, element pointer or select
      // instruction.
      assert((isa<AllocaInst>(MemRefValue) ||
              isEffectiveAddrValue(MemRefValue) ||
              isa<GlobalValue>(MemRefValue) || isa<SelectInst>(MemRefValue) ||
              isa<GetElementPtrInst>(MemRefValue) ||
              MemRefValue->getType()->isPointerTy()) &&
             "Unexpected type of memory reference in CALL64m instruction");

      PointerType *PtrTy =
          PointerType::get(getPhysRegOperandType(MI, LoadOpIndex), 0);
      if ((isEffectiveAddrValue(MemRefValue)) || isa<SelectInst>(MemRefValue)) {
        IntToPtrInst *ConvIntToPtr = new IntToPtrInst(MemRefValue, PtrTy);
        // Set or copy rodata metadata, if any
        getRaisedValues()->setInstMetadataRODataIndex(MemRefValue,
                                                      ConvIntToPtr);
        RaisedBB->getInstList().push_back(ConvIntToPtr);
        MemRefValue = ConvIntToPtr;
      }
      assert(MemRefValue->getType()->isPointerTy() &&
             "Pointer type expected in load instruction");
      // Cast the pointer to match the size of memory being accessed by the
      // instruction, as needed.
      MemRefValue = getRaisedValues()->castValue(MemRefValue, PtrTy, RaisedBB);

      // Load the value from memory location
      Type *LdTy = getPointerElementType(MemRefValue);
      LoadInst *LdInst =
          new LoadInst(LdTy, MemRefValue, "memload", false, Align());
      LdInst = getRaisedValues()->setInstMetadataRODataContent(LdInst);
      RaisedBB->getInstList().push_back(LdInst);
      MemRefValue = LdInst;

      Func = MemRefValue;
    }

    // Cast the function pointer address to function type pointer.
    Type *FuncTy = FT->getPointerTo();
    if (Func->getType() != FuncTy) {
      CastInst *CInst = CastInst::Create(
          CastInst::getCastOpcode(Func, false, FuncTy, false), Func, FuncTy);
      RaisedBB->getInstList().push_back(CInst);
      Func = CInst;
    }

    // Construct call instruction.
    CallInst *CallInst =
        CallInst::Create(FT, Func, ArrayRef<Value *>(ArgValueVector));
    RaisedBB->getInstList().push_back(CallInst);

    // A function call with a non-void return will modify RAX.
    if (ReturnType && !ReturnType->isVoidTy())
      raisedValues->setPhysRegSSAValue(X86::RAX, MBBNo, CallInst);
    Success = true;
  } break;
  default: {
    assert(false && "Unhandled call instruction");
  } break;
  }

  return Success;
}

// Top-level function that calls appropriate function that raises
// a MachineInstruction.
// Returns true upon success.

bool X86MachineInstructionRaiser::raiseMachineInstr(MachineInstr &MI) {
  const MCInstrDesc &MIDesc = MI.getDesc();
  bool Success = false;

  if (MIDesc.mayLoad() || MIDesc.mayStore()) {
    Success = raiseMemRefMachineInstr(MI);
  } else if (MIDesc.isReturn()) {
    Success = raiseReturnMachineInstr(MI);
  } else {
    Success = raiseGenericMachineInstr(MI);
  }
  return Success;
}

// Raise MachineInstr in MachineFunction to MachineInstruction

bool X86MachineInstructionRaiser::raiseMachineFunction() {
  Function *CurFunction = getRaisedFunction();
  LLVMContext &Ctx(CurFunction->getContext());

  // Initialize the raised value tracking mechanism.
  raisedValues = new X86RaisedValueTracker(this);

  // Initialize the value set analysis class.
  valueSetAnalysis = new X86ValueSetAnalysis(this);

  Value *Zero64BitValue =
      ConstantInt::get(Type::getInt64Ty(Ctx), 0, false /* isSigned */);

  // Start with an assumption that value of EFLAGS is 0 at the
  // entry of each function.
  for (auto EFBit : EFlagBits)
    // raisedValues->setPhysRegSSAValue(b, 0, Zero1BitValue);
    raisedValues->setEflagBoolean(EFBit, 0, false);

  // Set values of some registers that appear to be used in main function to
  // 0.
  if (CurFunction->getName().equals("main")) {
    raisedValues->setPhysRegSSAValue(X86::RCX, 0, Zero64BitValue);
  }

  // Walk basic blocks of the MachineFunction in LoopTraversal - except that
  // do not walk the block coming from back edge.By performing this
  // traversal, the idea is to make sure predecessors are translated before
  // a block.

  // Raise all non control transfer MachineInstrs of each MachineBasicBlocks
  // of MachineFunction, except branch instructions.
  LoopTraversal Traversal;
  LoopTraversal::TraversalOrder TraversedMBBOrder = Traversal.traverse(MF);
  for (LoopTraversal::TraversedMBBInfo TraversedMBB : TraversedMBBOrder) {
    // Only perform the primary pass as we do not want to translate one
    // block more than once.
    if (!TraversedMBB.PrimaryPass)
      continue;
    MachineBasicBlock &MBB = *(TraversedMBB.MBB);
    // Get the number of MachineBasicBlock being looked at.
    int MBBNo = MBB.getNumber();
    // Name of the corresponding BasicBlock to be created
    std::string BBName = MBBNo == 0 ? "entry" : "bb." + std::to_string(MBBNo);
    // Create a BasicBlock instance corresponding to MBB being looked at.
    // The raised form of MachineInstr of MBB will be added to curBlock.
    // Do not use CurFunction here, as CurFunction might change if it's return
    // type is changed
    BasicBlock *CurIBB = BasicBlock::Create(Ctx, BBName, getRaisedFunction());
    // Record the mapping of the number of MBB to corresponding BasicBlock.
    // This information is used to raise branch instructions, if any, of the
    // MBB in a later walk of MachineBasicBlocks of MF.
    mbbToBBMap.insert(std::make_pair(MBBNo, CurIBB));
    // Walk MachineInsts of the MachineBasicBlock
    for (MachineInstr &MI : MBB.instrs()) {
      // Ignore padding instructions. ld uses nop and lld uses int3 for
      // alignment padding in text section.
      // TODO : For now ignore ENDBR instructions. These can be used as clues
      // for functions that are indirect branch targets.
      auto Opcode = MI.getOpcode();
      if (isNoop(Opcode) || (Opcode == X86::INT3) || (Opcode == X86::ENDBR32) ||
          (Opcode == X86::ENDBR64)) {
        continue;
      }
      // If this is a terminator instruction, record
      // necessary information to raise it in a later pass.
      if (MI.isTerminator() && !MI.isReturn()) {
        recordMachineInstrInfo(MI);
        continue;
      }
      if (MI.isCall()) {
        if (!raiseCallMachineInstr(MI)) {
          return false;
        }
      } else if (!raiseMachineInstr(MI)) {
        return false;
      }
      fprintf(stderr, "VSA at instruction: \n");
      MI.dump();
      valueSetAnalysis->dump();
      fprintf(stderr, "-------------------------\n");
    }
  }
  return createFunctionStackFrame() && raiseBranchMachineInstrs() &&
         handleUnpromotedReachingDefs() && handleUnterminatedBlocks();
}

bool X86MachineInstructionRaiser::raise() {
  bool Success = raiseMachineFunction();
  if (Success) {
    // Delete empty basic blocks with no predecessors
    SmallVector<BasicBlock *, 4> UnConnectedBEmptyBs;
    for (BasicBlock &BB : *RaisedFunction) {
      if (BB.hasNPredecessors(0) && BB.size() == 0)
        UnConnectedBEmptyBs.push_back(&BB);
    }

    DeleteDeadBlocks(ArrayRef<BasicBlock *>(UnConnectedBEmptyBs));

    // Unify all exit nodes of the raised function
    legacy::PassManager PM;
    PM.add(createUnifyFunctionExitNodesPass());
    PM.run(*(RaisedFunction->getParent()));

    valueSetAnalysis->dump();
  }
  return Success;
}

// NOTE : The following X86ModuleRaiser class function is defined here as
// they reference MachineFunctionRaiser class that has a forward declaration
// in ModuleRaiser.h.

// Create a new MachineFunctionRaiser object and add it to the list of
// MachineFunction raiser objects of this module.
MachineFunctionRaiser *X86ModuleRaiser::CreateAndAddMachineFunctionRaiser(
    Function *F, const ModuleRaiser *MR, uint64_t Start, uint64_t End) {
  MachineFunctionRaiser *MFR = new MachineFunctionRaiser(
      *M, MR->getMachineModuleInfo()->getOrCreateMachineFunction(*F), MR, Start,
      End);
  MFR->setMachineInstrRaiser(new X86MachineInstructionRaiser(
      MFR->getMachineFunction(), MR, MFR->getMCInstRaiser()));
  MFRaiserVector.push_back(MFR);
  return MFR;
}

#undef DEBUG_TYPE
