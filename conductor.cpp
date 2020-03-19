#include "conductor.h"

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <utility>
#include <vector>
#include <QDebug>

#include "third_party/abseil-cpp/absl/memory/memory.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/jsoncpp/source/include/json/json.h"
#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/create_peerconnection_factory.h"
#include "api/rtp_sender_interface.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "examples/peerconnection/client/defaults.h"
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "modules/video_capture/video_capture.h"
#include "modules/video_capture/video_capture_factory.h"
#include "p2p/base/port_allocator.h"
#include "pc/video_track_source.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/rtc_certificate_generator.h"
#include "rtc_base/strings/json.h"
#include "test/vcm_capturer.h"

namespace {
// Names used for a IceCandidate JSON object.
const char kCandidateSdpMidName[] = "sdpMid";
const char kCandidateSdpMlineIndexName[] = "sdpMLineIndex";
const char kCandidateSdpName[] = "candidate";

// Names used for a SessionDescription JSON object.
const char kSessionDescriptionTypeName[] = "type";
const char kSessionDescriptionSdpName[] = "sdp";
}

class DummySetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
public:
    static DummySetSessionDescriptionObserver* Create() {
        return new rtc::RefCountedObject<DummySetSessionDescriptionObserver>();
    }
    virtual void OnSuccess() { qDebug() << __FUNCTION__; }
    virtual void OnFailure(webrtc::RTCError error) {
        qDebug() << __FUNCTION__ << " " << error.message();
    }
};


Conductor::Conductor(PeerConnectionClient* client, QObject *parent)
    : QObject{parent},
      peer_id_(-1),
      loopback_(false),
      client_(client) {
//    client_->RegisterObserver(this);
}

Conductor::~Conductor() {
    RTC_DCHECK(!peer_connection_);
}

bool Conductor::connection_active() const {
    return peer_connection_ != nullptr;
}

void Conductor::Close() {
    client_->SignOut();
    DeletePeerConnection();
}

bool Conductor::InitializePeerConnection() {
    RTC_DCHECK(!peer_connection_factory_);
    RTC_DCHECK(!peer_connection_);

    peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
                nullptr /* network_thread */, nullptr /* worker_thread */,
                nullptr /* signaling_thread */, nullptr /* default_adm */,
                webrtc::CreateBuiltinAudioEncoderFactory(),
                webrtc::CreateBuiltinAudioDecoderFactory(),
                webrtc::CreateBuiltinVideoEncoderFactory(),
                webrtc::CreateBuiltinVideoDecoderFactory(), nullptr /* audio_mixer */,
                nullptr /* audio_processing */);

    if (!peer_connection_factory_) {
        DeletePeerConnection();
        return false;
    }

    if (!CreatePeerConnection(/*dtls=*/true)) {
        DeletePeerConnection();
    }

    AddTracks();

    return peer_connection_ != nullptr;
}

bool Conductor::ReinitializePeerConnectionForLoopback() {
    loopback_ = true;
    std::vector<rtc::scoped_refptr<webrtc::RtpSenderInterface>> senders =
            peer_connection_->GetSenders();
    peer_connection_ = nullptr;
    if (CreatePeerConnection(/*dtls=*/false)) {
        for (const auto& sender : senders) {
            peer_connection_->AddTrack(sender->track(), sender->stream_ids());
        }
        peer_connection_->CreateOffer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    }
    return peer_connection_ != nullptr;
}

bool Conductor::CreatePeerConnection(bool dtls) {
    RTC_DCHECK(peer_connection_factory_);
    RTC_DCHECK(!peer_connection_);

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    config.enable_dtls_srtp = dtls;
    webrtc::PeerConnectionInterface::IceServer server;
    server.uri = GetPeerConnectionString();
    config.servers.push_back(server);

    peer_connection_ = peer_connection_factory_->CreatePeerConnection(config, nullptr, nullptr, this);
    return peer_connection_ != nullptr;
}

void Conductor::DeletePeerConnection() {
    peer_connection_ = nullptr;
    peer_connection_factory_ = nullptr;
    peer_id_ = -1;
    loopback_ = false;
}

//
// PeerConnectionObserver implementation.
//

