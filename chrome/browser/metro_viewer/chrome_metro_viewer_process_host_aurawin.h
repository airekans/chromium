// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_METRO_VIEWER_CHROME_METRO_VIEWER_PROCESS_HOST_AURAWIN_H_
#define CHROME_BROWSER_METRO_VIEWER_CHROME_METRO_VIEWER_PROCESS_HOST_AURAWIN_H_

#include <string>

#include "win8/viewer/metro_viewer_process_host.h"

class ChromeMetroViewerProcessHost : public win8::MetroViewerProcessHost {
 public:
  explicit ChromeMetroViewerProcessHost(const std::string& ipc_channel_name);

 private:
  // win8::MetroViewerProcessHost implementation
  virtual void OnChannelError() OVERRIDE;
  virtual void OnSetTargetSurface(gfx::NativeViewId target_surface) OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(ChromeMetroViewerProcessHost);
};

#endif  // CHROME_BROWSER_METRO_VIEWER_CHROME_METRO_VIEWER_PROCESS_HOST_AURAWIN_H_
