#pragma once

#include "modules/meta/base.hpp"

POLYBAR_NS

namespace modules {
  template <class Impl>
  class unsupported_module : public module<Impl> {
   public:
    using module<Impl>::module;

    void start() override {}

    bool build(builder*, string) const {
      return true;
    }
  };
}  // namespace modules

POLYBAR_NS_END
