#include "addkitstospeeddialoperation.h"

#include "packagemanagercore.h"
#include "qsettingswrapper.h"

#ifdef LUMIT_INSTALLER
#include <QDirIterator>
#include <QtXml/QDomDocument>
#include <QXmlStreamReader>
#include <QStandardPaths>
#endif

#ifdef Q_OS_WIN
#include <shlobj.h>
#elif defined(Q_OS_MAC)
#include "MacUtils.h"
#endif

using namespace QInstaller;

namespace
{
#ifdef Q_OS_WIN
    static const QString kRitMixDirectoryLocation = QString::fromLocal8Bit("../");
    static const QString kFolderIconName = QString::fromLocal8Bit("KitFolder.ico");
#elif defined(Q_OS_MAC)
    static const QString kRitMixDirectoryLocation = QString::fromLocal8Bit("../../");
    static const QString kFolderIconName = QString::fromLocal8Bit("KitFolder.icns");
#endif

    static const QString kSpeedDialKitsFile = QString::fromLocal8Bit("SpeedDialKits.xml");
    static const QString kRitMixFolder = QString::fromLocal8Bit("RitMix");
    
    static void fixPermissions(const QString &repoPath)
    {
        QFile::Permissions permissions = QFile::ReadUser | QFile::ReadGroup | QFile::ReadOwner | QFile::ReadOther
                                        | QFile::WriteUser | QFile::WriteGroup | QFile::WriteOwner | QFile::WriteOther
                                        | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOwner | QFile::ExeOther;

        QDirIterator it(repoPath, QDirIterator::Subdirectories);
        while (it.hasNext() && !it.next().isEmpty()) {
            if (!it.fileInfo().isFile())
                continue;

            if (!QFile::setPermissions(it.filePath(), permissions)) {
                    return;
            }
        }
    }
}

AddKitsToSpeedDialOperation::AddKitsToSpeedDialOperation()
{
    setName(QLatin1String("AddKitsToSpeedDial"));
}

void AddKitsToSpeedDialOperation::backup()
{
}

bool AddKitsToSpeedDialOperation::performOperation()
{
    QStringList argList = arguments();
    QDir appDataDirectory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    appDataDirectory.cd(kRitMixDirectoryLocation);
    appDataDirectory.mkdir(kRitMixFolder);
    appDataDirectory.cd(kRitMixFolder);
    
    // set correct permissions
    QFile::Permissions permissions = QFile::ReadUser | QFile::ReadGroup | QFile::ReadOwner | QFile::ReadOther
                                    | QFile::WriteUser | QFile::WriteGroup | QFile::WriteOwner | QFile::WriteOther
                                    | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOwner | QFile::ExeOther;
    QFile::setPermissions(appDataDirectory.absolutePath(), permissions);
    
    QString speedDialKitsFilePath = appDataDirectory.absoluteFilePath(kSpeedDialKitsFile);
    QDomDocument speedDialKitsDocument;
    QDomNodeList listOfAvailableKits = getListOfAvailableKits(speedDialKitsDocument, speedDialKitsFilePath);

    QDir defaultKitsFolder(argList[0]);
    defaultKitsFolder.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

    QStringList kitsDirs = defaultKitsFolder.entryList();
    for(int i = 0; i < kitsDirs.count(); ++i)
    {
        QDir directory(argList[0]);
        QString kitPath = directory.absoluteFilePath(kitsDirs[i]);

        QDir ritmixFile(kitPath);
        ritmixFile.setNameFilters(QStringList(QString::fromLocal8Bit("*.ritmix")));
        ritmixFile.setFilter(QDir::Files);

        QStringList filesList = ritmixFile.entryList();
        if(filesList.empty()) continue;

        QString ritmixFileName = filesList[0];
        QString kitRitmixFilePath = ritmixFile.absoluteFilePath(ritmixFileName);
        QString kitID = getKitID(kitRitmixFilePath);

        // create icon for kit folder
        createIconForKit(kitPath);

        // add kit to speed dial list
        if (!kitID.isEmpty())
        {
            bool isNewKit = true;
            for (int i = 0; i < listOfAvailableKits.count(); ++i)
            {
                if (listOfAvailableKits.at(i).firstChildElement(QString::fromLocal8Bit("ID")).text() == kitID)
                {
                    listOfAvailableKits.at(i).firstChildElement(QString::fromLocal8Bit("Path")).firstChild().toText().setData(kitRitmixFilePath);
                    isNewKit = false;
                    break;
                }
            }

            if (isNewKit)
                addKitToSpeedDialList(speedDialKitsDocument, kitRitmixFilePath, kitID);
        }
    }

    // write new speed dial kits configuration in file
    QFile file(speedDialKitsFilePath);
    if (file.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        QTextStream outputFile(&file);
        outputFile << speedDialKitsDocument.toString() << endl;
    }

    file.close();

    // just in case :)
    fixPermissions(appDataDirectory.absolutePath());
    return true;
}

