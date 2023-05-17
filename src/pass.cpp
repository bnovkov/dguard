
#include "pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <memory>
#include <sstream>
#include <utility>
using namespace llvm;

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#define DEBUG_TYPE "dguard-pass"

static cl::opt<std::string> scheme("scheme", cl::init("hamming"),
                                   cl::desc("The desired DFI scheme"),
                                   cl::value_desc("DFI scheme"));

DGuard::DGuard() {
  schemeMap["dfi"] = &DGuard::dfiInst;
  schemeMap["manhattan"] = &DGuard::manhInst;
  schemeMap["prime"] = &DGuard::primeInst;
  schemeMap["hamming"] = &DGuard::hammingInst;
};

/*
 * Runs all defined static analysis plugins on a module.
 */
bool DGuard::runOnModule(Module &M) {
  bool changed = false;
  dfiSchemeFType instF;

  if (schemeMap.count(scheme)) {
    instF = schemeMap[scheme];
  } else {
    dbgs() << "Unknown DFI scheme '" << scheme << "' requested; exiting\n";
    return false;
  }

  for (auto it = DGuard::pluginMap.begin(); it != DGuard::pluginMap.end();
       it++) {
    bool passChanged = it->second(M, this);
    changed = changed || passChanged;

    dbgs() << "pass " << it->first() << " returned " << passChanged << "\n";
  }

  if (changed) {
    calculateMetadataType(M);
    createMetadataArray(M);
    calculateLabels();
    instrumentIsolatedVars(instF);
  }

  // M.dump();

  return changed;
}

/*
 * Define the label array.
 */
void DGuard::createMetadataArray(llvm::Module &m) {
  ArrayType *arrType = ArrayType::get(labelMetadataType, isolatedVars.size());

  GlobalVariable *arr = new GlobalVariable(
      m, arrType, false, GlobalValue::ExternalLinkage, nullptr, labelArrName,
      nullptr, llvm::GlobalValue::NotThreadLocal, 256, false);
  arr->setInitializer(ConstantAggregateZero::get(arrType));
  arr->setAlignment(MaybeAlign(8));
}

/*
 * Determine size of label type based on the number of
 * instrumented variable and labels.
 */
void DGuard::calculateMetadataType(llvm::Module &m) {
  LLVMContext &C = m.getContext();
  // TODO: determine type size dynamically
  labelMetadataType = IntegerType::getInt64Ty(C);
}

/*
 * Takes a stack variable and replaces it with a LocalExec TLS variable.
 */
void DGuard::promoteToThreadLocal(llvm::Module &m, AllocaInst *al) {
  std::ostringstream varName;

  BasicBlock::iterator ii(al);

  const Function *f = al->getFunction();
  if (f == nullptr) {
    varName << al->getNameOrAsOperand() << allocaId;
  } else {
    varName << f->getNameOrAsOperand() << al->getNameOrAsOperand() << allocaId;
  }

  /* Declare the new "stack" variable */
  GlobalVariable *alloca_global =
      new GlobalVariable(m, al->getAllocatedType(), false,
                         GlobalValue::InternalLinkage, // TODO: change to static
                         nullptr, varName.str(), nullptr,
                         GlobalValue::ThreadLocalMode::LocalExecTLSModel);

  /* Set initializer */
  UndefValue *allocaInitializer = UndefValue::get(al->getAllocatedType());
  alloca_global->setInitializer(allocaInitializer);

  ReplaceInstWithValue(al->getParent()->getInstList(), ii,
                       dyn_cast<Value>(alloca_global));

  isolatedVars.push_back(alloca_global);

  varName.clear();
  varName.str("");

  allocaId++;
}

void DGuard::promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas) {
  std::ostringstream varName;

  for (llvm::AllocaInst *al : *allocas) {
    promoteToThreadLocal(m, al);
  }
}

