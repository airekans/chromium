// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_CERT_VERIFY_RESULT_H_
#define NET_BASE_CERT_VERIFY_RESULT_H_

#include <vector>

#include "net/base/cert_status_flags.h"
#include "net/base/net_export.h"
#include "base/memory/ref_counted.h"
#include "net/base/x509_cert_types.h"

namespace net {

class X509Certificate;

// The result of certificate verification.
class NET_EXPORT CertVerifyResult {
 public:
  CertVerifyResult();
  ~CertVerifyResult();

  void Reset();

  // The certificate and chain that was constructed during verification.
  // Note that the though the verified certificate will match the originally
  // supplied certificate, the intermediate certificates stored within may
  // be substantially different. In the event of a verification failure, this
  // will contain the chain as supplied by the server. This may be NULL if
  // running within the sandbox.
  scoped_refptr<X509Certificate> verified_cert;

  // Bitmask of CERT_STATUS_* from net/base/cert_status_flags.h. Note that
  // these status flags apply to the certificate chain returned in
  // |verified_cert|, rather than the originally supplied certificate
  // chain.
  CertStatus cert_status;

  // Properties of the certificate chain.
  bool has_md5;
  bool has_md2;
  bool has_md4;
  bool has_md5_ca;
  bool has_md2_ca;

  // If the certificate was successfully verified then this contains the SHA1
  // fingerprints of the SubjectPublicKeyInfos of the chain. The fingerprint
  // from the leaf certificate will be the first element of the vector.
  std::vector<SHA1Fingerprint> public_key_hashes;

  // is_issued_by_known_root is true if we recognise the root CA as a standard
  // root.  If it isn't then it's probably the case that this certificate was
  // generated by a MITM proxy whose root has been installed locally. This is
  // meaningless if the certificate was not trusted.
  bool is_issued_by_known_root;
};

}  // namespace net

#endif  // NET_BASE_CERT_VERIFY_RESULT_H_
