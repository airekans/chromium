// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PROXY_PLUGIN_RESOURCE_H_
#define PPAPI_PROXY_PLUGIN_RESOURCE_H_

#include <map>

#include "base/compiler_specific.h"
#include "ipc/ipc_sender.h"
#include "ppapi/proxy/connection.h"
#include "ppapi/proxy/plugin_resource_callback.h"
#include "ppapi/proxy/ppapi_proxy_export.h"
#include "ppapi/shared_impl/resource.h"

namespace IPC {
class Message;
}

namespace ppapi {
namespace proxy {

class PluginDispatcher;

class PPAPI_PROXY_EXPORT PluginResource : public Resource {
 public:
  PluginResource(Connection connection, PP_Instance instance);
  virtual ~PluginResource();

  // Returns true if we've previously sent a create message to the browser
  // or renderer. Generally resources will use these to tell if they should
  // lazily send create messages.
  bool sent_create_to_browser() const { return sent_create_to_browser_; }
  bool sent_create_to_renderer() const { return sent_create_to_renderer_; }

  // This handles a reply to a resource call. It works by looking up the
  // callback that was registered when CallBrowser/CallRenderer was called
  // and calling it with |params| and |msg|.
  virtual void OnReplyReceived(const proxy::ResourceMessageReplyParams& params,
                               const IPC::Message& msg) OVERRIDE;
 protected:
  // Sends a create message to the browser or renderer for the current resource.
  void SendCreateToBrowser(const IPC::Message& msg);
  void SendCreateToRenderer(const IPC::Message& msg);

  // Sends the given IPC message as a resource request to the host
  // corresponding to this resource object and does not expect a reply.
  void PostToBrowser(const IPC::Message& msg);
  void PostToRenderer(const IPC::Message& msg);

  // Like PostToBrowser/Renderer but expects a response. |callback| is
  // a |base::Callback| that will be run when a reply message with a sequence
  // number matching that of the call is received. |ReplyMsgClass| is the type
  // of the reply message that is expected. An example of usage:
  //
  // CallBrowser<PpapiPluginMsg_MyResourceType_MyReplyMessage>(
  //     PpapiHostMsg_MyResourceType_MyRequestMessage(),
  //     base::Bind(&MyPluginResource::ReplyHandler, this));
  //
  // If a reply message to this call is received whose type does not match
  // |ReplyMsgClass| (for example, in the case of an error), the callback will
  // still be invoked but with the default values of the message parameters.
  //
  // Returns the new request's sequence number which can be used to identify
  // the callback.
  //
  // Note that all integers (including 0 and -1) are valid request IDs.
  template<typename ReplyMsgClass, typename CallbackType>
  int32_t CallBrowser(const IPC::Message& msg, const CallbackType& callback);
  template<typename ReplyMsgClass, typename CallbackType>
  int32_t CallRenderer(const IPC::Message& msg, const CallbackType& callback);

  // Call the browser/renderer with sync messages. The pepper error code from
  // the call is returned and the reply message is stored in |reply_msg|.
  int32_t CallBrowserSync(const IPC::Message& msg, IPC::Message* reply_msg);
  int32_t CallRendererSync(const IPC::Message& msg, IPC::Message* reply_msg);

 private:
  // Helper function to send a |PpapiHostMsg_ResourceCall| to the given sender
  // with |nested_msg| and |call_params|.
  bool SendResourceCall(IPC::Sender* sender,
                        const ResourceMessageCallParams& call_params,
                        const IPC::Message& nested_msg);

  // Helper function to make a Resource Call to a host with a callback.
  template<typename ReplyMsgClass, typename CallbackType>
  int32_t CallHost(IPC::Sender* sender,
                   const IPC::Message& msg,
                   const CallbackType& callback);

  Connection connection_;

  int32_t next_sequence_number_;

  bool sent_create_to_browser_;
  bool sent_create_to_renderer_;

  typedef std::map<int32_t, scoped_refptr<PluginResourceCallbackBase> >
      CallbackMap;
  CallbackMap callbacks_;

  DISALLOW_COPY_AND_ASSIGN(PluginResource);
};

template<typename ReplyMsgClass, typename CallbackType>
int32_t PluginResource::CallBrowser(const IPC::Message& msg,
                                    const CallbackType& callback) {
  return CallHost<ReplyMsgClass, CallbackType>(
      connection_.browser_sender, msg, callback);
}

template<typename ReplyMsgClass, typename CallbackType>
int32_t PluginResource::CallRenderer(const IPC::Message& msg,
                                    const CallbackType& callback) {
  return CallHost<ReplyMsgClass, CallbackType>(
      connection_.renderer_sender, msg, callback);
}

template<typename ReplyMsgClass, typename CallbackType>
int32_t PluginResource::CallHost(IPC::Sender* sender,
                                 const IPC::Message& msg,
                                 const CallbackType& callback) {
  ResourceMessageCallParams params(pp_resource(),
                                   next_sequence_number_++);
  // Stash the |callback| in |callbacks_| identified by the sequence number of
  // the call.
  scoped_refptr<PluginResourceCallbackBase> plugin_callback(
      new PluginResourceCallback<ReplyMsgClass, CallbackType>(callback));
  callbacks_.insert(std::make_pair(params.sequence(), plugin_callback));
  params.set_has_callback();
  SendResourceCall(sender, params, msg);
  return params.sequence();
}

}  // namespace proxy
}  // namespace ppapi

#endif  // PPAPI_PROXY_PLUGIN_RESOURCE_H_
