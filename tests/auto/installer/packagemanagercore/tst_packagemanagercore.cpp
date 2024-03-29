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

#include <binarycontent.h>
#include <component.h>
#include <errors.h>
#include <fileutils.h>
#include <packagemanagercore.h>
#include <progresscoordinator.h>

#include <QDir>
#include <QTemporaryFile>
#include <QTest>

using namespace QInstaller;

class DummyComponent : public Component
{
public:
    DummyComponent(PackageManagerCore *core)
        : Component(core)
    {
        setCheckState(Qt::Checked);
    }

    void beginInstallation()
    {
        throw Error(tr("Force crash to test rollback!"));
    }

    ~DummyComponent()
    {
    }
};

class NamedComponent : public Component
{
public:
    NamedComponent(PackageManagerCore *core, const QString &name)
        : Component(core)
    {
        setValue(scName, name);
        setValue(scVersion, QLatin1String("1.0.0"));
    }

    NamedComponent(PackageManagerCore *core, const QString &name, const QString &version)
        : Component(core)
    {
        setValue(scName, name);
        setValue(scVersion, version);
    }

};

class tst_PackageManagerCore : public QObject
{
    Q_OBJECT

private:
    void setIgnoreMessage(const QString &testDirectory)
    {
#if QT_VERSION < QT_VERSION_CHECK(5, 5, 0)
        const QString message = "\"\t- arguments: %1\" ";
#else
        const QString message = "\"\\t- arguments: %1\" ";
#endif
        QTest::ignoreMessage(QtDebugMsg, "Operations sanity check succeeded.");
        QTest::ignoreMessage(QtDebugMsg, "\"backup  operation: Mkdir\" ");
        QTest::ignoreMessage(QtDebugMsg, qPrintable(message.arg(testDirectory)));
        QTest::ignoreMessage(QtDebugMsg, qPrintable(message.arg(testDirectory)));
        QTest::ignoreMessage(QtDebugMsg, qPrintable(message.arg(testDirectory)));
        QTest::ignoreMessage(QtDebugMsg, "\"perform  operation: Mkdir\" ");
        QTest::ignoreMessage(QtDebugMsg, "Install size: 1 components ");
        QTest::ignoreMessage(QtDebugMsg, "create Error-Exception: \"Force crash to test rollback!\" ");
        QTest::ignoreMessage(QtDebugMsg, "\"created critical message box installationError: 'Error"
            "', Force crash to test rollback!\" ");
        QTest::ignoreMessage(QtDebugMsg, "ROLLING BACK operations= 1 ");
        QTest::ignoreMessage(QtDebugMsg, "\"undo  operation: Mkdir\" ");
        QTest::ignoreMessage(QtDebugMsg, "Done ");
        QTest::ignoreMessage(QtDebugMsg, "Done ");
        QTest::ignoreMessage(QtDebugMsg, "Done ");
    }

private slots:
    void testRollBackInstallationKeepTarget()
    {

        const QString testDirectory = QInstaller::generateTemporaryFileName();
        QVERIFY(QDir().mkpath(testDirectory));

        setIgnoreMessage(testDirectory);

        PackageManagerCore core(QInstaller::BinaryContent::MagicInstallerMarker,
            QList<QInstaller::OperationBlob>());
        // cancel the installer in error case
        core.autoRejectMessageBoxes();
        core.appendRootComponent(new DummyComponent(&core));
        core.setValue(QLatin1String("TargetDir"), testDirectory);
        core.setValue(QLatin1String("RemoveTargetDir"), QLatin1String("true"));

        QVERIFY(core.calculateComponentsToInstall());
        {
            QTemporaryFile dummy(testDirectory + QLatin1String("/dummy"));
            dummy.open();

            core.runInstaller();

            QVERIFY(QDir(testDirectory).exists());
            QVERIFY(QFileInfo(dummy.fileName()).exists());
        }
        QDir().rmdir(testDirectory);
        ProgressCoordinator::instance()->reset();
    }

    void testRollBackInstallationRemoveTarget()
    {
        const QString testDirectory = QInstaller::generateTemporaryFileName();
        QVERIFY(QDir().mkpath(testDirectory));

        setIgnoreMessage(testDirectory);

        PackageManagerCore core(QInstaller::BinaryContent::MagicInstallerMarker,
            QList<QInstaller::OperationBlob>());
        // cancel the installer in error case
        core.autoRejectMessageBoxes();
        core.appendRootComponent(new DummyComponent(&core));
        core.setValue(QLatin1String("TargetDir"), testDirectory);
        core.setValue(QLatin1String("RemoveTargetDir"), QLatin1String("true"));

        QVERIFY(core.calculateComponentsToInstall());

        core.runInstaller();
        QVERIFY(!QDir(testDirectory).exists());
        ProgressCoordinator::instance()->reset();
    }

