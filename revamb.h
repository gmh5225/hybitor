#ifndef _REVAMB_H
#define _REVAMB_H

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// Standard includes
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

// LLVM includes
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"

namespace llvm {
class GlobalVariable;
};

/// \brief Type of debug information to produce
enum class DebugInfoType {
  None, ///< no debug information.
  OriginalAssembly, ///< produce a file containing the assembly code of the
                    ///  input binary.
  PTC, ///< produce the PTC as translated by libtinycode.
  LLVMIR ///< produce an LLVM IR with debug metadata referring to itself.
};

// TODO: move me to another header file
/// \brief Classification of the various basic blocks we are creating
enum BlockType {
  UntypedBlock, ///< A basic block generated during translation that it's not a
                ///  jump target.
  DispatcherBlock, ///< Basic block representing the dispatcher.
  AnyPCBlock, ///< Basic block used to handle an expectedly unknown jump target.
  UnexpectedPCBlock, ///< Basic block used to handle an unexpectedly unknown
                     ///  jump target.
  JumpTargetBlock ///< A basic block generated during translation representing a
                  ///  jump target.
};

/// \brief 输入输出体系架构的基本信息 Basic information about an input/output architecture
class Architecture {
public:

  enum EndianessType {
    LittleEndian,
    BigEndian
  };

public:
  Architecture() :
    InstructionAlignment(1),
    DefaultAlignment(1),
    Endianess(LittleEndian),
    PointerSize(64),
    DelaySlotSize(0) { }

  Architecture(unsigned Type,
               unsigned InstructionAlignment,
               unsigned DefaultAlignment,
               bool IsLittleEndian,
               unsigned PointerSize,
               llvm::StringRef SyscallHelper,
               llvm::StringRef SyscallNumberRegister,
               llvm::ArrayRef<uint64_t> NoReturnSyscalls,
               unsigned DelaySlotSize) :
    Type(static_cast<llvm::Triple::ArchType>(Type)),
    InstructionAlignment(InstructionAlignment),
    DefaultAlignment(DefaultAlignment),
    Endianess(IsLittleEndian ? LittleEndian : BigEndian),
    PointerSize(PointerSize),
    SyscallHelper(SyscallHelper),
    SyscallNumberRegister(SyscallNumberRegister),
    NoReturnSyscalls(NoReturnSyscalls),
    DelaySlotSize(DelaySlotSize) { }

  unsigned instructionAlignment() const { return InstructionAlignment; }
  unsigned defaultAlignment() const { return DefaultAlignment; }
  EndianessType endianess() const { return Endianess; }
  unsigned pointerSize() const { return PointerSize; }
  bool isLittleEndian() const { return Endianess == LittleEndian; }
  llvm::StringRef syscallHelper() const { return SyscallHelper; }
  llvm::StringRef syscallNumberRegister() const {
    return SyscallNumberRegister;
  }
  llvm::ArrayRef<uint64_t> noReturnSyscalls() const { return NoReturnSyscalls; }
  unsigned delaySlotSize() const { return DelaySlotSize; }
  const char *name() const { return llvm::Triple::getArchTypeName(Type); }

private:
  llvm::Triple::ArchType Type;

  unsigned InstructionAlignment;
  unsigned DefaultAlignment;
  EndianessType Endianess;
  unsigned PointerSize;

  llvm::StringRef SyscallHelper;
  llvm::StringRef SyscallNumberRegister;
  llvm::ArrayRef<uint64_t> NoReturnSyscalls;
  unsigned DelaySlotSize;
};

// TODO: this requires C++14
template<typename FnTy, FnTy Ptr>
struct GenericFunctor {
  template<typename... Args>
  auto operator()(Args... args) -> decltype(Ptr(args...)) {
    return Ptr(args...);
  }
};

// TODO: move me somewhere more appropriate
static inline bool startsWith(std::string String, std::string Prefix) {
  return String.substr(0, Prefix.size()) == Prefix;
}

/// \brief Simple helper function asserting a pointer is not a `nullptr`
template<typename T>
static inline T *notNull(T *Pointer) {
  assert(Pointer != nullptr);
  return Pointer;
}

template<typename T>
static inline bool contains(T Range, typename T::value_type V) {
  return std::find(std::begin(Range), std::end(Range), V) != std::end(Range);
}

#endif // _REVAMB_H
