
#include "pass.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <cstdlib>
#include <sstream>

using namespace llvm;

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#define DEBUG_TYPE "dguard-pass"

bool DOPGuard::runOnModule(Module &M) {
  bool changed = false;

  for (auto it = DOPGuard::pluginMap.begin(); it != DOPGuard::pluginMap.end();
       it++) {
    bool passChanged = it->second(M);
    changed = changed || passChanged;

    LLVM_DEBUG(dbgs() << "pass " << it->first() << " returned " << passChanged
                      << "\n");
  }

  return changed;
}

void DOPGuard::promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas) {
  std::ostringstream ss;
  int allocaIdx = 0;

  for (llvm::AllocaInst *al : *allocas) {
    BasicBlock::iterator ii(al);
    const Function *f = ii->getFunction();
    if (f == nullptr) {
      ss << ii->getNameOrAsOperand() << allocaIdx;
    } else {
      ss << f->getNameOrAsOperand() << ii->getNameOrAsOperand() << allocaIdx;
    }

    GlobalVariable *alloca_global = new GlobalVariable(
        m, al->getAllocatedType(), false,
        GlobalValue::InternalLinkage, // TODO: change to static
        nullptr, ss.str(), nullptr,
        GlobalValue::ThreadLocalMode::LocalExecTLSModel);

    // Set initializer
    UndefValue *allocaInitializer = UndefValue::get(al->getAllocatedType());
    alloca_global->setInitializer(allocaInitializer);

    ReplaceInstWithValue(al->getParent()->getInstList(), ii,
                         dyn_cast<Value>(alloca_global));

    ss.clear();
    ss.str("");
    allocaIdx++;
  }
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
                                      /*CFGOnly=*/false, /*is_analysis=*/false);

llvm::StringMap<std::function<bool(llvm::Module &)>> DOPGuard::pluginMap = {};
