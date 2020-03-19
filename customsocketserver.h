#ifndef CUSTOMSOCKETSERVER_H
#define CUSTOMSOCKETSERVER_H
#include "rtc_base/physical_socket_server.h"
#include "peerconnectionclient.h"
#include "conductor.h"


class CustomSocketServer : public rtc::PhysicalSocketServer
{
public:
    explicit CustomSocketServer();
    virtual ~CustomSocketServer();

    void SetMessageQueue(rtc::MessageQueue* queue) override;
    void setClient(PeerConnectionClient* peerClient);
    void setConducotr(Conductor* appConductor);
    bool Wait(int cms, bool process_io) override;

protected:
    rtc::MessageQueue* messageQueue;
    Conductor* conductor;
    PeerConnectionClient* client;
};

#endif // CUSTOMSOCKETSERVER_H