bool AddKitsToSpeedDialOperation::undoOperation()
{
    return true;
}

bool AddKitsToSpeedDialOperation::testOperation()
{
    return true;
}

Operation *AddKitsToSpeedDialOperation::clone() const
{
    return new AddKitsToSpeedDialOperation();
}

QDomNodeList QInstaller::AddKitsToSpeedDialOperation::getListOfAvailableKits(QDomDocument& domDocument, const QString& speedDialKitsFilePath)
{
    QFile speedDialKitsFile(speedDialKitsFilePath);
    if (speedDialKitsFile.open(QIODevice::ReadOnly))
    {
        if (domDocument.setContent(&speedDialKitsFile))
            return domDocument.elementsByTagName(QString::fromLocal8Bit("Kit"));
    }
    else
    {
        QDomElement rootElement = domDocument.createElement(QString::fromLocal8Bit("SpeedDialKits"));
        domDocument.appendChild(rootElement);

        //
        QFile::Permissions permissions = QFile::ReadUser | QFile::ReadGroup | QFile::ReadOwner | QFile::ReadOther
                                | QFile::WriteUser | QFile::WriteGroup | QFile::WriteOwner | QFile::WriteOther
                                | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOwner | QFile::ExeOther;

        QFile::setPermissions(speedDialKitsFilePath, permissions);
    }

    return QDomNodeList();
}

QString QInstaller::AddKitsToSpeedDialOperation::getKitID(const QString& kitFilePath)
{
    QFile kitFile(kitFilePath);
    if (kitFile.open(QIODevice::ReadOnly))
    {
        QXmlStreamReader reader(&kitFile);

        do
        {
            reader.readNext();
            if (reader.name() == QString::fromLocal8Bit("ID"))
                break;
        } while (!reader.atEnd());

        reader.readNext();
        return *reader.text().string();
    }

    return QString();
}

void QInstaller::AddKitsToSpeedDialOperation::addKitToSpeedDialList(QDomDocument& speedDialKitsDocument, const QString& kitPath, const QString & kitID)
{
    QDomElement kitElement = speedDialKitsDocument.createElement(QString::fromLocal8Bit("Kit"));
    speedDialKitsDocument.documentElement().appendChild(kitElement);

    QDomElement IDElement = speedDialKitsDocument.createElement(QString::fromLocal8Bit("ID"));
    QDomText IDTextNode = speedDialKitsDocument.createTextNode(QString::fromLocal8Bit("ID"));
    IDTextNode.setData(kitID);
    IDElement.appendChild(IDTextNode);

    QDomElement pathElement = speedDialKitsDocument.createElement(QString::fromLocal8Bit("Path"));
    QDomText pathTextNode = speedDialKitsDocument.createTextNode(QString::fromLocal8Bit("Path"));
    pathTextNode.setData(kitPath);
    pathElement.appendChild(pathTextNode);

    kitElement.appendChild(IDElement);
    kitElement.appendChild(pathElement);
}

void QInstaller::AddKitsToSpeedDialOperation::createIconForKit(const QString& kitPath)
{
    QDir kitFolder(kitPath);
    QString kitIconPath = kitFolder.absoluteFilePath(kFolderIconName);
    if (QFile::exists(kitIconPath))
    {
#ifdef Q_OS_WIN
        SetFileAttributes((const wchar_t*)kitIconPath.utf16(), FILE_ATTRIBUTE_HIDDEN);

        // create and configure desktop.ini file
        QFile desktopIniFile(kitFolder.absoluteFilePath(QString::fromLocal8Bit("/desktop.ini")));
        if (desktopIniFile.open(QIODevice::ReadWrite))
        {
            SHFOLDERCUSTOMSETTINGS fcs = { 0 };
            fcs.dwSize = sizeof(SHFOLDERCUSTOMSETTINGS);
            fcs.dwMask = FCSM_ICONFILE;
            fcs.pszIconFile = (wchar_t*)kFolderIconName.utf16();
            fcs.cchIconFile = 0;
            fcs.iIconIndex = 0;
            SHGetSetFolderCustomSettings(&fcs, (const wchar_t*)kitPath.utf16(), FCS_FORCEWRITE);
        }
#else
        setIconForFile(kitIconPath.toStdString(), kitPath.toStdString());
        QFile::remove(kitIconPath);
#endif
    }
}