void DGuard::addIsolatedVars(llvm::Module &m, ValueVec *vvp) {
  for (auto vp : *vvp) {
    assert(isa<GlobalVariable>(vp) || isa<AllocaInst>(vp));
    isolatedVars.push_back(vp);
  }
}
/*
 * Inserts a block of instructions that enforce dataflow integrity
 * by indexing into the metadata field, calculating the chosen distance metric
 * for the last recorded store label and the current label, and comparing the
 * result with the local threshold.
 *
 * The target BasicBlock is split before the instruction "u" and the
 * instruction block is appended to the newly created, preceding BB.
 */
void DGuard::insertDFIInst(User *u, dfiSchemeFType instF) {
  Instruction *i, *predTerm;
  BasicBlock *old, *pred;
  Value *loadStoreTargetPtr;
  BasicBlock *abortBB;

  i = dyn_cast<llvm::Instruction>(u);

  Module *m = i->getModule();
  LLVMContext &C = u->getContext();
  Value *metadataArray = m->getGlobalVariable(DGuard::labelArrName);

  /* Fetch 'rdfsbase64' */
  //  Function *rdFS64 = Intrinsic::getDeclaration(m,
  //  Intrinsic::x86_rdfsbase_64);

  if (isa<llvm::LoadInst>(i)) {

    SmallPtrSet<BasicBlock *, 10> oldPreds = {};
    SmallPtrSet<BasicBlock *, 10> newPreds = {};

    old = i->getParent();
    if (old->getSinglePredecessor() == nullptr && instF == &DGuard::dfiInst) {
      return;
    }
    loadStoreTargetPtr = dyn_cast<LoadInst>(i)->getPointerOperand();
    StringRef oldBlockName = old->getName();

    for (auto it = pred_begin(old), et = pred_end(old); it != et; ++it) {
      oldPreds.insert(*it);
    }

    pred = SplitBlock(old, i, static_cast<DominatorTree *>(nullptr), nullptr,
                      nullptr, "",
                      /* before */ true);
    if (pred == nullptr) {
      abort();
    }

    for (auto it = pred_begin(pred), et = pred_end(pred); it != et; ++it) {
      newPreds.insert(*it);
    }
    SmallVector<ConstantInt *> cases;

    for (BasicBlock *bb : oldPreds) {
      if (newPreds.contains(bb))
        continue;

      /* SplitBlock failed to link all predecessors; do this manually */
      Instruction *term = &bb->back();

      /* Modify successors in predecessor terminators */
      if (BranchInst *bi = dyn_cast<BranchInst>(term)) {
        if (bi->isUnconditional()) {
          bi->setOperand(0, pred);
        } else {
          for (size_t i = 0; i < bi->getNumOperands(); i++) {
            if (BasicBlock *bbop = dyn_cast<BasicBlock>(bi->getOperand(i))) {
              if (oldBlockName.equals(bbop->getName())) {
                bi->setOperand(i, pred);
                break;
              }
            }
          }
        }
      } else if (SwitchInst *si = dyn_cast<SwitchInst>(term)) {
        if (si->getDefaultDest() == old) {
          si->setDefaultDest(pred);
        } else {
          //  cases.clear();

          for (auto Case : si->cases()) {
            if (Case.getCaseSuccessor() != old)
              continue;
            Case.setSuccessor(pred);
          }
        }
      } else if (InvokeInst *ii = dyn_cast<InvokeInst>(term)) {
        if (ii->getNormalDest() == old) {
          ii->setNormalDest(pred);
        }
        if (ii->getUnwindDest() == old) {
          ii->setUnwindDest(pred);
        }
      } else {
        assert(false && "Unsupported terminator inst type");
      }
    }

    predTerm = &pred->back();

    /* Create abortBB */
    abortBB = createAbortCallBB(m, old->getParent());

    IRBuilder<> builder(pred);
    builder.SetInsertPoint(predTerm);

    /* Get TLS block addr */
    //    CallInst *rdFSInst = builder.CreateCall(rdFS64);
    // Value *TLSPtrInt = builder.CreatePtrToInt(dyn_cast<Value>(rdFSInst),
    //                                          IntegerType::getInt64Ty(C));
    /* Fetch target address */
    Value *targetPtrInt =
        builder.CreatePtrToInt(loadStoreTargetPtr, IntegerType::getInt64Ty(C));
    Value *idx = builder.CreateLShr(targetPtrInt,
                                    ConstantInt::get(labelMetadataType, 65));
    /* Index into label array */
    Value *metadataPtr = builder.CreateGEP(
        metadataArray->getType()->getScalarType()->getPointerElementType(),
        metadataArray,
        ArrayRef<Value *>({Constant::getNullValue(labelMetadataType), idx}));

    /* Fetch last label */
    Value *lastLabel =
        builder.CreateLoad(DGuard::labelMetadataType, metadataPtr, false, "");

    assert(loadToStoreMap.count(dyn_cast<LoadInst>(i)) > 0);
    StoreInst *si = loadToStoreMap[dyn_cast<LoadInst>(i)];
    assert(rds.count(si) > 0);

    if (rds[si].size() > 1) {
      /* Instrument according to selected DFI scheme */
      (this->*instF)(builder, lastLabel, i, old, abortBB);
    } else {
      Value *equal = builder.CreateICmpEQ(
          dyn_cast<Value>(ConstantInt::get(DGuard::labelMetadataType, 0)),
          dyn_cast<Value>(ConstantInt::get(DGuard::labelMetadataType, 0)));

      builder.CreateCondBr(equal, old, abortBB);
    }

    pred->getTerminator()->eraseFromParent();
  } else if (isa<llvm::StoreInst>(i)) {
    loadStoreTargetPtr = dyn_cast<StoreInst>(i)->getPointerOperand();

    IRBuilder<> builder(i->getParent());
    builder.SetInsertPoint(i);

    /* Get TLS block addr */
    //  CallInst *rdFSInst = builder.CreateCall(rdFS64);
    // Value *TLSPtrInt = builder.CreatePtrToInt(dyn_cast<Value>(rdFSInst),
    //                                          IntegerType::getInt64Ty(C));

    //    Value *index = builder.CreateSub(
    //   TLSPtrInt,
    //   builder.CreatePtrToInt(loadStoreTargetPtr,
    //   IntegerType::getInt64Ty(C)));

    /* Index into label array */
    Value *targetPtrInt =
        builder.CreatePtrToInt(loadStoreTargetPtr, IntegerType::getInt64Ty(C));
    Value *idx = builder.CreateLShr(targetPtrInt,
                                    ConstantInt::get(labelMetadataType, 65));
    Value *metadataPtr = builder.CreateGEP(
        metadataArray->getType()->getScalarType()->getPointerElementType(),
        metadataArray,
        ArrayRef<Value *>({Constant::getNullValue(labelMetadataType), idx}));

    /* Store current label */
    // TODO: fetch labels from static store
    long long storeLabel = getThreshold(i);
    builder.CreateStore(ConstantInt::get(labelMetadataType, storeLabel),
                        metadataPtr);
  }
}

