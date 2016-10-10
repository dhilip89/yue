// Copyright 2016 Cheng Zhao. All rights reserved.
// Use of this source code is governed by the license that can be found in the
// LICENSE file.

#include "nativeui/state.h"

#include "base/lazy_instance.h"
#include "base/threading/thread_local.h"
#include "nativeui/gfx/font.h"

#if defined(OS_WIN)
#include "nativeui/win/subwin_holder.h"
#include "nativeui/win/util/class_registrar.h"
#include "nativeui/win/util/native_theme.h"
#endif

namespace nu {

namespace {

// A lazily created thread local storage for quick access to a thread's message
// loop, if one exists.  This should be safe and free of static constructors.
base::LazyInstance<base::ThreadLocalPointer<State>>::Leaky lazy_tls_ptr =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

State::State() {
  DCHECK_EQ(current(), nullptr) << "should only have one state per thread";
  lazy_tls_ptr.Pointer()->Set(this);
  PlatformInit();
}

State::~State() {
  // The GUI members must be destroyed before we shutdone GUI engine.
  default_font_ = nullptr;

  PlatformDestroy();
  DCHECK_EQ(current(), this);
  lazy_tls_ptr.Pointer()->Set(nullptr);
}

// static
State* State::current() {
  return lazy_tls_ptr.Pointer()->Get();
}

Font* State::GetDefaultFont() {
  if (!default_font_)
    default_font_ = Font::CreateDefault();
  return default_font_.get();
}

}  // namespace nu
