
#include "pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
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

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <sstream>
#include <utility>

using namespace llvm;

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#define DEBUG_TYPE "dguard-pass"

static cl::opt<std::string> scheme("scheme", cl::init("hamming"),
                                   cl::desc("The desired DFI scheme"),
                                   cl::value_desc("DFI scheme"));

/*
 * Runs all defined static analysis plugins on a module.
 */
bool DOPGuard::runOnModule(Module &M) {
  bool changed = false;
  dfiSchemeFType *instF;

  if (schemeMap.count(scheme)) {
    instF = schemeMap[scheme];
  } else {
    dbgs() << "Unknown DFI scheme '" << scheme << "' requested; exiting\n";
    return false;
  }

  for (auto it = DOPGuard::pluginMap.begin(); it != DOPGuard::pluginMap.end();
       it++) {
    bool passChanged = it->second(M);
    changed = changed || passChanged;

    dbgs() << "pass " << it->first() << " returned " << passChanged << "\n";
  }

  if (changed) {
    calculateMetadataType(M);
    createMetadataArray(M);
    calculateLabels();
    instrumentIsolatedVars(instF);
  }

  return changed;
}

/*
 * Define the label array.
 */
void DOPGuard::createMetadataArray(llvm::Module &m) {
  ArrayType *arrType = ArrayType::get(labelMetadataType, isolatedVars.size());

  GlobalVariable *arr = new GlobalVariable(
      m, arrType, false, GlobalValue::ExternalLinkage, nullptr, labelArrName);
  arr->setInitializer(ConstantAggregateZero::get(arrType));
  arr->setAlignment(MaybeAlign(8));
}

/*
 * Determine size of label type based on the number of
 * instrumented variable and labels.
 */
void DOPGuard::calculateMetadataType(llvm::Module &m) {
  LLVMContext &C = m.getContext();
  // TODO: determine type size dynamically
  labelMetadataType = IntegerType::getInt64Ty(C);
}

/*
 * Takes a stack variable and replaces it with a LocalExec TLS variable.
 */
void DOPGuard::promoteToThreadLocal(llvm::Module &m, AllocaInst *al) {
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

void DOPGuard::promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas) {
  std::ostringstream varName;

  for (llvm::AllocaInst *al : *allocas) {
    promoteToThreadLocal(m, al);
  }
}

/*
 * Inserts a block of instructions that enforce dataflow integrity
 * by indexing into the metadata field, calculating the chosen distance metric
 * for the last recorded store label and the current label, and comparing the
 * result with the local threshold.
 *
 * The target BasicBlock is split before the instruction "u" and the instruction
 * block is appended to the newly created, preceding BB.
 */
