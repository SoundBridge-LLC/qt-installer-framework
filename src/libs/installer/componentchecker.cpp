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

#include "componentchecker.h"

#include "component.h"
#include "constants.h"
#include "packagemanagercore.h"
#include "globals.h"

namespace QInstaller {

QStringList ComponentChecker::checkComponent(Component *component)
{
    QStringList checkResult;
    if (!component)
        return checkResult;

    PackageManagerCore *core = component->packageManagerCore();
    if (!core)
        return checkResult;

    const bool defaultPropertyScriptValue = component->variables().value(scDefault).compare(scScript, Qt::CaseInsensitive) == 0;
    const bool defaultPropertyValue = component->variables().value(scDefault).compare(scTrue, Qt::CaseInsensitive) == 0;
    const QStringList autoDependencies = component->autoDependencies();
    const QList<Component *> allComponents = core->components(PackageManagerCore::ComponentType::All);
    if (!autoDependencies.isEmpty()) {
        if (component->forcedInstallation()) {
            checkResult << QString::fromLatin1("Component %1 specifies \"ForcedInstallation\" property "
                "together with \"AutoDependOn\" list. This combination of states may not work properly.")
                .arg(component->name());
        }
        if (defaultPropertyScriptValue) {
            checkResult << QString::fromLatin1("Component %1 specifies script value for \"Default\" "
                "property together with \"AutoDependOn\" list. This combination of states may not "
                "work properly.").arg(component->name());
        }
        if (defaultPropertyValue) {
            checkResult << QString::fromLatin1("Component %1 specifies \"Default\" property together "
                "with \"AutoDependOn\" list. This combination of states may not work properly.")
                .arg(component->name());
        }
        if (!core->dependees(component).isEmpty()) {
            checkResult << QString::fromLatin1("Other components depend on auto dependent "
                "component %1. This may not work properly.")
                .arg(component->name());
        }
        const QStringList dependencies = component->dependencies();
        foreach (const QString &dependency, dependencies) {
            Component *dependencyComponent = PackageManagerCore::componentByName(
                        dependency, allComponents);
            if (dependencyComponent && autoDependencies.contains(dependencyComponent->name())) {
                checkResult << QString::fromLatin1("Component %1 specifies both dependency "
                    "and auto dependency on component %2. The dependency might be superfluous.")
                    .arg(component->name(), dependencyComponent->name());
            }
        }
    }
    if (component->packageManagerCore()->isInstaller()) {
        if (component->isTristate()) {
            if (defaultPropertyScriptValue) {
                checkResult << QString::fromLatin1("Component %1 specifies script value for \"Default\" "
                    "property while not being a leaf node. The \"Default\" property will get a "
                    "\"false\" value.").arg(component->name());
            }
            if (defaultPropertyValue) {
                checkResult << QString::fromLatin1("Component %1 specifies \"Default\" property "
                    "while not being a leaf node. The \"Default\" property will get a \"false\" value.")
                    .arg(component->name());
            }
        }
        if (!component->isCheckable()) {
            if (defaultPropertyScriptValue) {
                checkResult << QString::fromLatin1("Component %1 specifies script value for \"Default\" "
                    "property while being not checkable. The \"Default\" property will get a \"false\" "
                    "value.").arg(component->name());
            }
            if (defaultPropertyValue) {
                checkResult << QString::fromLatin1("Component %1 specifies \"Default\" property "
                    "while being not checkable. The \"Default\" property will get a \"false\" value.")
                    .arg(component->name());
            }
        }
        if (component->childCount()) {
            if (!autoDependencies.isEmpty()) {
                /* Use case:

                A (depends on C)
                A.B
                C

                Let's say we installed everything.
                Running maintenance tool and unselecting C will mark A for uninstallation too,
                while A.B stays marked as installed.
                After running maintenance tool again, it will check A automatically
                (since its child is selected), this will also mark C for installation (dependecy).
                Moreover, the "Next" button will be disabled.
                */
                checkResult << QString::fromLatin1("Component %1 auto depends on other components "
                    "while having children components. This will not work properly.")
                    .arg(component->name());
            }

            if (!component->dependencies().isEmpty()) {
                checkResult << QString::fromLatin1("Component %1 depends on other components "
                    "while having children components. This will not work properly.")
                    .arg(component->name());
            }

            if (!core->dependees(component).isEmpty()) {
                /*
                Use case:

                A
                A.B
                C (depends on A)

                Selecting C marks A for installation too, A.B is not marked for installation.
                So after installation, A and C are installed, while A.B is not.
                Maintenance tool will uncheck A automatically
                (since none of its children are installed) this will also mark C
                for uninstallation (dependency).
                Moreover, the "Next" button will be disabled.
                */
                checkResult << QString::fromLatin1("Other components depend on component %1 "
                    "which has children components. This will not work properly.")
                    .arg(component->name());
            }
        }
        foreach (const QString &autoDependency, autoDependencies) {
            Component *autoDependencyComponent = PackageManagerCore::componentByName(
                        autoDependency, allComponents);
            if (autoDependencyComponent && autoDependencyComponent->childCount()) {
                checkResult << QString::fromLatin1("Component %1 auto depends on component %2 "
                    "which has children components. This will not work properly.")
                    .arg(component->name(), autoDependencyComponent->name());
            }
        }
    }

    return checkResult;
}


} // namespace QInstaller
