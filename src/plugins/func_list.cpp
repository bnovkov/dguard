
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
  static bool runOnModule(llvm::Module &M) {
    bool changed = false;
    AllocaVec allocasToBePromoted;

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
      dbgs() << "Promoted " << allocasToBePromoted.size()
             << " allocas in function " << fnameDemangled << "\n";
    }

    if (allocasToBePromoted.size() != 0) {
      DGuard::promoteToThreadLocal(M, &allocasToBePromoted);
      changed = true;
    }

    return changed;
  }
};

StringSet<> FuncListPlugin::funcnameSet{
    "BlkSchlsEqEuroNoDiv",          /* blackscholes */
    "ComputeForcesMT",              /* fluidanimate */
    "pgain",                        /* streamcluster */
    "HJM_SimPath_Forward_Blocking", /* swaptions */
    "walksub",                      /* splash2x.barnes */
    "ModifyTwoBySupernodeB",        /* splash2x.cholesky */
    "FFT1DOnce",                    /* splash2x.fft */
    "lu",                           /* splash2x.lu_* */
    "relax",                        /* splash2x.ocean_cp */
    "subdivide_element",            /* splash2x.radiosity */
    "init",                         /* splash2x.radix */
    "CSHIFT",                       /* splash2x.water_* */
};

REGISTER_PASS_PLUGIN(funclist, FuncListPlugin::runOnModule);
