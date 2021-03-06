// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_APP_SHIM_HOST_MAC_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_APP_SHIM_HOST_MAC_H_

#include <string>

#include "apps/app_shim/app_shim_handler_mac.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_sender.h"

class Profile;

namespace IPC {
struct ChannelHandle;
class ChannelProxy;
class Message;
}  // namespace IPC

// This is the counterpart to AppShimController in
// chrome/app/chrome_main_app_mode_mac.mm. The AppShimHost owns itself, and is
// destroyed when the app it corresponds to is closed or when the channel
// connected to the app shim is closed.
class AppShimHost : public IPC::Listener,
                    public IPC::Sender,
                    public apps::AppShimHandler::Host,
                    public base::NonThreadSafe {
 public:
  AppShimHost();
  virtual ~AppShimHost();

  // Creates a new server-side IPC channel at |handle|, which should contain a
  // file descriptor of a channel created by an IPC::ChannelFactory, and begins
  // listening for messages on it.
  void ServeChannel(const IPC::ChannelHandle& handle);

 protected:

  // Used internally; virtual so they can be mocked for testing.
  virtual Profile* FetchProfileForDirectory(const base::FilePath& profile_dir);

  // IPC::Listener implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;
  virtual void OnChannelError() OVERRIDE;

  // IPC::Sender implementation.
  virtual bool Send(IPC::Message* message) OVERRIDE;

 private:
  // The app shim process is requesting to be associated with the given profile
  // and app_id. Once the profile and app_id are stored, and all future
  // messages from the app shim relate to this app. The app is launched
  // immediately if |launch_now| is true.
  void OnLaunchApp(base::FilePath profile_dir,
                   std::string app_id,
                   apps::AppShimLaunchType launch_type);

  // Called when the app shim process notifies that the app should be brought
  // to the front (i.e. the user has clicked on the app's icon in the dock or
  // Cmd+Tabbed to it.)
  void OnFocus();

  // Called when the app shim process notifies that the app should quit.
  void OnQuit();

  // apps::AppShimHandler::Host overrides:
  virtual void OnAppClosed() OVERRIDE;
  virtual Profile* GetProfile() const OVERRIDE;
  virtual std::string GetAppId() const OVERRIDE;

  // Closes the channel and destroys the AppShimHost.
  void Close();

  scoped_ptr<IPC::ChannelProxy> channel_;
  std::string app_id_;
  Profile* profile_;
};

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_APP_SHIM_HOST_MAC_H_
