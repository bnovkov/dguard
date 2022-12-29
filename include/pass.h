#pragma once

#include <functional>
#include <vector>

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"
#include "llvm/IR/IRBuilder.h"

typedef llvm::SmallVector<llvm::AllocaInst *, 32> AllocaVec;
typedef llvm::Value *dfiSchemeFType(llvm::IRBuilder<> &, llvm::Value *,
                                    llvm::Instruction *);
//------------------------------------------------------------------------------
// New PM interface
//------------------------------------------------------------------------------
class DOPGuard : public llvm::PassInfoMixin<DOPGuard> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
  bool runOnModule(llvm::Module &M);

  static bool isRequired() { return true; }

  /*
   * Adds a static analysis/transformation pass function to the plugin map.
   * These run each time the 'runOnModule' method is invoked.
   */
  static bool addPassPlugin(std::string, std::function<bool(llvm::Module &)>);
  static void promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas);
  static void promoteToThreadLocal(llvm::Module &m, llvm::AllocaInst *al);

private:
  /* Instrumentation helpers */
  static void instrumentIsolatedVars(dfiSchemeFType instF);
  static void insertDFIInst(llvm::User *u, dfiSchemeFType instF);
  static void createMetadataArray(llvm::Module &m);
  static void calculateMetadataType(llvm::Module &m);
  static llvm::BasicBlock *createAbortCallBB(llvm::Module *m,
                                             llvm::Function *F);

  /* Graph processing methods */
  static void calculateLabels(void);
  static long long getLabel(llvm::Instruction *i);
  static long long getThreshold(llvm::Instruction *loadI);

  /* Different DFI schemes */
  static dfiSchemeFType hammingInst;
  static dfiSchemeFType primeInst;
  static dfiSchemeFType dfiInst;
  static int allocaId;

  static std::vector<llvm::GlobalVariable *> isolatedVars;
  static llvm::StringMap<std::function<bool(llvm::Module &)>> pluginMap;
  static llvm::Type *labelMetadataType;
  static llvm::ValueMap<llvm::Value *, long long> labelStore;

  static const std::string labelArrName;
  static llvm::StringMap<dfiSchemeFType *> schemeMap;
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
