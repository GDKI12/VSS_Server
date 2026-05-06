#include <QCoreApplication>
#include "server.h"
#include "videoHandler.h"
#include "Log/vssLog.h"

void makeVideo(const QString& rawPath, const QString& outputPath)
{
    QProcess ffmpeg;
    QStringList args;
    int width = 2048;
    int height = 1536;

    args << "-f" << "rawvideo"
         << "-pixel_format" << "bayer_rggb8"
         << "-video_size" << QString("%1x%2").arg(width).arg(height)
         << "-framerate" << "10"
         << "-i" << "pipe:0"
         << "-vf" << "format=bgr24"
         << "-c:v" << "libx264"
         << "-preset" << "veryfast"
         << "-preset" << "veryfast"
         << "-pix_fmt" << "yuv420p"
         << outputPath;

    ffmpeg.setProgram("ffmpeg");
    ffmpeg.setArguments(args);
    ffmpeg.setProcessChannelMode(QProcess::SeparateChannels);
    ffmpeg.start();

    if(!ffmpeg.waitForStarted(3000))
    {
        qCritical() << "ffmpeg start fail";
        ffmpeg.closeWriteChannel();
        ffmpeg.waitForFinished(3000);
    }
    QDir dir(rawPath);
    if(!dir.exists())
        return;

    QFileInfoList rawFiles = dir.entryInfoList({"*raw"},
                                               QDir::Files | QDir::NoDotAndDotDot,
                                               QDir::Name);

    qint64 rawBytes = (qint64)width * height;
    QByteArray frame(rawBytes, Qt::Uninitialized);


    for(QFileInfo fi : rawFiles)
    {
        const QString rawPath = fi.absoluteFilePath();

        QFile in(rawPath);

        if(!in.open(QIODevice::ReadOnly))
            continue;

        qint64 readBytes = in.read(frame.data(), rawBytes);

        in.close();

        if(readBytes != rawBytes)
        {
            ffmpeg.closeWriteChannel();
            ffmpeg.waitForFinished(3000);
            return;
        }
        qint64 written = 0;

        while(written < frame.size())
        {
            qint64 chunk = ffmpeg.write(frame.constData() + written
                                        , frame.size() - written);

            if(chunk < 0)
            {
                if(ffmpeg.state() == QProcess::NotRunning)
                    return;

                ffmpeg.closeWriteChannel();
                if(ffmpeg.waitForFinished(3000))
                    return;

                ffmpeg.kill();
                ffmpeg.waitForFinished();
            }

            written += chunk;

            if(!ffmpeg.waitForBytesWritten(-1))
          {
              qCritical() << "ffmpeg stdin flush 실패:" << in.fileName();
              qCritical() << "state =" << ffmpeg.state();
              qCritical() << "exitCode =" << ffmpeg.exitCode();
              qCritical() << "error =" << ffmpeg.errorString();
              qCritical().noquote() << "stderr:\n" << ffmpeg.readAllStandardError();

              if(ffmpeg.state() == QProcess::NotRunning)
                  return;

              ffmpeg.closeWriteChannel();
              if(ffmpeg.waitForFinished(3000))
                  return;

              ffmpeg.kill();
              ffmpeg.waitForFinished();

              return;
          }
        }
    }

    ffmpeg.closeWriteChannel();
    ffmpeg.waitForFinished(-1);
}
int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    qRegisterMetaType<LogLevel>("LogLevel");

    VideoServer server1("cam1", 5000);
    VideoServer server2("cam2", 5001);
    VideoServer server3("cam3", 5002);

    return a.exec();


    return 0;
}
