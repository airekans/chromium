// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/chrome/adb_impl.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_tokenizer.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/test/chromedriver/chrome/log.h"
#include "chrome/test/chromedriver/chrome/status.h"
#include "chrome/test/chromedriver/net/adb_client_socket.h"

namespace {

const int kAdbPort = 5037;

void ReceiveAdbResponse(std::string* response_out, bool* success,
                        base::WaitableEvent* event, int result,
                        const std::string& response) {
  *response_out = response;
  *success = (result >= 0) ? true : false;
  event->Signal();
}

void ExecuteCommandOnIOThread(
    const std::string& command, std::string* response, bool* success,
    base::WaitableEvent* event) {
  CHECK(base::MessageLoop::current()->IsType(base::MessageLoop::TYPE_IO));
  AdbClientSocket::AdbQuery(kAdbPort, command,
      base::Bind(&ReceiveAdbResponse, response, success, event));
}

}  // namespace

AdbImpl::AdbImpl(
    const scoped_refptr<base::MessageLoopProxy>& io_message_loop_proxy,
    Log* log)
    : io_message_loop_proxy_(io_message_loop_proxy), log_(log) {
  CHECK(io_message_loop_proxy_);
}

AdbImpl::~AdbImpl() {}

Status AdbImpl::GetDevices(std::vector<std::string>* devices) {
  std::string response;
  Status status = ExecuteCommand("host:devices", &response);
  if (!status.IsOk())
    return status;
  base::StringTokenizer lines(response, "\n");
  while (lines.GetNext()) {
    std::vector<std::string> fields;
    base::SplitStringAlongWhitespace(lines.token(), &fields);
    if (fields.size() == 2 && fields[1] == "device") {
      devices->push_back(fields[0]);
    }
  }
  return Status(kOk);
}

Status AdbImpl::ForwardPort(
    const std::string& device_serial, int local_port,
    const std::string& remote_abstract) {
  std::string response;
  Status status = ExecuteHostCommand(
      device_serial,
      "forward:tcp:" + base::IntToString(local_port) + ";localabstract:" +
          remote_abstract,
      &response);
  if (!status.IsOk())
    return status;
  if (response == "OKAY")
    return Status(kOk);
  return Status(kUnknownError, "Failed to forward ports: " + response);
}

Status AdbImpl::SetChromeFlags(const std::string& device_serial) {
  std::string response;
  Status status = ExecuteHostShellCommand(
      device_serial,
      "echo chrome --disable-fre --metrics-recording-only "
          "--enable-remote-debugging > /data/local/chrome-command-line;"
          "echo $?",
      &response);
  if (!status.IsOk())
    return status;
  if (response.find("0") == std::string::npos)
    return Status(kUnknownError, "Failed to set Chrome flags");
  return Status(kOk);
}

Status AdbImpl::ClearAppData(
    const std::string& device_serial, const std::string& package) {
  std::string response;
  std::string command = "pm clear " + package;
  Status status = ExecuteHostShellCommand(device_serial, command, &response);
  if (!status.IsOk())
    return status;
  if (response.find("Success") == std::string::npos)
    return Status(kUnknownError, "Failed to clear app data: " + response);
  return Status(kOk);
}

Status AdbImpl::Launch(
    const std::string& device_serial, const std::string& package,
    const std::string& activity) {
  std::string response;
  Status status = ExecuteHostShellCommand(
      device_serial,
      "am start -a android.intent.action.VIEW -S -W -n " +
          package + "/" + activity + " -d \"data:text/html;charset=utf-8,\"",
      &response);
  if (!status.IsOk())
    return status;
  if (response.find("Complete") == std::string::npos)
    return Status(kUnknownError,
                  "Failed to start " + package + ": " + response);
  return Status(kOk);
}

Status AdbImpl::ForceStop(
    const std::string& device_serial, const std::string& package) {
  std::string response;
  return ExecuteHostShellCommand(
      device_serial, "am force-stop " + package, &response);
}

Status AdbImpl::ExecuteCommand(
    const std::string& command, std::string* response) {
  bool success;
  base::WaitableEvent event(false, false);
  log_->AddEntry(Log::kDebug, "Adb command: " + command);
  io_message_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(&ExecuteCommandOnIOThread, command, response, &success,
                 &event));
  event.Wait();
  log_->AddEntry(Log::kDebug, "Adb response: " + *response);
  if (success)
    return Status(kOk);
  return Status(kUnknownError,
      "Adb command \"" + command + "\" failed, is the Adb server running?");
}

Status AdbImpl::ExecuteHostCommand(
    const std::string& device_serial,
    const std::string& host_command, std::string* response) {
  return ExecuteCommand(
      "host-serial:" + device_serial + ":" + host_command, response);
}

Status AdbImpl::ExecuteHostShellCommand(
    const std::string& device_serial,
    const std::string& shell_command,
    std::string* response) {
  return ExecuteCommand(
      "host:transport:" + device_serial + "|shell:" + shell_command,
      response);
}

