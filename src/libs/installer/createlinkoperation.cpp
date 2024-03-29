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
#include "createlinkoperation.h"

#include "link.h"

#include <QFileInfo>

using namespace QInstaller;

CreateLinkOperation::CreateLinkOperation()
{
    setName(QLatin1String("CreateLink"));
}

void CreateLinkOperation::backup()
{
}

bool CreateLinkOperation::performOperation()
{
    QStringList args = arguments();

    if (args.count() != 2) {
        setError(InvalidArguments);
        setErrorString(tr("Invalid arguments in %0: %1 arguments given, %2 expected%3.")
            .arg(name()).arg(arguments().count()).arg(tr("exactly 2"), QLatin1String("")));
        return false;
    }

    const QString& linkPath = args.at(0);
    const QString& targetPath = args.at(1);
    Link link = Link::create(linkPath, targetPath);

    if (!link.exists()) {
        setError(UserDefinedError);
        setErrorString(tr("Could not create link from %1 to %2.").arg(linkPath, targetPath));
        return false;
    }

    return true;
}

bool CreateLinkOperation::undoOperation()
{
    QStringList args = arguments();

    const QString& linkPath = args.at(0);
    const QString& targetPath = args.at(1);

    Link link = Link(linkPath);
    if (!link.exists()) {
        return true;
    }
    if (!link.remove()) {
        setError(UserDefinedError);
        setErrorString(tr("Could not remove link from %1 to %2.").arg(linkPath, targetPath));
        return false;
    }

    return !QFileInfo(linkPath).exists();
}

bool CreateLinkOperation::testOperation()
{
    return true;
}

Operation *CreateLinkOperation::clone() const
{
    return new CreateLinkOperation();
}
