//========================================================================
// FILE:
//    InjectFuncCall.cpp
//
// DESCRIPTION:
//    For each function defined in the input IR module, InjectFuncCall inserts
//    a call to printf (from the C standard I/O library). The injected IR code
//    corresponds to the following function call in ANSI C:
//    ```C
//      printf("(llvm-tutor) Hello from: %s\n(llvm-tutor)   number of arguments:
//      %d\n",
//             FuncName, FuncNumArgs);
//    ```
//    This code is inserted at the beginning of each function, i.e. before any
//    other instruction is executed.
//
//    To illustrate, for `void foo(int a, int b, int c)`, the code added by
//    InjectFuncCall will generated the following output at runtime:
//    ```
//    (llvm-tutor) Hello World from: foo
//    (llvm-tutor)   number of arguments: 3
//    ```
//
// USAGE:
//    1. Legacy pass manager:
//      $ opt -load <BUILD_DIR>/lib/libInjectFuncCall.so `\`
//        --legacy-inject-func-call <bitcode-file>
//    2. New pass maanger:
//      $ opt -load-pass-plugin <BUILD_DIR>/lib/libInjectFunctCall.so `\`
//        -passes=-"inject-func-call" <bitcode-file>
//
// License: MIT
//========================================================================
#include "pass.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <llvm/ADT/StringRef.h>

using namespace llvm;

#define DEBUG_TYPE "dopg-pass"

llvm::StringMap<
    std::function<bool(llvm::CallBase *, llvm::SmallVector<AllocaInst *> *)>>
    DOPGuard::funcSymbolDispatchMap = {
        {"memcpy",
         [](llvm::CallBase *i, llvm::SmallVector<AllocaInst *> *vec) {
           Value *op = i->getOperand(0);
           if (AllocaInst *al = dyn_cast<AllocaInst>(op)) {
             vec->push_back(al);
             return true;
           }
           return false;
         }},
        {"read",
         [](llvm::CallBase *i, llvm::SmallVector<AllocaInst *> *vec) {
           Value *op = i->getOperand(1);
           if (AllocaInst *al = dyn_cast<AllocaInst>(op)) {
             vec->push_back(al);
             return true;
           }
           return false;
         }},
};

bool DOPGuard::runOnModule(Module &M) {
  bool changed = false;

  for (auto &Func : M) {
    llvm::SmallVector<PointerType> *ptrVars =
        new llvm::SmallVector<PointerType>();
    llvm::SmallVector<AllocaInst *> *allocas =
        new llvm::SmallVector<AllocaInst *>();
    llvm::SmallVector<AllocaInst *> *vulnAllocas =
        new llvm::SmallVector<AllocaInst *>();

    for (auto &BB : Func) {
      for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
           ++inst) {
        if (CallBase *cb = dyn_cast<CallBase>(inst)) {
          StringRef name = cb->getName();
          if (DOPGuard::funcSymbolDispatchMap.count(name)) {
            funcSymbolDispatchMap[name](cb, vulnAllocas);
          }
        }
      }
    }

    if (vulnAllocas) {
      continue;
    }
  }

  return changed;
}

PreservedAnalyses DOPGuard::run(llvm::Module &M,
                                llvm::ModuleAnalysisManager &) {
  bool Changed = runOnModule(M);

  return (Changed ? llvm::PreservedAnalyses::none()
                  : llvm::PreservedAnalyses::all());
}

bool LegacyDOPGuard::runOnModule(llvm::Module &M) {
  bool Changed = Impl.runOnModule(M);

  return Changed;
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
