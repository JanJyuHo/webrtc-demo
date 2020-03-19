#include <QQmlContext>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDebug>
#include "rtc_base/checks.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/socket.h"
#include "rtc_base/async_socket.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/flags.h"
#include "system_wrappers/include/field_trial.h"
#include "test/field_trial.h"

#include "customsocketserver.h"
#include "flag_defs.h"
#include "webrtcmanager.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QGuiApplication app(argc, argv);

    WebrtcManager webrtc;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("webrtc", &webrtc);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    rtc::FlagList::SetFlagsFromCommandLine(&argc, argv, true);
    if (FLAG_help) {
        rtc::FlagList::Print(NULL, false);
        return 0;
    }

    webrtc::test::ValidateFieldTrialsStringOrDie(FLAG_force_fieldtrials);
    webrtc::field_trial::InitFieldTrialsFromString(FLAG_force_fieldtrials);

    if ((FLAG_port < 1) || (FLAG_port > 65535)) {
        qDebug() << "Error: " << FLAG_port << " is not a valid port.";
        return -1;
    }

    CustomSocketServer socketServer;
    rtc::Thread thread(&socketServer);       // question
    rtc::AutoSocketServerThread autothread(&socketServer);

    rtc::InitializeSSL();
    PeerConnectionClient client;
    rtc::scoped_refptr<Conductor> conductor(new rtc::RefCountedObject<Conductor>(&client));
    socketServer.setClient(&client);
    socketServer.setConducotr(conductor);

    thread.Run();

    rtc::CleanupSSL();
    return app.exec();
}