/*
 * Returns a BB that calls "__dguard_abort" in function 'F'.
 */
BasicBlock *DGuard::createAbortCallBB(llvm::Module *m, Function *F) {
  BasicBlock *BB = nullptr;

  /* Check whether the corresponding BB is already defined */
  for (auto it = F->begin(); it != F->end(); it++) {
    if (!it->hasName())
      continue;

    if (it->getName().equals("__dguard_call_abort_block")) {
      BB = &(*it);
      break;
    }
  }

  if (BB != nullptr) {
    return BB;
  }

  /* Else create BB */
  LLVMContext &ctx = m->getContext();
  IRBuilder<> builder(ctx);
  std::stringstream ss("");

  /* Fetch "__dguard_abort" */
  PointerType *abortArgTy = PointerType::getUnqual(Type::getInt8Ty(ctx));
  FunctionType *type =
      FunctionType::get(Type::getVoidTy(ctx), abortArgTy, false);

  FunctionCallee dguardAbort = m->getOrInsertFunction("__dguard_abort", type);
  Function *dguardAbortF = dyn_cast<Function>(dguardAbort.getCallee());
  dguardAbortF->addFnAttr(Attribute::get(ctx, "noreturn", "true"));
  // dguardAbortF->deleteBody();

  BB = BasicBlock::Create(ctx, "__dguard_call_abort_block", F);
  ss << F->getName().str() << "_fname";

  Value *funcNameGlobVarVal = m->getGlobalVariable(ss.str());
  if (funcNameGlobVarVal == nullptr) {
    funcNameGlobVarVal = builder.CreateGlobalStringPtr(F->getName(), ss.str(),
                                                       F->getAddressSpace(), m);
  }

  builder.SetInsertPoint(BB);
  builder.CreateCall(dguardAbort, {funcNameGlobVarVal});
  builder.CreateUnreachable();

  return BB;
}

