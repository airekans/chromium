// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/rtc_peer_connection_handler.h"

#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/stl_util.h"
#include "base/utf_string_conversions.h"
#include "content/public/common/content_switches.h"
#include "content/renderer/media/media_stream_dependency_factory.h"
#include "content/renderer/media/peer_connection_tracker.h"
#include "content/renderer/media/remote_media_stream_impl.h"
#include "content/renderer/media/rtc_data_channel_handler.h"
#include "content/renderer/media/rtc_dtmf_sender_handler.h"
#include "content/renderer/media/rtc_media_constraints.h"
#include "content/renderer/render_thread_impl.h"
#include "third_party/WebKit/public/platform/WebMediaConstraints.h"
// TODO(hta): Move the following include to WebRTCStatsRequest.h file.
#include "third_party/WebKit/public/platform/WebMediaStreamSource.h"
#include "third_party/WebKit/public/platform/WebMediaStreamTrack.h"
#include "third_party/WebKit/public/platform/WebRTCConfiguration.h"
#include "third_party/WebKit/public/platform/WebRTCICECandidate.h"
#include "third_party/WebKit/public/platform/WebRTCPeerConnectionHandlerClient.h"
#include "third_party/WebKit/public/platform/WebRTCSessionDescription.h"
#include "third_party/WebKit/public/platform/WebRTCSessionDescriptionRequest.h"
#include "third_party/WebKit/public/platform/WebRTCStatsRequest.h"
#include "third_party/WebKit/public/platform/WebRTCVoidRequest.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"

namespace content {

// Converter functions from libjingle types to WebKit types.
WebKit::WebRTCPeerConnectionHandlerClient::ICEGatheringState
GetWebKitIceGatheringState(
    webrtc::PeerConnectionInterface::IceGatheringState state) {
  using WebKit::WebRTCPeerConnectionHandlerClient;
  switch (state) {
    case webrtc::PeerConnectionInterface::kIceGatheringNew:
      return WebRTCPeerConnectionHandlerClient::ICEGatheringStateNew;
    case webrtc::PeerConnectionInterface::kIceGatheringGathering:
      return WebRTCPeerConnectionHandlerClient::ICEGatheringStateGathering;
    case webrtc::PeerConnectionInterface::kIceGatheringComplete:
      return WebRTCPeerConnectionHandlerClient::ICEGatheringStateComplete;
    default:
      NOTREACHED();
      return WebRTCPeerConnectionHandlerClient::ICEGatheringStateNew;
  }
}

static WebKit::WebRTCPeerConnectionHandlerClient::ICEConnectionState
GetWebKitIceConnectionState(
    webrtc::PeerConnectionInterface::IceConnectionState ice_state) {
  using WebKit::WebRTCPeerConnectionHandlerClient;
  switch (ice_state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
      return WebRTCPeerConnectionHandlerClient::ICEConnectionStateStarting;
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
      return WebRTCPeerConnectionHandlerClient::ICEConnectionStateChecking;
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
      return WebRTCPeerConnectionHandlerClient::ICEConnectionStateConnected;
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
      return WebRTCPeerConnectionHandlerClient::ICEConnectionStateCompleted;
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
      return WebRTCPeerConnectionHandlerClient::ICEConnectionStateFailed;
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
      return WebRTCPeerConnectionHandlerClient::ICEConnectionStateDisconnected;
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
      return WebRTCPeerConnectionHandlerClient::ICEConnectionStateClosed;
    default:
      NOTREACHED();
      return WebRTCPeerConnectionHandlerClient::ICEConnectionStateClosed;
  }
}

static WebKit::WebRTCPeerConnectionHandlerClient::SignalingState
GetWebKitSignalingState(webrtc::PeerConnectionInterface::SignalingState state) {
  using WebKit::WebRTCPeerConnectionHandlerClient;
  switch (state) {
    case webrtc::PeerConnectionInterface::kStable:
      return WebRTCPeerConnectionHandlerClient::SignalingStateStable;
    case webrtc::PeerConnectionInterface::kHaveLocalOffer:
      return WebRTCPeerConnectionHandlerClient::SignalingStateHaveLocalOffer;
    case webrtc::PeerConnectionInterface::kHaveLocalPrAnswer:
      return WebRTCPeerConnectionHandlerClient::SignalingStateHaveLocalPrAnswer;
    case webrtc::PeerConnectionInterface::kHaveRemoteOffer:
      return WebRTCPeerConnectionHandlerClient::SignalingStateHaveRemoteOffer;
    case webrtc::PeerConnectionInterface::kHaveRemotePrAnswer:
      return
          WebRTCPeerConnectionHandlerClient::SignalingStateHaveRemotePrAnswer;
    case webrtc::PeerConnectionInterface::kClosed:
      return WebRTCPeerConnectionHandlerClient::SignalingStateClosed;
    default:
      NOTREACHED();
      return WebRTCPeerConnectionHandlerClient::SignalingStateClosed;
  }
}

static WebKit::WebRTCSessionDescription
CreateWebKitSessionDescription(
    const webrtc::SessionDescriptionInterface* native_desc) {
  WebKit::WebRTCSessionDescription description;
  if (!native_desc) {
    LOG(ERROR) << "Native session description is null.";
    return description;
  }

  std::string sdp;
  if (!native_desc->ToString(&sdp)) {
    LOG(ERROR) << "Failed to get SDP string of native session description.";
    return description;
  }

  description.initialize(UTF8ToUTF16(native_desc->type()), UTF8ToUTF16(sdp));
  return description;
}

// Converter functions from WebKit types to libjingle types.

static void GetNativeIceServers(
    const WebKit::WebRTCConfiguration& server_configuration,
    webrtc::PeerConnectionInterface::IceServers* servers) {
  if (server_configuration.isNull() || !servers)
    return;
  for (size_t i = 0; i < server_configuration.numberOfServers(); ++i) {
    webrtc::PeerConnectionInterface::IceServer server;
    const WebKit::WebRTCICEServer& webkit_server =
        server_configuration.server(i);
    server.username = UTF16ToUTF8(webkit_server.username());
    server.password = UTF16ToUTF8(webkit_server.credential());
    server.uri = webkit_server.uri().spec();
    servers->push_back(server);
  }
}

class SessionDescriptionRequestTracker {
 public:
  SessionDescriptionRequestTracker(RTCPeerConnectionHandler* handler,
                                   PeerConnectionTracker::Action action)
      : handler_(handler), action_(action) {}

