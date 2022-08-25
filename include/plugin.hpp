#include "pass.h"
#include "llvm/IR/Module.h"

#define REGISTER_PASS_PLUGIN(plugin_name, module_pass_func)                    \
  bool pass_plugin_name##_entry =                                              \
      PassPluginRegistry<Plugin>::add(#plugin_name, (module_pass_func))

class DGuardPassPlugin {
public:
  virtual bool modulePass(llvm::Module &m) = 0;
};