/*
 * Runs Reaching Definitions Analysis for protected variables and calculates
 * the RDS.
 */
void DGuard::calculateRDS(void) {
  SmallSet<Function *, 24> funcs{};

  /* Collect each function where the protected var has users */
  for (auto &g : isolatedVars) {
    Value *isolVar = g;
    for (auto it = isolVar->user_begin(); it != isolVar->user_end(); it++) {
      if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) {
        funcs.insert((dyn_cast<Instruction>(*it))->getFunction());
      }
    }
  }

  for (Function *f : funcs) {
    gens.clear();
    kills.clear();
    out.clear();
    in.clear();
    allDefs.clear();

    /* Collect all defs for variables in f */
    for (Value *isolVar : isolatedVars) {
      allDefs.insert(std::make_pair(isolVar, DefSet()));
      for (auto it = isolVar->user_begin(); it != isolVar->user_end(); it++) {
        if (StoreInst *si = dyn_cast<StoreInst>(*it)) {
          if (si->getFunction() == f) {
            allDefs[isolVar].insert(si);
          }
        } else if (GetElementPtrInst *gi = dyn_cast<GetElementPtrInst>(*it)) {
          for (auto u : gi->users()) {
            if (StoreInst *si = dyn_cast<StoreInst>(u)) {
              allDefs[isolVar].insert(si);
            }
          }
        }
      }
    }

    /* Collect GEN and KILL for each variable and BB */
    for (Value *g : isolatedVars) {
      if (allDefs[g].size() == 0) {
        continue;
      }

      for (BasicBlock &bi : *f) {
        BasicBlock *bb = &bi;
        gens.insert(std::make_pair(bb, DefSet()));

        for (Instruction &ii : bi) {
          if (StoreInst *si = dyn_cast<StoreInst>(&ii)) {
            if (GlobalVariable *gv =
                    dyn_cast<GlobalVariable>(si->getPointerOperand())) {
              if (gv == g) {
                gens[bb].insert(si);
              }
            }
          }
        }

        /* Calculate KILL set; Defs(g) - GEN(bb) */
        DefSet killSet = set_difference(allDefs[g], gens[bb]);
        kills.insert(std::make_pair(bb, DefSet(std::move(killSet))));
      }

      /* Run worklist algorithm */
      Worklist worklist{};

      /* Initialize worklist and in */
      for (BasicBlock &bi : *f) {
        worklist.push_back(&bi);
        in[&bi] = {};
      }

      /* Initialize out with gen */
      for (const std::pair<const BasicBlock *, DefSet> &p : gens) {
        out[p.first] = {p.second.begin(), p.second.end()};
      }

      while (!worklist.empty()) {
        const BasicBlock *N = worklist.pop_back_val();

        for (const BasicBlock *p : predecessors(N))
          llvm::set_union(in[N], out[p]);

        if (llvm::set_union(out[N], llvm::set_difference(in[N], kills[N]))) {
          for (const BasicBlock *s : successors(N))
            worklist.push_back(s);
        }
      }

      /* Populate RDS */
      for (StoreInst *def : allDefs[g]) {
        rds.insert(std::make_pair(def, SmallPtrSet<LoadInst *, 10>()));
        for (BasicBlock &bi : *f) {
          BasicBlock *bb = &bi;
          bool reachesBlock = (in[bb].count(def) != 0);
          bool propagates = (out[bb].count(def) != 0);

          /* Collect all uses in current BB */
          for (Instruction *i = def->getNextNonDebugInstruction(); i != nullptr;
               i = i->getNextNonDebugInstruction()) {
            if (LoadInst *li = dyn_cast<LoadInst>(i)) {
              if (li->getPointerOperand() == g) {
                rds[def].insert(li);
                loadToStoreMap[li] = def;
              }
            } else if (GetElementPtrInst *gi = dyn_cast<GetElementPtrInst>(i)) {
              for (auto u : gi->users()) {
                if (LoadInst *li = dyn_cast<LoadInst>(u)) {
                  rds[def].insert(li);
                  loadToStoreMap[li] = def;
                }
              }
            }
          }

          if (reachesBlock && propagates) {
            /* Collect all uses in this block */
            for (Instruction &ii : bi) {
              if (LoadInst *li = dyn_cast<LoadInst>(&ii)) {
                if (li->getPointerOperand() == g) {
                  rds[def].insert(li);
                  loadToStoreMap[li] = def;
                }
              }
            }
          } else if (reachesBlock && !propagates) {
            /* This block killed the def; Collect any uses prior to def that
             * killed it */
            for (Instruction &ii : bi) {
              if (StoreInst *si = dyn_cast<StoreInst>(&ii)) {
                if (gens[bb].count(si))
                  break;
              } else if (LoadInst *li = dyn_cast<LoadInst>(&ii)) {
                if (li->getPointerOperand() == g) {
                  rds[def].insert(li);
                  loadToStoreMap[li] = def;
                }
              }
            }
          }
        }
      }
    }
  }
}
/*
 * Calculates instruction labels and group thresholds.
 * The basis for calculation is a data-use graph constructed using use-def
 * chains and the control flow graph.
 */