  void TrackOnSuccess(const webrtc::SessionDescriptionInterface* desc) {
    std::string value;
    if (desc) {
      desc->ToString(&value);
      value = "type: " + desc->type() + ", sdp: " + value;
    }
    if (handler_->peer_connection_tracker())
      handler_->peer_connection_tracker()->TrackSessionDescriptionCallback(
          handler_, action_, "OnSuccess", value);
  }

  void TrackOnFailure(const std::string& error) {
    if (handler_->peer_connection_tracker())
      handler_->peer_connection_tracker()->TrackSessionDescriptionCallback(
          handler_, action_, "OnFailure", error);
  }

 private:
  RTCPeerConnectionHandler* handler_;
  PeerConnectionTracker::Action action_;
};

// Class mapping responses from calls to libjingle CreateOffer/Answer and
// the WebKit::WebRTCSessionDescriptionRequest.
class CreateSessionDescriptionRequest
    : public webrtc::CreateSessionDescriptionObserver {
 public:
  explicit CreateSessionDescriptionRequest(
      const WebKit::WebRTCSessionDescriptionRequest& request,
      RTCPeerConnectionHandler* handler,
      PeerConnectionTracker::Action action)
      : webkit_request_(request), tracker_(handler, action) {}

  virtual void OnSuccess(webrtc::SessionDescriptionInterface* desc) OVERRIDE {
    tracker_.TrackOnSuccess(desc);
    webkit_request_.requestSucceeded(CreateWebKitSessionDescription(desc));
  }
  virtual void OnFailure(const std::string& error) OVERRIDE {
    tracker_.TrackOnFailure(error);
    webkit_request_.requestFailed(UTF8ToUTF16(error));
  }

 protected:
  virtual ~CreateSessionDescriptionRequest() {}

 private:
  WebKit::WebRTCSessionDescriptionRequest webkit_request_;
  SessionDescriptionRequestTracker tracker_;
};

// Class mapping responses from calls to libjingle
// SetLocalDescription/SetRemoteDescription and a WebKit::WebRTCVoidRequest.
class SetSessionDescriptionRequest
    : public webrtc::SetSessionDescriptionObserver {
 public:
  explicit SetSessionDescriptionRequest(
      const WebKit::WebRTCVoidRequest& request,
      RTCPeerConnectionHandler* handler,
      PeerConnectionTracker::Action action)
      : webkit_request_(request), tracker_(handler, action) {}

  virtual void OnSuccess() OVERRIDE {
    tracker_.TrackOnSuccess(NULL);
    webkit_request_.requestSucceeded();
  }
  virtual void OnFailure(const std::string& error) OVERRIDE {
    tracker_.TrackOnFailure(error);
    webkit_request_.requestFailed(UTF8ToUTF16(error));
  }

 protected:
  virtual ~SetSessionDescriptionRequest() {}

 private:
  WebKit::WebRTCVoidRequest webkit_request_;
  SessionDescriptionRequestTracker tracker_;
};

// Class mapping responses from calls to libjingle
// GetStats into a WebKit::WebRTCStatsCallback.
class StatsResponse : public webrtc::StatsObserver {
 public:
  explicit StatsResponse(const scoped_refptr<LocalRTCStatsRequest>& request)
      : request_(request.get()), response_(request_->createResponse()) {}

