include_directories(
  ${LLVM_MAIN_SRC_DIR}/lib/Target/X86
  ${LLVM_BINARY_DIR}/lib/Target/X86
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../Raiser
)

if(NOT LLVM_MCTOLL_BUILT_STANDALONE)
  set(LLVM_MCTOLL_DEPS intrinsics_gen X86CommonTableGen)
endif()

add_llvm_library(mctollX86Raiser
  X86AdditionalInstrInfo.cpp
  X86ModuleRaiser.cpp
  X86MachineInstructionRaiser.cpp
  X86MachineInstructionRaiserUtils.cpp
  X86MachineInstructionRaiserSSE.cpp
  X86JumpTables.cpp
  X86RaisedValueTracker.cpp
  X86RegisterUtils.cpp
  X86FuncPrototypeDiscovery.cpp
  X86ValueSetAnalysis.cpp

  LINK_COMPONENTS
  Analysis
  Core
  BitWriter
  X86CodeGen
  DebugInfoDWARF
  DebugInfoPDB
  Demangle
  MC
  MCDisassembler
  Object
  SelectionDAG
  Symbolize
  Support
  TransformUtils
  )

target_link_libraries(mctollX86Raiser PRIVATE mctollRaiser)