void DGuard::calculateLabels(void) { calculateRDS(); }

long long DGuard::getLabel(Instruction *i) {
  /* TODO: Implement when label calculation is done */
  return 0;
}

long long DGuard::getThreshold(Instruction *loadI) {
  /* TODO: Implement when label calculation is done */
  return 0;
}

bool DGuard::addPassPlugin(std::string name,
                           std::function<bool(llvm::Module &, DGuard *)> func) {
  return DGuard::pluginMap
      .insert(
          std::pair<std::string, std::function<bool(llvm::Module &, DGuard *)>>(
              name, func))
      .second;
}

/*
 * Traverses each user of a DFI-protected variable and instruments
 * accordingly.
 */
void DGuard::instrumentIsolatedVars(dfiSchemeFType instF) {
  dbgs() << "RDS size: " << rds.size() << "\n";
  for (auto &it : rds) {
    insertDFIInst(it.first, instF);
    for (LoadInst *li : it.second) {
      insertDFIInst(li, instF);
    }
  }
}

void DGuard::hammingInst(IRBuilder<> &builder, Value *lastLabel, Instruction *i,
                         BasicBlock *old, BasicBlock *abortBB) {
  /* Calculate distance metric */
  Value *distance = builder.CreateXor(lastLabel, lastLabel);

  /* Compare with threshold */
  // TODO: calculate and store thresholds in a
  // "ProtectedVar" wrapper class
  long long loadThresh = getThreshold(i);
  Value *equal = builder.CreateICmpEQ(
      distance,
      dyn_cast<Value>(ConstantInt::get(DGuard::labelMetadataType, loadThresh)));

  builder.CreateCondBr(equal, old, abortBB);
}

void DGuard::primeInst(IRBuilder<> &builder, Value *lastLabel, Instruction *i,
                       BasicBlock *old, BasicBlock *abortBB) {

  Value *rem = builder.CreateURem(
      lastLabel, ConstantInt::get(DGuard::labelMetadataType, 14));

  Value *equal = builder.CreateICmpEQ(
      rem, dyn_cast<Value>(ConstantInt::get(DGuard::labelMetadataType, 0)));

  builder.CreateCondBr(equal, old, abortBB);
}

