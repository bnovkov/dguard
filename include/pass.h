#pragma once

#include <functional>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Pass.h"
#include "llvm/IR/IRBuilder.h"

typedef llvm::SmallVector<llvm::AllocaInst *, 32> AllocaVec;
typedef llvm::SmallVector<llvm::Value *, 32> ValueVec;

using DefSet = llvm::SmallPtrSet<llvm::StoreInst *, 32>;
using BBDefMap = llvm::DenseMap<const llvm::BasicBlock *, DefSet>;
using VarDefMap = llvm::DenseMap<const llvm::Value *, DefSet>;
using Worklist = llvm::SmallVector<const llvm::BasicBlock *, 0>;
//------------------------------------------------------------------------------
// New PM interface
//------------------------------------------------------------------------------
class DGuard : public llvm::PassInfoMixin<DGuard> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
  bool runOnModule(llvm::Module &M);
  DGuard();
  DGuard(DGuard &&d) : schemeMap(d.schemeMap){};
  static bool isRequired() { return true; }
  /*
   * Adds a static analysis/transformation pass function to the plugin map.
   * These run each time the 'runOnModule' method is invoked.
   */
  static bool addPassPlugin(std::string,
                            std::function<bool(llvm::Module &, DGuard *)>);
  void promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas);
  void promoteToThreadLocal(llvm::Module &m, llvm::AllocaInst *al);
  void addIsolatedVars(llvm::Module &m, ValueVec *vvp);

private:
  typedef void (DGuard::*dfiSchemeFType)(llvm::IRBuilder<> &, llvm::Value *,
                                         llvm::Instruction *,
                                         llvm::BasicBlock *,
                                         llvm::BasicBlock *);
  /* Instrumentation helpers */
  void instrumentIsolatedVars(dfiSchemeFType instF);
  void insertDFIInst(llvm::User *u, dfiSchemeFType instF);
  void createMetadataArray(llvm::Module &m);
  void calculateMetadataType(llvm::Module &m);
  llvm::BasicBlock *createAbortCallBB(llvm::Module *m, llvm::Function *F);

  /* Graph processing methods */
  void calculateLabels(void);
  long long getLabel(llvm::Instruction *i);
  long long getThreshold(llvm::Instruction *loadI);
  void calculateRDS(void);

  /* Different DFI schemes */
  void hammingInst(llvm::IRBuilder<> &, llvm::Value *, llvm::Instruction *,
                   llvm::BasicBlock *, llvm::BasicBlock *);
  ;
  void primeInst(llvm::IRBuilder<> &, llvm::Value *, llvm::Instruction *,
                 llvm::BasicBlock *, llvm::BasicBlock *);
  ;
  void dfiInst(llvm::IRBuilder<> &, llvm::Value *, llvm::Instruction *,
               llvm::BasicBlock *, llvm::BasicBlock *);
  ;
  void manhInst(llvm::IRBuilder<> &, llvm::Value *, llvm::Instruction *,
                llvm::BasicBlock *, llvm::BasicBlock *);
  ;

  int allocaId = 0;

  /* N.B. - either a GlobalVariable or AllocaInst */
  std::vector<llvm::Value *> isolatedVars;
  static llvm::StringMap<std::function<bool(llvm::Module &, DGuard *)>>
      pluginMap;
  llvm::Type *labelMetadataType = nullptr;
  llvm::ValueMap<llvm::Value *, long long> labelStore;

  llvm::DenseMap<llvm::LoadInst *, llvm::StoreInst *> loadToStoreMap;
  llvm::DenseMap<llvm::StoreInst *, llvm::SmallPtrSet<llvm::LoadInst *, 10>>
      rds;

  const std::string labelArrName = "__dguard_label_arr";
  llvm::StringMap<dfiSchemeFType> schemeMap;

  BBDefMap gens;
  BBDefMap kills;
  BBDefMap out;
  BBDefMap in;
  VarDefMap allDefs;
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