void Conductor::onAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver, const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&streams)
{
    QString receiverId = QString(receiver->id().c_str());
    qDebug() << __FUNCTION__ << " " << receiverId;
}

void Conductor::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
    QString receiverId = QString(receiver->id().c_str());
    qDebug() << __FUNCTION__ << " " << receiverId;
    UIThreadCallback(TRACK_REMOVED, receiver->track().release());
//    main_wnd_->QueueUIThreadCallback(TRACK_REMOVED, receiver->track().release());
}

void Conductor::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    qDebug() << __FUNCTION__ << " " << candidate->sdp_mline_index();
    // For loopback test. To save some connecting delay.
    if (loopback_) {
        if (!peer_connection_->AddIceCandidate(candidate)) {
            qDebug() << "Failed to apply the received candidate";
        }
        return;
    }

    Json::StyledWriter writer;
    Json::Value jmessage;

    jmessage[kCandidateSdpMidName] = candidate->sdp_mid();
    jmessage[kCandidateSdpMlineIndexName] = candidate->sdp_mline_index();
    std::string sdp;
    if (!candidate->ToString(&sdp)) {
        qDebug() << "Failed to serialize candidate";
        return;
    }
    jmessage[kCandidateSdpName] = sdp;
    SendMessage(writer.write(jmessage));
}

//
// PeerConnectionClientObserver implementation.
//

void Conductor::OnSignedIn() {
    qDebug() << __FUNCTION__;
    main_wnd_->SwitchToPeerList(client_->peers());
}

void Conductor::OnDisconnected() {
    qDebug() << __FUNCTION__;

    DeletePeerConnection();

    if (main_wnd_->IsWindow())
        main_wnd_->SwitchToConnectUI();
}

void Conductor::OnPeerConnected(int id, const std::string& name) {
    qDebug() << __FUNCTION__;
    // Refresh the list if we're showing it.
    if (main_wnd_->current_ui() == MainWindow::LIST_PEERS)
        main_wnd_->SwitchToPeerList(client_->peers());
}

void Conductor::OnPeerDisconnected(int id) {
    qDebug() << __FUNCTION__;
    if (id == peer_id_) {
        qDebug() << "Our peer disconnected";
        UIThreadCallback(PEER_CONNECTION_CLOSED, NULL);
//        main_wnd_->QueueUIThreadCallback(PEER_CONNECTION_CLOSED, NULL);
    } else {
        // Refresh the list if we're showing it.
        if (main_wnd_->current_ui() == MainWindow::LIST_PEERS)
            main_wnd_->SwitchToPeerList(client_->peers());
    }
}

