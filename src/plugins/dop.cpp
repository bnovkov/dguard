

#include "llvm/ADT/None.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include <llvm/ADT/StringRef.h>

using namespace llvm;

#define DEBUG_TYPE "dop-plugin-pass"

class DOPPassPlugin {

private:
  // TODO: find a way of handling llvm builtin functions
  static llvm::StringMap<std::function<void(llvm::CallBase *, AllocaVec *)>>
      funcSymbolDispatchMap;

  static bool findBranch(llvm::CmpInst *Inst) {
    for (User *U : Inst->users()) {
      if (dyn_cast<BranchInst>(U) != nullptr) {
        return true;
      } else {
        Instruction *cb3 = dyn_cast<Instruction>(U);
        return findInstruction(cb3);
      }
    }
    return false;
  }

  static bool findInstruction(llvm::Instruction *Inst) {
    LLVM_DEBUG(dbgs() << "+++++++++\n"
                      << "\n");
    for (User *U : Inst->users()) {
      LLVM_DEBUG(dbgs() << "\t" << *U << "\n");
      if (CmpInst *cb2 = dyn_cast<CmpInst>(U)) {

        return findBranch(cb2);
      } else {
        Instruction *cb3 = dyn_cast<Instruction>(U);
        return findInstruction(cb3);
      }
    }
    return false;
  }

public:
  static bool runOnModule(llvm::Module &M) {
    bool changed = false;

    for (auto &Func : M) {
      llvm::SmallVector<PointerType, 10> ptrVars;
      AllocaVec allocasToBePromoted;
      AllocaVec vulnAllocas;

      for (auto &BB : Func) {
        for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
             ++inst) {
          if (CallBase *cb = dyn_cast<CallBase>(inst)) {
            Function *f = cb->getCalledFunction();
            if (!f) // check for indirect call
              continue;
            StringRef name = f->getName();
            LLVM_DEBUG(dbgs() << "NAME " << name << "\n");

            if (funcSymbolDispatchMap.count(name)) {
              funcSymbolDispatchMap[name](cb, &vulnAllocas);
            }
          }
        }
      }

      if (vulnAllocas.size() == 0) {
        continue;
      }

      for (auto &BB : Func) {
        for (BasicBlock::iterator inst = BB.begin(), IE = BB.end(); inst != IE;
             ++inst) {

          if (AllocaInst *cb = dyn_cast<AllocaInst>(inst)) {
            Instruction *Inst = dyn_cast<Instruction>(inst);

            if (findInstruction(Inst)) {
              allocasToBePromoted.push_back(cb);
            }
          }
        }
      }
      if (allocasToBePromoted.size() != 0) {
        DOPGuard::promoteToThreadLocal(M, &allocasToBePromoted);
        changed = true;
      }
    }
    return changed;
  };
};

llvm::StringMap<std::function<void(llvm::CallBase *, AllocaVec *)>>
    DOPPassPlugin::funcSymbolDispatchMap = {
        {"memcpy",
         [](llvm::CallBase *i, AllocaVec *vec) {
           Value *op = i->getOperand(0);
           if (GetElementPtrInst *geInst = dyn_cast<GetElementPtrInst>(op)) {
             Value *dstVar = geInst->getPointerOperand();
             if (AllocaInst *al = dyn_cast<AllocaInst>(dstVar)) {
               vec->push_back(al);
             }
           }
         }},
        {"read",
         [](llvm::CallBase *i, AllocaVec *vec) {
           Value *op = i->getOperand(1);
           if (GetElementPtrInst *geInst = dyn_cast<GetElementPtrInst>(op)) {
             Value *dstVar = geInst->getPointerOperand();
             if (AllocaInst *al = dyn_cast<AllocaInst>(dstVar)) {
               vec->push_back(al);
             }
           }
         }},
};

REGISTER_PASS_PLUGIN(dop, DOPPassPlugin::runOnModule);
