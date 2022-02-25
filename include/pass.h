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

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

//------------------------------------------------------------------------------
// New PM interface
//------------------------------------------------------------------------------
struct DOPGuard : public llvm::PassInfoMixin<DOPGuard> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &);
  bool runOnModule(llvm::Module &M);
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

