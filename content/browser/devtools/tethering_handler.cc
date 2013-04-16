// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/tethering_handler.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/stl_util.h"
#include "base/values.h"
#include "content/browser/devtools/devtools_http_handler_impl.h"
#include "content/public/browser/devtools_client_host.h"
#include "content/public/browser/devtools_http_handler_delegate.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/socket/stream_listen_socket.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_server_socket.h"

namespace content {

namespace {

const char kTetheringBind[] = "Tethering.bind";
const char kTetheringUnbind[] = "Tethering.unbind";

const char kTetheringAccepted[] = "Tethering.accepted";

const char kPortParam[] = "port";
const char kConnectionIdParam[] = "connectionId";

const char kLocalhost[] = "127.0.0.1";

const int kListenBacklog = 5;
const int kBufferSize = 16 * 1024;

const int kMinTetheringPort = 5000;
const int kMaxTetheringPort = 10000;

class SocketPump : public net::StreamListenSocket::Delegate {
 public:
  SocketPump(DevToolsHttpHandlerDelegate* delegate,
             net::StreamSocket* client_socket)
      : client_socket_(client_socket),
        delegate_(delegate) {
  }

  std::string Init() {
    std::string channel_name;
    server_socket_ = delegate_->CreateSocketForTethering(this, &channel_name);
    buffer_ = new net::IOBuffer(kBufferSize);
    if (!server_socket_ || channel_name.empty())
      SelfDestruct();
    return channel_name;
  }

  virtual ~SocketPump() { }

 private:
  virtual void DidAccept(net::StreamListenSocket* server,
                         net::StreamListenSocket* socket) OVERRIDE {
    if (accepted_socket_)
      return;

    accepted_socket_ = socket;
    int result = client_socket_->Read(buffer_, kBufferSize,
                                      base::Bind(&SocketPump::OnClientRead,
                                                 base::Unretained(this)));
    if (result != net::ERR_IO_PENDING)
      OnClientRead(result);
  }

  virtual void DidRead(net::StreamListenSocket* socket,
                       const char* data,
                       int len) OVERRIDE {
    std::string payload = std::string(data, len);
    int result = client_socket_->Write(new net::StringIOBuffer(payload), len,
                                       base::Bind(&SocketPump::OnClientWrite,
                                                  base::Unretained(this)));
    if (result < 0 && result != net::ERR_IO_PENDING)
      SelfDestruct();
  }

  virtual void DidClose(net::StreamListenSocket* socket) OVERRIDE {
    SelfDestruct();
  }

  void OnClientRead(int result) {
    if (result <= 0) {
      SelfDestruct();
      return;
    }

    accepted_socket_->Send(buffer_->data(), result);

    result = client_socket_->Read(buffer_, kBufferSize,
                                 base::Bind(&SocketPump::OnClientRead,
                                            base::Unretained(this)));
    if (result != net::ERR_IO_PENDING)
      OnClientRead(result);
  }

  void OnClientWrite(int result) {
    if (result < 0)
      SelfDestruct();
  }

  void SelfDestruct() {
    delete this;
  }

 private:
  scoped_ptr<net::StreamSocket> client_socket_;
  scoped_refptr<net::StreamListenSocket> server_socket_;
  scoped_refptr<net::StreamListenSocket> accepted_socket_;
  scoped_refptr<net::IOBuffer> buffer_;
  DevToolsHttpHandlerDelegate* delegate_;
};

}  // namespace

const char TetheringHandler::kDomain[] = "Tethering";

class TetheringHandler::BoundSocket {
 public:
  BoundSocket(TetheringHandler* handler,
              DevToolsHttpHandlerDelegate* delegate)
      : handler_(handler),
        delegate_(delegate),
        socket_(new net::TCPServerSocket(NULL, net::NetLog::Source())),
        port_(0) {
  }

  virtual ~BoundSocket() {
  }

  bool Listen(int port) {
    port_ = port;
    net::IPAddressNumber ip_number;
    if (!net::ParseIPLiteralToNumber(kLocalhost, &ip_number))
      return false;

    net::IPEndPoint end_point(ip_number, port);
    socket_->AllowAddressReuse();
    int result = socket_->Listen(end_point, kListenBacklog);
    if (result < 0)
      return false;

    net::IPEndPoint local_address;
    result = socket_->GetLocalAddress(&local_address);
    if (result < 0)
      return false;

    DoAccept();
    return true;
  }

 private:
  typedef std::map<net::IPEndPoint, net::StreamSocket*> AcceptedSocketsMap;

  void DoAccept() {
    while (true) {
      int result = socket_->Accept(
          &accept_socket_,
          base::Bind(&BoundSocket::OnAccepted, base::Unretained(this)));
      if (result == net::ERR_IO_PENDING)
        break;
      else
        HandleAcceptResult(result);
    }
  }

  void OnAccepted(int result) {
    HandleAcceptResult(result);
    if (result == net::OK)
      DoAccept();
  }

  void HandleAcceptResult(int result) {
    if (result != net::OK)
      return;

    SocketPump* pump = new SocketPump(delegate_, accept_socket_.release());
    std::string name = pump->Init();
    if (!name.empty())
      handler_->Accepted(port_, name);
  }

  TetheringHandler* handler_;
  DevToolsHttpHandlerDelegate* delegate_;
  scoped_ptr<net::ServerSocket> socket_;
  scoped_ptr<net::StreamSocket> accept_socket_;
  int port_;
};

TetheringHandler::TetheringHandler(DevToolsHttpHandlerDelegate* delegate)
    : delegate_(delegate) {
  RegisterCommandHandler(kTetheringBind,
                         base::Bind(&TetheringHandler::OnBind,
                                    base::Unretained(this)));
  RegisterCommandHandler(kTetheringUnbind,
                         base::Bind(&TetheringHandler::OnUnbind,
                                    base::Unretained(this)));
}

TetheringHandler::~TetheringHandler() {
   STLDeleteContainerPairSecondPointers(bound_sockets_.begin(),
                                        bound_sockets_.end());
}

void TetheringHandler::Accepted(int port, const std::string& name) {
  base::DictionaryValue* params = new base::DictionaryValue();
  params->SetInteger(kPortParam, port);
  params->SetString(kConnectionIdParam, name);
  SendNotification(kTetheringAccepted, params);
}

static int GetPort(DevToolsProtocol::Command* command) {
  base::DictionaryValue* params = command->params();
  int port = 0;
  if (!params || !params->GetInteger(kPortParam, &port) ||
      port < kMinTetheringPort || port > kMaxTetheringPort)
    return 0;
  return port;
}

scoped_ptr<DevToolsProtocol::Response>
TetheringHandler::OnBind(DevToolsProtocol::Command* command) {
  int port = GetPort(command);
  if (port == 0)
    return command->InvalidParamResponse(kPortParam);

  scoped_ptr<BoundSocket> bound_socket(new BoundSocket(this, delegate_));
  if (!bound_socket->Listen(port))
    return command->InternalErrorResponse("Could not bind port");

  bound_sockets_[port] = bound_socket.release();
  return command->SuccessResponse(NULL);
}

scoped_ptr<DevToolsProtocol::Response>
TetheringHandler::OnUnbind(DevToolsProtocol::Command* command) {
  int port = GetPort(command);
  if (port == 0)
    return command->InvalidParamResponse(kPortParam);

  BoundSockets::iterator it = bound_sockets_.find(port);
  if (it == bound_sockets_.end())
    return command->InternalErrorResponse("Port is not bound");

  delete it->second;
  bound_sockets_.erase(it);
  return command->SuccessResponse(NULL);
}

}  // namespace content
