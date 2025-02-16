include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/.
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/include
  ${LLVM_BINARY_DIR}/tools/clang/include
)

add_llvm_library(mctollRaiser
  AlocType.cpp
  FunctionFilter.cpp
  IncludedFileInfo.cpp
  MachineFunctionRaiser.cpp
  MCInstOrData.cpp
  MCInstRaiser.cpp
  ModuleRaiser.cpp
  ReducedIntervalCongruence.cpp
  RuntimeFunction.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Core
  BitWriter
  CodeGen
  DebugInfoDWARF
  DebugInfoPDB
  Demangle
  MC
  MCDisassembler
  Object
  Symbolize
  Support
  )

target_link_libraries(mctollRaiser PRIVATE clangTooling clangBasic clangAST clangASTMatchers clangFrontend clangSerialization)