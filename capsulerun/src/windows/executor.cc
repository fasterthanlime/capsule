
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

#include "executor.h"

#include <NktHookLib.h>

#include <lab/strings.h>
#include <lab/paths.h>
#include <lab/env.h>

#include <string>

#include "wasapi_receiver.h"
#include "../logging.h"

namespace capsule {
namespace windows {

void Process::Wait(ProcessFate *fate) {
  WaitForSingleObject(process_handle_, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(process_handle_, &exit_code);
  Log("Process::Wait, DWORD exit_code = %d", exit_code);

  fate->status = kProcessStatusExited;
  fate->code = static_cast<int>(exit_code);
}

Process::~Process() {
  // muffin
}

Executor::Executor() {
  // muffin
}

ProcessInterface * Executor::LaunchProcess(MainArgs *args) {
  // If the injected dll string passed to Deviare-InProc ends with 'x86.dll', 'x64.dll', '32.dll',
  // '64.dll', the dll name will be adjusted in order to match the process platform.
  // i.e.: "mydll_x86.dll" will become "mydll_x64.dll" on 64-bit processes.
  std::string libcapsule_path = lab::paths::Join(std::string(args->libpath), "capsule32.dll");

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));  
  si.cb = sizeof(si);

  ZeroMemory(&pi, sizeof(pi));  

  auto executable_path_w = lab::strings::ToWide(std::string(args->exec));
  auto libcapsule_path_w = lab::strings::ToWide(libcapsule_path);

  bool env_success = true;
  env_success &= lab::env::Set("CAPSULE_PIPE_PATH", std::string(args->pipe));
  env_success &= lab::env::Set("CAPSULE_LIBRARY_PATH", libcapsule_path);
  if (!env_success) {
    Log("Could not set environment variables for the child");
    return nullptr;
  }

  bool first_arg = true;

  std::wstring command_line_w;
  for (int i = 0; i < args->exec_argc; i++) {
    auto arg_w = lab::strings::ToWide(args->exec_argv[i]);

    if (first_arg) {
      first_arg = false;
    } else {
      command_line_w.append(L" ");
    }
    lab::strings::ArgvQuote(arg_w, command_line_w, false);
  }

  Log("Launching '%S' with args '%S'", executable_path_w.c_str(), command_line_w.c_str());
  Log("Injecting '%S'", libcapsule_path_w.c_str());
  const char* libcapsule_init_function_name = "CapsuleWindowsInit";

  DWORD err = NktHookLibHelpers::CreateProcessWithDllW(
    (LPCWSTR) executable_path_w.c_str(), // applicationName
    (LPWSTR) command_line_w.c_str(), // commandLine
    NULL, // processAttributes
    NULL, // threadAttributes
    FALSE, // inheritHandles
    0, // creationFlags
    NULL, // environment
    NULL, // currentDirectory
    &si, // startupInfo
    &pi, // processInfo
    (LPCWSTR) libcapsule_path_w.c_str(), // dllName
    NULL, // signalCompletedEvent
    libcapsule_init_function_name // initFunctionName
  );

  if (err == ERROR_SUCCESS) {
    Log("Process #%lu successfully launched with dll injected!", pi.dwProcessId);
  } else {
    Log("Error %lu: Cannot launch process and inject dll.", err);
    return nullptr;
  }

  return new Process(pi.hProcess);
}

static audio::AudioReceiver *WasapiReceiverFactory() {
  return new audio::WasapiReceiver();
}

AudioReceiverFactory Executor::GetAudioReceiverFactory() {
  return WasapiReceiverFactory;
}

Executor::~Executor() {
  // muffin
}

}
}