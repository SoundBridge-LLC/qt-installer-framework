/**************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Qt Installer Framework.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
**
** $QT_END_LICENSE$
**
**************************************************************************/

#include "constants.h"
#include "commandlineparser.h"
#include "installerbase.h"
#include "installerbasecommons.h"
#include "tabcontroller.h"

#include <binaryformatenginehandler.h>
#include <copydirectoryoperation.h>
#include <errors.h>
#include <init.h>
#include <kdupdaterupdateoperations.h>
#include <messageboxhandler.h>
#include <packagemanagercore.h>
#include <packagemanagerproxyfactory.h>
#include <qprocesswrapper.h>
#include <protocol.h>
#include <productkeycheck.h>
#include <settings.h>
#include <utils.h>
#include <globals.h>

#include <kdrunoncechecker.h>
#include <kdupdaterfiledownloaderfactory.h>

#include <QDirIterator>
#include <QTemporaryFile>
#include <QTranslator>
#include <QUuid>
#include <QLoggingCategory>
#include <QLocalSocket>
#include <QSystemSemaphore>

#include "VectorStyle.h"
#include "SkinManager.h"

const QString kApplicationId = QLatin1String("SoundBridge Setup");

InstallerBase::InstallerBase(int &argc, char *argv[])
    : SDKApp<QApplication>(argc, argv)
    , m_core(0)
    , mAnotherInstanceRunning(kApplicationId)
{
    QInstaller::init(); // register custom operations
    setStyle(new VectorStyle(false));
    new SkinManager(); // singleton

    //
    QPalette palette(this->palette());
    palette.setColor(QPalette::Base, SkinManager::instance().getColor(SC_Dialog_Background));
    palette.setColor(QPalette::Window, SkinManager::instance().getColor(SC_Dialog_Background));
    palette.setColor(QPalette::WindowText, SkinManager::instance().getColor(SC_Text));
    palette.setColor(QPalette::ButtonText, SkinManager::instance().getColor(SC_Text));
    palette.setColor(QPalette::Text, SkinManager::instance().getColor(SC_Text));
    setPalette(palette);
}

InstallerBase::~InstallerBase()
{
    delete m_core;
}

