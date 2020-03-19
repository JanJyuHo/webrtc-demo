#ifndef CONDUCTOR_H
#define CONDUCTOR_H

#include <QObject>
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "peerconnectionclient.h"

class Conductor : public QObject, public webrtc::PeerConnectionObserver, public webrtc::CreateSessionDescriptionObserver, public PeerConnectionClientObserver
{
    Q_OBJECT
public:
    enum CallbackID {
        MEDIA_CHANNELS_INITIALIZED = 1,
        PEER_CONNECTION_CLOSED,
        SEND_MESSAGE_TO_PEER,
        NEW_TRACK_ADDED,
        TRACK_REMOVED,
    };
    Conductor(PeerConnectionClient *client, QObject *parent = 0);

    bool connection_active() const;

    virtual void Close();
    ~Conductor();
    bool InitializePeerConnection();
    bool ReinitializePeerConnectionForLoopback();
    bool CreatePeerConnection(bool dtls);
    void DeletePeerConnection();
    void AddTracks();
    void SetAudioControl(bool mute);

    //
    // PeerConnectionObserver implementation.
    //

    void OnSignalingChange(
            webrtc::PeerConnectionInterface::SignalingState new_state) override {}
    void onAddTrack(
            rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
            const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&
            streams);
    void OnRemoveTrack(
            rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
    void OnDataChannel(
            rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
    void OnRenegotiationNeeded() override {}
    void OnIceConnectionChange(
            webrtc::PeerConnectionInterface::IceConnectionState new_state) override {}
    void OnIceGatheringChange(
            webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
    void OnIceConnectionReceivingChange(bool receiving) override {}

    //
    // PeerConnectionClientObserver implementation.
    //

    void OnSignedIn() override;

    void OnDisconnected() override;

    void OnPeerConnected(int id, const std::string& name) override;

    void OnPeerDisconnected(int id) override;

    void OnMessageFromPeer(int peer_id, const std::string& message) override;

    void OnMessageSent(int err) override;

    void OnServerConnectionFailure() override;

    //
    // MainWndCallback implementation.
    //

    void StartLogin(const std::string& server, int port);

    void DisconnectFromServer();

    void ConnectToPeer(int peer_id);

    void DisconnectFromCurrentPeer();

    void UIThreadCallback(int msg_id, void* data);

    // CreateSessionDescriptionObserver implementation.
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
    void OnFailure(webrtc::RTCError error) override;

protected:
    // Send a message to the remote peer.
    void SendMessage(const std::string& json_object);

    int peer_id_;
    bool loopback_;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
    peer_connection_factory_;
    PeerConnectionClient* client_;
    std::deque<std::string*> pending_messages_;
    std::string server_;
    webrtc::MediaStreamInterface* remote_stream;

};

#endif // CONDUCTOR_H