void Conductor::OnMessageFromPeer(int peer_id, const std::string& message) {
    RTC_DCHECK(peer_id_ == peer_id || peer_id_ == -1);
    RTC_DCHECK(!message.empty());

    if (!peer_connection_.get()) {
        RTC_DCHECK(peer_id_ == -1);
        peer_id_ = peer_id;

        if (!InitializePeerConnection()) {
            qDebug() << "Failed to initialize our PeerConnection instance";
            client_->SignOut();
            return;
        }
    }
    else if (peer_id != peer_id_) {
        RTC_DCHECK(peer_id_ != -1);
        qDebug() << "Received a message from unknown peer while already in a conversation with a different peer.";
        return;
    }

    Json::Reader reader;
    Json::Value jmessage;
    QString messageFromPeer = QString(message.c_str());
    if (!reader.parse(message, jmessage)) {
        qDebug() << "Received unknown message. " << messageFromPeer;
        return;
    }
    std::string type_str;
    std::string json_object;

    rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionTypeName, &type_str);
    if (!type_str.empty()) {
        if (type_str == "offer-loopback") {
            // This is a loopback call.
            // Recreate the peerconnection with DTLS disabled.
            if (!ReinitializePeerConnectionForLoopback()) {
                qDebug() << "Failed to initialize our PeerConnection instance";
                DeletePeerConnection();
                client_->SignOut();
            }
            return;
        }
        absl::optional<webrtc::SdpType> type_maybe = webrtc::SdpTypeFromString(type_str);
        if (!type_maybe) {
            QString sdtType = QString(type_str.c_str());
            qDebug() << "Unknown SDP type: " << sdtType;
            return;
        }
        webrtc::SdpType type = *type_maybe;
        std::string sdp;
        if (!rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionSdpName, &sdp)) {
            qDebug() << "Can't parse received session description message.";
            return;
        }
        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::SessionDescriptionInterface> session_description = webrtc::CreateSessionDescription(type, sdp, &error);
        QString errorDescription = QString(error.description.c_str());
        if (!session_description) {
            qDebug() << "Can't parse received session description message. SdpParseError was: " << errorDescription;
            return;
        }
        qDebug() << " Received session description :" << messageFromPeer;
        peer_connection_->SetRemoteDescription(
                    DummySetSessionDescriptionObserver::Create(),
                    session_description.release());
        if (type == webrtc::SdpType::kOffer) {
            peer_connection_->CreateAnswer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
        }
    }
    else {
        std::string sdp_mid;
        int sdp_mlineindex = 0;
        std::string sdp;
        if (!rtc::GetStringFromJsonObject(jmessage, kCandidateSdpMidName,
                                          &sdp_mid) ||
                !rtc::GetIntFromJsonObject(jmessage, kCandidateSdpMlineIndexName,
                                           &sdp_mlineindex) ||
                !rtc::GetStringFromJsonObject(jmessage, kCandidateSdpName, &sdp)) {
            qDebug() << "Can't parse received message.";
            return;
        }
        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error));
        QString errorDescription = QString(error.description.c_str());
        if (!candidate.get()) {
            qDebug() << "Can't parse received candidate message. SdpParseError was: " << errorDescription;
            return;
        }
        if (!peer_connection_->AddIceCandidate(candidate.get())) {
            qDebug() << "Failed to apply the received candidate";
            return;
        }
        qDebug() << " Received candidate :" << messageFromPeer;
    }
}

void Conductor::OnMessageSent(int err) {
    // Process the next pending message if any.
    UIThreadCallback(SEND_MESSAGE_TO_PEER, NULL);
//    main_wnd_->QueueUIThreadCallback(SEND_MESSAGE_TO_PEER, NULL);
}

void Conductor::OnServerConnectionFailure() {
    qDebug() << "Error: Failed to connect to the server";
}

//
// MainWndCallback implementation.
//

void Conductor::StartLogin(const std::string& server, int port) {
    if (client_->is_connected())
        return;
    server_ = server;
    client_->Connect(server, port, GetPeerName());
}

void Conductor::DisconnectFromServer() {
    if (client_->is_connected())
        client_->SignOut();
}

void Conductor::ConnectToPeer(int peer_id) {
    RTC_DCHECK(peer_id_ == -1);
    RTC_DCHECK(peer_id != -1);

//    if (peer_connection_.get()) {
//        main_wnd_->MessageBox(
//                    "Error", "We only support connecting to one peer at a time", true);
//        return;
//    }
    if (InitializePeerConnection()) {
        peer_id_ = peer_id;
        peer_connection_->CreateOffer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    } else {
        qDebug() << "Error: Failed to initialize PeerConnection";
    }
}

void Conductor::AddTracks() {
    if (!peer_connection_->GetSenders().empty()) {
        return;  // Already added tracks.
    }

    rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
                peer_connection_factory_->CreateAudioTrack(
                    kAudioLabel, peer_connection_factory_->CreateAudioSource(
                        cricket::AudioOptions())));
    auto result_or_error = peer_connection_->AddTrack(audio_track, {kStreamId});
    if (!result_or_error.ok()) {
        qDebug() << "Failed to add audio track to PeerConnection: " << result_or_error.error().message();
    }

//    rtc::scoped_refptr<CapturerTrackSource> video_device =
//            CapturerTrackSource::Create();
//    if (video_device) {
//        rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_(
//                    peer_connection_factory_->CreateVideoTrack(kVideoLabel, video_device));
//        main_wnd_->StartLocalRenderer(video_track_);

//        result_or_error = peer_connection_->AddTrack(video_track_, {kStreamId});
//        if (!result_or_error.ok()) {
//            qDebug() << "Failed to add video track to PeerConnection: "
//                     << result_or_error.error().message();
//        }
//    } else {
//        qDebug() << "OpenVideoCaptureDevice failed";
//    }

    //    main_wnd_->SwitchToStreamingUI();
}

