
#include "pass.h"

#include "llvm/ADT/None.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/GlobalValue.h>

using namespace llvm;

#define DEBUG_TYPE "dopg-pass"

bool DOPGuard::runOnModule(Module &M) {
  bool changed = false;

  for (auto &Func : M) {
    llvm::SmallVector<PointerType, 10> ptrVars;
    AllocaVec allocasToBePromoted;
    AllocaVec vulnAllocas;

    for (auto &BB : Func) {
      for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
           ++inst) {
        if (CallBase *cb = dyn_cast<CallBase>(inst)) {
          Function *f = cb->getCalledFunction();
          if (!f) // check for indirect call
            continue;
          StringRef name = f->getName();
          if (DOPGuard::funcSymbolDispatchMap.count(name)) {
            funcSymbolDispatchMap[name](cb, &vulnAllocas);
          }
        }
      }
    }

    if (vulnAllocas.size() == 0) {
      continue;
    }

    for (auto &BB : Func) {
      for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
           ++inst) {

        if (AllocaInst *cb = dyn_cast<AllocaInst>(inst)) {
          Instruction *Inst = dyn_cast<Instruction>(inst);

          if (DOPGuard::findInstruction(Inst)) {
            allocasToBePromoted.push_back(cb);
          }
        }
      }
    }
    if (allocasToBePromoted.size() != 0) {
      promoteToThreadLocal(M, &allocasToBePromoted);
      changed = true;
    }
  }
  return changed;
}

PreservedAnalyses DOPGuard::run(llvm::Module &M,
                                llvm::ModuleAnalysisManager &) {

  return (runOnModule(M) ? llvm::PreservedAnalyses::none()
                         : llvm::PreservedAnalyses::all());
}

bool LegacyDOPGuard::runOnModule(llvm::Module &M) {
  return Impl.runOnModule(M);
}

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getDOPGuardPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "dopg-pass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineEarlySimplificationEPCallback(
                [](ModulePassManager &MPM,
                   llvm::PassBuilder::OptimizationLevel Level) {
                  MPM.addPass(DOPGuard());
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getDOPGuardPluginInfo();
}

//-----------------------------------------------------------------------------
// Legacy PM Registration
//-----------------------------------------------------------------------------
char LegacyDOPGuard::ID = 0;

// Register the pass - required for (among others) opt
static RegisterPass<LegacyDOPGuard> X(/*PassArg=*/"legacy-dopg-pass",
                                      /*Name=*/"LegacyDOPGuard",
                                      /*CFGOnly=*/false, /*is_analysis=*/false);