void DOPGuard::insertDFIInst(User *u, dfiSchemeFType instF) {
  Instruction *i, *predTerm;
  BasicBlock *old, *pred;
  // Value *loadStoreTargetPtr;
  BasicBlock *abortBB;

  i = dyn_cast<llvm::Instruction>(u);

  Module *m = i->getModule();
  // LLVMContext &C = u->getContext();
  Value *metadataArray = m->getGlobalVariable(DOPGuard::labelArrName);

  /* Fetch 'rdfsbase64' */
  //  Function *rdFS64 = Intrinsic::getDeclaration(m,
  //  Intrinsic::x86_rdfsbase_64);
  if (isa<llvm::LoadInst>(i)) {

    SmallPtrSet<BasicBlock *, 10> oldPreds = {};
    SmallPtrSet<BasicBlock *, 10> newPreds = {};

    old = i->getParent();
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

    for (BasicBlock *bb : oldPreds) {
      if (newPreds.contains(bb))
        continue;

      /* SplitBlock failed to link all predecessors; do this manually */
      Instruction *term = &bb->back();

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
    // Value *targetPtrInt =
    //  builder.CreatePtrToInt(loadStoreTargetPtr, IntegerType::getInt64Ty(C));

    /* Index into label array */
    Value *metadataPtr = builder.CreateGEP(
        metadataArray->getType()->getScalarType()->getPointerElementType(),
        metadataArray,
        ArrayRef<Value *>({Constant::getNullValue(labelMetadataType),
                           ConstantInt::get(labelMetadataType, 0)}));

    /* Fetch last label */
    Value *lastLabel =
        builder.CreateLoad(DOPGuard::labelMetadataType, metadataPtr);

    // /* Calculate distance metric */
    // Value *distance = builder.CreateXor(lastLabel, lastLabel);

    // /* Compare with threshold */
    // // TODO: calculate and store thresholds in a
    // // "ProtectedVar" wrapper class
    // long long loadThresh = getThreshold(i);
    // Value *equal = builder.CreateICmpEQ(
    //     distance,
    //     dyn_cast<Value>(ConstantInt::get(DOPGuard::labelMetadataType,
    //                                                loadThresh)));
    assert(loadToStoreMap.count(dyn_cast<LoadInst>(i)) > 0);
    StoreInst *si = loadToStoreMap[dyn_cast<LoadInst>(i)];
    assert(rds.count(si) > 0);

    if (rds[si].size() > 1) {
      /* Instrument according to selected DFI scheme */
      instF(builder, lastLabel, i, old, abortBB);
    } else {
      Value *equal = builder.CreateICmpEQ(
          dyn_cast<Value>(ConstantInt::get(DOPGuard::labelMetadataType, 0)),
          dyn_cast<Value>(ConstantInt::get(DOPGuard::labelMetadataType, 0)));

      builder.CreateCondBr(equal, old, abortBB);
    }
    /* Insert a conditional branch */
    // builder.CreateCondBr(equal, old, abortBB);
    /* Erase unconditional branch placed by SplitBlock */
    pred->getTerminator()->eraseFromParent();
  } else if (isa<llvm::StoreInst>(i)) {
    // loadStoreTargetPtr = i->getOperand(1);

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
    Value *metadataPtr = builder.CreateGEP(
        metadataArray->getType()->getScalarType()->getPointerElementType(),
        metadataArray,
        ArrayRef<Value *>({Constant::getNullValue(labelMetadataType),
                           ConstantInt::get(labelMetadataType, 0)}));

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
BasicBlock *DOPGuard::createAbortCallBB(llvm::Module *m, Function *F) {
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

using DefSet = SmallPtrSet<StoreInst *, 10>;
using BBDefMap = DenseMap<const BasicBlock *, DefSet>;
using VarDefMap = DenseMap<const GlobalVariable *, DefSet>;
using Worklist = SmallVector<const BasicBlock *, 10>;

/*
 * Runs Reaching Definitions Analysis for protected variables and calculates
 * the RDS.
 */
void DOPGuard::calculateRDS(void) {

  SmallVector<Function *, 5> funcs;

  /* Collect each function where the protected var has users */
  for (auto &g : isolatedVars) {
    GlobalVariable *isolVar = g;
    for (auto it = isolVar->user_begin(); it != isolVar->user_end(); it++) {
      if (isa<LoadInst>(*it) || isa<StoreInst>(*it)) {
        funcs.push_back((dyn_cast<Instruction>(*it))->getFunction());
      }
    }
  }

  for (Function *f : funcs) {
    BBDefMap gens;
    BBDefMap kills;
    BBDefMap out;
    BBDefMap in;
    VarDefMap allDefs;

    /* Collect all defs for variables in f */
    for (GlobalVariable *isolVar : isolatedVars) {
      allDefs.insert(std::make_pair(isolVar, DefSet()));
      for (auto it = isolVar->user_begin(); it != isolVar->user_end(); it++) {
        if (StoreInst *si = dyn_cast<StoreInst>(*it)) {
          if (si->getFunction() == f) {
            allDefs[isolVar].insert(si);
          }
        }
      }
    }

    /* Collect GEN and KILL for each variable and BB */
    for (GlobalVariable *g : isolatedVars) {
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
void DOPGuard::calculateLabels(void) { calculateRDS(); }

long long DOPGuard::getLabel(Instruction *i) {
  /* TODO: Implement when label calculation is done */
  return 0;
}

long long DOPGuard::getThreshold(Instruction *loadI) {
  /* TODO: Implement when label calculation is done */
  return 0;
}

bool DOPGuard::addPassPlugin(std::string name,
                             std::function<bool(llvm::Module &)> func) {
  return DOPGuard::pluginMap
      .insert(std::pair<std::string, std::function<bool(llvm::Module &)>>(name,
                                                                          func))
      .second;
}

/*
 * Traverses each user of a DFI-protected variable and instruments
 * accordingly.
 */
void DOPGuard::instrumentIsolatedVars(dfiSchemeFType instF) {
  for (auto &it : rds) {
    insertDFIInst(it.first, instF);

    for (LoadInst *li : it.second) {
      insertDFIInst(li, instF);
    }
  }
}

void DOPGuard::hammingInst(IRBuilder<> &builder, Value *lastLabel,
                           Instruction *i, BasicBlock *old,
                           BasicBlock *abortBB) {
  /* Calculate distance metric */
  Value *distance = builder.CreateXor(lastLabel, lastLabel);

  /* Compare with threshold */
  // TODO: calculate and store thresholds in a
  // "ProtectedVar" wrapper class
  long long loadThresh = getThreshold(i);
  Value *equal = builder.CreateICmpEQ(
      distance, dyn_cast<Value>(
                    ConstantInt::get(DOPGuard::labelMetadataType, loadThresh)));

  builder.CreateCondBr(equal, old, abortBB);
}

void DOPGuard::primeInst(IRBuilder<> &builder, Value *lastLabel, Instruction *i,
                         BasicBlock *old, BasicBlock *abortBB) {

  Value *rem = builder.CreateURem(
      lastLabel, ConstantInt::get(DOPGuard::labelMetadataType, 14));

  Value *equal = builder.CreateICmpEQ(
      rem, dyn_cast<Value>(ConstantInt::get(DOPGuard::labelMetadataType, 0)));

  builder.CreateCondBr(equal, old, abortBB);
}

void DOPGuard::manhInst(IRBuilder<> &builder, Value *lastLabel, Instruction *i,
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

void DOPGuard::dfiInst(IRBuilder<> &builder, Value *lastLabel, Instruction *i,
                       BasicBlock *old, BasicBlock *abortBB) {
  int numblocks = 0;
  SmallVector<BasicBlock *, 10> blocks = {};
  BasicBlock *pred = old->getSinglePredecessor();

  StoreInst *si = loadToStoreMap[dyn_cast<LoadInst>(i)];
  numblocks = rds[si].size();

  blocks.push_back(pred);
  for (int j = 0; j < (numblocks - 1); j++) {
    blocks.push_back(BasicBlock::Create(i->getParent()->getContext(), "",
                                        i->getFunction(), nullptr));
  }

  /* Link each block in the compare-branch chain, starting from the last to
   * the first */
  BasicBlock *falseTgt = abortBB;
  for (int j = 0; j < numblocks; j++) {
    BasicBlock *bb = blocks.back();
    IRBuilder<> curBuilder(bb);

    if (bb->size() != 0)
      curBuilder.SetInsertPoint(&bb->back());

    Value *equal = curBuilder.CreateICmpEQ(
        lastLabel, ConstantInt::get(DOPGuard::labelMetadataType, j));
    curBuilder.CreateCondBr(equal, old, falseTgt);

    falseTgt = bb;
    blocks.pop_back();
  }
}
/*
 * LLVM pass boilerplate code.
 */
PreservedAnalyses DOPGuard::run(llvm::Module &M,
                                llvm::ModuleAnalysisManager &) {

  return (runOnModule(M) ? llvm::PreservedAnalyses::none()
                         : llvm::PreservedAnalyses::all());
}

bool LegacyDOPGuard::runOnModule(llvm::Module &M) {
  return Impl.runOnModule(M);
}

llvm::PassPluginLibraryInfo getDOPGuardPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "dguard-pass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [&](ModulePassManager &MPM, auto) {
                  MPM.addPass(DOPGuard());
                  return true;
                });
            /*          [](PassBuilder &PB) {
                    PB.registerPipelineParsingCallback(
                        [](StringRef Name, ModulePassManager &MPM,
                           ArrayRef<PassBuilder::PipelineElement>) {
                          if (Name == "dguard-pass") {
                            MPM.addPass(DOPGuard());
                            return true;
                          }
                          return false;
                        });*/
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getDOPGuardPluginInfo();
}

/*
 * Private class data initialization.
 */
llvm::StringMap<std::function<bool(llvm::Module &)>> DOPGuard::pluginMap = {};
std::vector<llvm::GlobalVariable *> DOPGuard::isolatedVars = {};
int DOPGuard::allocaId = 0;
const std::string DOPGuard::labelArrName = "__dguard_label_arr";
llvm::Type *DOPGuard::labelMetadataType = nullptr;
llvm::ValueMap<llvm::Value *, long long> DOPGuard::labelStore{};

llvm::StringMap<dfiSchemeFType *> DOPGuard::schemeMap = {
    {"dfi", dfiInst},
    {"hamming", hammingInst},
    {"prime", primeInst},
    {"manhattan", manhInst},
};

llvm::DenseMap<llvm::LoadInst *, llvm::StoreInst *> DOPGuard::loadToStoreMap{};
llvm::DenseMap<llvm::StoreInst *, SmallPtrSet<LoadInst *, 10>> DOPGuard::rds{};
