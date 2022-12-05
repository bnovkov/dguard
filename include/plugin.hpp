#include "pass.h"
#include "llvm/IR/Module.h"

#define REGISTER_PASS_PLUGIN(plugin_name, module_pass_func)                    \
  bool pass_plugin_name_##plugin_name =                                        \
      DOPGuard::addPassPlugin(#plugin_name, (module_pass_func))
