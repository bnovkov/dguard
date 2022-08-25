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
class DOPGuard : public llvm::PassInfoMixin<DOPGuard> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
  bool runOnModule(llvm::Module &M);

  static bool addPassPlugin(std::string, std::function<bool(llvm::Module &)>);

private:
  static llvm::StringMap<std::function<bool(llvm::Module &)>> pluginMap;
};
//------------------------------------------------------------------------------
// Legacy PM interface
//------------------------------------------------------------------------------
class LegacyDOPGuard : public llvm::ModulePass {
public:
  static char ID;
  LegacyDOPGuard() : ModulePass(ID) {}
  bool runOnModule(llvm::Module &M) override;

  DOPGuard Impl;
};
