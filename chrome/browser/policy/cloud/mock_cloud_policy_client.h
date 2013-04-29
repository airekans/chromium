// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CLOUD_MOCK_CLOUD_POLICY_CLIENT_H_
#define CHROME_BROWSER_POLICY_CLOUD_MOCK_CLOUD_POLICY_CLIENT_H_

#include <string>

#include "base/basictypes.h"
#include "chrome/browser/policy/cloud/cloud_policy_client.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace policy {

class MockCloudPolicyClient : public CloudPolicyClient {
 public:
  MockCloudPolicyClient();
  virtual ~MockCloudPolicyClient();

  MOCK_METHOD2(SetupRegistration, void(const std::string&, const std::string&));
  MOCK_METHOD4(Register, void(
      enterprise_management::DeviceRegisterRequest::Type type,
      const std::string&,
      const std::string&,
      bool));
  MOCK_METHOD0(FetchPolicy, void(void));
  MOCK_METHOD0(Unregister, void(void));

  // Sets the DMToken.
  void SetDMToken(const std::string& token);

  // Injects policy.
  void SetPolicy(const PolicyNamespaceKey& policy_ns_key,
                 const enterprise_management::PolicyFetchResponse& policy);

  // Sets the status field.
  void SetStatus(DeviceManagementStatus status);

  // Make the notification helpers public.
  using CloudPolicyClient::NotifyPolicyFetched;
  using CloudPolicyClient::NotifyRegistrationStateChanged;
  using CloudPolicyClient::NotifyClientError;

  using CloudPolicyClient::dm_token_;
  using CloudPolicyClient::client_id_;
  using CloudPolicyClient::submit_machine_id_;
  using CloudPolicyClient::last_policy_timestamp_;
  using CloudPolicyClient::public_key_version_;
  using CloudPolicyClient::public_key_version_valid_;
  using CloudPolicyClient::namespaces_to_fetch_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCloudPolicyClient);
};

class MockCloudPolicyClientObserver : public CloudPolicyClient::Observer {
 public:
  MockCloudPolicyClientObserver();
  virtual ~MockCloudPolicyClientObserver();

  MOCK_METHOD1(OnPolicyFetched, void(CloudPolicyClient*));
  MOCK_METHOD1(OnRegistrationStateChanged, void(CloudPolicyClient*));
  MOCK_METHOD1(OnRobotAuthCodesFetched, void(CloudPolicyClient*));
  MOCK_METHOD1(OnClientError, void(CloudPolicyClient*));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCloudPolicyClientObserver);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CLOUD_MOCK_CLOUD_POLICY_CLIENT_H_
