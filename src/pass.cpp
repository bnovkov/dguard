
#include "pass.h"

#include "llvm/ADT/None.h"
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

llvm::StringMap<std::function<void(llvm::CallBase *, DOPGuard::AllocaVec *)>>
    DOPGuard::funcSymbolDispatchMap = {
        {"memcpy",
         [](llvm::CallBase *i, DOPGuard::AllocaVec *vec) {
           Value *op = i->getOperand(0);
           if (AllocaInst *al = dyn_cast<AllocaInst>(op)) {
             vec->push_back(al);
           }
         }},
        {"read",
         [](llvm::CallBase *i, DOPGuard::AllocaVec *vec) {
           Value *op = i->getOperand(1);
           if (AllocaInst *al = dyn_cast<AllocaInst>(op)) {
             vec->push_back(al);
           }
         }},
};

void DOPGuard::promoteToThreadLocal(llvm::Module &m, llvm::AllocaInst *al) {
  BasicBlock::iterator ii(al);
  GlobalVariable *alloca_global = new GlobalVariable(
      m, al->getAllocatedType(), false,
      GlobalValue::InternalLinkage, // TODO: change to static
      nullptr, "", nullptr, GlobalValue::ThreadLocalMode::LocalExecTLSModel);

  ReplaceInstWithValue(al->getParent()->getInstList(), ii,
                       dyn_cast<Value>(alloca_global));
}

bool DOPGuard::runOnModule(Module &M) {
  bool changed = false;

  for (auto &Func : M) {
    llvm::SmallVector<PointerType, 10> ptrVars;
    AllocaVec allocas;
    AllocaVec vulnAllocas;

    for (auto &BB : Func) {
      for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
           ++inst) {
        if (CallBase *cb = dyn_cast<CallBase>(inst)) {
          StringRef name = cb->getName();
          if (DOPGuard::funcSymbolDispatchMap.count(name)) {
            funcSymbolDispatchMap[name](cb, &vulnAllocas);
          }
        }
      }
    }

    if (vulnAllocas.size() == 0) {
      continue;
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
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "dopg-pass") {
                    MPM.addPass(DOPGuard());
                    return true;
                  }
                  return false;
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
