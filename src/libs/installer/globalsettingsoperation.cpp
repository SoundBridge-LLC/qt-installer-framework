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

#include "globalsettingsoperation.h"
#include "qsettingswrapper.h"

using namespace QInstaller;

GlobalSettingsOperation::GlobalSettingsOperation()
{
    setName(QLatin1String("GlobalConfig"));
}

void GlobalSettingsOperation::backup()
{
}

bool GlobalSettingsOperation::performOperation()
{
    QString key, value;
    QScopedPointer<QSettingsWrapper> settings(setup(&key, &value, arguments()));
    if (settings.isNull())
        return false;

    if (!settings->isWritable()) {
        setError(UserDefinedError);
        setErrorString(tr("Settings are not writable"));
        return false;
    }

    const QVariant oldValue = settings->value(key);
    settings->setValue(key, value);
    settings->sync();

    if (settings->status() != QSettingsWrapper::NoError) {
        setError(UserDefinedError);
        setErrorString(tr("Failed to write settings"));
        return false;
    }

    setValue(QLatin1String("oldvalue"), oldValue);
    return true;
}

bool GlobalSettingsOperation::undoOperation()
{
    QString key, val;
    QScopedPointer<QSettingsWrapper> settings(setup(&key, &val, arguments()));
    if (settings.isNull())
        return false;

    // be sure it's still our value and nobody changed it in between
    const QVariant oldValue = value(QLatin1String("oldvalue"));
    if (settings->value(key) == val) {
        // restore the previous state
        if (oldValue.isNull())
            settings->remove(key);
        else
            settings->setValue(key, oldValue);
    }

    return true;
}

bool GlobalSettingsOperation::testOperation()
{
    return true;
}

Operation *GlobalSettingsOperation::clone() const
{
    return new GlobalSettingsOperation();
}

QSettingsWrapper *GlobalSettingsOperation::setup(QString *key, QString *value, const QStringList &arguments)
{
    if (arguments.count() != 3 && arguments.count() != 4 && arguments.count() != 5) {
        setError(InvalidArguments);
        setErrorString(tr("Invalid arguments in %0: %1 arguments given, %2 expected%3.")
            .arg(name()).arg(arguments.count()).arg(tr("3, 4 or 5"), QLatin1String("")));
        return 0;
    }

    if (arguments.count() == 5) {
        QSettingsWrapper::Scope scope = QSettingsWrapper::UserScope;
        if (arguments.at(0) == QLatin1String("SystemScope"))
          scope = QSettingsWrapper::SystemScope;
        const QString &company = arguments.at(1);
        const QString &application = arguments.at(2);
        *key = arguments.at(3);
        *value = arguments.at(4);
        return new QSettingsWrapper(scope, company, application);
    } else if (arguments.count() == 4) {
        const QString &company = arguments.at(0);
        const QString &application = arguments.at(1);
        *key = arguments.at(2);
        *value = arguments.at(3);
        return new QSettingsWrapper(company, application);
    } else if (arguments.count() == 3) {
        const QString &filename = arguments.at(0);
        *key = arguments.at(1);
        *value = arguments.at(2);
        return new QSettingsWrapper(filename, QSettingsWrapper::NativeFormat);
    }

    return 0;
}
