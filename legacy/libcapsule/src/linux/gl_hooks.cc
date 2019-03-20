
/*
 *  capsule - the game recording and overlay toolkit
 *  Copyright (C) 2017, Amos Wenger
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details:
 * https://github.com/itchio/capsule/blob/master/LICENSE
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <lab/strings.h>

#include "../gl_capture.h"
#include "../ensure.h"
#include "../logging.h"
#include "dlopen_hooks.h"

namespace capsule {
namespace gl {

// using the leading underscore convention because of GLSYM
typedef int (*glXQueryExtension_t)(void*, void*, void*);
static glXQueryExtension_t _glXQueryExtension = nullptr;

typedef void (*glXSwapBuffers_t)(void*, void*);
static glXSwapBuffers_t _glXSwapBuffers = nullptr;

typedef void* (*glXGetProcAddressARB_t)(const char*);
static glXGetProcAddressARB_t _glXGetProcAddressARB = nullptr;

bool LoadOpengl (const char *path) {
  handle = dl::NakedOpen(path, (RTLD_NOW|RTLD_LOCAL));
  if (!handle) {
    return false;
  }

  GLSYM(glXQueryExtension)
  GLSYM(glXSwapBuffers)
  GLSYM(glXGetProcAddressARB)

  return true;
}

void *GetProcAddress (const char *symbol) {
  void *addr = nullptr;

  if (_glXGetProcAddressARB) {
    addr = _glXGetProcAddressARB(symbol);
  }
  if (!addr) {
    addr = dlsym(handle, symbol);
  }

  return addr;
}

} // namespace gl
} // namespace capsule

extern "C" {

// interposed libGL function
void glXSwapBuffers (void *a, void *b) {
  capsule::gl::Capture(0, 0);
  return capsule::gl::_glXSwapBuffers(a, b);
}

// interposed libGL function
int glXQueryExtension (void *a, void *b, void *c) {
  return capsule::gl::_glXQueryExtension(a, b, c);
}

// interposed libGL function
void* glXGetProcAddressARB (const char *name) {
  if (lab::strings::CEquals(name, "glXSwapBuffers")) {
    capsule::Log("Hooking glXSwapBuffers");
    return (void*) &glXSwapBuffers;
  }

  if (!capsule::gl::EnsureOpengl()) {
    capsule::Log("Could not load opengl library, cannot get proc address for child");
    exit(124);
  }
  return capsule::gl::_glXGetProcAddressARB(name);
}

} // extern "C"
