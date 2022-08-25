
#include "plugin.hpp"

class DOPPassPlugin {

private:
  // TODO: find a way of handling llvm builtin functions
  static llvm::StringMap<std::function<void(llvm::CallBase *, AllocaVec *)>>
      funcSymbolDispatchMap;

  static void promoteToThreadLocal(llvm::Module &m, AllocaVec *allocas) {
    for (llvm::AllocaInst *al : *allocas) {
      BasicBlock::iterator ii(al);
      GlobalVariable *alloca_global = new GlobalVariable(
          m, al->getAllocatedType(), false,
          GlobalValue::InternalLinkage, // TODO: change to static
          nullptr, "", nullptr,
          GlobalValue::ThreadLocalMode::LocalExecTLSModel);

      // Set initializer
      UndefValue *allocaInitializer = UndefValue::get(al->getAllocatedType());
      alloca_global->setInitializer(allocaInitializer);

      ReplaceInstWithValue(al->getParent()->getInstList(), ii,
                           dyn_cast<Value>(alloca_global));
    }
  }
  static bool findBranch(llvm::CmpInst *Inst) {
    for (User *U : Inst->users()) {
      if (dyn_cast<BranchInst>(U) != nullptr) {
        return true;
      } else {
        Instruction *cb3 = dyn_cast<Instruction>(U);
        return DOPGuard::findInstruction(cb3);
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

        return DOPGuard::findBranch(cb2);
      } else {
        Instruction *cb3 = dyn_cast<Instruction>(U);
        return DOPGuard::findInstruction(cb3);
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
        promoteToThreadLocal(M, &allocasToBePromoted);
        changed = true;
      }
    }
    return changed;
  };
};

llvm::StringMap<std::function<void(llvm::CallBase *, DOPGuard::AllocaVec *)>>
    DOPPassPlugin::funcSymbolDispatchMap = {
        {"memcpy",
         [](llvm::CallBase *i, DOPGuard::AllocaVec *vec) {
           Value *op = i->getOperand(0);
           if (GetElementPtrInst *geInst = dyn_cast<GetElementPtrInst>(op)) {
             Value *dstVar = geInst->getPointerOperand();
             if (AllocaInst *al = dyn_cast<AllocaInst>(dstVar)) {
               vec->push_back(al);
             }
           }
         }},
        {"read",
         [](llvm::CallBase *i, DOPGuard::AllocaVec *vec) {
           Value *op = i->getOperand(1);
           if (GetElementPtrInst *geInst = dyn_cast<GetElementPtrInst>(op)) {
             Value *dstVar = geInst->getPointerOperand();
             if (AllocaInst *al = dyn_cast<AllocaInst>(dstVar)) {
               vec->push_back(al);
             }
           }
         }},
};
