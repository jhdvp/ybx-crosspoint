#pragma once

#include <I18n.h>

#include <cstddef>
#include <memory>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace nativeapps {

// Build-time C++ modules; NOT executable SD/PluginHub packages.
// Factories are called only after the user opens a module. Each app should
// own/release its buffers with its Activity and use the platform HAL.
struct Descriptor {
  const char* id;                         // stable /apps/id + /.crosspoint/apps/id namespace
  StrId name;
  StrId summary;
  std::unique_ptr<Activity> (*create)(GfxRenderer&, MappedInputManager&);
  bool (*supported)();                    // nullptr means supported by this firmware
  size_t minLargestFreeBlockBytes;        // admission gate, not a reserved allocation
};

bool isCatalogValid();
size_t availableCount();
const Descriptor* availableAt(size_t index);  // nullptr if out of range

}  // namespace nativeapps