  virtual void OnComplete(
      const std::vector<webrtc::StatsReport>& reports) OVERRIDE {
    for (std::vector<webrtc::StatsReport>::const_iterator it = reports.begin();
         it != reports.end(); ++it) {
      if (it->values.size() > 0) {
        AddReport(*it);
      }
    }
    request_->requestSucceeded(response_);
  }

 private:
  void AddReport(const webrtc::StatsReport& report) {
    int idx = response_->addReport(WebKit::WebString::fromUTF8(report.id),
                                   WebKit::WebString::fromUTF8(report.type),
                                   report.timestamp);
    for (webrtc::StatsReport::Values::const_iterator value_it =
         report.values.begin();
         value_it != report.values.end(); ++value_it) {
      AddStatistic(idx, value_it->name, value_it->value);
    }
  }

  void AddStatistic(int idx, const std::string& name,
                    const std::string& value) {
    response_->addStatistic(idx,
                            WebKit::WebString::fromUTF8(name),
                            WebKit::WebString::fromUTF8(value));
  }

  talk_base::scoped_refptr<LocalRTCStatsRequest> request_;
  talk_base::scoped_refptr<LocalRTCStatsResponse> response_;
};

// Implementation of LocalRTCStatsRequest.
LocalRTCStatsRequest::LocalRTCStatsRequest(WebKit::WebRTCStatsRequest impl)
    : impl_(impl),
      response_(NULL) {
}

LocalRTCStatsRequest::LocalRTCStatsRequest() {}
LocalRTCStatsRequest::~LocalRTCStatsRequest() {}

bool LocalRTCStatsRequest::hasSelector() const {
  return impl_.hasSelector();
}

WebKit::WebMediaStream LocalRTCStatsRequest::stream() const {
  return impl_.stream();
}

WebKit::WebMediaStreamTrack LocalRTCStatsRequest::component() const {
  return impl_.component();
}

scoped_refptr<LocalRTCStatsResponse> LocalRTCStatsRequest::createResponse() {
  DCHECK(!response_);
  response_ = new talk_base::RefCountedObject<LocalRTCStatsResponse>(
      impl_.createResponse());
  return response_.get();
}

void LocalRTCStatsRequest::requestSucceeded(
    const LocalRTCStatsResponse* response) {
  impl_.requestSucceeded(response->webKitStatsResponse());
}

// Implementation of LocalRTCStatsResponse.
WebKit::WebRTCStatsResponse LocalRTCStatsResponse::webKitStatsResponse() const {
  return impl_;
}

size_t LocalRTCStatsResponse::addReport(WebKit::WebString type,
                                        WebKit::WebString id,
                                        double timestamp) {
  return impl_.addReport(type, id, timestamp);
}

void LocalRTCStatsResponse::addStatistic(size_t report,
                                         WebKit::WebString name,
                                         WebKit::WebString value) {
  impl_.addStatistic(report, name, value);
}

RTCPeerConnectionHandler::RTCPeerConnectionHandler(
    WebKit::WebRTCPeerConnectionHandlerClient* client,
    MediaStreamDependencyFactory* dependency_factory)
    : PeerConnectionHandlerBase(dependency_factory),
      client_(client),
      frame_(NULL),
      peer_connection_tracker_(NULL) {
}

RTCPeerConnectionHandler::~RTCPeerConnectionHandler() {
  if (peer_connection_tracker_)
    peer_connection_tracker_->UnregisterPeerConnection(this);
  STLDeleteValues(&remote_streams_);
}

void RTCPeerConnectionHandler::associateWithFrame(WebKit::WebFrame* frame) {
  DCHECK(frame);
  frame_ = frame;
}

bool RTCPeerConnectionHandler::initialize(
    const WebKit::WebRTCConfiguration& server_configuration,
    const WebKit::WebMediaConstraints& options) {
  DCHECK(frame_);

  peer_connection_tracker_ =
      RenderThreadImpl::current()->peer_connection_tracker();

  webrtc::PeerConnectionInterface::IceServers servers;
  GetNativeIceServers(server_configuration, &servers);

  RTCMediaConstraints constraints(options);

  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableSCTPDataChannels)) {
    // TODO(jiayl): replace the hard coded string with
    // webrtc::MediaConstraintsInterface::kEnableSctpDataChannels when
    // the Libjingle change is rolled.
    constraints.AddOptional("internalSctpDataChannels", "true");
  }

  native_peer_connection_ =
      dependency_factory_->CreatePeerConnection(
          servers, &constraints, frame_, this);
  if (!native_peer_connection_.get()) {
    LOG(ERROR) << "Failed to initialize native PeerConnection.";
    return false;
  }
  if (peer_connection_tracker_)
    peer_connection_tracker_->RegisterPeerConnection(
        this, servers, constraints, frame_);

  return true;
}

