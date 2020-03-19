#include "webrtcmanager.h"

WebrtcManager::WebrtcManager(QObject *parent)
    : client(new PeerConnectionClient),
      conductor(new Conductor(client))
{
}

void WebrtcManager::startLogin(const QString &server, int port)
{
    conductor->StartLogin(server.toStdString(), port);
}

void WebrtcManager::disconnectFromServer()
{
    conductor->DisconnectFromServer();
}


void WebrtcManager::connectToPeer(int peerId)
{
    conductor->ConnectToPeer(peerId);
}

void WebrtcManager::disconnectFromCurrentPeer()
{
    conductor->DisconnectFromCurrentPeer();
}

void WebrtcManager::close()
{
    conductor->Close();
}

void WebrtcManager::setAudioControl(bool mute)
{
    conductor->SetAudioControl(mute);
}

