#include <init.h>
#include <binarycontent.h>
#include <fileio.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>

#include <iostream>

using namespace QInstaller;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QInstaller::init();

    QFile input(QLatin1String("installerbase.exe"));
    QLatin1String uninstallerFileName(argv[1]);
    QFile output(uninstallerFileName);
    if(input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly))
    {
        QInstaller::appendData(&output, &input, input.size());
        QInstaller::appendInt64(&output, 0);   // operations start
        QInstaller::appendInt64(&output, 0);   // operations end
        QInstaller::appendInt64(&output, 0);   // resource count
        QInstaller::appendInt64(&output, 4 * sizeof(qint64));   // data block size
        QInstaller::appendInt64(&output, BinaryContent::MagicUninstallerMarker);
        QInstaller::appendInt64(&output, BinaryContent::MagicCookie);

        input.close();
        output.close();
    }
    else
    {
        qCritical("Could not create Uninstaller.exe");
        return -1;
    }

    return 0;
}