bool RTCPeerConnectionHandler::InitializeForTest(
    const WebKit::WebRTCConfiguration& server_configuration,
    const WebKit::WebMediaConstraints& options,
    PeerConnectionTracker* peer_connection_tracker) {
  webrtc::PeerConnectionInterface::IceServers servers;
  GetNativeIceServers(server_configuration, &servers);

  RTCMediaConstraints constraints(options);
  native_peer_connection_ =
      dependency_factory_->CreatePeerConnection(
          servers, &constraints, NULL, this);
  if (!native_peer_connection_.get()) {
    LOG(ERROR) << "Failed to initialize native PeerConnection.";
    return false;
  }
  peer_connection_tracker_ = peer_connection_tracker;
  return true;
}

void RTCPeerConnectionHandler::createOffer(
    const WebKit::WebRTCSessionDescriptionRequest& request,
    const WebKit::WebMediaConstraints& options) {
  scoped_refptr<CreateSessionDescriptionRequest> description_request(
      new talk_base::RefCountedObject<CreateSessionDescriptionRequest>(
          request, this, PeerConnectionTracker::ACTION_CREATE_OFFER));
  RTCMediaConstraints constraints(options);
  native_peer_connection_->CreateOffer(description_request.get(), &constraints);

  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackCreateOffer(this, constraints);
}

void RTCPeerConnectionHandler::createAnswer(
    const WebKit::WebRTCSessionDescriptionRequest& request,
    const WebKit::WebMediaConstraints& options) {
  scoped_refptr<CreateSessionDescriptionRequest> description_request(
      new talk_base::RefCountedObject<CreateSessionDescriptionRequest>(
          request, this, PeerConnectionTracker::ACTION_CREATE_ANSWER));
  RTCMediaConstraints constraints(options);
  native_peer_connection_->CreateAnswer(description_request.get(),
                                        &constraints);

  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackCreateAnswer(this, constraints);
}

void RTCPeerConnectionHandler::setLocalDescription(
    const WebKit::WebRTCVoidRequest& request,
    const WebKit::WebRTCSessionDescription& description) {
  webrtc::SdpParseError error;
  webrtc::SessionDescriptionInterface* native_desc =
      CreateNativeSessionDescription(description, &error);
  if (!native_desc) {
    std::string reason_str = "Failed to parse SessionDescription. ";
    reason_str.append(error.line);
    reason_str.append(" ");
    reason_str.append(error.description);
    LOG(ERROR) << reason_str;
    request.requestFailed(WebKit::WebString::fromUTF8(reason_str));
    return;
  }
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackSetSessionDescription(
        this, description, PeerConnectionTracker::SOURCE_LOCAL);

  scoped_refptr<SetSessionDescriptionRequest> set_request(
      new talk_base::RefCountedObject<SetSessionDescriptionRequest>(
          request, this, PeerConnectionTracker::ACTION_SET_LOCAL_DESCRIPTION));
  native_peer_connection_->SetLocalDescription(set_request.get(), native_desc);
}

