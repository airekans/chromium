// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "net/base/net_errors.h"
#include "remoting/base/rsa_key_pair.h"
#include "remoting/protocol/authenticator_test_base.h"
#include "remoting/protocol/channel_authenticator.h"
#include "remoting/protocol/connection_tester.h"
#include "remoting/protocol/negotiating_authenticator_base.h"
#include "remoting/protocol/negotiating_client_authenticator.h"
#include "remoting/protocol/negotiating_host_authenticator.h"
#include "remoting/protocol/pairing_registry.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/libjingle/source/talk/xmllite/xmlelement.h"

using testing::_;
using testing::DeleteArg;
using testing::SaveArg;

namespace remoting {
namespace protocol {

namespace {

const int kMessageSize = 100;
const int kMessages = 1;

const char kNoClientId[] = "";
const char kNoPairedSecret[] = "";
const char kTestClientId[] = "client-id";
const char kTestHostId[] = "12345678910123456";

const char kTestPairedSecret[] = "1111-2222-3333";
const char kTestPairedSecretBad[] = "4444-5555-6666";
const char kTestPin[] = "123456";
const char kTestPinBad[] = "654321";

}  // namespace

class NegotiatingAuthenticatorTest : public AuthenticatorTestBase {
 public:
  NegotiatingAuthenticatorTest() {
  }
  virtual ~NegotiatingAuthenticatorTest() {
  }

 protected:
  void InitAuthenticators(
      const std::string& client_id,
      const std::string& client_paired_secret,
      const std::string& client_interactive_pin,
      const std::string& host_secret,
      AuthenticationMethod::HashFunction hash_function,
      bool client_hmac_only,
      scoped_refptr<PairingRegistry> pairing_registry) {
    std::string host_secret_hash = AuthenticationMethod::ApplyHashFunction(
        hash_function, kTestHostId, host_secret);
    host_ = NegotiatingHostAuthenticator::CreateWithSharedSecret(
        host_cert_, key_pair_, host_secret_hash, hash_function,
        pairing_registry);

    std::vector<AuthenticationMethod> methods;
    methods.push_back(AuthenticationMethod::Spake2Pair());
    methods.push_back(AuthenticationMethod::Spake2(
        AuthenticationMethod::HMAC_SHA256));
    if (!client_hmac_only) {
      methods.push_back(AuthenticationMethod::Spake2(
          AuthenticationMethod::NONE));
    }
    bool pairing_expected = pairing_registry.get() != NULL;
    FetchSecretCallback fetch_secret_callback =
        base::Bind(&NegotiatingAuthenticatorTest::FetchSecret,
                   client_interactive_pin,
                   pairing_expected);
    client_as_negotiating_authenticator_ = new NegotiatingClientAuthenticator(
        client_id, client_paired_secret,
        kTestHostId, fetch_secret_callback,
        scoped_ptr<ThirdPartyClientAuthenticator::TokenFetcher>(), methods);
    client_.reset(client_as_negotiating_authenticator_);
  }

  scoped_refptr<PairingRegistry> CreatePairingRegistry(
      PairingRegistry::Pairing* pairings, size_t num_pairings) {
    PairingRegistry::PairedClients clients;
    for (size_t i = 0; i < num_pairings; ++i) {
      clients[pairings[i].client_id] = pairings[i];
    }
    scoped_refptr<PairingRegistry> result(
        new PairingRegistry(
            scoped_ptr<PairingRegistry::Delegate>(
                new NotImplementedPairingRegistryDelegate),
            clients));
    return result;
  }

  static void FetchSecret(
      const std::string& client_secret,
      bool pairing_supported,
      bool pairing_expected,
      const protocol::SecretFetchedCallback& secret_fetched_callback) {
    secret_fetched_callback.Run(client_secret);
    ASSERT_EQ(pairing_supported, pairing_expected);
  }

  void VerifyRejected(Authenticator::RejectionReason reason) {
    ASSERT_TRUE(client_->state() == Authenticator::REJECTED ||
                host_->state() == Authenticator::REJECTED);
    if (client_->state() == Authenticator::REJECTED) {
      ASSERT_EQ(client_->rejection_reason(), reason);
    }
    if (host_->state() == Authenticator::REJECTED) {
      ASSERT_EQ(host_->rejection_reason(), reason);
    }
  }

  void VerifyAccepted(const AuthenticationMethod& expected_method) {
    ASSERT_NO_FATAL_FAILURE(RunAuthExchange());

    ASSERT_EQ(Authenticator::ACCEPTED, host_->state());
    ASSERT_EQ(Authenticator::ACCEPTED, client_->state());

    client_auth_ = client_->CreateChannelAuthenticator();
    host_auth_ = host_->CreateChannelAuthenticator();
    RunChannelAuth(false);

    EXPECT_TRUE(client_socket_.get() != NULL);
    EXPECT_TRUE(host_socket_.get() != NULL);

    StreamConnectionTester tester(host_socket_.get(), client_socket_.get(),
                                  kMessageSize, kMessages);

    tester.Start();
    message_loop_.Run();
    tester.CheckResults();
    EXPECT_EQ(
        expected_method,
        client_as_negotiating_authenticator_->current_method_for_testing());
  }

