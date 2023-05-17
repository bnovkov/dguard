
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

#define DEBUG_TYPE "funclist-plugin-pass"

class FuncListPlugin {

private:
  static StringSet<> funcnameSet;

public:
  static bool runOnModule(llvm::Module &M, DGuard *dguard) {
    bool changed = false;
    ValueVec allocasToBePromoted;

    for (auto &Func : M) {
      std::string fnameDemangled = llvm::demangle(Func.getName().str());

      /* Check for arg types in demangled fname */
      size_t bracketPos = fnameDemangled.find_first_of('(');
      if (bracketPos != std::string::npos) {
        fnameDemangled = fnameDemangled.substr(0, bracketPos);
      }

      if (!funcnameSet.contains(fnameDemangled)) {
        continue;
      }

      dbgs() << "Function " << fnameDemangled << " found\n";
      /* Promote each stack variable */
      for (auto &BB : Func) {
        for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
             ++inst) {

          if (AllocaInst *al = dyn_cast<AllocaInst>(inst)) {
            allocasToBePromoted.push_back(al);
          }
        }
      }

      for (auto it = M.global_begin(); it != M.global_end(); it++) {
        allocasToBePromoted.push_back(&*it);
      }

      dbgs() << "Promoted " << allocasToBePromoted.size()
             << " allocas in function " << fnameDemangled << "\n";
    }

    if (allocasToBePromoted.size() != 0) {
      dguard->addIsolatedVars(M, &allocasToBePromoted);
      changed = true;
    }

    return changed;
  }
};

StringSet<> FuncListPlugin::funcnameSet{"bubble_sort"};

REGISTER_PASS_PLUGIN(funclist, FuncListPlugin::runOnModule);
