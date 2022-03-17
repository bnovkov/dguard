//==============================================================================
// FILE:
//    DynamicCallCounter.h
//
// DESCRIPTION:
//    Declares the DynamicCallCounter pass for the new and the legacy pass
//    managers.
//
// License: MIT
//==============================================================================

#pragma once

#include <functional>

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

//------------------------------------------------------------------------------
// New PM interface
//------------------------------------------------------------------------------
struct DOPGuard : public llvm::PassInfoMixin<DOPGuard> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
  bool runOnModule(llvm::Module &M);

private:
  typedef llvm::SmallVector<llvm::AllocaInst *, 32> AllocaVec;

  static llvm::StringMap<std::function<void(llvm::CallBase *, AllocaVec *)>>
      funcSymbolDispatchMap;

  void promoteToThreadLocal(llvm::Module &m, llvm::AllocaInst *al);
};
//------------------------------------------------------------------------------
// Legacy PM interface
//------------------------------------------------------------------------------
struct LegacyDOPGuard : public llvm::ModulePass {
  static char ID;
  LegacyDOPGuard() : ModulePass(ID) {}
  bool runOnModule(llvm::Module &M) override;

  DOPGuard Impl;
};
