#ifndef TOOLS_LLVM_MCTOLL_ALOCTYPE_H_
#define TOOLS_LLVM_MCTOLL_ALOCTYPE_H_

#include "llvm/MC/MCRegister.h"

namespace llvm {
namespace mctoll {

class AlocType {

public:
  enum AlocTypeID {
    RegisterTy = 0,       // Register
    GlobalMemLocTy = 1,   // Global variables
    LocalMemLocTy = 2,    // Function variables
  };

private:

  AlocTypeID ID;
  union {
    MCRegister Register;
    uint64_t Address;
  };

public: 

  AlocType() = delete;
  AlocType(const MCRegister &Reg);
  AlocType(const AlocTypeID ID, const uint64_t Address);

  AlocTypeID getAlocTypeID() const { return ID; }

  bool isRegisterType() const { return ID == 0; }
  bool isGlobalMemLocType() const { return ID == 1; }
  bool isLocalMemLocType() const { return ID == 2; }

  MCRegister getRegister() const { return Register; }
  uint64_t getGlobalAddress() const { return Address; }
  uint64_t getLocalAddress() const { return Address; }

  bool operator==(const AlocType &a) const {
    switch (ID) {
      case 0:
        return a.getAlocTypeID() == 0 && Register == a.getRegister();
      case 1:
        return a.getAlocTypeID() == 1 && Address == a.getGlobalAddress();
      case 2:
        return a.getAlocTypeID() == 2 && Address == a.getLocalAddress();
      default:
        assert(false && "Invalid AlocTypeID");
    }
  }

};

} // end namespace mctoll
} // end namespace llvm

namespace std {
template<>
struct hash<llvm::mctoll::AlocType>
{
    size_t operator()(const llvm::mctoll::AlocType& a) const noexcept
    {
        size_t h1 = a.getAlocTypeID();
        size_t h2;
        if (h1 == 0) {
          h2 = hash_value(a.getRegister());
        } else {
          h2 = a.getGlobalAddress();
        }
        return h2 ^ (h1 << 1);
    }
};
} // end namespace std

#endif /* TOOLS_LLVM_MCTOLL_ALOCTYPE_H_ */