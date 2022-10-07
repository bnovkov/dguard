
#include "pass.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
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
    injectMetadataInitializer(M);
    instrumentIsolatedVars();
    emitModuleMetadata(M);
  }

  return changed;
}

/*
 * Takes a stack variable and replaces it with a LocalExec TLS variable.
 * Also defines a separate metadata variable that represents the TLS offset to
 * the promoted variable.
 */
void DOPGuard::promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas) {
  std::ostringstream varName;
  std::ostringstream offVarName;

  for (llvm::AllocaInst *al : *allocas) {
    BasicBlock::iterator ii(al);
    const Function *f = ii->getFunction();
    if (f == nullptr) {
      varName << ii->getNameOrAsOperand() << allocaId;
    } else {
      varName << f->getNameOrAsOperand() << ii->getNameOrAsOperand()
              << allocaId;
    }

    /* Declare new "stack" variable */
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

    /*
     * Declare a new symbol holding the TLS offset of the isolated stack
     * variable. Our patched linker will recognize this metadata variable and
     * inject the offset in the appropriate MOV instruction
     */
    offVarName << "__dguardoff_" << varName.str() << "_offset";
    GlobalVariable *alloca_global_off = new GlobalVariable(
        m, llvm::IntegerType::getInt32Ty(m.getContext()), false,
        GlobalValue::InternalLinkage, nullptr, offVarName.str(), nullptr,
        GlobalValue::ThreadLocalMode::LocalExecTLSModel);

    isolatedVars.push_back(std::make_pair(alloca_global, alloca_global_off));

    varName.clear();
    varName.str("");

    offVarName.clear();
    offVarName.str("");

    allocaId++;
  }
}

/*
 * Inserts a block of instructions that enforce variable isolation
 * by calculating the target address and comparing it with the allowed ones.
 */
void DOPGuard::insertIsolationInsBlockSingleUser(void) {}

void DOPGuard::instrumentIsolatedVars(void) {
  for (auto &g : isolatedVars) {
    //    GlobalVariable *isolVar = g.first;
  }
}

void DOPGuard::emitModuleMetadata(llvm::Module &m) {
  std::stringstream ss;
  std::ofstream module_metadata_file;

  ss << m.getName().str() << ".mtdt";

  module_metadata_file.open(ss.str(), std::ios_base::out);

  for (auto &g : isolatedVars) {
    module_metadata_file << g.first->getName().str() << "\n";
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
std::vector<std::pair<llvm::GlobalVariable *, llvm::GlobalVariable *>>
    DOPGuard::isolatedVars = {};
int DOPGuard::allocaId = 0;