void RTCPeerConnectionHandler::setRemoteDescription(
    const WebKit::WebRTCVoidRequest& request,
    const WebKit::WebRTCSessionDescription& description) {
  webrtc::SdpParseError error;
  webrtc::SessionDescriptionInterface* native_desc =
      CreateNativeSessionDescription(description, &error);
  if (!native_desc) {
    std::string reason_str = "Failed to parse SessionDescription. ";
    reason_str.append(error.line);
    reason_str.append(" ");
    reason_str.append(error.description);
    LOG(ERROR) << reason_str;
    request.requestFailed(WebKit::WebString::fromUTF8(reason_str));
    return;
  }
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackSetSessionDescription(
        this, description, PeerConnectionTracker::SOURCE_REMOTE);

  scoped_refptr<SetSessionDescriptionRequest> set_request(
      new talk_base::RefCountedObject<SetSessionDescriptionRequest>(
          request, this, PeerConnectionTracker::ACTION_SET_REMOTE_DESCRIPTION));
  native_peer_connection_->SetRemoteDescription(set_request.get(), native_desc);
}

WebKit::WebRTCSessionDescription
RTCPeerConnectionHandler::localDescription() {
  const webrtc::SessionDescriptionInterface* native_desc =
      native_peer_connection_->local_description();
  WebKit::WebRTCSessionDescription description =
      CreateWebKitSessionDescription(native_desc);
  return description;
}

WebKit::WebRTCSessionDescription
RTCPeerConnectionHandler::remoteDescription() {
  const webrtc::SessionDescriptionInterface* native_desc =
      native_peer_connection_->remote_description();
  WebKit::WebRTCSessionDescription description =
      CreateWebKitSessionDescription(native_desc);
  return description;
}

bool RTCPeerConnectionHandler::updateICE(
    const WebKit::WebRTCConfiguration& server_configuration,
    const WebKit::WebMediaConstraints& options) {
  webrtc::PeerConnectionInterface::IceServers servers;
  GetNativeIceServers(server_configuration, &servers);
  RTCMediaConstraints constraints(options);

  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackUpdateIce(this, servers, constraints);

  return native_peer_connection_->UpdateIce(servers,
                                            &constraints);
}

bool RTCPeerConnectionHandler::addICECandidate(
    const WebKit::WebRTCICECandidate& candidate) {
  scoped_ptr<webrtc::IceCandidateInterface> native_candidate(
      dependency_factory_->CreateIceCandidate(
          UTF16ToUTF8(candidate.sdpMid()),
          candidate.sdpMLineIndex(),
          UTF16ToUTF8(candidate.candidate())));
  if (!native_candidate) {
    LOG(ERROR) << "Could not create native ICE candidate.";
    return false;
  }

  bool return_value =
      native_peer_connection_->AddIceCandidate(native_candidate.get());
  LOG_IF(ERROR, !return_value) << "Error processing ICE candidate.";

  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackAddIceCandidate(
        this, candidate, PeerConnectionTracker::SOURCE_REMOTE);

  return return_value;
}

bool RTCPeerConnectionHandler::addStream(
    const WebKit::WebMediaStream& stream,
    const WebKit::WebMediaConstraints& options) {
  RTCMediaConstraints constraints(options);

  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackAddStream(
        this, stream, PeerConnectionTracker::SOURCE_LOCAL);
  return AddStream(stream, &constraints);
}

void RTCPeerConnectionHandler::removeStream(
    const WebKit::WebMediaStream& stream) {
  RemoveStream(stream);
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackRemoveStream(
        this, stream, PeerConnectionTracker::SOURCE_LOCAL);
}

void RTCPeerConnectionHandler::getStats(
    const WebKit::WebRTCStatsRequest& request) {
  scoped_refptr<LocalRTCStatsRequest> inner_request(
      new talk_base::RefCountedObject<LocalRTCStatsRequest>(request));
  getStats(inner_request.get());
}

