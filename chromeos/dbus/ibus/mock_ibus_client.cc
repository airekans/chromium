// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/ibus/mock_ibus_client.h"

namespace chromeos {

MockIBusClient::MockIBusClient() {}

MockIBusClient::~MockIBusClient() {}

void MockIBusClient::CreateInputContext(
    const std::string& client_name,
    const CreateInputContextCallback& callback,
    const ErrorCallback& error_callback) {
}

}  // namespace chromeos
