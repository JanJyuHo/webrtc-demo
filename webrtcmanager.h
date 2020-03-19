#ifndef WEBRTCMANAGER_H
#define WEBRTCMANAGER_H
#include <QObject>
#include "conductor.h"
#include "peerconnectionclient.h"


class WebrtcManager : public QObject
{
    Q_OBJECT
public:
    WebrtcManager(QObject *parent = 0);
    virtual ~WebrtcManager();
    Q_INVOKABLE void startLogin(const QString &server, int port);
    void disconnectFromServer();
    void connectToPeer(int peerId);
    void disconnectFromCurrentPeer();
    void close();
    Q_INVOKABLE void setAudioControl(bool mute);

private:
    Conductor *conductor;
    PeerConnectionClient *client;
};

#endif // WEBRTCMANAGER_H
