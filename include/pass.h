#pragma once

#include <functional>
#include <vector>

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

typedef llvm::SmallVector<llvm::AllocaInst *, 32> AllocaVec;

//------------------------------------------------------------------------------
// New PM interface
//------------------------------------------------------------------------------
class DOPGuard : public llvm::PassInfoMixin<DOPGuard> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
  bool runOnModule(llvm::Module &M);

  /*
   * Adds a static analysis/transformation pass function to the plugin map.
   * These are run each time the 'runOnModule' method is invoked.
   */
  static bool addPassPlugin(std::string, std::function<bool(llvm::Module &)>);
  static void promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas);

private:
  static void instrumentIsolatedVars(void);
  static void emitModuleMetadata(llvm::Module &m);
  static void injectMetadataInitializer(llvm::Module &m);

  static int allocaId;
  static std::vector<llvm::GlobalVariable *> isolatedVars;
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
