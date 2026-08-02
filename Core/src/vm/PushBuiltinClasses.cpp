#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_push_class(std::string_view name) {
  if (name == "javax/microedition/io/PushRegistry") {
    return make_class("javax/microedition/io/PushRegistry", "java/lang/Object",
                      kOrdinary | kFinal, {},
                      {
                          method(kPublic | kStatic, "registerConnection",
                                 "(Ljava/lang/String;Ljava/lang/String;"
                                 "Ljava/lang/String;)V"),
                          method(kPublic | kStatic, "unregisterConnection",
                                 "(Ljava/lang/String;)Z"),
                          method(kPublic | kStatic, "listConnections",
                                 "(Z)[Ljava/lang/String;"),
                          method(kPublic | kStatic, "getMIDlet",
                                 "(Ljava/lang/String;)Ljava/lang/String;"),
                          method(kPublic | kStatic, "getFilter",
                                 "(Ljava/lang/String;)Ljava/lang/String;"),
                          method(kPublic | kStatic, "registerAlarm",
                                 "(Ljava/lang/String;J)J"),
                      });
  }
  return nullptr;
}

} // namespace

void register_push_classes(BuiltinClassRegistry &registry) {
  registry.add_factory(build_push_class);
}

} // namespace phoneme::vm
