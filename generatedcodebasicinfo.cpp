/// \file generatedcodebasicinfo.cpp
/// \brief Implements the GeneratedCodeBasicInfo pass which provides basic
///        information about the translated code (e.g., which CSV is the PC).

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// Standard includes
#include <set>
#include <queue>

// LLVM includes
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

// Local includes
#include "debug.h"
#include "generatedcodebasicinfo.h"

using namespace llvm;

char GeneratedCodeBasicInfo::ID = 0;
static RegisterPass<GeneratedCodeBasicInfo> X("gcbi",
                                              "Generated Code Basic Info",
                                              true,
                                              true);

bool GeneratedCodeBasicInfo::runOnFunction(llvm::Function &F) {
  DBG("passes", { dbg << "Starting GeneratedCodeBasicInfo\n"; });

  Module *M = F.getParent();
  const char *MDName = "revamb.input.architecture";
  NamedMDNode *InputArchMD = M->getOrInsertNamedMetadata(MDName);
  auto *Tuple = dyn_cast<MDTuple>(InputArchMD->getOperand(0));

  QuickMetadata QMD(M->getContext());
  DelaySlotSize = QMD.extract<uint32_t>(Tuple, 0);
  PC = M->getGlobalVariable(QMD.extract<StringRef>(Tuple, 1));

  for (BasicBlock &BB : F) {
    if (!BB.empty()) {
      switch (getType(&BB)) {
      case DispatcherBlock:
        assert(Dispatcher == nullptr);
        Dispatcher = &BB;
        break;
      case AnyPCBlock:
        assert(AnyPC == nullptr);
        AnyPC = &BB;
        break;
      case UnexpectedPCBlock:
        assert(UnexpectedPC == nullptr);
        UnexpectedPC = &BB;
        break;
      case JumpTargetBlock:
        {
          auto *Call = cast<CallInst>(&*BB.begin());
          assert(Call->getCalledFunction()->getName() == "newpc");
          JumpTargets[getLimitedValue(Call->getArgOperand(0))] = &BB;
          break;
        }
      case UntypedBlock:
        // Nothing to do here
        break;
      }
    }
  }

  assert(Dispatcher != nullptr
         && AnyPC != nullptr
         && UnexpectedPC != nullptr);

  DBG("passes", { dbg << "Ending GeneratedCodeBasicInfo\n"; });

  return false;
}

std::pair<uint64_t, uint64_t>
GeneratedCodeBasicInfo::getPC(Instruction *TheInstruction) const {
  CallInst *NewPCCall = nullptr;
  std::set<BasicBlock *> Visited;
  std::queue<BasicBlock::reverse_iterator> WorkList;
  if (TheInstruction->getIterator() == TheInstruction->getParent()->begin())
    WorkList.push(--TheInstruction->getParent()->rend());
  else
    WorkList.push(make_reverse_iterator(TheInstruction));

  while (!WorkList.empty()) {
    auto I = WorkList.front();
    WorkList.pop();
    auto *BB = I->getParent();
    auto End = BB->rend();

    // Go through the instructions looking for calls to newpc
    for (; I != End; I++) {
      if (auto Marker = dyn_cast<CallInst>(&*I)) {
        // TODO: comparing strings is not very elegant
        auto *Callee = Marker->getCalledFunction();
        if (Callee != nullptr && Callee->getName() == "newpc") {

          // We found two distinct newpc leading to the requested instruction
          if (NewPCCall != nullptr)
            return { 0, 0 };

          NewPCCall = Marker;
          break;
        }
      }
    }

    // If we haven't find a newpc call yet, continue exploration backward
    if (NewPCCall == nullptr) {
      // If one of the predecessors is the dispatcher, don't explore any further
      for (BasicBlock *Predecessor : predecessors(BB)) {
        // Assert we didn't reach the almighty dispatcher
        assert(!(NewPCCall == nullptr && Predecessor == Dispatcher));
        if (Predecessor == Dispatcher)
          continue;
      }

      for (BasicBlock *Predecessor : predecessors(BB)) {
        // Ignore already visited or empty BBs
        if (!Predecessor->empty()
            && Visited.find(Predecessor) == Visited.end()) {
          WorkList.push(Predecessor->rbegin());
          Visited.insert(Predecessor);
        }
      }
    }

  }

  // Couldn't find the current PC
  if (NewPCCall == nullptr)
    return { 0, 0 };

  uint64_t PC = getLimitedValue(NewPCCall->getArgOperand(0));
  uint64_t Size = getLimitedValue(NewPCCall->getArgOperand(1));
  assert(Size != 0);
  return { PC, Size };
}