int InstallerBase::run()
{
    // make sure that application is single instance
    if(isConnectedToServer(kApplicationId) || isAnotherInstanceRunning())
    {
        QInstaller::MessageBoxHandler::information(0, QLatin1String("AlreadyRunning"),
            QLatin1String("Setup"),
            QLatin1String("You are actively installing a SoundBridge, LLC product.\n"
                          "Please wait for this installation to complete before proceeding to install another product."));

        return EXIT_FAILURE;
    }

    //
    QString fileName = datFile(binaryFile());
    quint64 cookie = QInstaller::BinaryContent::MagicCookieDat;
    if (fileName.isEmpty()) {
        fileName = binaryFile();
        cookie = QInstaller::BinaryContent::MagicCookie;
    }

    QFile binary(fileName);
    QInstaller::openForRead(&binary);

    qint64 magicMarker;
    QInstaller::ResourceCollectionManager manager;
    QList<QInstaller::OperationBlob> oldOperations;
    QInstaller::BinaryContent::readBinaryContent(&binary, &oldOperations, &manager, &magicMarker,
        cookie);

    // Usually resources simply get mapped into memory and therefore the file does not need to be
    // kept open during application runtime. Though in case of offline installers we need to access
    // the appended binary content (packages etc.), so we close only in maintenance mode.
    if (magicMarker != QInstaller::BinaryContent::MagicInstallerMarker)
        binary.close();

    CommandLineParser parser;
    parser.parse(arguments());

    QString loggingRules(QLatin1String("ifw.* = false")); // disable all by default
    if (QInstaller::isVerbose()) {
        loggingRules = QString(); // enable all in verbose mode
        if (parser.isSet(QLatin1String(CommandLineOptions::LoggingRules))) {
            loggingRules = parser.value(QLatin1String(CommandLineOptions::LoggingRules))
                           .split(QLatin1Char(','), QString::SkipEmptyParts)
                           .join(QLatin1Char('\n')); // take rules from command line
        }
    }
    QLoggingCategory::setFilterRules(loggingRules);

    qCDebug(QInstaller::lcTranslations) << "Language:" << QLocale().uiLanguages()
        .value(0, QLatin1String("No UI language set")).toUtf8().constData();
    qDebug() << "Arguments: " << arguments().join(QLatin1String(", ")).toUtf8().constData();

    SDKApp::registerMetaResources(manager.collectionByName("QResources"));
    if (parser.isSet(QLatin1String(CommandLineOptions::StartClient))) {
        const QStringList arguments = parser.value(QLatin1String(CommandLineOptions::StartClient))
            .split(QLatin1Char(','), QString::SkipEmptyParts);
        m_core = new QInstaller::PackageManagerCore(
            magicMarker, oldOperations,
            arguments.value(0, QLatin1String(QInstaller::Protocol::DefaultSocket)),
            arguments.value(1, QLatin1String(QInstaller::Protocol::DefaultAuthorizationKey)),
            QInstaller::Protocol::Mode::Debug);
    } else {
        m_core = new QInstaller::PackageManagerCore(magicMarker, oldOperations,
            QUuid::createUuid().toString(), QUuid::createUuid().toString());
    }

    {
        using namespace QInstaller;
        ProductKeyCheck::instance()->init(m_core);
        ProductKeyCheck::instance()->addPackagesFromXml(QLatin1String(":/metadata/Updates.xml"));
        BinaryFormatEngineHandler::instance()->registerResources(manager.collections());
    }

    dumpResourceTree();

    // try loading skin
    SkinManager::instance().setActiveSkin(QLatin1String("setup_skin"), true);

    // see whether SoundBridge is running
    if(!m_core->settings().applicationId().isEmpty() && isConnectedToServer(m_core->settings().applicationId()))
    {
        QInstaller::MessageBoxHandler::information(0, QLatin1String("AlreadyRunning"),
            QLatin1String("Setup"),
            QLatin1String("A SoundBridge, LLC app is currently running.\n"
                          "Please close it to proceed with the installation process."));

        return EXIT_FAILURE;
    }

    QString controlScript;
    if (parser.isSet(QLatin1String(CommandLineOptions::Script))) {
        controlScript = parser.value(QLatin1String(CommandLineOptions::Script));
        if (!QFileInfo(controlScript).exists())
            throw QInstaller::Error(QLatin1String("Script file does not exist."));
    } else if (!m_core->settings().controlScript().isEmpty()) {
        controlScript = QLatin1String(":/metadata/installer-config/")
            + m_core->settings().controlScript();
    }

    if (parser.isSet(QLatin1String(CommandLineOptions::Proxy))) {
        m_core->settings().setProxyType(QInstaller::Settings::SystemProxy);
        KDUpdater::FileDownloaderFactory::instance().setProxyFactory(m_core->proxyFactory());
    }

    if (parser.isSet(QLatin1String(CommandLineOptions::ShowVirtualComponents))) {
        QFont f;
        f.setItalic(true);
        QInstaller::PackageManagerCore::setVirtualComponentsFont(f);
        QInstaller::PackageManagerCore::setVirtualComponentsVisible(true);
    }

    if (parser.isSet(QLatin1String(CommandLineOptions::Updater))) {
        if (m_core->isInstaller())
            throw QInstaller::Error(QLatin1String("Cannot start installer binary as updater."));
        m_core->setUpdater();
    }

    if (parser.isSet(QLatin1String(CommandLineOptions::ManagePackages))) {
        if (m_core->isInstaller())
            throw QInstaller::Error(QLatin1String("Cannot start installer binary as package manager."));
        m_core->setPackageManager();
    }

    if (parser.isSet(QLatin1String(CommandLineOptions::AddRepository))) {
        const QStringList repoList = repositories(parser
            .value(QLatin1String(CommandLineOptions::AddRepository)));
        if (repoList.isEmpty())
            throw QInstaller::Error(QLatin1String("Empty repository list for option 'addRepository'."));
        m_core->addUserRepositories(repoList);
    }

    if (parser.isSet(QLatin1String(CommandLineOptions::AddTmpRepository))) {
        const QStringList repoList = repositories(parser
            .value(QLatin1String(CommandLineOptions::AddTmpRepository)));
        if (repoList.isEmpty())
            throw QInstaller::Error(QLatin1String("Empty repository list for option 'addTempRepository'."));
        m_core->setTemporaryRepositories(repoList, false);
    }

    if (parser.isSet(QLatin1String(CommandLineOptions::SetTmpRepository))) {
        const QStringList repoList = repositories(parser
            .value(QLatin1String(CommandLineOptions::SetTmpRepository)));
        if (repoList.isEmpty())
            throw QInstaller::Error(QLatin1String("Empty repository list for option 'setTempRepository'."));
        m_core->setTemporaryRepositories(repoList, true);
    }

    QInstaller::PackageManagerCore::setNoForceInstallation(parser
        .isSet(QLatin1String(CommandLineOptions::NoForceInstallation)));
    QInstaller::PackageManagerCore::setCreateLocalRepositoryFromBinary(parser
        .isSet(QLatin1String(CommandLineOptions::CreateLocalRepository))
        || m_core->settings().createLocalRepository());

    QHash<QString, QString> params;
    const QStringList positionalArguments = parser.positionalArguments();
    foreach (const QString &argument, positionalArguments) {
        if (argument.contains(QLatin1Char('='))) {
            const QString name = argument.section(QLatin1Char('='), 0, 0);
            const QString value = argument.section(QLatin1Char('='), 1, 1);
            params.insert(name, value);
            m_core->setValue(name, value);
        }
    }

    const QString directory = QLatin1String(":/translations");
    const QStringList translations = m_core->settings().translations();

    if (translations.isEmpty()) {
        foreach (const QLocale locale, QLocale().uiLanguages()) {
            QScopedPointer<QTranslator> qtTranslator(new QTranslator(QCoreApplication::instance()));
            const bool qtLoaded = qtTranslator->load(locale, QLatin1String("qt"),
                                              QLatin1String("_"), directory);

            if (qtLoaded || locale.language() == QLocale::English) {
                if (qtLoaded)
                    QCoreApplication::instance()->installTranslator(qtTranslator.take());

                QScopedPointer<QTranslator> ifwTranslator(new QTranslator(QCoreApplication::instance()));
                if (ifwTranslator->load(locale, QString(), QString(), directory))
                    QCoreApplication::instance()->installTranslator(ifwTranslator.take());

                // To stop loading other translations it's sufficient that
                // qt was loaded successfully or we hit English as system language
                break;
            }
        }
    } else {
        foreach (const QString &translation, translations) {
            QScopedPointer<QTranslator> translator(new QTranslator(QCoreApplication::instance()));
            if (translator->load(translation, QLatin1String(":/translations")))
                QCoreApplication::instance()->installTranslator(translator.take());
        }
    }

    //create the wizard GUI
    TabController controller(0);
    controller.setManager(m_core);
    controller.setManagerParams(params);
    controller.setControlScript(controlScript);

    if (m_core->isInstaller())
        controller.setGui(new InstallerGui(m_core));
    else
        controller.setGui(new MaintenanceGui(m_core));

    QInstaller::PackageManagerCore::Status status =
        QInstaller::PackageManagerCore::Status(controller.init());
    if (status != QInstaller::PackageManagerCore::Success)
        return status;

    const int result = QCoreApplication::instance()->exec();
    if (result != 0)
        return result;

    if (m_core->finishedWithSuccess())
        return QInstaller::PackageManagerCore::Success;

    status = m_core->status();
    switch (status) {
        case QInstaller::PackageManagerCore::Success:
            return status;

        case QInstaller::PackageManagerCore::Canceled:
            return status;

        default:
            break;
    }
    return QInstaller::PackageManagerCore::Failure;
}