void Conductor::SetAudioControl(bool mute)
{
    webrtc::MediaStreamInterface* remote_stream = nullptr;
    webrtc::AudioTrackVector tracks = remote_stream->GetAudioTracks();
    webrtc::AudioTrackInterface* audio_track = tracks[0];
    for (auto& track : tracks) {
            track->set_enabled(mute);
    }
}

void Conductor::DisconnectFromCurrentPeer() {
    qDebug() << __FUNCTION__;
    if (peer_connection_.get()) {
        client_->SendHangUp(peer_id_);
        DeletePeerConnection();
    }

    if (main_wnd_->IsWindow())
        main_wnd_->SwitchToPeerList(client_->peers());
}

void Conductor::UIThreadCallback(int msg_id, void* data) {
    switch (msg_id) {
    case PEER_CONNECTION_CLOSED:
        qDebug() << "PEER_CONNECTION_CLOSED";
        DeletePeerConnection();
        DisconnectFromServer();
//        if (main_wnd_->IsWindow()) {
//            if (client_->is_connected()) {
//                main_wnd_->SwitchToPeerList(client_->peers());
//            } else {
//                main_wnd_->SwitchToConnectUI();
//            }
//        } else {
//            DisconnectFromServer();
//        }
        break;

    case SEND_MESSAGE_TO_PEER: {
        qDebug() << "SEND_MESSAGE_TO_PEER";
        std::string* msg = reinterpret_cast<std::string*>(data);
        if (msg) {
            // For convenience, we always run the message through the queue.
            // This way we can be sure that messages are sent to the server
            // in the same order they were signaled without much hassle.
            pending_messages_.push_back(msg);
        }

        if (!pending_messages_.empty() && !client_->IsSendingMessage()) {
            msg = pending_messages_.front();
            pending_messages_.pop_front();

            if (!client_->SendToPeer(peer_id_, *msg) && peer_id_ != -1) {
                qDebug() << "SendToPeer failed";
                DisconnectFromServer();
            }
            delete msg;
        }

        if (!peer_connection_.get())
            peer_id_ = -1;

        break;
    }

//    case NEW_TRACK_ADDED: {
//        auto* track = reinterpret_cast<webrtc::MediaStreamTrackInterface*>(data);
//        if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
//            auto* video_track = static_cast<webrtc::VideoTrackInterface*>(track);
//            main_wnd_->StartRemoteRenderer(video_track);
//        }
//        track->Release();
//        break;
//    }

    case TRACK_REMOVED: {
        // Remote peer stopped sending a track.
        auto* track = reinterpret_cast<webrtc::MediaStreamTrackInterface*>(data);
        track->Release();
        break;
    }

    default:
        RTC_NOTREACHED();
        break;
    }
}

void Conductor::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
    peer_connection_->SetLocalDescription(DummySetSessionDescriptionObserver::Create(), desc);

    std::string sdp;
    desc->ToString(&sdp);
    webrtc::SdpParseError error;

    // For loopback test. To save some connecting delay.
//    if (loopback_) {
//        // Replace message type from "offer" to "answer"
//        std::unique_ptr<webrtc::SessionDescriptionInterface> session_description =
//                webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp, &error);
//        peer_connection_->SetRemoteDescription(
//                    DummySetSessionDescriptionObserver::Create(),
//                    session_description.release());
//        return;
//    }

    Json::StyledWriter writer;
    Json::Value jmessage;
    jmessage[kSessionDescriptionTypeName] = webrtc::SdpTypeToString(desc->GetType());
    jmessage[kSessionDescriptionSdpName] = sdp;
    SendMessage(writer.write(jmessage));
}

void Conductor::OnFailure(webrtc::RTCError error)
{
    qDebug() << error.message();
}

void Conductor::SendMessage(const std::string& json_object)
{
    std::string* msg = new std::string(json_object);
    UIThreadCallback(SEND_MESSAGE_TO_PEER, msg);
//    main_wnd_->QueueUIThreadCallback(SEND_MESSAGE_TO_PEER, msg);
}

