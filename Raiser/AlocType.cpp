#include "AlocType.h"

#define DEBUG_TYPE "mctoll"

using namespace llvm;
using namespace llvm::mctoll;

AlocType::AlocType(const MCRegister &Reg) {
    ID = AlocType::RegisterTy;
    Register = MCRegister(Reg.id());
}
  
AlocType::AlocType(const AlocTypeID cID, const uint64_t cAddress) {
    ID = cID;
    Address = cAddress;
}