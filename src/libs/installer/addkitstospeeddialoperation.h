#ifndef ADDKITSTOSPEEDDIALOPERATION_H
#define ADDKITSTOSPEEDDIALOPERATION_H

#include "qinstallerglobal.h"

namespace QInstaller {

class INSTALLER_EXPORT AddKitsToSpeedDialOperation : public QObject, public Operation
{
    //Q_OBJECT

public:
    AddKitsToSpeedDialOperation();

    void backup();
    bool performOperation();
    bool undoOperation();
    bool testOperation();
    Operation *clone() const;

private:
    QDomNodeList getListOfAvailableKits(QDomDocument& domDocument, const QString& speedDialKitsFilePath);
    QString getKitID(const QString& kitFilePath);

    void addKitToSpeedDialList(QDomDocument& speedDialKitsDocument, const QString& kitPath, const QString& kitID);
    void createIconForKit(const QString& kitPath);
};

}

#endif
