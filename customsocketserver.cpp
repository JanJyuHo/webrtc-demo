#include "customsocketserver.h"

CustomSocketServer::CustomSocketServer()
    : conductor(NULL),
      client(NULL)
{
}

CustomSocketServer::~CustomSocketServer()
{
}

void CustomSocketServer::SetMessageQueue(rtc::MessageQueue *queue)
{
    messageQueue = queue;
}

void CustomSocketServer::setClient(PeerConnectionClient *peerClient)
{
    client = peerClient;
}

void CustomSocketServer::setConducotr(Conductor *appConductor)
{
    conductor = appConductor;
}

bool CustomSocketServer::Wait(int cms, bool process_io)
{
    if (!conductor->connection_active() && client != NULL && !client->is_connected()) {
        messageQueue->Quit();
    }
    return rtc::PhysicalSocketServer::Wait(0, process_io);
}
