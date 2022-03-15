//========================================================================
// FILE:
//    InjectFuncCall.cpp
//
// DESCRIPTION:
//    For each function defined in the input IR module, InjectFuncCall inserts
//    a call to printf (from the C standard I/O library). The injected IR code
//    corresponds to the following function call in ANSI C:
//    ```C
//      printf("(llvm-tutor) Hello from: %s\n(llvm-tutor)   number of arguments: %d\n",
//             FuncName, FuncNumArgs);
//    ```
//    This code is inserted at the beginning of each function, i.e. before any
//    other instruction is executed.
//
//    To illustrate, for `void foo(int a, int b, int c)`, the code added by InjectFuncCall
//    will generated the following output at runtime:
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
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "kipc-pass"

//-----------------------------------------------------------------------------
// InjectFuncCall implementation
//-----------------------------------------------------------------------------
StaticCallCounter::Result StaticCallCounter::runOnModule(Module &M) {
    for(auto &Func : M){
        for(auto &BB : Func){
            llvm::SmallVector<PointerType*> *ptrVars = new llvm::SmallVector<PointerType*>();
            llvm::SmallVector<AllocaInst*> *allocas = new llvm::SmallVector<AllocaInst*>();
            for(auto &Inst : BB){
                //https://stackoverflow.com/questions/48333206/how-to-check-if-a-target-of-an-llvm-allocainst-is-a-function-pointer
                auto *DecOp = dyn_cast<PointerType>(Inst);

                if(DecOp && !isFunctionPointerType(DecOp->getElementType())){
                    ptrVars.append(DecOp);
                    //TODO ako je ispravan nacin trazenja pointera dodaj u PtrVariables
                }
                if(AllocaInst* WriteOp = dyn_cast<AllocaInst*>(Inst)){
                    allocas.append(WriteOp);
                }

            }
            for(AllocaInst *Inst : allocas){
                PointerType *p = Inst->getType();
                for(PointerType* ptr : ptrVars){
                  if(p->getName().compare(ptr->getName())){
                    //dalje
                  }
                }
            }
        }
    }

}

PreservedAnalyses DOPGuard::run(llvm::Module &M,
                                       llvm::ModuleAnalysisManager &) {
  bool Changed =  runOnModule(M);

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
static RegisterPass<LegacyDOPGuard>
    X(/*PassArg=*/"legacy-dopg-pass", /*Name=*/"LegacyDOPGuard",
      /*CFGOnly=*/false, /*is_analysis=*/false);