    void testComponentSetterGetter()
    {
        {
            PackageManagerCore core;
            core.setPackageManager();

            QCOMPARE(core.components(PackageManagerCore::ComponentType::Root).count(), 0);
            QCOMPARE(core.components(PackageManagerCore::ComponentType::All).count(), 0);

            Component *root = new NamedComponent(&core, QLatin1String("root1"));
            root->appendComponent(new NamedComponent(&core, QLatin1String("root1.foo"),
                QLatin1String("1.0.1")));
            root->appendComponent(new NamedComponent(&core, QLatin1String("root1.bar")));
            core.appendRootComponent(root);

            QCOMPARE(core.components(PackageManagerCore::ComponentType::Root).count(), 1);
            QCOMPARE(core.components(PackageManagerCore::ComponentType::All).count(), 3);

            Component *foo = core.componentByName(QLatin1String("root1.foo-1.0.1"));
            QVERIFY(foo != 0);
            QCOMPARE(foo->name(), QLatin1String("root1.foo"));
            QCOMPARE(foo->value(scVersion), QLatin1String("1.0.1"));

            foo->appendComponent(new NamedComponent(&core, QLatin1String("root1.foo.child")));
            Component *v = new NamedComponent(&core, QLatin1String("root1.foo.virtual.child"));
            v->setValue(scVirtual, QLatin1String("true"));
            foo->appendComponent(v);

            QCOMPARE(core.components(PackageManagerCore::ComponentType::Root).count(), 1);
            QCOMPARE(core.components(PackageManagerCore::ComponentType::All).count(), 5);

            core.appendRootComponent(new NamedComponent(&core, QLatin1String("root2")));

            QCOMPARE(core.components(PackageManagerCore::ComponentType::Root).count(), 2);
            QCOMPARE(core.components(PackageManagerCore::ComponentType::All).count(), 6);
        }

        {
            PackageManagerCore core;
            core.setUpdater();

            Component *root = new NamedComponent(&core, QLatin1String("root1"));
            try {
                QTest::ignoreMessage(QtDebugMsg, "create Error-Exception: \"Components cannot "
                    "have children in updater mode.\" ");
                root->appendComponent(new NamedComponent(&core, QLatin1String("root1.foo")));
                QFAIL("Components cannot have children in updater mode.");
            } catch (const QInstaller::Error &error) {
                QCOMPARE(error.message(), QString("Components cannot have children in updater mode."));
            }
            core.appendUpdaterComponent(root);
            core.appendUpdaterComponent(new NamedComponent(&core, QLatin1String("root2")));

            Component *v = new NamedComponent(&core, QLatin1String("root3"), QLatin1String("2.0.1"));
            v->setValue(scVirtual, QLatin1String("true"));
            core.appendUpdaterComponent(v);

            QCOMPARE(core.components(PackageManagerCore::ComponentType::Root).count(), 3);
            QCOMPARE(core.components(PackageManagerCore::ComponentType::All).count(), 3);

            Component *root3 = core.componentByName(QLatin1String("root3->2.0.2"));
            QVERIFY(root3 == 0);

            root3 = core.componentByName(QLatin1String("root3->2.0.0"));
            QVERIFY(root3 != 0);
            QCOMPARE(root3->name(), QLatin1String("root3"));
            QCOMPARE(root3->value(scVersion), QLatin1String("2.0.1"));
        }
    }

    void testRequiredDiskSpace()
    {
        // test installer
        QTest::ignoreMessage(QtDebugMsg, "Operations sanity check succeeded.");
        PackageManagerCore core(QInstaller::BinaryContent::MagicInstallerMarker,
            QList<QInstaller::OperationBlob>());

        DummyComponent *root = new DummyComponent(&core);
        root->setValue(scName, "root");
        root->setValue(scUncompressedSize, QString::number(1000));
        core.appendRootComponent(root);

        DummyComponent *child1 = new DummyComponent(&core);
        child1->setValue(scName, "root.child1");
        child1->setValue(scUncompressedSize, QString::number(1500));
        root->appendComponent(child1);

        DummyComponent *child2 = new DummyComponent(&core);
        child2->setValue(scName, "root.child2");
        child2->setValue(scUncompressedSize, QString::number(250));
        root->appendComponent(child2);

        // install root, child1 (child2 remains uninstalled)
        root->setUninstalled();
        child1->setUninstalled();
        child2->setInstalled();
        core.calculateComponentsToInstall();
        QCOMPARE(core.requiredDiskSpace(), 2500ULL);

        // additionally install child2
        root->setInstalled();
        child1->setInstalled();
        child2->setUninstalled();
        core.componentsToInstallNeedsRecalculation();
        core.calculateComponentsToInstall();
        QCOMPARE(core.requiredDiskSpace(), 250ULL);
    }
};


QTEST_MAIN(tst_PackageManagerCore)

#include "tst_packagemanagercore.moc"
