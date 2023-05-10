
#include "pass.h"
#include "plugin.hpp"

#include "llvm/ADT/None.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include <cstddef>
#include <llvm/ADT/StringRef.h>
#include "llvm/Demangle/Demangle.h"

using namespace llvm;

#define DEBUG_TYPE "fulldfi-plugin-pass"

class FullDFIPlugin {

private:
  static StringSet<> funcnameSet;

public:
  static bool runOnModule(llvm::Module &M, DGuard *dguard) {
    bool changed = false;
    ValueVec varsToBeIsolated;

    for (auto &Func : M) {
      /* Promote each stack variable */
      for (auto &BB : Func) {
        for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
             ++inst) {

          if (AllocaInst *al = dyn_cast<AllocaInst>(inst)) {
            varsToBeIsolated.push_back(al);
          }
        }
      }
    }

    dbgs() << "Isolated " << varsToBeIsolated.size()
           << " stack variables in module " << M.getName() << "\n";

    for (auto it = M.global_begin(); it != M.global_end(); it++) {
      varsToBeIsolated.push_back(&*it);
    }

    dbgs() << "Isolated " << varsToBeIsolated.size() << " variables in total\n";

    if (varsToBeIsolated.size() != 0) {
      dguard->addIsolatedVars(M, &varsToBeIsolated);
      changed = true;
    }

    return changed;
  }
};

REGISTER_PASS_PLUGIN(full, FullDFIPlugin::runOnModule);