// -- private

bool InstallerBase::isConnectedToServer(const QString &serverId)
{
    bool connectedToServer = false;
    {
        QLocalSocket socket(this);
        socket.setServerName(serverId);
        socket.connectToServer(QIODevice::WriteOnly);
        connectedToServer = socket.waitForConnected(1000);
        socket.close();
    }

    return connectedToServer;
}

bool InstallerBase::isAnotherInstanceRunning()
{
    QString semaphoreKey = kApplicationId + QString::fromStdString("_semaphore");
    QSystemSemaphore synchronizeSharedMemory(semaphoreKey, 1);
    synchronizeSharedMemory.acquire();

    // on Unix based system we need to reattach shared memory to make sure that crashed application release shared memory
    {
        QSharedMemory sharedMemory(kApplicationId);
        sharedMemory.attach();
    }

    //
    bool isSharedMemoryAttached = mAnotherInstanceRunning.attach();
    if(!isSharedMemoryAttached)
        mAnotherInstanceRunning.create(1);

    synchronizeSharedMemory.release();
    return isSharedMemoryAttached;
}

void InstallerBase::dumpResourceTree() const
{
    qCDebug(QInstaller::lcResources) << "Resource tree:";
    QDirIterator it(QLatin1String(":/"), QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (it.next().startsWith(QLatin1String(":/qt-project.org")))
            continue;
        qCDebug(QInstaller::lcResources) << "    " << it.filePath().toUtf8().constData();
    }
}

QStringList InstallerBase::repositories(const QString &list) const
{
    const QStringList items = list.split(QLatin1Char(','), QString::SkipEmptyParts);
    foreach (const QString &item, items)
        qDebug() << "Adding custom repository:" << item.toUtf8().constData();
    return items;
}
