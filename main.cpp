#include "UI/mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QLocalSocket>
#include <QLocalServer>
#include <QElapsedTimer>
#include "globalobjects.h"
#include "Play/Playlist/playlist.h"
#include "Play/playcontext.h"
#include "Extension/App/appmanager.h"
#include "Common/logger.h"
#include "Common/kstats.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment (lib,"DbgHelp.lib")
LONG AppCrashHandler(EXCEPTION_POINTERS *pException) 
{			
	HANDLE hDumpFile = CreateFile((LPCWSTR)(QCoreApplication::applicationDirPath() + QDateTime::currentDateTime().toString("\\yyyy-MM-dd-hh-mm-ss")+".dmp").utf16(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDumpFile != INVALID_HANDLE_VALUE) 
	{
		MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
		dumpInfo.ExceptionPointers = pException;
		dumpInfo.ThreadId = GetCurrentThreadId();
		dumpInfo.ClientPointers = TRUE;
		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, NULL, NULL);
	}
	EXCEPTION_RECORD* record = pException->ExceptionRecord;
	QString errCode(QString::number(record->ExceptionCode, 16)), errAdr(QString::number((uint)record->ExceptionAddress, 16)), errMod;
	QMessageBox::critical(nullptr, "Error", QString("Error Code: %1 Error Address: %2").arg(errCode).arg(errAdr),QMessageBox::Ok);
	return EXCEPTION_EXECUTE_HANDLER;
}
#endif
void decodeParam()
{
    QStringList args = QCoreApplication::arguments();
    if(args.count() <= 1) return;
    args.pop_front();
    QStringList urlList = args;
    if (!urlList.empty())
    {
        if (GlobalObjects::playlist->isAddExternal())
        {
            GlobalObjects::playlist->addURL(urlList,QModelIndex());
            const PlayListItem *curItem = GlobalObjects::playlist->setCurrentItem(urlList.last());
            if (curItem)
            {
                PlayContext::context()->playItem(curItem);
            }
        }
        else
        {
            PlayContext::context()->playURL(urlList.last());
        }
    }
}
bool isRunning()
{
    QLocalSocket socket;
    socket.connectToServer("KikoPlaySingleServer");
    if (socket.waitForConnected())
    {
        QStringList args=QCoreApplication::arguments();
        if (args.count() > 1)
        {
            args.pop_front();
            QStringList urlList = args;
            if (!urlList.empty())
            {
                QByteArray data;
                QDataStream s(&data,QIODevice::WriteOnly);
                s << urlList;
                socket.write(data);
                socket.waitForBytesWritten();
            }
        }
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#else
    //根据实际屏幕缩放比例更改
    qputenv("QT_SCALE_FACTOR", "1.5");
#endif
#endif
    QElapsedTimer timer;
    timer.start();
    QApplication a(argc, argv);
    a.setApplicationName("KikoPlay");
    a.setWindowIcon(QIcon(":/res/images/app.png"));
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)AppCrashHandler);
#endif
    if(isRunning()) return 0;
    GlobalObjects::init(&timer);

    MainWindow w;
    GlobalObjects::mainWindow = &w;
    GlobalObjects::context()->devicePixelRatioF = w.devicePixelRatioF();
    QLocalServer *singleServer=new QLocalServer(&a);
    QObject::connect(singleServer, &QLocalServer::newConnection, [singleServer,&w](){
        QLocalSocket *localSocket = singleServer->nextPendingConnection();
        localSocket->waitForReadyRead();
        QByteArray data(localSocket->readAll());
        QStringList args;
        QDataStream s(data);
        s >> args;
        if (args.count() > 0)
        {
            if (GlobalObjects::playlist->isAddExternal())
            {
                GlobalObjects::playlist->addURL(args, QModelIndex());
                const PlayListItem *curItem = GlobalObjects::playlist->setCurrentItem(args.last());
                if (curItem)
                {
                    PlayContext::context()->playItem(curItem);
                }
            }
            else
            {
                PlayContext::context()->playURL(args.last());
            }
        }
        w.raise();
        w.activateWindow();
        w.setWindowState((w.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        w.show();
    });
    if (!singleServer->listen("KikoPlaySingleServer"))
    {
        if (singleServer->serverError() == QAbstractSocket::AddressInUseError)
        {
            QLocalServer::removeServer("KikoPlaySingleServer");
            singleServer->listen("KikoPlaySingleServer");
        }
    }
    w.raise();
    w.activateWindow();
    w.show();
    GlobalObjects::context()->tick(&timer, "ui");
	decodeParam();
    QTimer::singleShot(100, [](){
        GlobalObjects::appManager->autoStart();
    });
    GlobalObjects::context()->startupTime += timer.elapsed();
    Logger::logger()->log(Logger::APP, QString("startup time: %1ms").arg(GlobalObjects::context()->startupTime));
    QStringList stepTimes{"step time: "};
    for (auto &p : GlobalObjects::context()->stepTime)
    {
        stepTimes.append(QString("%1:\t%2").arg(p.first, -10).arg(p.second));
    }
    Logger::logger()->log(Logger::APP, stepTimes.join('\n'));
    KStats::instance();
    return a.exec();
}