void RTCPeerConnectionHandler::getStats(LocalRTCStatsRequest* request) {
  talk_base::scoped_refptr<webrtc::StatsObserver> observer(
      new talk_base::RefCountedObject<StatsResponse>(request));
  webrtc::MediaStreamTrackInterface* track = NULL;
  if (request->hasSelector()) {
      track = GetNativeMediaStreamTrack(request->stream(),
                                        request->component());
    if (!track) {
      DVLOG(1) << "GetStats: Track not found.";
      // TODO(hta): Consider how to get an error back.
      std::vector<webrtc::StatsReport> no_reports;
      observer->OnComplete(no_reports);
      return;
    }
  }
  GetStats(observer, track);
}

void RTCPeerConnectionHandler::GetStats(
    webrtc::StatsObserver* observer,
    webrtc::MediaStreamTrackInterface* track) {
  if (!native_peer_connection_->GetStats(observer, track)) {
    DVLOG(1) << "GetStats failed.";
    // TODO(hta): Consider how to get an error back.
    std::vector<webrtc::StatsReport> no_reports;
    observer->OnComplete(no_reports);
    return;
  }
}

WebKit::WebRTCDataChannelHandler* RTCPeerConnectionHandler::createDataChannel(
    const WebKit::WebString& label, const WebKit::WebRTCDataChannelInit& init) {
  // TODO(jiayl): update reliable to
  // (init.ordered && init.maxRetransmitTime == -1 && init.maxRetransmits == -1)
  // once Libjingle is updated to ignore this field for RTP data channels.
  bool reliable = false;
  return createDataChannel(label, reliable);
}

WebKit::WebRTCDataChannelHandler* RTCPeerConnectionHandler::createDataChannel(
    const WebKit::WebString& label, bool reliable) {
  DVLOG(1) << "createDataChannel label " << UTF16ToUTF8(label);

  webrtc::DataChannelInit config;
  config.reliable = reliable;

  talk_base::scoped_refptr<webrtc::DataChannelInterface> webrtc_channel(
      native_peer_connection_->CreateDataChannel(UTF16ToUTF8(label), &config));
  if (!webrtc_channel) {
    DLOG(ERROR) << "Could not create native data channel.";
    return NULL;
  }
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackCreateDataChannel(
        this, webrtc_channel.get(), PeerConnectionTracker::SOURCE_LOCAL);

  return new RtcDataChannelHandler(webrtc_channel);
}

WebKit::WebRTCDTMFSenderHandler* RTCPeerConnectionHandler::createDTMFSender(
    const WebKit::WebMediaStreamTrack& track) {
  DVLOG(1) << "createDTMFSender.";

  if (track.source().type() != WebKit::WebMediaStreamSource::TypeAudio) {
    DLOG(ERROR) << "Could not create DTMF sender from a non-audio track.";
    return NULL;
  }

  webrtc::AudioTrackInterface* audio_track =
      static_cast<webrtc::AudioTrackInterface*>(
          GetNativeMediaStreamTrack(track.stream(), track));

  talk_base::scoped_refptr<webrtc::DtmfSenderInterface> sender(
      native_peer_connection_->CreateDtmfSender(audio_track));
  if (!sender) {
    DLOG(ERROR) << "Could not create native DTMF sender.";
    return NULL;
  }
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackCreateDTMFSender(this, track);

  return new RtcDtmfSenderHandler(sender);
}

void RTCPeerConnectionHandler::stop() {
  DVLOG(1) << "RTCPeerConnectionHandler::stop";

  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackStop(this);
  native_peer_connection_->Close();
}

void RTCPeerConnectionHandler::OnError() {
  // TODO(perkj): Implement.
  NOTIMPLEMENTED();
}

void RTCPeerConnectionHandler::OnSignalingChange(
    webrtc::PeerConnectionInterface::SignalingState new_state) {
  WebKit::WebRTCPeerConnectionHandlerClient::SignalingState state =
      GetWebKitSignalingState(new_state);
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackSignalingStateChange(this, state);
  client_->didChangeSignalingState(state);
}

// Called any time the IceConnectionState changes
void RTCPeerConnectionHandler::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  WebKit::WebRTCPeerConnectionHandlerClient::ICEConnectionState state =
      GetWebKitIceConnectionState(new_state);
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackIceConnectionStateChange(this, state);
  client_->didChangeICEConnectionState(state);
}