  // Use a bare pointer because the storage is managed by the base class.
  NegotiatingClientAuthenticator* client_as_negotiating_authenticator_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NegotiatingAuthenticatorTest);
};

TEST_F(NegotiatingAuthenticatorTest, SuccessfulAuthHmac) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kNoClientId, kNoPairedSecret, kTestPin, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false, NULL));
  VerifyAccepted(
      AuthenticationMethod::Spake2(AuthenticationMethod::HMAC_SHA256));
}

TEST_F(NegotiatingAuthenticatorTest, SuccessfulAuthPlain) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kNoClientId, kNoPairedSecret, kTestPin, kTestPin,
      AuthenticationMethod::NONE, false, NULL));
  VerifyAccepted(AuthenticationMethod::Spake2(AuthenticationMethod::NONE));
}

TEST_F(NegotiatingAuthenticatorTest, InvalidSecretHmac) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kNoClientId, kNoPairedSecret, kTestPinBad, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false, NULL));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());

  VerifyRejected(Authenticator::INVALID_CREDENTIALS);
}

TEST_F(NegotiatingAuthenticatorTest, InvalidSecretPlain) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kNoClientId, kNoPairedSecret, kTestPin, kTestPinBad,
      AuthenticationMethod::NONE, false, NULL));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());

  VerifyRejected(Authenticator::INVALID_CREDENTIALS);
}

TEST_F(NegotiatingAuthenticatorTest, IncompatibleMethods) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kNoClientId, kNoPairedSecret, kTestPin, kTestPinBad,
      AuthenticationMethod::NONE, true, NULL));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());

  VerifyRejected(Authenticator::PROTOCOL_ERROR);
}

TEST_F(NegotiatingAuthenticatorTest, PairingNotSupported) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kTestClientId, kTestPairedSecret, kTestPin, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false, NULL));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());
  VerifyAccepted(
      AuthenticationMethod::Spake2(AuthenticationMethod::HMAC_SHA256));
}

TEST_F(NegotiatingAuthenticatorTest, PairingSupportedButNotPaired) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kNoClientId, kNoPairedSecret, kTestPin, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false,
      CreatePairingRegistry(NULL, 0)));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());
  VerifyAccepted(AuthenticationMethod::Spake2Pair());
}

TEST_F(NegotiatingAuthenticatorTest, PairingRevokedPinOkay) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kTestClientId, kTestPairedSecret, kTestPin, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false,
      CreatePairingRegistry(NULL, 0)));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());
  VerifyAccepted(AuthenticationMethod::Spake2Pair());
}

TEST_F(NegotiatingAuthenticatorTest, PairingRevokedPinBad) {
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kTestClientId, kTestPairedSecret, kTestPinBad, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false,
      CreatePairingRegistry(NULL, 0)));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());
  VerifyRejected(Authenticator::INVALID_CREDENTIALS);
}

TEST_F(NegotiatingAuthenticatorTest, PairingSucceeded) {
  PairingRegistry::Pairing pairing;
  pairing.client_id = kTestClientId;
  pairing.shared_secret = kTestPairedSecret;
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kTestClientId, kTestPairedSecret, kTestPinBad, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false,
      CreatePairingRegistry(&pairing, 1)));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());
  VerifyAccepted(AuthenticationMethod::Spake2Pair());
}

TEST_F(NegotiatingAuthenticatorTest, PairingSucceededInvalidSecretButPinOkay) {
  PairingRegistry::Pairing pairing;
  pairing.client_id = kTestClientId;
  pairing.shared_secret = kTestPairedSecret;
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kTestClientId, kTestPairedSecretBad, kTestPin, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false,
      CreatePairingRegistry(&pairing, 1)));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());
  VerifyAccepted(AuthenticationMethod::Spake2Pair());
}

TEST_F(NegotiatingAuthenticatorTest, PairingFailedInvalidSecretAndPin) {
  PairingRegistry::Pairing pairing;
  pairing.client_id = kTestClientId;
  pairing.shared_secret = kTestPairedSecret;
  ASSERT_NO_FATAL_FAILURE(InitAuthenticators(
      kTestClientId, kTestPairedSecretBad, kTestPinBad, kTestPin,
      AuthenticationMethod::HMAC_SHA256, false,
      CreatePairingRegistry(&pairing, 1)));
  ASSERT_NO_FATAL_FAILURE(RunAuthExchange());
  VerifyRejected(Authenticator::INVALID_CREDENTIALS);
}

}  // namespace protocol
}  // namespace remoting
