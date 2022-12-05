
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
#include <llvm/ADT/StringRef.h>

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
      if (!funcnameSet.contains(Func.getName())) {
        continue;
      }
      dbgs() << "Function " << Func.getName() << "found\n";
      /* Promote each stack variable */
      for (auto &BB : Func) {
        for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
             ++inst) {

          if (AllocaInst *al = dyn_cast<AllocaInst>(inst)) {
            allocasToBePromoted.push_back(al);
          }
        }
      }
    }

    if (allocasToBePromoted.size() != 0) {
      DOPGuard::promoteToThreadLocal(M, &allocasToBePromoted);
      changed = true;
    }

    return changed;
  }
};

StringSet<> FuncListPlugin::funcnameSet{
    "BlkSchlsEqEuroNoDiv",                   /* blackscholes */
    "ComputeForcesMT",                       /* fluidanimate */
    "ComputeDensitiesMT",                    /* fluidanimate */
    "pgain",                                 /* streamcluster */
    "HJM_SimPath_Forward_Blocking",          /* swaptions */
    "CumNormalInv",                          /* swaptions */
    "conv_gen",                              /* vips */
    "vips_interpolate_bilinear_interpolate", /* vips */
    "lintran_gen",                           /* vips */
    "walksub",                               /* splash2x.barnes */
    "gravsub",                               /* splash2x.barnes */
    "ModifyTwoBySupernodeB",                 /* splash2x.cholesky */
    "FillIn",                                /* splash2x.cholesky */
    "FFT1DOnce",                             /* splash2x.fft */
    "Transpose",                             /* splash2x.fft */
    "lu",                                    /* splash2x.lu_* */
    "relax",                                 /* splash2x.ocean_cp */
    "subdivide_element",                     /* splash2x.radiosity */
    "init",                                  /* splash2x.radix */
    "CSHIFT",                                /* splash2x.water_* */
    "INTERF",                                /* splash2x.water */
    "MDMAIN",                                /* splash2x.water */
};

REGISTER_PASS_PLUGIN(funclist, FuncListPlugin::runOnModule);