// Called any time the IceGatheringState changes
void RTCPeerConnectionHandler::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState new_state) {
  if (new_state == webrtc::PeerConnectionInterface::kIceGatheringComplete) {
    // If ICE gathering is completed, generate a NULL ICE candidate,
    // to signal end of candidates.
    WebKit::WebRTCICECandidate null_candidate;
    client_->didGenerateICECandidate(null_candidate);
  }

  WebKit::WebRTCPeerConnectionHandlerClient::ICEGatheringState state =
      GetWebKitIceGatheringState(new_state);
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackIceGatheringStateChange(this, state);
  client_->didChangeICEGatheringState(state);
}

void RTCPeerConnectionHandler::OnAddStream(
    webrtc::MediaStreamInterface* stream_interface) {
  DCHECK(stream_interface);
  DCHECK(remote_streams_.find(stream_interface) == remote_streams_.end());

  RemoteMediaStreamImpl* remote_stream =
      new RemoteMediaStreamImpl(stream_interface);
  remote_streams_.insert(
      std::pair<webrtc::MediaStreamInterface*, RemoteMediaStreamImpl*> (
          stream_interface, remote_stream));

  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackAddStream(
        this, remote_stream->webkit_stream(),
        PeerConnectionTracker::SOURCE_REMOTE);

  client_->didAddRemoteStream(remote_stream->webkit_stream());
}

void RTCPeerConnectionHandler::OnRemoveStream(
    webrtc::MediaStreamInterface* stream_interface) {
  DCHECK(stream_interface);
  RemoteStreamMap::iterator it = remote_streams_.find(stream_interface);
  if (it == remote_streams_.end()) {
    NOTREACHED() << "Stream not found";
    return;
  }

  scoped_ptr<RemoteMediaStreamImpl> remote_stream(it->second);
  const WebKit::WebMediaStream& webkit_stream = remote_stream->webkit_stream();
  DCHECK(!webkit_stream.isNull());
  remote_streams_.erase(it);

  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackRemoveStream(
        this, webkit_stream, PeerConnectionTracker::SOURCE_REMOTE);

  client_->didRemoveRemoteStream(webkit_stream);
}

void RTCPeerConnectionHandler::OnIceCandidate(
    const webrtc::IceCandidateInterface* candidate) {
  DCHECK(candidate);
  std::string sdp;
  if (!candidate->ToString(&sdp)) {
    NOTREACHED() << "OnIceCandidate: Could not get SDP string.";
    return;
  }
  WebKit::WebRTCICECandidate web_candidate;
  web_candidate.initialize(UTF8ToUTF16(sdp),
                           UTF8ToUTF16(candidate->sdp_mid()),
                           candidate->sdp_mline_index());
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackAddIceCandidate(
        this, web_candidate, PeerConnectionTracker::SOURCE_LOCAL);

  client_->didGenerateICECandidate(web_candidate);
}

void RTCPeerConnectionHandler::OnDataChannel(
    webrtc::DataChannelInterface* data_channel) {
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackCreateDataChannel(
        this, data_channel, PeerConnectionTracker::SOURCE_REMOTE);

  DVLOG(1) << "RTCPeerConnectionHandler::OnDataChannel "
           << data_channel->label();
  client_->didAddRemoteDataChannel(new RtcDataChannelHandler(data_channel));
}

void RTCPeerConnectionHandler::OnRenegotiationNeeded() {
  if (peer_connection_tracker_)
    peer_connection_tracker_->TrackOnRenegotiationNeeded(this);
  client_->negotiationNeeded();
}

PeerConnectionTracker* RTCPeerConnectionHandler::peer_connection_tracker() {
  return peer_connection_tracker_;
}

webrtc::SessionDescriptionInterface*
RTCPeerConnectionHandler::CreateNativeSessionDescription(
    const WebKit::WebRTCSessionDescription& description,
    webrtc::SdpParseError* error) {
  std::string sdp = UTF16ToUTF8(description.sdp());
  std::string type = UTF16ToUTF8(description.type());
  webrtc::SessionDescriptionInterface* native_desc =
      dependency_factory_->CreateSessionDescription(type, sdp, error);

  LOG_IF(ERROR, !native_desc) << "Failed to create native session description."
                              << " Type: " << type << " SDP: " << sdp;

  return native_desc;
}

}  // namespace content
