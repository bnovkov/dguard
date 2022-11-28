
#include "pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Use.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <cstdlib>
#include <fstream>
#include <ios>
#include <sstream>
#include <utility>

using namespace llvm;

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#define DEBUG_TYPE "dguard-pass"

/*
 * Runs all defined static analysis plugins on a module.
 */
bool DOPGuard::runOnModule(Module &M) {
  bool changed = false;

  for (auto it = DOPGuard::pluginMap.begin(); it != DOPGuard::pluginMap.end();
       it++) {
    bool passChanged = it->second(M);
    changed = changed || passChanged;

    LLVM_DEBUG(dbgs() << "pass " << it->first() << " returned " << passChanged
                      << "\n");
  }

  if (changed) {
    calculateMetadataType(M);
    createMetadataArray(M);
    instrumentIsolatedVars();
    emitModuleMetadata(M);
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
  labelMetadataType = IntegerType::getInt64Ty(C);
}

/*
 * Takes a stack variable and replaces it with a LocalExec TLS variable.
 * Also defines a separate metadata variable that represents the TLS offset to
 * the promoted variable.
 */
void DOPGuard::promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas) {
  std::ostringstream varName;

  for (llvm::AllocaInst *al : *allocas) {
    BasicBlock::iterator ii(al);
    const Function *f = ii->getFunction();
    if (f == nullptr) {
      varName << ii->getNameOrAsOperand() << allocaId;
    } else {
      varName << f->getNameOrAsOperand() << ii->getNameOrAsOperand()
              << allocaId;
    }

    /* Declare the new "stack" variable */
    GlobalVariable *alloca_global = new GlobalVariable(
        m, al->getAllocatedType(), false,
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
void DOPGuard::insertDFIInst(User *u) {
  Instruction *i, *predTerm;
  BasicBlock *old, *pred;
  FunctionType *rdFSType;
  Value *loadStoreTargetPtr;
  BasicBlock *abortBB;

  i = dyn_cast<llvm::Instruction>(u);

  Module *m = i->getModule();
  LLVMContext &C = u->getContext();
  Value *metadataArray = m->getGlobalVariable(DOPGuard::labelArrName);

  /* Fetch 'rdfsbase64' */
  rdFSType = FunctionType::get(IntegerType::getInt64PtrTy(C), false);
  FunctionCallee rdFS64 =
      m->getOrInsertFunction("__builtin_ia32_rdfsbase64", rdFSType);

  if (isa<llvm::LoadInst>(i)) {
    loadStoreTargetPtr = i->getOperand(0);

    old = i->getParent();
    pred = SplitBlock(old, i, static_cast<DominatorTree *>(nullptr), nullptr,
                      nullptr, "",
                      /* before */ true);
    if (pred == nullptr) {
      // debug
      abort();
    }
    predTerm = &pred->back();

    /* Create abortBB */
    abortBB = createAbortCallBB(m, old->getParent());

    IRBuilder<> builder(pred);
    builder.SetInsertPoint(predTerm);

    /* Get TLS block addr */
    CallInst *rdFSInst = builder.CreateCall(rdFS64);
    Value *TLSPtrInt = builder.CreatePtrToInt(dyn_cast<Value>(rdFSInst),
                                              IntegerType::getInt64Ty(C));
    /* Fetch target address */
    Value *targetPtrInt =
        builder.CreatePtrToInt(loadStoreTargetPtr, IntegerType::getInt64Ty(C));

    /* Index into label array */
    Value *metadataPtr = builder.CreateGEP(
        metadataArray->getType()->getScalarType()->getPointerElementType(),
        metadataArray,
        ArrayRef<Value *>({Constant::getNullValue(labelMetadataType),
                           builder.CreateSub(targetPtrInt, TLSPtrInt)}));

    /* Fetch last label */
    Value *lastLabel =
        builder.CreateLoad(DOPGuard::labelMetadataType, metadataPtr);

    /* Calculate distance metric */
    Value *distance = builder.CreateXor(lastLabel, lastLabel);

    /* Compare with threshold */
    // TODO: calculate and store thresholds in a
    // "ProtectedVar" wrapper class
    Value *equal = builder.CreateICmpEQ(
        distance,
        dyn_cast<Value>(Constant::getNullValue(DOPGuard::labelMetadataType)));

    /* Insert a conditional branch */
    builder.CreateCondBr(equal, old, abortBB);

    /* Erase unconditional branch placed by SplitBlock */
    pred->getTerminator()->eraseFromParent();

  } else if (isa<llvm::StoreInst>(i)) {
    loadStoreTargetPtr = i->getOperand(1);

    IRBuilder<> builder(i->getParent());
    builder.SetInsertPoint(i);

    /* Get TLS block addr */
    CallInst *rdFSInst = builder.CreateCall(rdFS64);
    Value *TLSPtrInt = builder.CreatePtrToInt(dyn_cast<Value>(rdFSInst),
                                              IntegerType::getInt64Ty(C));

    Value *index = builder.CreateSub(
        builder.CreatePtrToInt(loadStoreTargetPtr, IntegerType::getInt64Ty(C)),
        TLSPtrInt);

    /* Index into label array */
    Value *metadataPtr = builder.CreateGEP(
        metadataArray->getType()->getScalarType()->getPointerElementType(),
        metadataArray,
        ArrayRef<Value *>({Constant::getNullValue(labelMetadataType), index}));

    /* Store current label */
    // TODO: fetch labels from static store
    builder.CreateStore(Constant::getNullValue(labelMetadataType), metadataPtr);
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

/*
 * Traverses each user of an isolated variable and instruments accordingly.
 */
void DOPGuard::instrumentIsolatedVars(void) {
  for (auto &g : isolatedVars) {
    GlobalVariable *isolVar = g;
    for (auto it = isolVar->user_begin(); it != isolVar->user_end(); it++) {
      insertDFIInst(*it);
    }
  }
}

void DOPGuard::emitModuleMetadata(llvm::Module &m) {
  std::stringstream ss;
  std::ofstream module_metadata_file;

  ss << m.getName().str() << ".mtdt";

  module_metadata_file.open(ss.str(), std::ios_base::out);

  for (auto &g : isolatedVars) {
    module_metadata_file << g->getName().str() << "\n";
  }

  module_metadata_file.close();
}

/*
 * Injects a call to a function that populates relevant runtime metadata.
 * The function call is performed immediately after entering main().
 */
void DOPGuard::injectMetadataInitializer(llvm::Module &m) {
  Function *main = m.getFunction("main");

  FunctionType *metadata_init_ty;
  Function *metadata_initF;
  llvm::LLVMContext &ctx = m.getContext();

  /* Bail if module does not contain main() */
  if (main == nullptr)
    return;

  /* Declare int __tls_isol_metadata_init(void); */
  metadata_init_ty =
      FunctionType::get(IntegerType::getInt32Ty(ctx), /*IsVarArgs=*/false);
  FunctionCallee metadata_init =
      m.getOrInsertFunction("__tls_isol_metadata_init", metadata_init_ty);
  metadata_initF = dyn_cast<Function>(metadata_init.getCallee());
  metadata_initF->setDoesNotThrow();

  /* Inject call to __tls_isol_metadata_init */
  IRBuilder<> Builder(&*main->getEntryBlock().getFirstInsertionPt());
  Builder.CreateCall(metadata_init);
}

bool DOPGuard::addPassPlugin(std::string name,
                             std::function<bool(llvm::Module &)> func) {
  return DOPGuard::pluginMap
      .insert(std::pair<std::string, std::function<bool(llvm::Module &)>>(name,
                                                                          func))
      .second;
}

PreservedAnalyses DOPGuard::run(llvm::Module &M,
                                llvm::ModuleAnalysisManager &) {

  return (runOnModule(M) ? llvm::PreservedAnalyses::none()
                         : llvm::PreservedAnalyses::all());
}

bool LegacyDOPGuard::runOnModule(llvm::Module &M) {
  return Impl.runOnModule(M);
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
static RegisterPass<LegacyDOPGuard> X(/*PassArg=*/"legacy-dopg-pass",
                                      /*Name=*/"LegacyDOPGuard",
                                      /*CFGOnly=*/false,
                                      /*is_analysis=*/false);

/*
 * Private class data
 */
llvm::StringMap<std::function<bool(llvm::Module &)>> DOPGuard::pluginMap = {};
std::vector<llvm::GlobalVariable *> DOPGuard::isolatedVars = {};
int DOPGuard::allocaId = 0;
const std::string DOPGuard::labelArrName = "__dguard_label_arr";
llvm::Type *DOPGuard::labelMetadataType = nullptr;