void DGuard::manhInst(IRBuilder<> &builder, Value *lastLabel, Instruction *i,
                      BasicBlock *old, BasicBlock *abortBB) {
  LLVMContext &c = i->getContext();
  Function *abs = Intrinsic::getDeclaration(i->getModule(), Intrinsic::abs,
                                            {IntegerType::getInt32Ty(c)});
  IntegerType *coordTy = IntegerType::getInt32Ty(c);

  /* Slice value and load coords */
  Value *x = builder.CreateTrunc(lastLabel, coordTy);
  Value *y = builder.CreateTrunc(
      builder.CreateLShr(lastLabel, ConstantInt::get(lastLabel->getType(), 32)),
      coordTy);

  /* Calculate abs diff of points */
  Value *xDiff = builder.CreateSub(x, ConstantInt::get(coordTy, 0));
  Value *yDiff = builder.CreateSub(y, ConstantInt::get(coordTy, 0));

  Value *xDiffAbs = builder.CreateCall(
      abs, {xDiff, ConstantInt::getFalse(IntegerType::getInt1Ty(c))});
  Value *yDiffAbs = builder.CreateCall(
      abs, {yDiff, ConstantInt::getFalse(IntegerType::getInt1Ty(c))});

  /* Sum the diffs */
  Value *sum = builder.CreateAdd(xDiffAbs, yDiffAbs);

  Value *equal = builder.CreateICmpEQ(sum, ConstantInt::get(coordTy, 0));

  builder.CreateCondBr(equal, old, abortBB);
}

void DGuard::dfiInst(IRBuilder<> &builder, Value *lastLabel, Instruction *i,
                     BasicBlock *old, BasicBlock *abortBB) {
  int numblocks = 0;
  SmallVector<BasicBlock *, 10> blocks = {};
  BasicBlock *pred = old->getSinglePredecessor();

  StoreInst *si = loadToStoreMap[dyn_cast<LoadInst>(i)];
  numblocks = rds[si].size();

  blocks.push_back(pred);

  for (int j = 0; j < (numblocks - 1); j++) {
    BasicBlock *BB = BasicBlock::Create(i->getParent()->getContext(), "",
                                        i->getFunction(), nullptr);
    if (BB == nullptr) {
      /* Bail */
      return;
    }
    blocks.push_back(BB);
  }

  /* Link each block in the compare-branch chain, starting from the last to
   * the first */
  BasicBlock *falseTgt = abortBB;
  for (int j = 0; j < numblocks; j++) {
    BasicBlock *bb = blocks.back();
    assert(bb != nullptr);
    IRBuilder<> curBuilder(bb);

    if (!bb->empty()) {
      curBuilder.SetInsertPoint(&bb->back());
    }

    Value *equal = curBuilder.CreateICmpEQ(
        lastLabel, ConstantInt::get(DGuard::labelMetadataType, j));
    curBuilder.CreateCondBr(equal, old, falseTgt);

    falseTgt = bb;
    blocks.pop_back();
  }
}
/*
 * LLVM pass boilerplate code.
 */
PreservedAnalyses DGuard::run(llvm::Module &M, llvm::ModuleAnalysisManager &) {

  return (runOnModule(M) ? llvm::PreservedAnalyses::none()
                         : llvm::PreservedAnalyses::all());
}

bool LegacyDGuard::runOnModule(llvm::Module &M) { return Impl.runOnModule(M); }

llvm::PassPluginLibraryInfo getDGuardPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "dguard-pass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                // PB.registerPipelineStartEPCallback(

                [&](ModulePassManager &MPM, auto) {
                  MPM.addPass(DGuard());
                  return true;
                });
            /*          [](PassBuilder &PB) {
                    PB.registerPipelineParsingCallback(
                        [](StringRef Name, ModulePassManager &MPM,
                           ArrayRef<PassBuilder::PipelineElement>) {
                          if (Name == "dguard-pass") {
                            MPM.addPass(DGuard());
                            return true;
                          }
                          return false;
                        });*/
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getDGuardPluginInfo();
}

llvm::StringMap<std::function<bool(llvm::Module &, DGuard *)>>
    DGuard::pluginMap = {};
