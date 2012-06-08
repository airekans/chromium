// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_BREAKPAD_H_
#define REMOTING_HOST_BREAKPAD_H_

namespace remoting {

// Returns true if the user has agreed to crash dump collection and uploading.
bool IsCrashReportingEnabled();

}  // remoting

#endif  // REMOTING_HOST_BREAKPAD_H_
