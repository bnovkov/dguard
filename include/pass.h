#pragma once

#include <functional>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"
#include "llvm/IR/IRBuilder.h"

typedef llvm::SmallVector<llvm::AllocaInst *, 32> AllocaVec;
typedef llvm::SmallVector<llvm::Value *, 32> ValueVec;
typedef void dfiSchemeFType(llvm::IRBuilder<> &, llvm::Value *,
                            llvm::Instruction *, llvm::BasicBlock *,
                            llvm::BasicBlock *);
//------------------------------------------------------------------------------
// New PM interface
//------------------------------------------------------------------------------
class DGuard : public llvm::PassInfoMixin<DGuard> {
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
  static void addIsolatedVars(llvm::Module &m, ValueVec *vvp);

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
  static void calculateRDS(void);

  /* Different DFI schemes */
  static dfiSchemeFType hammingInst;
  static dfiSchemeFType primeInst;
  static dfiSchemeFType dfiInst;
  static dfiSchemeFType manhInst;

  static int allocaId;

  /* N.B. - either a GlobalVariable or AllocaInst */
  static std::vector<llvm::Value *> isolatedVars;
  static llvm::StringMap<std::function<bool(llvm::Module &)>> pluginMap;
  static llvm::Type *labelMetadataType;
  static llvm::ValueMap<llvm::Value *, long long> labelStore;

  static llvm::DenseMap<llvm::LoadInst *, llvm::StoreInst *> loadToStoreMap;
  static llvm::DenseMap<llvm::StoreInst *,
                        llvm::SmallPtrSet<llvm::LoadInst *, 10>>
      rds;

  static const std::string labelArrName;
  static llvm::StringMap<dfiSchemeFType *> schemeMap;
};
//------------------------------------------------------------------------------
// Legacy PM interface
//------------------------------------------------------------------------------
class LegacyDGuard : public llvm::ModulePass {
public:
  static char ID;
  LegacyDGuard() : ModulePass(ID) {}
  bool runOnModule(llvm::Module &M) override;

  DGuard Impl;
};
