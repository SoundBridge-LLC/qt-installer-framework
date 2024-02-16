/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtWidgets module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
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
** $QT_END_LICENSE$
**
****************************************************************************/

#include "VectorWizard.h"

#include "BackgroundWidget.h"
#include "Button.h"
#include "StepIndicatorWidget.h"

#define QT_NO_STYLE_WINDOWSVISTA

#ifndef QT_NO_WIZARD

#include "qabstractspinbox.h"
#include "qalgorithms.h"
#include "qapplication.h"
#include "qboxlayout.h"
#include "qlayoutitem.h"
#include "qdesktopwidget.h"
#include "qevent.h"
#include "qframe.h"
#include "qlabel.h"
#include "qlineedit.h"
#include "qpainter.h"
#include "qwindow.h"
#include "qpushbutton.h"
#include "qset.h"
#include "qstyle.h"
#include "qvarlengtharray.h"
#if defined(Q_OS_MACX)
#include <QtCore/QMetaMethod>
#include <QtGui/QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#elif !defined(QT_NO_STYLE_WINDOWSVISTA)
#include "qwizard_win_p.h"
#include "qtimer.h"
#endif

#include "private/qdialog_p.h"
#include <qdebug.h>

#ifdef Q_OS_WINCE
extern bool qt_wince_is_mobile();     //defined in qguifunctions_wce.cpp
#endif

#include <string.h>     // for memset()
#include <algorithm>

// Lumit Installer
static const int kSidebarMargin = 10; // from edge of sidebar to label
static const int kLabelMargin = 8; // from edge of label to text

QT_BEGIN_NAMESPACE

// These fudge terms were needed a few places to obtain pixel-perfect results
const int GapBetweenLogoAndRightEdge = 5;
const int ModernHeaderTopMargin = 2;
const int ClassicHMargin = 4;
const int MacButtonTopMargin = 13;
const int MacLayoutLeftMargin = 20;
//const int MacLayoutTopMargin = 14; // Unused. Save some space and avoid warning.
const int MacLayoutRightMargin = 20;
const int MacLayoutBottomMargin = 17;

static void changeSpacerSize(QLayout *layout, int index, int width, int height)
{
    QSpacerItem *spacer = layout->itemAt(index)->spacerItem();
    if (!spacer)
        return;
    spacer->changeSize(width, height);
}

static QWidget *iWantTheFocus(QWidget *ancestor)
{
    const int MaxIterations = 100;

    QWidget *candidate = ancestor;
    for (int i = 0; i < MaxIterations; ++i) {
        candidate = candidate->nextInFocusChain();
        if (!candidate)
            break;

        if (candidate->focusPolicy() & Qt::TabFocus) {
            if (candidate != ancestor && ancestor->isAncestorOf(candidate))
                return candidate;
        }
    }
    return 0;
}

static bool objectInheritsXAndXIsCloserThanY(const QObject *object, const QByteArray &classX,
                                             const QByteArray &classY)
{
    const QMetaObject *metaObject = object->metaObject();
    while (metaObject) {
        if (metaObject->className() == classX)
            return true;
        if (metaObject->className() == classY)
            return false;
        metaObject = metaObject->superClass();
    }
    return false;
}

const struct {
    const char className[16];
    const char property[13];
} fallbackProperties[] = {
    // If you modify this list, make sure to update the documentation (and the auto test)
    { "QAbstractButton", "checked" },
    { "QAbstractSlider", "value" },
    { "QComboBox", "currentIndex" },
    { "QDateTimeEdit", "dateTime" },
    { "QLineEdit", "text" },
    { "QListWidget", "currentRow" },
    { "QSpinBox", "value" },
};
const size_t NFallbackDefaultProperties = sizeof fallbackProperties / sizeof *fallbackProperties;

static const char *changed_signal(int which)
{
    // since it might expand to a runtime function call (to
    // qFlagLocations()), we cannot store the result of SIGNAL() in a
    // character array and expect it to be statically initialized. To
    // avoid the relocations caused by a char pointer table, use a
    // switch statement:
    switch (which) {
    case 0: return SIGNAL(toggled(bool));
    case 1: return SIGNAL(valueChanged(int));
    case 2: return SIGNAL(currentIndexChanged(int));
    case 3: return SIGNAL(dateTimeChanged(QDateTime));
    case 4: return SIGNAL(textChanged(QString));
    case 5: return SIGNAL(currentRowChanged(int));
    case 6: return SIGNAL(valueChanged(int));
    };
    Q_STATIC_ASSERT(7 == NFallbackDefaultProperties);
    Q_UNREACHABLE();
    return 0;
}

class QWizardDefaultProperty
{
public:
    QByteArray className;
    QByteArray property;
    QByteArray changedSignal;

    inline QWizardDefaultProperty() {}
    inline QWizardDefaultProperty(const char *className, const char *property,
                                   const char *changedSignal)
        : className(className), property(property), changedSignal(changedSignal) {}
};
Q_DECLARE_TYPEINFO(QWizardDefaultProperty, Q_MOVABLE_TYPE);

class QWizardField
{
public:
    inline QWizardField() {}
    QWizardField(VectorWizardPage *page, const QString &spec, QObject *object, const char *property,
                  const char *changedSignal);

    void resolve(const QVector<QWizardDefaultProperty> &defaultPropertyTable);
    void findProperty(const QWizardDefaultProperty *properties, int propertyCount);

    VectorWizardPage *page;
    QString name;
    bool mandatory;
    QObject *object;
    QByteArray property;
    QByteArray changedSignal;
    QVariant initialValue;
};
Q_DECLARE_TYPEINFO(QWizardField, Q_MOVABLE_TYPE);

QWizardField::QWizardField(VectorWizardPage *page, const QString &spec, QObject *object,
                           const char *property, const char *changedSignal)
    : page(page), name(spec), mandatory(false), object(object), property(property),
      changedSignal(changedSignal)
{
    if (name.endsWith(QLatin1Char('*'))) {
        name.chop(1);
        mandatory = true;
    }
}

void QWizardField::resolve(const QVector<QWizardDefaultProperty> &defaultPropertyTable)
{
    if (property.isEmpty())
        findProperty(defaultPropertyTable.constData(), defaultPropertyTable.count());
    initialValue = object->property(property);
}

void QWizardField::findProperty(const QWizardDefaultProperty *properties, int propertyCount)
{
    QByteArray className;

    for (int i = 0; i < propertyCount; ++i) {
        if (objectInheritsXAndXIsCloserThanY(object, properties[i].className, className)) {
            className = properties[i].className;
            property = properties[i].property;
            changedSignal = properties[i].changedSignal;
        }
    }
}

class QWizardLayoutInfo
{
public:
    inline QWizardLayoutInfo()
    : topLevelMarginLeft(-1), topLevelMarginRight(-1), topLevelMarginTop(-1),
      topLevelMarginBottom(-1), childMarginLeft(-1), childMarginRight(-1),
      childMarginTop(-1), childMarginBottom(-1), hspacing(-1), vspacing(-1),
      wizStyle(VectorWizard::ClassicStyle), header(false), watermark(false), title(false),
      subTitle(false), extension(false), sideWidget(false) {}

    int topLevelMarginLeft;
    int topLevelMarginRight;
    int topLevelMarginTop;
    int topLevelMarginBottom;
    int childMarginLeft;
    int childMarginRight;
    int childMarginTop;
    int childMarginBottom;
    int hspacing;
    int vspacing;
    int buttonSpacing;
    VectorWizard::WizardStyle wizStyle;
    bool header;
    bool watermark;
    bool title;
    bool subTitle;
    bool extension;
    bool sideWidget;

    bool operator==(const QWizardLayoutInfo &other);
    inline bool operator!=(const QWizardLayoutInfo &other) { return !operator==(other); }
};

bool QWizardLayoutInfo::operator==(const QWizardLayoutInfo &other)
{
    return topLevelMarginLeft == other.topLevelMarginLeft
           && topLevelMarginRight == other.topLevelMarginRight
           && topLevelMarginTop == other.topLevelMarginTop
           && topLevelMarginBottom == other.topLevelMarginBottom
           && childMarginLeft == other.childMarginLeft
           && childMarginRight == other.childMarginRight
           && childMarginTop == other.childMarginTop
           && childMarginBottom == other.childMarginBottom
           && hspacing == other.hspacing
           && vspacing == other.vspacing
           && buttonSpacing == other.buttonSpacing
           && wizStyle == other.wizStyle
           && header == other.header
           && watermark == other.watermark
           && title == other.title
           && subTitle == other.subTitle
           && extension == other.extension
           && sideWidget == other.sideWidget;
}

class QWizardHeader : public QWidget
{
public:
    enum RulerType { Ruler };

    inline QWizardHeader(RulerType /* ruler */, QWidget *parent = 0)
        : QWidget(parent) { setFixedHeight(2); }
    QWizardHeader(QWidget *parent = 0);

    void setup(const QWizardLayoutInfo &info, const QString &title,
               const QString &subTitle, const QPixmap &logo, const QPixmap &banner,
               Qt::TextFormat titleFormat, Qt::TextFormat subTitleFormat);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
private:
    bool vistaDisabled() const;
#endif
private:
    QLabel *titleLabel;
    QLabel *subTitleLabel;
    QLabel *logoLabel;
    QGridLayout *layout;
    QPixmap bannerPixmap;
};

QWizardHeader::QWizardHeader(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setBackgroundRole(QPalette::Base);

    titleLabel = new QLabel(this);
    titleLabel->setBackgroundRole(QPalette::Base);

    subTitleLabel = new QLabel(this);
    subTitleLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    subTitleLabel->setWordWrap(true);

    logoLabel = new QLabel(this);

    QFont font = titleLabel->font();
    font.setBold(true);
    titleLabel->setFont(font);

    layout = new QGridLayout(this);
    layout->setMargin(0);
    layout->setSpacing(0);

    layout->setRowMinimumHeight(3, 1);
    layout->setRowStretch(4, 1);

    layout->setColumnStretch(2, 1);
    layout->setColumnMinimumWidth(4, 2 * GapBetweenLogoAndRightEdge);
    layout->setColumnMinimumWidth(6, GapBetweenLogoAndRightEdge);

    layout->addWidget(titleLabel, 2, 1, 1, 2);
    layout->addWidget(subTitleLabel, 4, 2);
    layout->addWidget(logoLabel, 1, 5, 5, 1);
}

#if !defined(QT_NO_STYLE_WINDOWSVISTA)
bool QWizardHeader::vistaDisabled() const
{
    bool styleDisabled = false;
    QWizard *wiz = parentWidget() ? qobject_cast <QWizard *>(parentWidget()->parentWidget()) : 0;
    if (wiz) {
        // Designer dosen't support the Vista style for Wizards. This property is used to turn
        // off the Vista style.
        const QVariant v = wiz->property("_q_wizard_vista_off");
        styleDisabled = v.isValid() && v.toBool();
    }
    return styleDisabled;
}
#endif

void QWizardHeader::setup(const QWizardLayoutInfo &info, const QString &title,
                          const QString &subTitle, const QPixmap &logo, const QPixmap &banner,
                          Qt::TextFormat titleFormat, Qt::TextFormat subTitleFormat)
{
    bool modern = ((info.wizStyle == VectorWizard::ModernStyle)
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
        || ((info.wizStyle == VectorWizard::AeroStyle
            && QVistaHelper::vistaState() == QVistaHelper::Classic) || vistaDisabled())
#endif
    );

    layout->setRowMinimumHeight(0, modern ? ModernHeaderTopMargin : 0);
    layout->setRowMinimumHeight(1, modern ? info.topLevelMarginTop - ModernHeaderTopMargin - 1 : 0);
    layout->setRowMinimumHeight(6, (modern ? 3 : GapBetweenLogoAndRightEdge) + 2);

    int minColumnWidth0 = modern ? info.topLevelMarginLeft + info.topLevelMarginRight : 0;
    int minColumnWidth1 = modern ? info.topLevelMarginLeft + info.topLevelMarginRight + 1
                                 : info.topLevelMarginLeft + ClassicHMargin;
    layout->setColumnMinimumWidth(0, minColumnWidth0);
    layout->setColumnMinimumWidth(1, minColumnWidth1);

    titleLabel->setTextFormat(titleFormat);
    titleLabel->setText(title);
    logoLabel->setPixmap(logo);

    subTitleLabel->setTextFormat(subTitleFormat);
    subTitleLabel->setText(QLatin1String("Pq\nPq"));
    int desiredSubTitleHeight = subTitleLabel->sizeHint().height();
    subTitleLabel->setText(subTitle);

    if (modern) {
        bannerPixmap = banner;
    } else {
        bannerPixmap = QPixmap();
    }

    if (bannerPixmap.isNull()) {
        /*
            There is no widthForHeight() function, so we simulate it with a loop.
        */
        int candidateSubTitleWidth = qMin(512, 2 * QApplication::desktop()->width() / 3);
        int delta = candidateSubTitleWidth >> 1;
        while (delta > 0) {
            if (subTitleLabel->heightForWidth(candidateSubTitleWidth - delta)
                        <= desiredSubTitleHeight)
                candidateSubTitleWidth -= delta;
            delta >>= 1;
        }

        subTitleLabel->setMinimumSize(candidateSubTitleWidth, desiredSubTitleHeight);

        QSize size = layout->totalMinimumSize();
        setMinimumSize(size);
        setMaximumSize(QWIDGETSIZE_MAX, size.height());
    } else {
        subTitleLabel->setMinimumSize(0, 0);
        setFixedSize(banner.size() + QSize(0, 2));
    }
    updateGeometry();
}

void QWizardHeader::paintEvent(QPaintEvent * /* event */)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, bannerPixmap);

    int x = width() - 2;
    int y = height() - 2;
    const QPalette &pal = palette();
    painter.setPen(pal.mid().color());
    painter.drawLine(0, y, x, y);
    painter.setPen(pal.base().color());
    painter.drawPoint(x + 1, y);
    painter.drawLine(0, y + 1, x + 1, y + 1);
}

// We save one vtable by basing QWizardRuler on QWizardHeader
class QWizardRuler : public QWizardHeader
{
public:
    inline QWizardRuler(QWidget *parent = 0)
        : QWizardHeader(Ruler, parent) {}
};

class QWatermarkLabel : public QLabel
{
public:
    QWatermarkLabel(QWidget *parent, QWidget *sideWidget) : QLabel(parent), m_sideWidget(sideWidget) {
        m_layout = new QVBoxLayout(this);
        if (m_sideWidget)
            m_layout->addWidget(m_sideWidget);
    }

    QSize minimumSizeHint() const Q_DECL_OVERRIDE {
        if (!pixmap() && !pixmap()->isNull())
            return pixmap()->size();
        return QFrame::minimumSizeHint();
    }

    void setSideWidget(QWidget *widget) {
        if (m_sideWidget == widget)
            return;
        if (m_sideWidget) {
            m_layout->removeWidget(m_sideWidget);
            m_sideWidget->hide();
        }
        m_sideWidget = widget;
        if (m_sideWidget)
            m_layout->addWidget(m_sideWidget);
    }
    QWidget *sideWidget() const {
        return m_sideWidget;
    }
private:
    QVBoxLayout *m_layout;
    QWidget *m_sideWidget;
};

class VectorWizardPagePrivate : public QWidgetPrivate
{
    Q_DECLARE_PUBLIC(VectorWizardPage)

public:
    enum TriState { Tri_Unknown = -1, Tri_False, Tri_True };

    inline VectorWizardPagePrivate()
        : wizard(0), completeState(Tri_Unknown), explicitlyFinal(false), commit(false) {}

    bool cachedIsComplete() const;
    void _q_maybeEmitCompleteChanged();
    void _q_updateCachedCompleteState();

    VectorWizard *wizard;
    QString title;
    QString subTitle;
    QPixmap pixmaps[VectorWizard::NPixmaps];
    QVector<QWizardField> pendingFields;
    mutable TriState completeState;
    bool explicitlyFinal;
    bool commit;
    QMap<int, QString> buttonCustomTexts;
};

bool VectorWizardPagePrivate::cachedIsComplete() const
{
    Q_Q(const VectorWizardPage);
    if (completeState == Tri_Unknown)
        completeState = q->isComplete() ? Tri_True : Tri_False;
    return completeState == Tri_True;
}

void VectorWizardPagePrivate::_q_maybeEmitCompleteChanged()
{
    Q_Q(VectorWizardPage);
    TriState newState = q->isComplete() ? Tri_True : Tri_False;
    if (newState != completeState)
        emit q->completeChanged();
}

void VectorWizardPagePrivate::_q_updateCachedCompleteState()
{
    Q_Q(VectorWizardPage);
    completeState = q->isComplete() ? Tri_True : Tri_False;
}

class QWizardAntiFlickerWidget : public QWidget
{
public:
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    VectorWizardPrivate *wizardPrivate;
    QWizardAntiFlickerWidget(QWizard *wizard, VectorWizardPrivate *wizardPrivate)
        : QWidget(wizard)
        , wizardPrivate(wizardPrivate) {}
protected:
    void paintEvent(QPaintEvent *);
#else
    QWizardAntiFlickerWidget(VectorWizard *wizard, VectorWizardPrivate *)
        : QWidget(wizard)
    {}
#endif
};

class VectorWizardPrivate : public QDialogPrivate
{
    Q_DECLARE_PUBLIC(VectorWizard)

public:
    typedef QMap<int, VectorWizardPage *> PageMap;

    enum Direction {
        Backward,
        Forward
    };

    inline VectorWizardPrivate()
        : start(-1)
        , startSetByUser(false)
        , current(-1)
        , canContinue(false)
        , canFinish(false)
        , disableUpdatesCount(0)
        , wizStyle(VectorWizard::ClassicStyle)
        , opts(0)
        , buttonsHaveCustomLayout(false)
        , titleFmt(Qt::AutoText)
        , subTitleFmt(Qt::AutoText)
        , placeholderWidget1(0)
        , placeholderWidget2(0)
        , headerWidget(0)
        , watermarkLabel(0)
        , sideWidget(0)
        , pageFrame(0)
        , titleLabel(0)
        , subTitleLabel(0)
        , bottomRuler(0)
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
        , vistaHelper(0)
        , vistaInitPending(false)
        , vistaState(QVistaHelper::Dirty)
        , vistaStateChanged(false)
        , inHandleAeroStyleChange(false)
#endif
        , minimumWidth(0)
        , minimumHeight(0)
        , maximumWidth(QWIDGETSIZE_MAX)
        , maximumHeight(QWIDGETSIZE_MAX)
    {
        std::fill(btns, btns + VectorWizard::NButtons, static_cast<QAbstractButton *>(0));

#if !defined(QT_NO_STYLE_WINDOWSVISTA)
        if (QSysInfo::WindowsVersion >= QSysInfo::WV_VISTA
            && (QSysInfo::WindowsVersion & QSysInfo::WV_NT_based))
            vistaInitPending = true;
#endif
    }

    void init(const QString &logoFileName, const QString &bgFileName);
    void reset();
    void cleanupPagesNotInHistory();
    void addField(const QWizardField &field);
    void removeFieldAt(int index);
    void switchToPage(int newId, Direction direction);
    QWizardLayoutInfo layoutInfoForCurrentPage();
    void updateLayout();
    void updateCurrentPage();
    bool ensureButton(VectorWizard::WizardButton which) const;
    void connectButton(VectorWizard::WizardButton which) const;
    void updateButtonTexts();
    void updateButtonLayout();
    void setButtonLayout(const VectorWizard::WizardButton *array, int size);
    bool buttonLayoutContains(VectorWizard::WizardButton which);
    void updatePixmap(VectorWizard::WizardPixmap which);
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    bool vistaDisabled() const;
    bool isVistaThemeEnabled(QVistaHelper::VistaState state) const;
    bool handleAeroStyleChange();
#endif
    bool isVistaThemeEnabled() const;
    void disableUpdates();
    void enableUpdates();
    void _q_emitCustomButtonClicked();
    void _q_updateButtonStates();
    void _q_handleFieldObjectDestroyed(QObject *);
    void setStyle(QStyle *style);
#ifdef Q_OS_MACX
    static QPixmap findDefaultBackgroundPixmap();
#endif

    void setSidebarItems(const QList<QString> &items);
    void highlightSidebarItem(const QString &item);
    void setVersionInfo(const QString &versionInfo);
    void hideStepIndicator();

    PageMap pageMap;
    QVector<QWizardField> fields;
    QMap<QString, int> fieldIndexMap;
    QVector<QWizardDefaultProperty> defaultPropertyTable;
    QList<int> history;
    QSet<int> initialized; // ### remove and move bit to VectorWizardPage?
    int start;
    bool startSetByUser;
    int current;
    bool canContinue;
    bool canFinish;
    QWizardLayoutInfo layoutInfo;
    int disableUpdatesCount;

    VectorWizard::WizardStyle wizStyle;
    VectorWizard::WizardOptions opts;
    QMap<int, QString> buttonCustomTexts;
    bool buttonsHaveCustomLayout;
    QList<VectorWizard::WizardButton> buttonsCustomLayout;
    Qt::TextFormat titleFmt;
    Qt::TextFormat subTitleFmt;
    mutable QPixmap defaultPixmaps[VectorWizard::NPixmaps];

    union {
        // keep in sync with VectorWizard::WizardButton
        mutable struct {
            QAbstractButton *back;
            QAbstractButton *next;
            QAbstractButton *commit;
            QAbstractButton *finish;
            QAbstractButton *cancel;
            QAbstractButton *help;
        } btn;
        mutable QAbstractButton *btns[VectorWizard::NButtons];
    };
    QWizardAntiFlickerWidget *antiFlickerWidget;
    QWidget *placeholderWidget1;
    QWidget *placeholderWidget2;
    QWizardHeader *headerWidget;
    QWatermarkLabel *watermarkLabel;
    QWidget *sideWidget;
    QFrame *pageFrame;
    QLabel *titleLabel;
    QLabel *subTitleLabel;
    QWizardRuler *bottomRuler;

    QVBoxLayout *pageVBoxLayout;
    QHBoxLayout *buttonLayout;

    QVBoxLayout *mSidebarItemsLayout;
    BackgroundWidget *mHighlightWidget;
    QList<QLabel*> mSidebarLabels;
    QLabel *mVersionInfoLabel;
    StepIndicatorWidget *mStepIndicatorWidget;

#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    QVistaHelper *vistaHelper;
    bool vistaInitPending;
    QVistaHelper::VistaState vistaState;
    bool vistaStateChanged;
    bool inHandleAeroStyleChange;
#endif
    int minimumWidth;
    int minimumHeight;
    int maximumWidth;
    int maximumHeight;
};

static QString buttonDefaultText(int wstyle, int which, const VectorWizardPrivate *wizardPrivate)
{
#if defined(QT_NO_STYLE_WINDOWSVISTA)
    Q_UNUSED(wizardPrivate);
#endif
    const bool macStyle = (wstyle == VectorWizard::MacStyle);
    switch (which) {
    case VectorWizard::BackButton:
        return macStyle ? VectorWizard::tr("Go Back") : VectorWizard::tr("< &Back");
    case VectorWizard::NextButton:
        if (macStyle)
            return VectorWizard::tr("Continue");
        else
            return wizardPrivate->isVistaThemeEnabled()
                ? VectorWizard::tr("&Next") : VectorWizard::tr("&Next >");
    case VectorWizard::CommitButton:
        return VectorWizard::tr("Commit");
    case VectorWizard::FinishButton:
        return macStyle ? VectorWizard::tr("Done") : VectorWizard::tr("&Finish");
    case VectorWizard::CancelButton:
        return VectorWizard::tr("Cancel");
    case VectorWizard::HelpButton:
        return macStyle ? VectorWizard::tr("Help") : VectorWizard::tr("&Help");
    default:
        return QString();
    }
}

void VectorWizardPrivate::init(const QString &logoFileName, const QString &bgFileName)
{
    Q_Q(VectorWizard);

    // installer size will be fixed to the size of this image
    q->setBackground(QString::fromLatin1(":/vector/installer_bg.svg"), 0, false);

    // content holder
    antiFlickerWidget = new QWizardAntiFlickerWidget(q, this);

    // layout structure:
    // outermost layout is of the wizard, and it contains main layout (of content holder)
    // main layout is horizontal: sidebar on the left and everything else on the right
    // left sidebar is vertical: logo at top, sidebar items in the middle and version info at bottom
    // right area is vertical: page at top, then step indicator, and button area at bottom
    // button area is horizontal

    // outermost layout
    QHBoxLayout *dialogLayout = new QHBoxLayout(q);
    dialogLayout->setMargin(0);
    dialogLayout->setSpacing(6);

    // main horizontal layout
    QHBoxLayout *hMainLayout = new QHBoxLayout(antiFlickerWidget);
    hMainLayout->setContentsMargins(12, 10, 12, 10);
    hMainLayout->setSpacing(6);
    dialogLayout->addLayout(hMainLayout);

    // left
    BackgroundWidget *leftPanelBackground = new BackgroundWidget(antiFlickerWidget);
    leftPanelBackground->setBackground(QString::fromLatin1(":/vector/navigation_bg.svg"), 0, false);
    leftPanelBackground->setFixedSize(leftPanelBackground->sizeHint());
    hMainLayout->addWidget(leftPanelBackground);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanelBackground);
    leftLayout->setSpacing(0);
    leftLayout->setContentsMargins(kSidebarMargin, kSidebarMargin, kSidebarMargin, kSidebarMargin);
    hMainLayout->addStretch();

    // right
    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(kSidebarMargin);
    hMainLayout->addLayout(rightLayout);

    // page background
    BackgroundWidget *pageBackground = new BackgroundWidget(antiFlickerWidget);
    pageBackground->setBackground(bgFileName.isEmpty() ? QString::fromLatin1(":/vector/right_bg.svg") : bgFileName, 0, false);
    pageBackground->setFixedSize(pageBackground->sizeHint());
    rightLayout->addWidget(pageBackground);
    QVBoxLayout *pageLayout = new QVBoxLayout(pageBackground);
    pageLayout->setContentsMargins(2, 2, 2, 2); // border of background
    pageLayout->setSpacing(0);

    // page content
    pageFrame = new QFrame;
    pageFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    pageLayout->addWidget(pageFrame);
    pageVBoxLayout = new QVBoxLayout(pageFrame);
    pageVBoxLayout->setContentsMargins(0, 0, 0, 0);
    pageVBoxLayout->setSpacing(0);
    pageVBoxLayout->addSpacing(0);

    // step indicator
    mStepIndicatorWidget = new StepIndicatorWidget(antiFlickerWidget);
    rightLayout->addWidget(mStepIndicatorWidget);
    rightLayout->addStretch();

    // buttons
    buttonLayout = new QHBoxLayout;
    buttonLayout->setMargin(0);
    buttonLayout->setSpacing(6);
    rightLayout->addLayout(buttonLayout);
    
    // build left panel
    BackgroundWidget *logo = new BackgroundWidget(antiFlickerWidget);
    logo->setBackground(logoFileName.isEmpty() ? QString::fromLatin1(":/vector/installer_logo.svg") : logoFileName, 0, false);
    logo->setFixedSize(logo->sizeHint());
    QHBoxLayout *logoLayout = new QHBoxLayout;
    logoLayout->setSpacing(0);
    logoLayout->setContentsMargins(0, 0, 0, 0);
    logoLayout->addStretch();
    logoLayout->addWidget(logo);
    logoLayout->addStretch();
    leftLayout->addLayout(logoLayout);

    leftLayout->addSpacing(kSidebarMargin * 2);

    // create a frame for sidebar
    // the blue highlighted stripe will reside on this frame
    QFrame *sidebarFrame = new QFrame;
    sidebarFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftLayout->addWidget(sidebarFrame);
    mSidebarItemsLayout = new QVBoxLayout(sidebarFrame);
    mSidebarItemsLayout->setContentsMargins(0, 0, 0, 0);
    mSidebarItemsLayout->setSpacing(4);

    leftLayout->addStretch();

    mVersionInfoLabel = new QLabel(antiFlickerWidget);
    mVersionInfoLabel->setObjectName(QLatin1String("versionInfoLabel")); // this name is used in stylesheet.css, so don't change it
    mVersionInfoLabel->setContentsMargins(kLabelMargin, 0, kLabelMargin, 0);
    mVersionInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    leftLayout->addWidget(mVersionInfoLabel);

    // the position of this stripe will be adjusted later depending on current highlighted item on sidebar
    mHighlightWidget = new BackgroundWidget(sidebarFrame);
    mHighlightWidget->setBackground(QString::fromLatin1(":/vector/navigation_highlight.svg"), 0, false);
    mHighlightWidget->setFixedSize(mHighlightWidget->sizeHint());

    //
    wizStyle = VectorWizard::WizardStyle(q->style()->styleHint(QStyle::SH_WizardStyle, 0, q));
    if (wizStyle == VectorWizard::MacStyle) {
        opts = (VectorWizard::NoDefaultButton | VectorWizard::NoCancelButton);
    } else if (wizStyle == VectorWizard::ModernStyle) {
        opts = VectorWizard::HelpButtonOnRight;
    }

#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    vistaHelper = new QVistaHelper(q);
#endif

    // create these buttons right away; create the other buttons as necessary
    ensureButton(VectorWizard::BackButton);
    ensureButton(VectorWizard::NextButton);
    ensureButton(VectorWizard::CommitButton);
    ensureButton(VectorWizard::FinishButton);

    updateButtonLayout();

    defaultPropertyTable.reserve(NFallbackDefaultProperties);
    for (uint i = 0; i < NFallbackDefaultProperties; ++i)
        defaultPropertyTable.append(QWizardDefaultProperty(fallbackProperties[i].className,
                                                           fallbackProperties[i].property,
                                                           changed_signal(i)));

    //
    antiFlickerWidget->setFixedSize(antiFlickerWidget->minimumSizeHint());
    q->setFixedSize(q->sizeHint());
    q->addShadow();
}

void VectorWizardPrivate::setSidebarItems(const QList<QString> &items)
{
    for(const QString &item : items)
    {
        QLabel *label = new QLabel;
        label->setObjectName(QLatin1String("sidebarItemLabel")); // this name is used in stylesheet.css, so don't change it
        label->setText(item);
        label->setFixedSize(mHighlightWidget->size());
        label->setContentsMargins(kLabelMargin, 0, kLabelMargin, 0);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        mSidebarLabels.append(label);

        // push label to the left
        QHBoxLayout *layout = new QHBoxLayout;
        layout->setSpacing(0);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(label);
        layout->addStretch();
        mSidebarItemsLayout->addLayout(layout);
    }
}

void VectorWizardPrivate::highlightSidebarItem(const QString &item)
{
    for(QLabel *label : mSidebarLabels)
    {
        if(label->text() == item)
        {
            // move the blue highlighted stripe to the position of the label
            mHighlightWidget->move(mHighlightWidget->parentWidget()->mapFromGlobal(label->mapToGlobal(QPoint(0, 0))));
            break;
        }
    }
}

void VectorWizardPrivate::setVersionInfo(const QString &versionInfo)
{
    mVersionInfoLabel->setText(versionInfo);
}

void VectorWizardPrivate::hideStepIndicator()
{
    mStepIndicatorWidget->hide();
}

void VectorWizardPrivate::reset()
{
    Q_Q(VectorWizard);
    if (current != -1) {
        q->currentPage()->hide();
        cleanupPagesNotInHistory();
        for (int i = history.count() - 1; i >= 0; --i)
            q->cleanupPage(history.at(i));
        history.clear();
        initialized.clear();

        current = -1;
        emit q->currentIdChanged(-1);
    }
}

void VectorWizardPrivate::cleanupPagesNotInHistory()
{
    Q_Q(VectorWizard);

    const QSet<int> original = initialized;
    QSet<int>::const_iterator i = original.constBegin();
    QSet<int>::const_iterator end = original.constEnd();

    for (; i != end; ++i) {
        if (!history.contains(*i)) {
            q->cleanupPage(*i);
            initialized.remove(*i);
        }
    }
}

void VectorWizardPrivate::addField(const QWizardField &field)
{
    Q_Q(VectorWizard);

    QWizardField myField = field;
    myField.resolve(defaultPropertyTable);

    if (fieldIndexMap.contains(myField.name)) {
        qWarning("VectorWizardPage::addField: Duplicate field '%s'", qPrintable(myField.name));
        return;
    }

    fieldIndexMap.insert(myField.name, fields.count());
    fields += myField;
    if (myField.mandatory && !myField.changedSignal.isEmpty())
        QObject::connect(myField.object, myField.changedSignal,
                         myField.page, SLOT(_q_maybeEmitCompleteChanged()));
    QObject::connect(
        myField.object, SIGNAL(destroyed(QObject*)), q,
        SLOT(_q_handleFieldObjectDestroyed(QObject*)));
}

void VectorWizardPrivate::removeFieldAt(int index)
{
    Q_Q(VectorWizard);

    const QWizardField &field = fields.at(index);
    fieldIndexMap.remove(field.name);
    if (field.mandatory && !field.changedSignal.isEmpty())
        QObject::disconnect(field.object, field.changedSignal,
                            field.page, SLOT(_q_maybeEmitCompleteChanged()));
    QObject::disconnect(
        field.object, SIGNAL(destroyed(QObject*)), q,
        SLOT(_q_handleFieldObjectDestroyed(QObject*)));
    fields.remove(index);
}

void VectorWizardPrivate::switchToPage(int newId, Direction direction)
{
    Q_Q(VectorWizard);

    disableUpdates();

    int oldId = current;
    if (VectorWizardPage *oldPage = q->currentPage()) {
        oldPage->hide();

        if (direction == Backward) {
            if (!(opts & VectorWizard::IndependentPages)) {
                q->cleanupPage(oldId);
                initialized.remove(oldId);
            }
            Q_ASSERT(history.last() == oldId);
            history.removeLast();
            Q_ASSERT(history.last() == newId);
        }
    }

    current = newId;

    // update step indicator
    QList<int> pageIds = q->pageIds();
    mStepIndicatorWidget->setCurrentStep(pageIds.indexOf(current));

    VectorWizardPage *newPage = q->currentPage();
    if (newPage) {
        if (direction == Forward) {
            if (!initialized.contains(current)) {
                initialized.insert(current);
                q->initializePage(current);
            }
            history.append(current);
        }
        newPage->show();
    }

    canContinue = (q->nextId() != -1);
    canFinish = (newPage && newPage->isFinalPage());

    _q_updateButtonStates();
    updateButtonTexts();

    const VectorWizard::WizardButton nextOrCommit =
        newPage && newPage->isCommitPage() ? VectorWizard::CommitButton : VectorWizard::NextButton;
    QAbstractButton *nextOrFinishButton =
        btns[canContinue ? nextOrCommit : VectorWizard::FinishButton];
    QWidget *candidate = 0;

    /*
        If there is no default button and the Next or Finish button
        is enabled, give focus directly to it as a convenience to the
        user. This is the normal case on OS X.

        Otherwise, give the focus to the new page's first child that
        can handle it. If there is no such child, give the focus to
        Next or Finish.
    */
    if ((opts & VectorWizard::NoDefaultButton) && nextOrFinishButton->isEnabled()) {
        candidate = nextOrFinishButton;
    } else if (newPage) {
        candidate = iWantTheFocus(newPage);
    }
    if (!candidate)
        candidate = nextOrFinishButton;
    candidate->setFocus();

    if (wizStyle == VectorWizard::MacStyle)
        q->updateGeometry();

    enableUpdates();
    updateLayout();

    emit q->currentIdChanged(current);
}

// keep in sync with VectorWizard::WizardButton
static const char * buttonSlots(VectorWizard::WizardButton which)
{
    switch (which) {
    case VectorWizard::BackButton:
        return SLOT(back());
    case VectorWizard::NextButton:
    case VectorWizard::CommitButton:
        return SLOT(next());
    case VectorWizard::FinishButton:
        return SLOT(accept());
    case VectorWizard::CancelButton:
        return SLOT(reject());
    case VectorWizard::HelpButton:
        return SIGNAL(helpRequested());
    case VectorWizard::CustomButton1:
    case VectorWizard::CustomButton2:
    case VectorWizard::CustomButton3:
    case VectorWizard::Stretch:
    case VectorWizard::NoButton:
        Q_UNREACHABLE();
    };
    return 0;
};

QWizardLayoutInfo VectorWizardPrivate::layoutInfoForCurrentPage()
{
    Q_Q(VectorWizard);
    QStyle *style = q->style();

    QWizardLayoutInfo info;

    const int layoutHorizontalSpacing = style->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
    info.topLevelMarginLeft = style->pixelMetric(QStyle::PM_LayoutLeftMargin, 0, q);
    info.topLevelMarginRight = style->pixelMetric(QStyle::PM_LayoutRightMargin, 0, q);
    info.topLevelMarginTop = style->pixelMetric(QStyle::PM_LayoutTopMargin, 0, q);
    info.topLevelMarginBottom = style->pixelMetric(QStyle::PM_LayoutBottomMargin, 0, q);
    info.childMarginLeft = style->pixelMetric(QStyle::PM_LayoutLeftMargin, 0, titleLabel);
    info.childMarginRight = style->pixelMetric(QStyle::PM_LayoutRightMargin, 0, titleLabel);
    info.childMarginTop = style->pixelMetric(QStyle::PM_LayoutTopMargin, 0, titleLabel);
    info.childMarginBottom = style->pixelMetric(QStyle::PM_LayoutBottomMargin, 0, titleLabel);
    info.hspacing = (layoutHorizontalSpacing == -1)
        ? style->layoutSpacing(QSizePolicy::DefaultType, QSizePolicy::DefaultType, Qt::Horizontal)
        : layoutHorizontalSpacing;
    info.vspacing = style->pixelMetric(QStyle::PM_LayoutVerticalSpacing);
    info.buttonSpacing = (layoutHorizontalSpacing == -1)
        ? style->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal)
        : layoutHorizontalSpacing;

    if (wizStyle == VectorWizard::MacStyle)
        info.buttonSpacing = 12;

    info.wizStyle = wizStyle;
    if (info.wizStyle == VectorWizard::AeroStyle
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
        && (QVistaHelper::vistaState() == QVistaHelper::Classic || vistaDisabled())
#endif
        )
        info.wizStyle = VectorWizard::ModernStyle;

    QString titleText;
    QString subTitleText;
    QPixmap backgroundPixmap;
    QPixmap watermarkPixmap;

    if (VectorWizardPage *page = q->currentPage()) {
        titleText = page->title();
        subTitleText = page->subTitle();
        backgroundPixmap = page->pixmap(VectorWizard::BackgroundPixmap);
        watermarkPixmap = page->pixmap(VectorWizard::WatermarkPixmap);
    }

    info.header = (info.wizStyle == VectorWizard::ClassicStyle || info.wizStyle == VectorWizard::ModernStyle)
        && !(opts & VectorWizard::IgnoreSubTitles) && !subTitleText.isEmpty();
    info.sideWidget = sideWidget;
    info.watermark = (info.wizStyle != VectorWizard::MacStyle) && (info.wizStyle != VectorWizard::AeroStyle)
        && !watermarkPixmap.isNull();
    info.title = !info.header && !titleText.isEmpty();
    info.subTitle = !(opts & VectorWizard::IgnoreSubTitles) && !info.header && !subTitleText.isEmpty();
    info.extension = (info.watermark || info.sideWidget) && (opts & VectorWizard::ExtendedWatermarkPixmap);

    return info;
}

void VectorWizardPrivate::updateLayout()
{
    Q_Q(VectorWizard);

    disableUpdates();

    QWizardLayoutInfo info = layoutInfoForCurrentPage();
    if(layoutInfo != info)
        layoutInfo = info;

    VectorWizardPage *page = q->currentPage();

    // If the page can expand vertically, let it stretch "infinitely" more
    // than the QSpacerItem at the bottom. Otherwise, let the QSpacerItem
    // stretch "infinitely" more than the page. Change the bottom item's
    // policy accordingly. The case that the page has no layout is basically
    // for Designer, only.
    if (page) {
        bool expandPage = !page->layout();
        if (!expandPage) {
            const QLayoutItem *pageItem = pageVBoxLayout->itemAt(pageVBoxLayout->indexOf(page));
            expandPage = pageItem->expandingDirections() & Qt::Vertical;
        }
        QSpacerItem *bottomSpacer = pageVBoxLayout->itemAt(pageVBoxLayout->count() -  1)->spacerItem();
        Q_ASSERT(bottomSpacer);
        bottomSpacer->changeSize(0, 0, QSizePolicy::Ignored, expandPage ? QSizePolicy::Ignored : QSizePolicy::MinimumExpanding);
        pageVBoxLayout->invalidate();
    }

    if (info.header) {
        Q_ASSERT(page);
        headerWidget->setup(info, page->title(), page->subTitle(),
                            page->pixmap(VectorWizard::LogoPixmap), page->pixmap(VectorWizard::BannerPixmap),
                            titleFmt, subTitleFmt);
    }

    if (info.watermark || info.sideWidget) {
        QPixmap pix;
        if (info.watermark) {
            if (page)
                pix = page->pixmap(VectorWizard::WatermarkPixmap);
            else
                pix = q->pixmap(VectorWizard::WatermarkPixmap);
        }
        watermarkLabel->setPixmap(pix); // in case there is no watermark and we show the side widget we need to clear the watermark
    }

    if (info.title) {
        Q_ASSERT(page);
        titleLabel->setTextFormat(titleFmt);
        titleLabel->setText(page->title());
    }
    if (info.subTitle) {
        Q_ASSERT(page);
        subTitleLabel->setTextFormat(subTitleFmt);
        subTitleLabel->setText(page->subTitle());
    }

    enableUpdates();
}

void VectorWizardPrivate::updateCurrentPage()
{
    Q_Q(VectorWizard);
    if (q->currentPage()) {
        canContinue = (q->nextId() != -1);
        canFinish = q->currentPage()->isFinalPage();
    } else {
        canContinue = false;
        canFinish = false;
    }
    _q_updateButtonStates();
    updateButtonTexts();
}

static QString object_name_for_button(VectorWizard::WizardButton which)
{
    switch (which) {
    case VectorWizard::CommitButton:
        return QLatin1String("qt_wizard_") + QLatin1String("commit");
    case VectorWizard::FinishButton:
        return QLatin1String("qt_wizard_") + QLatin1String("finish");
    case VectorWizard::CancelButton:
        return QLatin1String("qt_wizard_") + QLatin1String("cancel");
    case VectorWizard::BackButton:
    case VectorWizard::NextButton:
    case VectorWizard::HelpButton:
    case VectorWizard::CustomButton1:
    case VectorWizard::CustomButton2:
    case VectorWizard::CustomButton3:
        // Make navigation buttons detectable as passive interactor in designer
        return QLatin1String("__qt__passive_wizardbutton") + QString::number(which);
    case VectorWizard::Stretch:
    case VectorWizard::NoButton:
    //case VectorWizard::NStandardButtons:
    //case VectorWizard::NButtons:
        ;
    }
    Q_UNREACHABLE();
    return QString();
}

bool VectorWizardPrivate::ensureButton(VectorWizard::WizardButton which) const
{
    Q_Q(const VectorWizard);
    if (uint(which) >= VectorWizard::NButtons)
        return false;

    if (!btns[which]) {
        Button *pushButton = new Button(antiFlickerWidget);

        QStyle *style = q->style();
        if (style != QApplication::style()) // Propagate style
            pushButton->setStyle(style);
        pushButton->setObjectName(object_name_for_button(which));

#ifndef LUMIT_INSTALLER
        // we don't need this code below anymore since now it's not QPushButton
#ifdef Q_OS_MACX
        pushButton->setAutoDefault(false);
#endif
#endif

        pushButton->hide();
#ifdef Q_CC_HPACC
        const_cast<VectorWizardPrivate *>(this)->btns[which] = pushButton;
#else
        btns[which] = pushButton;
#endif
        if (which < VectorWizard::NStandardButtons)
            pushButton->setText(buttonDefaultText(wizStyle, which, this));

        connectButton(which);
    }
    return true;
}

void VectorWizardPrivate::connectButton(VectorWizard::WizardButton which) const
{
    Q_Q(const VectorWizard);
    if (which < VectorWizard::NStandardButtons) {
        QObject::connect(btns[which], SIGNAL(clicked()), q, buttonSlots(which));
    } else {
        QObject::connect(btns[which], SIGNAL(clicked()), q, SLOT(_q_emitCustomButtonClicked()));
    }
}

void VectorWizardPrivate::updateButtonTexts()
{
    Q_Q(VectorWizard);
    for (int i = 0; i < VectorWizard::NButtons; ++i) {
        if (btns[i]) {
            if (q->currentPage() && (q->currentPage()->d_func()->buttonCustomTexts.contains(i)))
                btns[i]->setText(q->currentPage()->d_func()->buttonCustomTexts.value(i));
            else if (buttonCustomTexts.contains(i))
                btns[i]->setText(buttonCustomTexts.value(i));
            else if (i < VectorWizard::NStandardButtons)
                btns[i]->setText(buttonDefaultText(wizStyle, i, this));
        }
    }
    // Vista: Add shortcut for 'next'. Note: native dialogs use ALT-Right
    // even in RTL mode, so do the same, even if it might be counter-intuitive.
    // The shortcut for 'back' is set in class QVistaBackButton.
    if (btns[VectorWizard::NextButton])
        btns[VectorWizard::NextButton]->setShortcut(isVistaThemeEnabled() ? QKeySequence(Qt::ALT | Qt::Key_Right) : QKeySequence());
}

void VectorWizardPrivate::updateButtonLayout()
{
    if (buttonsHaveCustomLayout) {
        QVarLengthArray<VectorWizard::WizardButton, VectorWizard::NButtons> array(buttonsCustomLayout.count());
        for (int i = 0; i < buttonsCustomLayout.count(); ++i)
            array[i] = buttonsCustomLayout.at(i);
        setButtonLayout(array.constData(), array.count());
    } else {
        // Positions:
        //     Help Stretch Custom1 Custom2 Custom3 Cancel Back Next Commit Finish Cancel Help

        const int ArraySize = 12;
        VectorWizard::WizardButton array[ArraySize];
        memset(array, -1, sizeof(array));
        Q_ASSERT(array[0] == VectorWizard::NoButton);

        if (opts & VectorWizard::HaveHelpButton) {
            int i = (opts & VectorWizard::HelpButtonOnRight) ? 11 : 0;
            array[i] = VectorWizard::HelpButton;
        }
        array[1] = VectorWizard::Stretch;
        if (opts & VectorWizard::HaveCustomButton1)
            array[2] = VectorWizard::CustomButton1;
        if (opts & VectorWizard::HaveCustomButton2)
            array[3] = VectorWizard::CustomButton2;
        if (opts & VectorWizard::HaveCustomButton3)
            array[4] = VectorWizard::CustomButton3;

        if (!(opts & VectorWizard::NoCancelButton)) {
            int i = (opts & VectorWizard::CancelButtonOnLeft) ? 5 : 10;
            array[i] = VectorWizard::CancelButton;
        }
        array[6] = VectorWizard::BackButton;
        array[7] = VectorWizard::NextButton;
        array[8] = VectorWizard::CommitButton;
        array[9] = VectorWizard::FinishButton;

        setButtonLayout(array, ArraySize);
    }
}

void VectorWizardPrivate::setButtonLayout(const VectorWizard::WizardButton *array, int size)
{
    QWidget *prev = pageFrame;

    for (int i = buttonLayout->count() - 1; i >= 0; --i) {
        QLayoutItem *item = buttonLayout->takeAt(i);
        if (QWidget *widget = item->widget())
            widget->hide();
        delete item;
    }

    for (int i = 0; i < size; ++i) {
        VectorWizard::WizardButton which = array[i];
        if (which == VectorWizard::Stretch) {
            buttonLayout->addStretch(1);
        } else if (which != VectorWizard::NoButton) {
            ensureButton(which);
            buttonLayout->addWidget(btns[which]);

            // Back, Next, Commit, and Finish are handled in _q_updateButtonStates()
            if (which != VectorWizard::BackButton && which != VectorWizard::NextButton
                && which != VectorWizard::CommitButton && which != VectorWizard::FinishButton)
                btns[which]->show();

            if (prev)
                QWidget::setTabOrder(prev, btns[which]);
            prev = btns[which];
        }
    }

    _q_updateButtonStates();
}

bool VectorWizardPrivate::buttonLayoutContains(VectorWizard::WizardButton which)
{
    return !buttonsHaveCustomLayout || buttonsCustomLayout.contains(which);
}

void VectorWizardPrivate::updatePixmap(VectorWizard::WizardPixmap which)
{
    Q_Q(VectorWizard);
    if (which == VectorWizard::BackgroundPixmap) {
        if (wizStyle == VectorWizard::MacStyle) {
            q->update();
            q->updateGeometry();
        }
    } else {
        updateLayout();
    }
}

#if !defined(QT_NO_STYLE_WINDOWSVISTA)
bool VectorWizardPrivate::vistaDisabled() const
{
    Q_Q(const VectorWizard);
    const QVariant v = q->property("_q_wizard_vista_off");
    return v.isValid() && v.toBool();
}

bool VectorWizardPrivate::isVistaThemeEnabled(QVistaHelper::VistaState state) const
{
    return wizStyle == VectorWizard::AeroStyle
        && QVistaHelper::vistaState() == state
        && !vistaDisabled();
}

bool VectorWizardPrivate::handleAeroStyleChange()
{
    Q_Q(VectorWizard);

    if (inHandleAeroStyleChange)
        return false; // prevent recursion
    // For top-level wizards, we need the platform window handle for the
    // DWM changes. Delay aero initialization to the show event handling if
    // it does not exist. If we are a child, skip DWM and just make room by
    // moving the antiFlickerWidget.
    const bool isWindow = q->isWindow();
    if (isWindow && (!q->windowHandle() || !q->windowHandle()->handle()))
        return false;
    inHandleAeroStyleChange = true;

    vistaHelper->disconnectBackButton();
    q->removeEventFilter(vistaHelper);

    bool vistaMargins = false;

    if (isVistaThemeEnabled()) {
        if (isVistaThemeEnabled(QVistaHelper::VistaAero)) {
            if (isWindow) {
                vistaHelper->setDWMTitleBar(QVistaHelper::ExtendedTitleBar);
                q->installEventFilter(vistaHelper);
            }
            q->setMouseTracking(true);
            antiFlickerWidget->move(0, vistaHelper->titleBarSize() + vistaHelper->topOffset());
            vistaHelper->backButton()->move(
                0, vistaHelper->topOffset() // ### should ideally work without the '+ 1'
                - qMin(vistaHelper->topOffset(), vistaHelper->topPadding() + 1));
            vistaMargins = true;
            vistaHelper->backButton()->show();
        } else {
            if (isWindow)
                vistaHelper->setDWMTitleBar(QVistaHelper::NormalTitleBar);
            q->setMouseTracking(true);
            antiFlickerWidget->move(0, vistaHelper->topOffset());
            vistaHelper->backButton()->move(0, -1); // ### should ideally work with (0, 0)
        }
        if (isWindow)
            vistaHelper->setTitleBarIconAndCaptionVisible(false);
        QObject::connect(
            vistaHelper->backButton(), SIGNAL(clicked()), q, buttonSlots(VectorWizard::BackButton));
        vistaHelper->backButton()->show();
    } else {
        q->setMouseTracking(true); // ### original value possibly different
#ifndef QT_NO_CURSOR
        q->unsetCursor(); // ### ditto
#endif
        antiFlickerWidget->move(0, 0);
        vistaHelper->hideBackButton();
        if (isWindow)
            vistaHelper->setTitleBarIconAndCaptionVisible(true);
    }

    _q_updateButtonStates();

    vistaHelper->updateCustomMargins(vistaMargins);

    inHandleAeroStyleChange = false;
    return true;
}
#endif

bool VectorWizardPrivate::isVistaThemeEnabled() const
{
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    return isVistaThemeEnabled(QVistaHelper::VistaAero)
        || isVistaThemeEnabled(QVistaHelper::VistaBasic);
#else
    return false;
#endif
}

void VectorWizardPrivate::disableUpdates()
{
    Q_Q(VectorWizard);
    if (disableUpdatesCount++ == 0) {
        q->setUpdatesEnabled(false);
        antiFlickerWidget->hide();
    }
}

void VectorWizardPrivate::enableUpdates()
{
    Q_Q(VectorWizard);
    if (--disableUpdatesCount == 0) {
        antiFlickerWidget->show();
        q->setUpdatesEnabled(true);
    }
}

void VectorWizardPrivate::_q_emitCustomButtonClicked()
{
    Q_Q(VectorWizard);
    QObject *button = q->sender();
    for (int i = VectorWizard::NStandardButtons; i < VectorWizard::NButtons; ++i) {
        if (btns[i] == button) {
            emit q->customButtonClicked(VectorWizard::WizardButton(i));
            break;
        }
    }
}

void VectorWizardPrivate::_q_updateButtonStates()
{
    Q_Q(VectorWizard);

    disableUpdates();

    const VectorWizardPage *page = q->currentPage();
    bool complete = page && page->isComplete();

    btn.back->setEnabled(history.count() > 1
                         && !q->page(history.at(history.count() - 2))->isCommitPage()
                         && (!canFinish || !(opts & VectorWizard::DisabledBackButtonOnLastPage)));
    btn.next->setEnabled(canContinue && complete);
    btn.commit->setEnabled(canContinue && complete);
    btn.finish->setEnabled(canFinish && complete);

    const bool backButtonVisible = buttonLayoutContains(VectorWizard::BackButton)
        && (history.count() > 1 || !(opts & VectorWizard::NoBackButtonOnStartPage))
        && (canContinue || !(opts & VectorWizard::NoBackButtonOnLastPage));
    bool commitPage = page && page->isCommitPage();
    btn.back->setVisible(backButtonVisible);
    btn.next->setVisible(buttonLayoutContains(VectorWizard::NextButton) && !commitPage
                         && (canContinue || (opts & VectorWizard::HaveNextButtonOnLastPage)));
    btn.commit->setVisible(buttonLayoutContains(VectorWizard::CommitButton) && commitPage
                           && canContinue);
    btn.finish->setVisible(buttonLayoutContains(VectorWizard::FinishButton)
                           && (canFinish || (opts & VectorWizard::HaveFinishButtonOnEarlyPages)));

    if (!(opts & VectorWizard::NoCancelButton))
        btn.cancel->setVisible(buttonLayoutContains(VectorWizard::CancelButton)
                               && (canContinue || !(opts & VectorWizard::NoCancelButtonOnLastPage)));

    bool useDefault = !(opts & VectorWizard::NoDefaultButton);
    if (QPushButton *nextPush = qobject_cast<QPushButton *>(btn.next))
        nextPush->setDefault(canContinue && useDefault && !commitPage);
    if (QPushButton *commitPush = qobject_cast<QPushButton *>(btn.commit))
        commitPush->setDefault(canContinue && useDefault && commitPage);
    if (QPushButton *finishPush = qobject_cast<QPushButton *>(btn.finish))
        finishPush->setDefault(!canContinue && useDefault);

#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    if (isVistaThemeEnabled()) {
        vistaHelper->backButton()->setEnabled(btn.back->isEnabled());
        vistaHelper->backButton()->setVisible(backButtonVisible);
        btn.back->setVisible(false);
    }
#endif

    enableUpdates();
}

void VectorWizardPrivate::_q_handleFieldObjectDestroyed(QObject *object)
{
    int destroyed_index = -1;
    QVector<QWizardField>::iterator it = fields.begin();
    while (it != fields.end()) {
        const QWizardField &field = *it;
        if (field.object == object) {
            destroyed_index = fieldIndexMap.value(field.name, -1);
            fieldIndexMap.remove(field.name);
            it = fields.erase(it);
        } else {
            ++it;
        }
    }
    if (destroyed_index != -1) {
        QMap<QString, int>::iterator it2 = fieldIndexMap.begin();
        while (it2 != fieldIndexMap.end()) {
            int index = it2.value();
            if (index > destroyed_index) {
                QString field_name = it2.key();
                fieldIndexMap.insert(field_name, index-1);
            }
            ++it2;
        }
    }
}

void VectorWizardPrivate::setStyle(QStyle *style)
{
    for (int i = 0; i < VectorWizard::NButtons; i++)
        if (btns[i])
            btns[i]->setStyle(style);
    const PageMap::const_iterator pcend = pageMap.constEnd();
    for (PageMap::const_iterator it = pageMap.constBegin(); it != pcend; ++it)
        it.value()->setStyle(style);
}

#ifdef Q_OS_MACX

QPixmap VectorWizardPrivate::findDefaultBackgroundPixmap()
{
    QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!app)
        return QPixmap();
    QPlatformNativeInterface *platformNativeInterface = app->platformNativeInterface();
    int at = platformNativeInterface->metaObject()->indexOfMethod("defaultBackgroundPixmapForQWizard()");
    if (at == -1)
        return QPixmap();
    QMetaMethod defaultBackgroundPixmapForQWizard = platformNativeInterface->metaObject()->method(at);
    QPixmap result;
    if (!defaultBackgroundPixmapForQWizard.invoke(platformNativeInterface, Q_RETURN_ARG(QPixmap, result)))
        return QPixmap();
    return result;
}

#endif

#if !defined(QT_NO_STYLE_WINDOWSVISTA)
void QWizardAntiFlickerWidget::paintEvent(QPaintEvent *)
{
    if (wizardPrivate->isVistaThemeEnabled()) {
        int leftMargin, topMargin, rightMargin, bottomMargin;
        wizardPrivate->buttonLayout->getContentsMargins(
            &leftMargin, &topMargin, &rightMargin, &bottomMargin);
        const int buttonLayoutTop = wizardPrivate->buttonLayout->contentsRect().top() - topMargin;
        QPainter painter(this);
        const QBrush brush(QColor(240, 240, 240)); // ### hardcoded for now
        painter.fillRect(0, buttonLayoutTop, width(), height() - buttonLayoutTop, brush);
        painter.setPen(QPen(QBrush(QColor(223, 223, 223)), 0)); // ### hardcoded for now
        painter.drawLine(0, buttonLayoutTop, width(), buttonLayoutTop);
        if (wizardPrivate->isVistaThemeEnabled(QVistaHelper::VistaBasic)) {
            if (window()->isActiveWindow())
                painter.setPen(QPen(QBrush(QColor(169, 191, 214)), 0)); // ### hardcoded for now
            else
                painter.setPen(QPen(QBrush(QColor(182, 193, 204)), 0)); // ### hardcoded for now
            painter.drawLine(0, 0, width(), 0);
        }
    }
}
#endif

/*!
    \class QWizard
    \since 4.3
    \brief The QWizard class provides a framework for wizards.

    \inmodule QtWidgets

    A wizard (also called an assistant on OS X) is a special type
    of input dialog that consists of a sequence of pages. A wizard's
    purpose is to guide the user through a process step by step.
    Wizards are useful for complex or infrequent tasks that users may
    find difficult to learn.

    QWizard inherits QDialog and represents a wizard. Each page is a
    VectorWizardPage (a QWidget subclass). To create your own wizards, you
    can use these classes directly, or you can subclass them for more
    control.

    Topics:

    \tableofcontents

    \section1 A Trivial Example

    The following example illustrates how to create wizard pages and
    add them to a wizard. For more advanced examples, see
    \l{dialogs/classwizard}{Class Wizard} and \l{dialogs/licensewizard}{License
    Wizard}.

    \snippet dialogs/trivialwizard/trivialwizard.cpp 1
    \snippet dialogs/trivialwizard/trivialwizard.cpp 3
    \dots
    \snippet dialogs/trivialwizard/trivialwizard.cpp 4
    \codeline
    \snippet dialogs/trivialwizard/trivialwizard.cpp 5
    \snippet dialogs/trivialwizard/trivialwizard.cpp 7
    \dots
    \snippet dialogs/trivialwizard/trivialwizard.cpp 8
    \codeline
    \snippet dialogs/trivialwizard/trivialwizard.cpp 10

    \section1 Wizard Look and Feel

    QWizard supports four wizard looks:

    \list
    \li ClassicStyle
    \li ModernStyle
    \li MacStyle
    \li AeroStyle
    \endlist

    You can explicitly set the look to use using setWizardStyle()
    (e.g., if you want the same look on all platforms).

    \table
    \header \li ClassicStyle
            \li ModernStyle
            \li MacStyle
            \li AeroStyle
    \row    \li \inlineimage qtwizard-classic1.png
            \li \inlineimage qtwizard-modern1.png
            \li \inlineimage qtwizard-mac1.png
            \li \inlineimage qtwizard-aero1.png
    \row    \li \inlineimage qtwizard-classic2.png
            \li \inlineimage qtwizard-modern2.png
            \li \inlineimage qtwizard-mac2.png
            \li \inlineimage qtwizard-aero2.png
    \endtable

    Note: AeroStyle has effect only on a Windows Vista system with alpha compositing enabled.
    ModernStyle is used as a fallback when this condition is not met.

    In addition to the wizard style, there are several options that
    control the look and feel of the wizard. These can be set using
    setOption() or setOptions(). For example, HaveHelpButton makes
    QWizard show a \uicontrol Help button along with the other wizard
    buttons.

    You can even change the order of the wizard buttons to any
    arbitrary order using setButtonLayout(), and you can add up to
    three custom buttons (e.g., a \uicontrol Print button) to the button
    row. This is achieved by calling setButton() or setButtonText()
    with CustomButton1, CustomButton2, or CustomButton3 to set up the
    button, and by enabling the HaveCustomButton1, HaveCustomButton2,
    or HaveCustomButton3 options. Whenever the user clicks a custom
    button, customButtonClicked() is emitted. For example:

    \snippet dialogs/licensewizard/licensewizard.cpp 29

    \section1 Elements of a Wizard Page

    Wizards consist of a sequence of \l{VectorWizardPage}s. At any time,
    only one page is shown. A page has the following attributes:

    \list
    \li A \l{VectorWizardPage::}{title}.
    \li A \l{VectorWizardPage::}{subTitle}.
    \li A set of pixmaps, which may or may not be honored, depending
       on the wizard's style:
        \list
        \li WatermarkPixmap (used by ClassicStyle and ModernStyle)
        \li BannerPixmap (used by ModernStyle)
        \li LogoPixmap (used by ClassicStyle and ModernStyle)
        \li BackgroundPixmap (used by MacStyle)
        \endlist
    \endlist

    The diagram belows shows how QWizard renders these attributes,
    assuming they are all present and ModernStyle is used:

    \image qtwizard-nonmacpage.png

    When a \l{VectorWizardPage::}{subTitle} is set, QWizard displays it
    in a header, in which case it also uses the BannerPixmap and the
    LogoPixmap to decorate the header. The WatermarkPixmap is
    displayed on the left side, below the header. At the bottom,
    there is a row of buttons allowing the user to navigate through
    the pages.

    The page itself (the \l{VectorWizardPage} widget) occupies the area
    between the header, the watermark, and the button row. Typically,
    the page is a VectorWizardPage on which a QGridLayout is installed,
    with standard child widgets (\l{QLabel}s, \l{QLineEdit}s, etc.).

    If the wizard's style is MacStyle, the page looks radically
    different:

    \image qtwizard-macpage.png

    The watermark, banner, and logo pixmaps are ignored by the
    MacStyle. If the BackgroundPixmap is set, it is used as the
    background for the wizard; otherwise, a default "assistant" image
    is used.

    The title and subtitle are set by calling
    VectorWizardPage::setTitle() and VectorWizardPage::setSubTitle() on the
    individual pages. They may be plain text or HTML (see titleFormat
    and subTitleFormat). The pixmaps can be set globally for the
    entire wizard using setPixmap(), or on a per-page basis using
    VectorWizardPage::setPixmap().

    \target field mechanism
    \section1 Registering and Using Fields

    In many wizards, the contents of a page may affect the default
    values of the fields of a later page. To make it easy to
    communicate between pages, QWizard supports a "field" mechanism
    that allows you to register a field (e.g., a QLineEdit) on a page
    and to access its value from any page. It is also possible to
    specify mandatory fields (i.e., fields that must be filled before
    the user can advance to the next page).

    To register a field, call VectorWizardPage::registerField() field.
    For example:

    \snippet dialogs/classwizard/classwizard.cpp 8
    \dots
    \snippet dialogs/classwizard/classwizard.cpp 10
    \snippet dialogs/classwizard/classwizard.cpp 11
    \dots
    \snippet dialogs/classwizard/classwizard.cpp 13

    The above code registers three fields, \c className, \c
    baseClass, and \c qobjectMacro, which are associated with three
    child widgets. The asterisk (\c *) next to \c className denotes a
    mandatory field.

    \target initialize page
    The fields of any page are accessible from any other page. For
    example:

    \snippet dialogs/classwizard/classwizard.cpp 17

    Here, we call VectorWizardPage::field() to access the contents of the
    \c className field (which was defined in the \c ClassInfoPage)
    and use it to initialize the \c OutputFilePage. The field's
    contents is returned as a QVariant.

    When we create a field using VectorWizardPage::registerField(), we
    pass a unique field name and a widget. We can also provide a Qt
    property name and a "changed" signal (a signal that is emitted
    when the property changes) as third and fourth arguments;
    however, this is not necessary for the most common Qt widgets,
    such as QLineEdit, QCheckBox, and QComboBox, because QWizard
    knows which properties to look for.

    \target mandatory fields

    If an asterisk (\c *) is appended to the name when the property
    is registered, the field is a \e{mandatory field}. When a page has
    mandatory fields, the \uicontrol Next and/or \uicontrol Finish buttons are
    enabled only when all mandatory fields are filled.

    To consider a field "filled", QWizard simply checks that the
    field's current value doesn't equal the original value (the value
    it had when initializePage() was called). For QLineEdit and
    QAbstractSpinBox subclasses, QWizard also checks that
    \l{QLineEdit::hasAcceptableInput()}{hasAcceptableInput()} returns
    true, to honor any validator or mask.

    QWizard's mandatory field mechanism is provided for convenience.
    A more powerful (but also more cumbersome) alternative is to
    reimplement VectorWizardPage::isComplete() and to emit the
    VectorWizardPage::completeChanged() signal whenever the page becomes
    complete or incomplete.

    The enabled/disabled state of the \uicontrol Next and/or \uicontrol Finish
    buttons is one way to perform validation on the user input.
    Another way is to reimplement validateCurrentPage() (or
    VectorWizardPage::validatePage()) to perform some last-minute
    validation (and show an error message if the user has entered
    incomplete or invalid information). If the function returns \c true,
    the next page is shown (or the wizard finishes); otherwise, the
    current page stays up.

    \section1 Creating Linear Wizards

    Most wizards have a linear structure, with page 1 followed by
    page 2 and so on until the last page. The \l{dialogs/classwizard}{Class
    Wizard} example is such a wizard. With QWizard, linear wizards
    are created by instantiating the \l{VectorWizardPage}s and inserting
    them using addPage(). By default, the pages are shown in the
    order in which they were added. For example:

    \snippet dialogs/classwizard/classwizard.cpp 0
    \dots
    \snippet dialogs/classwizard/classwizard.cpp 2

    When a page is about to be shown, QWizard calls initializePage()
    (which in turn calls VectorWizardPage::initializePage()) to fill the
    page with default values. By default, this function does nothing,
    but it can be reimplemented to initialize the page's contents
    based on other pages' fields (see the \l{initialize page}{example
    above}).

    If the user presses \uicontrol Back, cleanupPage() is called (which in
    turn calls VectorWizardPage::cleanupPage()). The default
    implementation resets the page's fields to their original values
    (the values they had before initializePage() was called). If you
    want the \uicontrol Back button to be non-destructive and keep the
    values entered by the user, simply enable the IndependentPages
    option.

    \section1 Creating Non-Linear Wizards

    Some wizards are more complex in that they allow different
    traversal paths based on the information provided by the user.
    The \l{dialogs/licensewizard}{License Wizard} example illustrates this.
    It provides five wizard pages; depending on which options are
    selected, the user can reach different pages.

    \image licensewizard-flow.png

    In complex wizards, pages are identified by IDs. These IDs are
    typically defined using an enum. For example:

    \snippet dialogs/licensewizard/licensewizard.h 0
    \dots
    \snippet dialogs/licensewizard/licensewizard.h 2
    \dots
    \snippet dialogs/licensewizard/licensewizard.h 3

    The pages are inserted using setPage(), which takes an ID and an
    instance of VectorWizardPage (or of a subclass):

    \snippet dialogs/licensewizard/licensewizard.cpp 1
    \dots
    \snippet dialogs/licensewizard/licensewizard.cpp 8

    By default, the pages are shown in increasing ID order. To
    provide a dynamic order that depends on the options chosen by the
    user, we must reimplement VectorWizardPage::nextId(). For example:

    \snippet dialogs/licensewizard/licensewizard.cpp 18
    \codeline
    \snippet dialogs/licensewizard/licensewizard.cpp 23
    \codeline
    \snippet dialogs/licensewizard/licensewizard.cpp 24
    \codeline
    \snippet dialogs/licensewizard/licensewizard.cpp 25
    \codeline
    \snippet dialogs/licensewizard/licensewizard.cpp 26

    It would also be possible to put all the logic in one place, in a
    VectorWizard::nextId() reimplementation. For example:

    \snippet code/src_gui_dialogs_qwizard.cpp 0

    To start at another page than the page with the lowest ID, call
    setStartId().

    To test whether a page has been visited or not, call
    hasVisitedPage(). For example:

    \snippet dialogs/licensewizard/licensewizard.cpp 27

    \sa VectorWizardPage, {Class Wizard Example}, {License Wizard Example}
*/

/*!
    \enum VectorWizard::WizardButton

    This enum specifies the buttons in a wizard.

    \value BackButton  The \uicontrol Back button (\uicontrol {Go Back} on OS X)
    \value NextButton  The \uicontrol Next button (\uicontrol Continue on OS X)
    \value CommitButton  The \uicontrol Commit button
    \value FinishButton  The \uicontrol Finish button (\uicontrol Done on OS X)
    \value CancelButton  The \uicontrol Cancel button (see also NoCancelButton)
    \value HelpButton    The \uicontrol Help button (see also HaveHelpButton)
    \value CustomButton1  The first user-defined button (see also HaveCustomButton1)
    \value CustomButton2  The second user-defined button (see also HaveCustomButton2)
    \value CustomButton3  The third user-defined button (see also HaveCustomButton3)

    The following value is only useful when calling setButtonLayout():

    \value Stretch  A horizontal stretch in the button layout

    \omitvalue NoButton
    \omitvalue NStandardButtons
    \omitvalue NButtons

    \sa setButton(), setButtonText(), setButtonLayout(), customButtonClicked()
*/

/*!
    \enum VectorWizard::WizardPixmap

    This enum specifies the pixmaps that can be associated with a page.

    \value WatermarkPixmap  The tall pixmap on the left side of a ClassicStyle or ModernStyle page
    \value LogoPixmap  The small pixmap on the right side of a ClassicStyle or ModernStyle page header
    \value BannerPixmap  The pixmap that occupies the background of a ModernStyle page header
    \value BackgroundPixmap  The pixmap that occupies the background of a MacStyle wizard

    \omitvalue NPixmaps

    \sa setPixmap(), VectorWizardPage::setPixmap(), {Elements of a Wizard Page}
*/

/*!
    \enum VectorWizard::WizardStyle

    This enum specifies the different looks supported by QWizard.

    \value ClassicStyle  Classic Windows look
    \value ModernStyle  Modern Windows look
    \value MacStyle  OS X look
    \value AeroStyle  Windows Aero look

    \omitvalue NStyles

    \sa setWizardStyle(), WizardOption, {Wizard Look and Feel}
*/

/*!
    \enum VectorWizard::WizardOption

    This enum specifies various options that affect the look and feel
    of a wizard.

    \value IndependentPages  The pages are independent of each other
                             (i.e., they don't derive values from each
                             other).
    \value IgnoreSubTitles  Don't show any subtitles, even if they are set.
    \value ExtendedWatermarkPixmap  Extend any WatermarkPixmap all the
                                    way down to the window's edge.
    \value NoDefaultButton  Don't make the \uicontrol Next or \uicontrol Finish button the
                            dialog's \l{QPushButton::setDefault()}{default button}.
    \value NoBackButtonOnStartPage  Don't show the \uicontrol Back button on the start page.
    \value NoBackButtonOnLastPage   Don't show the \uicontrol Back button on the last page.
    \value DisabledBackButtonOnLastPage  Disable the \uicontrol Back button on the last page.
    \value HaveNextButtonOnLastPage  Show the (disabled) \uicontrol Next button on the last page.
    \value HaveFinishButtonOnEarlyPages  Show the (disabled) \uicontrol Finish button on non-final pages.
    \value NoCancelButton  Don't show the \uicontrol Cancel button.
    \value CancelButtonOnLeft  Put the \uicontrol Cancel button on the left of \uicontrol Back (rather than on
                               the right of \uicontrol Finish or \uicontrol Next).
    \value HaveHelpButton  Show the \uicontrol Help button.
    \value HelpButtonOnRight  Put the \uicontrol Help button on the far right of the button layout
                              (rather than on the far left).
    \value HaveCustomButton1  Show the first user-defined button (CustomButton1).
    \value HaveCustomButton2  Show the second user-defined button (CustomButton2).
    \value HaveCustomButton3  Show the third user-defined button (CustomButton3).
    \value NoCancelButtonOnLastPage   Don't show the \uicontrol Cancel button on the last page.

    \sa setOptions(), setOption(), testOption()
*/

/*!
    Constructs a wizard with the given \a parent and window \a flags.

    \sa parent(), windowFlags()
*/
VectorWizard::VectorWizard(QWidget *parent)
    : BackgroundWindow(*new VectorWizardPrivate, parent)
{
#ifndef LUMIT_INSTALLER
    init(QString(), QString());
#endif
}

void VectorWizard::init(const QString &logoFileName, const QString &bgFileName)
{
    Q_D(VectorWizard);
    d->init(logoFileName, bgFileName);
#ifdef Q_OS_WINCE
    if(!qt_wince_is_mobile())
        setWindowFlags(windowFlags() & ~Qt::WindowOkButtonHint);
#endif
}

/*!
    Destroys the wizard and its pages, releasing any allocated resources.
*/
VectorWizard::~VectorWizard()
{
    Q_D(VectorWizard);
    delete d->buttonLayout;
}

void VectorWizard::setSidebarItems(const QList<QString> &items)
{
    Q_D(VectorWizard);
    d->setSidebarItems(items);
}

void VectorWizard::highlightSidebarItem(const QString &item)
{
    Q_D(VectorWizard);
    d->highlightSidebarItem(item);
}

void VectorWizard::setVersionInfo(const QString &versionInfo)
{
    Q_D(VectorWizard);
    d->setVersionInfo(versionInfo);
}

void VectorWizard::hideStepIndicator()
{
    Q_D(VectorWizard);
    d->hideStepIndicator();
}

/*!
    Adds the given \a page to the wizard, and returns the page's ID.

    The ID is guaranteed to be larger than any other ID in the
    QWizard so far.

    \sa setPage(), page(), pageAdded()
*/
int VectorWizard::addPage(VectorWizardPage *page)
{
    Q_D(VectorWizard);
    int theid = 0;
    if (!d->pageMap.isEmpty())
        theid = (d->pageMap.constEnd() - 1).key() + 1;
    setPage(theid, page);
    return theid;
}

/*!
    \fn void VectorWizard::setPage(int id, VectorWizardPage *page)

    Adds the given \a page to the wizard with the given \a id.

    \note Adding a page may influence the value of the startId property
    in case it was not set explicitly.

    \sa addPage(), page(), pageAdded()
*/
void VectorWizard::setPage(int theid, VectorWizardPage *page)
{
    Q_D(VectorWizard);

    if (!page) {
        qWarning("VectorWizard::setPage: Cannot insert null page");
        return;
    }

    if (theid == -1) {
        qWarning("VectorWizard::setPage: Cannot insert page with ID -1");
        return;
    }

    if (d->pageMap.contains(theid)) {
        qWarning("VectorWizard::setPage: Page with duplicate ID %d ignored", theid);
        return;
    }

    page->setParent(d->pageFrame);

    QVector<QWizardField> &pendingFields = page->d_func()->pendingFields;
    for (int i = 0; i < pendingFields.count(); ++i)
        d->addField(pendingFields.at(i));
    pendingFields.clear();

    connect(page, SIGNAL(completeChanged()), this, SLOT(_q_updateButtonStates()));

    d->pageMap.insert(theid, page);
    page->d_func()->wizard = this;

    // update step indicator
    d->mStepIndicatorWidget->setNumSteps(d->pageMap.count());

    int n = d->pageVBoxLayout->count();

    // disable layout to prevent layout updates while adding
    bool pageVBoxLayoutEnabled = d->pageVBoxLayout->isEnabled();
    d->pageVBoxLayout->setEnabled(false);

    d->pageVBoxLayout->insertWidget(n - 1, page);

    // hide new page and reset layout to old status
    page->hide();
    d->pageVBoxLayout->setEnabled(pageVBoxLayoutEnabled);

    if (!d->startSetByUser && d->pageMap.constBegin().key() == theid)
        d->start = theid;
    emit pageAdded(theid);
}

/*!
    Removes the page with the given \a id. cleanupPage() will be called if necessary.

    \note Removing a page may influence the value of the startId property.

    \since 4.5
    \sa addPage(), setPage(), pageRemoved(), startId()
*/
void VectorWizard::removePage(int id)
{
    Q_D(VectorWizard);

    VectorWizardPage *removedPage = 0;

    // update startItem accordingly
    if (d->pageMap.count() > 0) { // only if we have any pages
        if (d->start == id) {
            const int firstId = d->pageMap.constBegin().key();
            if (firstId == id) {
                if (d->pageMap.count() > 1)
                    d->start = (++d->pageMap.constBegin()).key(); // secondId
                else
                    d->start = -1; // removing the last page
            } else { // startSetByUser has to be "true" here
                d->start = firstId;
            }
            d->startSetByUser = false;
        }
    }

    if (d->pageMap.contains(id))
        emit pageRemoved(id);

    if (!d->history.contains(id)) {
        // Case 1: removing a page not in the history
        removedPage = d->pageMap.take(id);
        d->updateCurrentPage();
    } else if (id != d->current) {
        // Case 2: removing a page in the history before the current page
        removedPage = d->pageMap.take(id);
        d->history.removeOne(id);
        d->_q_updateButtonStates();
    } else if (d->history.count() == 1) {
        // Case 3: removing the current page which is the first (and only) one in the history
        d->reset();
        removedPage = d->pageMap.take(id);
        if (d->pageMap.isEmpty())
            d->updateCurrentPage();
        else
            restart();
    } else {
        // Case 4: removing the current page which is not the first one in the history
        back();
        removedPage = d->pageMap.take(id);
        d->updateCurrentPage();
    }

    if (removedPage) {
        // update step indicator
        d->mStepIndicatorWidget->setNumSteps(d->pageMap.count());

        if (d->initialized.contains(id)) {
            cleanupPage(id);
            d->initialized.remove(id);
        }

        d->pageVBoxLayout->removeWidget(removedPage);

        for (int i = d->fields.count() - 1; i >= 0; --i) {
            if (d->fields.at(i).page == removedPage) {
                removedPage->d_func()->pendingFields += d->fields.at(i);
                d->removeFieldAt(i);
            }
        }
    }
}

/*!
    \fn VectorWizardPage *VectorWizard::page(int id) const

    Returns the page with the given \a id, or 0 if there is no such
    page.

    \sa addPage(), setPage()
*/
VectorWizardPage *VectorWizard::page(int theid) const
{
    Q_D(const VectorWizard);
    return d->pageMap.value(theid);
}

/*!
    \fn bool VectorWizard::hasVisitedPage(int id) const

    Returns \c true if the page history contains page \a id; otherwise,
    returns \c false.

    Pressing \uicontrol Back marks the current page as "unvisited" again.

    \sa visitedPages()
*/
bool VectorWizard::hasVisitedPage(int theid) const
{
    Q_D(const VectorWizard);
    return d->history.contains(theid);
}

/*!
    Returns the list of IDs of visited pages, in the order in which the pages
    were visited.

    Pressing \uicontrol Back marks the current page as "unvisited" again.

    \sa hasVisitedPage()
*/
QList<int> VectorWizard::visitedPages() const
{
    Q_D(const VectorWizard);
    return d->history;
}

/*!
    Returns the list of page IDs.
   \since 4.5
*/
QList<int> VectorWizard::pageIds() const
{
  Q_D(const VectorWizard);
  return d->pageMap.keys();
}

/*!
    \property VectorWizard::startId
    \brief the ID of the first page

    If this property isn't explicitly set, this property defaults to
    the lowest page ID in this wizard, or -1 if no page has been
    inserted yet.

    \sa restart(), nextId()
*/
void VectorWizard::setStartId(int theid)
{
    Q_D(VectorWizard);
    int newStart = theid;
    if (theid == -1)
        newStart = d->pageMap.count() ? d->pageMap.constBegin().key() : -1;

    if (d->start == newStart) {
        d->startSetByUser = theid != -1;
        return;
    }

    if (!d->pageMap.contains(newStart)) {
        qWarning("VectorWizard::setStartId: Invalid page ID %d", newStart);
        return;
    }
    d->start = newStart;
    d->startSetByUser = theid != -1;
}

int VectorWizard::startId() const
{
    Q_D(const VectorWizard);
    return d->start;
}

/*!
    Returns a pointer to the current page, or 0 if there is no current
    page (e.g., before the wizard is shown).

    This is equivalent to calling page(currentId()).

    \sa page(), currentId(), restart()
*/
VectorWizardPage *VectorWizard::currentPage() const
{
    Q_D(const VectorWizard);
    return page(d->current);
}

/*!
    \property VectorWizard::currentId
    \brief the ID of the current page

    This property cannot be set directly. To change the current page,
    call next(), back(), or restart().

    By default, this property has a value of -1, indicating that no page is
    currently shown.

    \sa currentPage()
*/
int VectorWizard::currentId() const
{
    Q_D(const VectorWizard);
    return d->current;
}

/*!
    Sets the value of the field called \a name to \a value.

    This function can be used to set fields on any page of the wizard.

    \sa VectorWizardPage::registerField(), VectorWizardPage::setField(), field()
*/
void VectorWizard::setField(const QString &name, const QVariant &value)
{
    Q_D(VectorWizard);

    int index = d->fieldIndexMap.value(name, -1);
    if (index != -1) {
        const QWizardField &field = d->fields.at(index);
        if (!field.object->setProperty(field.property, value))
            qWarning("VectorWizard::setField: Couldn't write to property '%s'",
                     field.property.constData());
        return;
    }

    qWarning("VectorWizard::setField: No such field '%s'", qPrintable(name));
}

/*!
    Returns the value of the field called \a name.

    This function can be used to access fields on any page of the wizard.

    \sa VectorWizardPage::registerField(), VectorWizardPage::field(), setField()
*/
QVariant VectorWizard::field(const QString &name) const
{
    Q_D(const VectorWizard);

    int index = d->fieldIndexMap.value(name, -1);
    if (index != -1) {
        const QWizardField &field = d->fields.at(index);
        return field.object->property(field.property);
    }

    qWarning("VectorWizard::field: No such field '%s'", qPrintable(name));
    return QVariant();
}

/*!
    \property VectorWizard::wizardStyle
    \brief the look and feel of the wizard

    By default, QWizard uses the AeroStyle on a Windows Vista system with alpha compositing
    enabled, regardless of the current widget style. If this is not the case, the default
    wizard style depends on the current widget style as follows: MacStyle is the default if
    the current widget style is QMacStyle, ModernStyle is the default if the current widget
    style is QWindowsStyle, and ClassicStyle is the default in all other cases.

    \sa {Wizard Look and Feel}, options
*/
void VectorWizard::setWizardStyle(WizardStyle style)
{
    Q_D(VectorWizard);

    const bool styleChange = style != d->wizStyle;

#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    const bool aeroStyleChange =
        d->vistaInitPending || d->vistaStateChanged || (styleChange && (style == AeroStyle || d->wizStyle == AeroStyle));
    d->vistaStateChanged = false;
    d->vistaInitPending = false;
#endif

    if (styleChange
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
        || aeroStyleChange
#endif
        ) {
        d->disableUpdates();
        d->wizStyle = style;
        d->updateButtonTexts();
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
        if (aeroStyleChange) {
            //Send a resizeevent since the antiflicker widget probably needs a new size
            //because of the backbutton in the window title
            QResizeEvent ev(geometry().size(), geometry().size());
            QApplication::sendEvent(this, &ev);
        }
#endif
        d->updateLayout();
        updateGeometry();
        d->enableUpdates();
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
        // Delay initialization when activating Aero style fails due to missing native window.
        if (aeroStyleChange && !d->handleAeroStyleChange() && d->wizStyle == AeroStyle)
            d->vistaInitPending = true;
#endif
    }
}

VectorWizard::WizardStyle VectorWizard::wizardStyle() const
{
    Q_D(const VectorWizard);
    return d->wizStyle;
}

/*!
    Sets the given \a option to be enabled if \a on is true;
    otherwise, clears the given \a option.

    \sa options, testOption(), setWizardStyle()
*/
void VectorWizard::setOption(WizardOption option, bool on)
{
    Q_D(VectorWizard);
    if (!(d->opts & option) != !on)
        setOptions(d->opts ^ option);
}

/*!
    Returns \c true if the given \a option is enabled; otherwise, returns
    false.

    \sa options, setOption(), setWizardStyle()
*/
bool VectorWizard::testOption(WizardOption option) const
{
    Q_D(const VectorWizard);
    return (d->opts & option) != 0;
}

/*!
    \property VectorWizard::options
    \brief the various options that affect the look and feel of the wizard

    By default, the following options are set (depending on the platform):

    \list
    \li Windows: HelpButtonOnRight.
    \li OS X: NoDefaultButton and NoCancelButton.
    \li X11 and QWS (Qt for Embedded Linux): none.
    \endlist

    \sa wizardStyle
*/
void VectorWizard::setOptions(WizardOptions options)
{
    Q_D(VectorWizard);

    WizardOptions changed = (options ^ d->opts);
    if (!changed)
        return;

    d->disableUpdates();

    d->opts = options;
    if ((changed & IndependentPages) && !(d->opts & IndependentPages))
        d->cleanupPagesNotInHistory();

    if (changed & (NoDefaultButton | HaveHelpButton | HelpButtonOnRight | NoCancelButton
                   | CancelButtonOnLeft | HaveCustomButton1 | HaveCustomButton2
                   | HaveCustomButton3)) {
        d->updateButtonLayout();
    } else if (changed & (NoBackButtonOnStartPage | NoBackButtonOnLastPage
                          | HaveNextButtonOnLastPage | HaveFinishButtonOnEarlyPages
                          | DisabledBackButtonOnLastPage | NoCancelButtonOnLastPage)) {
        d->_q_updateButtonStates();
    }

    d->enableUpdates();
    d->updateLayout();
}

VectorWizard::WizardOptions VectorWizard::options() const
{
    Q_D(const VectorWizard);
    return d->opts;
}

/*!
    Sets the text on button \a which to be \a text.

    By default, the text on buttons depends on the wizardStyle. For
    example, on OS X, the \uicontrol Next button is called \uicontrol
    Continue.

    To add extra buttons to the wizard (e.g., a \uicontrol Print button),
    one way is to call setButtonText() with CustomButton1,
    CustomButton2, or CustomButton3 to set their text, and make the
    buttons visible using the HaveCustomButton1, HaveCustomButton2,
    and/or HaveCustomButton3 options.

    Button texts may also be set on a per-page basis using VectorWizardPage::setButtonText().

    \sa setButton(), button(), setButtonLayout(), setOptions(), VectorWizardPage::setButtonText()
*/
void VectorWizard::setButtonText(WizardButton which, const QString &text)
{
    Q_D(VectorWizard);

    if (!d->ensureButton(which))
        return;

    d->buttonCustomTexts.insert(which, text);

    if (!currentPage() || !currentPage()->d_func()->buttonCustomTexts.contains(which))
        d->btns[which]->setText(text);
}

/*!
    Returns the text on button \a which.

    If a text has ben set using setButtonText(), this text is returned.

    By default, the text on buttons depends on the wizardStyle. For
    example, on OS X, the \uicontrol Next button is called \uicontrol
    Continue.

    \sa button(), setButton(), setButtonText(), VectorWizardPage::buttonText(),
    VectorWizardPage::setButtonText()
*/
QString VectorWizard::buttonText(WizardButton which) const
{
    Q_D(const VectorWizard);

    if (!d->ensureButton(which))
        return QString();

    if (d->buttonCustomTexts.contains(which))
        return d->buttonCustomTexts.value(which);

    const QString defText = buttonDefaultText(d->wizStyle, which, d);
    if(!defText.isNull())
        return defText;

    return d->btns[which]->text();
}

/*!
    Sets the order in which buttons are displayed to \a layout, where
    \a layout is a list of \l{WizardButton}s.

    The default layout depends on the options (e.g., whether
    HelpButtonOnRight) that are set. You can call this function if
    you need more control over the buttons' layout than what \l
    options already provides.

    You can specify horizontal stretches in the layout using \l
    Stretch.

    Example:

    \snippet code/src_gui_dialogs_qwizard.cpp 1

    \sa setButton(), setButtonText(), setOptions()
*/
void VectorWizard::setButtonLayout(const QList<WizardButton> &layout)
{
    Q_D(VectorWizard);

    for (int i = 0; i < layout.count(); ++i) {
        WizardButton button1 = layout.at(i);

        if (button1 == NoButton || button1 == Stretch)
            continue;
        if (!d->ensureButton(button1))
            return;

        // O(n^2), but n is very small
        for (int j = 0; j < i; ++j) {
            WizardButton button2 = layout.at(j);
            if (button2 == button1) {
                qWarning("VectorWizard::setButtonLayout: Duplicate button in layout");
                return;
            }
        }
    }

    d->buttonsHaveCustomLayout = true;
    d->buttonsCustomLayout = layout;
    d->updateButtonLayout();
}

/*!
    Sets the button corresponding to role \a which to \a button.

    To add extra buttons to the wizard (e.g., a \uicontrol Print button),
    one way is to call setButton() with CustomButton1 to
    CustomButton3, and make the buttons visible using the
    HaveCustomButton1 to HaveCustomButton3 options.

    \sa setButtonText(), setButtonLayout(), options
*/
void VectorWizard::setButton(WizardButton which, QAbstractButton *button)
{
    Q_D(VectorWizard);

    if (uint(which) >= NButtons || d->btns[which] == button)
        return;

    if (QAbstractButton *oldButton = d->btns[which]) {
        d->buttonLayout->removeWidget(oldButton);
        delete oldButton;
    }

    d->btns[which] = button;
    if (button) {
        button->setParent(d->antiFlickerWidget);
        d->buttonCustomTexts.insert(which, button->text());
        d->connectButton(which);
    } else {
        d->buttonCustomTexts.remove(which); // ### what about page-specific texts set for 'which'
        d->ensureButton(which);             // (VectorWizardPage::setButtonText())? Clear them as well?
    }

    d->updateButtonLayout();
}

/*!
    Returns the button corresponding to role \a which.

    \sa setButton(), setButtonText()
*/
QAbstractButton *VectorWizard::button(WizardButton which) const
{
    Q_D(const VectorWizard);
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    if (d->wizStyle == AeroStyle && which == BackButton)
        return d->vistaHelper->backButton();
#endif
    if (!d->ensureButton(which))
        return 0;
    return d->btns[which];
}

/*!
    \property VectorWizard::titleFormat
    \brief the text format used by page titles

    The default format is Qt::AutoText.

    \sa VectorWizardPage::title, subTitleFormat
*/
void VectorWizard::setTitleFormat(Qt::TextFormat format)
{
    Q_D(VectorWizard);
    d->titleFmt = format;
    d->updateLayout();
}

Qt::TextFormat VectorWizard::titleFormat() const
{
    Q_D(const VectorWizard);
    return d->titleFmt;
}

/*!
    \property VectorWizard::subTitleFormat
    \brief the text format used by page subtitles

    The default format is Qt::AutoText.

    \sa VectorWizardPage::title, titleFormat
*/
void VectorWizard::setSubTitleFormat(Qt::TextFormat format)
{
    Q_D(VectorWizard);
    d->subTitleFmt = format;
    d->updateLayout();
}

Qt::TextFormat VectorWizard::subTitleFormat() const
{
    Q_D(const VectorWizard);
    return d->subTitleFmt;
}

/*!
    Sets the pixmap for role \a which to \a pixmap.

    The pixmaps are used by QWizard when displaying a page. Which
    pixmaps are actually used depend on the \l{Wizard Look and
    Feel}{wizard style}.

    Pixmaps can also be set for a specific page using
    VectorWizardPage::setPixmap().

    \sa VectorWizardPage::setPixmap(), {Elements of a Wizard Page}
*/
void VectorWizard::setPixmap(WizardPixmap which, const QPixmap &pixmap)
{
    Q_D(VectorWizard);
    Q_ASSERT(uint(which) < NPixmaps);
    d->defaultPixmaps[which] = pixmap;
    d->updatePixmap(which);
}

/*!
    Returns the pixmap set for role \a which.

    By default, the only pixmap that is set is the BackgroundPixmap on
    OS X.

    \sa VectorWizardPage::pixmap(), {Elements of a Wizard Page}
*/
QPixmap VectorWizard::pixmap(WizardPixmap which) const
{
    Q_D(const VectorWizard);
    Q_ASSERT(uint(which) < NPixmaps);
#ifdef Q_OS_MACX
    if (which == BackgroundPixmap && d->defaultPixmaps[BackgroundPixmap].isNull())
        d->defaultPixmaps[BackgroundPixmap] = d->findDefaultBackgroundPixmap();
#endif
    return d->defaultPixmaps[which];
}

/*!
    Sets the default property for \a className to be \a property,
    and the associated change signal to be \a changedSignal.

    The default property is used when an instance of \a className (or
    of one of its subclasses) is passed to
    VectorWizardPage::registerField() and no property is specified.

    QWizard knows the most common Qt widgets. For these (or their
    subclasses), you don't need to specify a \a property or a \a
    changedSignal. The table below lists these widgets:

    \table
    \header \li Widget          \li Property                            \li Change Notification Signal
    \row    \li QAbstractButton \li bool \l{QAbstractButton::}{checked} \li \l{QAbstractButton::}{toggled()}
    \row    \li QAbstractSlider \li int \l{QAbstractSlider::}{value}    \li \l{QAbstractSlider::}{valueChanged()}
    \row    \li QComboBox       \li int \l{QComboBox::}{currentIndex}   \li \l{QComboBox::}{currentIndexChanged()}
    \row    \li QDateTimeEdit   \li QDateTime \l{QDateTimeEdit::}{dateTime} \li \l{QDateTimeEdit::}{dateTimeChanged()}
    \row    \li QLineEdit       \li QString \l{QLineEdit::}{text}       \li \l{QLineEdit::}{textChanged()}
    \row    \li QListWidget     \li int \l{QListWidget::}{currentRow}   \li \l{QListWidget::}{currentRowChanged()}
    \row    \li QSpinBox        \li int \l{QSpinBox::}{value}           \li \l{QSpinBox::}{valueChanged()}
    \endtable

    \sa VectorWizardPage::registerField()
*/
void VectorWizard::setDefaultProperty(const char *className, const char *property,
                                 const char *changedSignal)
{
    Q_D(VectorWizard);
    for (int i = d->defaultPropertyTable.count() - 1; i >= 0; --i) {
        if (qstrcmp(d->defaultPropertyTable.at(i).className, className) == 0) {
            d->defaultPropertyTable.remove(i);
            break;
        }
    }
    d->defaultPropertyTable.append(QWizardDefaultProperty(className, property, changedSignal));
}

/*!
    \since 4.7

    Sets the given \a widget to be shown on the left side of the wizard.
    For styles which use the WatermarkPixmap (ClassicStyle and ModernStyle)
    the side widget is displayed on top of the watermark, for other styles
    or when the watermark is not provided the side widget is displayed
    on the left side of the wizard.

    Passing 0 shows no side widget.

    When the \a widget is not 0 the wizard reparents it.

    Any previous side widget is hidden.

    You may call setSideWidget() with the same widget at different
    times.

    All widgets set here will be deleted by the wizard when it is
    destroyed unless you separately reparent the widget after setting
    some other side widget (or 0).

    By default, no side widget is present.
*/
void VectorWizard::setSideWidget(QWidget *widget)
{
    Q_D(VectorWizard);

    d->sideWidget = widget;
    if (d->watermarkLabel) {
        d->watermarkLabel->setSideWidget(widget);
        d->updateLayout();
    }
}

/*!
    \since 4.7

    Returns the widget on the left side of the wizard or 0.

    By default, no side widget is present.
*/
QWidget *VectorWizard::sideWidget() const
{
    Q_D(const VectorWizard);

    return d->sideWidget;
}

/*!
    \reimp
*/
void VectorWizard::setVisible(bool visible)
{
    Q_D(VectorWizard);
    if (visible) {
        if (d->current == -1)
            restart();
    }
    QDialog::setVisible(visible);
}

/*!
    \reimp
*/
QSize VectorWizard::sizeHint() const
{
    return BackgroundWindow::sizeHint();
}

/*!
    \fn void VectorWizard::currentIdChanged(int id)

    This signal is emitted when the current page changes, with the new
    current \a id.

    \sa currentId(), currentPage()
*/

/*!
    \fn void VectorWizard::pageAdded(int id)

    \since 4.7

    This signal is emitted whenever a page is added to the
    wizard. The page's \a id is passed as parameter.

    \sa addPage(), setPage(), startId()
*/

/*!
    \fn void VectorWizard::pageRemoved(int id)

    \since 4.7

    This signal is emitted whenever a page is removed from the
    wizard. The page's \a id is passed as parameter.

    \sa removePage(), startId()
*/

/*!
    \fn void VectorWizard::helpRequested()

    This signal is emitted when the user clicks the \uicontrol Help button.

    By default, no \uicontrol Help button is shown. Call
    setOption(HaveHelpButton, true) to have one.

    Example:

    \snippet dialogs/licensewizard/licensewizard.cpp 0
    \dots
    \snippet dialogs/licensewizard/licensewizard.cpp 5
    \snippet dialogs/licensewizard/licensewizard.cpp 7
    \dots
    \snippet dialogs/licensewizard/licensewizard.cpp 8
    \codeline
    \snippet dialogs/licensewizard/licensewizard.cpp 10
    \dots
    \snippet dialogs/licensewizard/licensewizard.cpp 12
    \codeline
    \snippet dialogs/licensewizard/licensewizard.cpp 14
    \codeline
    \snippet dialogs/licensewizard/licensewizard.cpp 15

    \sa customButtonClicked()
*/

/*!
    \fn void VectorWizard::customButtonClicked(int which)

    This signal is emitted when the user clicks a custom button. \a
    which can be CustomButton1, CustomButton2, or CustomButton3.

    By default, no custom button is shown. Call setOption() with
    HaveCustomButton1, HaveCustomButton2, or HaveCustomButton3 to have
    one, and use setButtonText() or setButton() to configure it.

    \sa helpRequested()
*/

/*!
    Goes back to the previous page.

    This is equivalent to pressing the \uicontrol Back button.

    \sa next(), accept(), reject(), restart()
*/
void VectorWizard::back()
{
    Q_D(VectorWizard);
    int n = d->history.count() - 2;
    if (n < 0)
        return;
    d->switchToPage(d->history.at(n), VectorWizardPrivate::Backward);
}

/*!
    Advances to the next page.

    This is equivalent to pressing the \uicontrol Next or \uicontrol Commit button.

    \sa nextId(), back(), accept(), reject(), restart()
*/
void VectorWizard::next()
{
    Q_D(VectorWizard);

    if (d->current == -1)
        return;

    if (validateCurrentPage()) {
        int next = nextId();
        if (next != -1) {
            if (d->history.contains(next)) {
                qWarning("VectorWizard::next: Page %d already met", next);
                return;
            }
            if (!d->pageMap.contains(next)) {
                qWarning("VectorWizard::next: No such page %d", next);
                return;
            }
            d->switchToPage(next, VectorWizardPrivate::Forward);
        }
    }
}

/*!
    Restarts the wizard at the start page. This function is called automatically when the
    wizard is shown.

    \sa startId()
*/
void VectorWizard::restart()
{
    Q_D(VectorWizard);
    d->disableUpdates();
    d->reset();
    d->switchToPage(startId(), VectorWizardPrivate::Forward);
    d->enableUpdates();
}

/*!
    \reimp
*/
bool VectorWizard::event(QEvent *event)
{
    Q_D(VectorWizard);
    if (event->type() == QEvent::StyleChange) { // Propagate style
        d->setStyle(style());
        d->updateLayout();
    }
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    else if (event->type() == QEvent::Show && d->vistaInitPending) {
        d->vistaInitPending = false;
        // Do not force AeroStyle when in Classic theme.
        // Note that d->handleAeroStyleChange() needs to be called in any case as it does some
        // necessary initialization, like ensures that the Aero specific back button is hidden if
        // Aero theme isn't active.
        if (QVistaHelper::vistaState() != QVistaHelper::Classic)
            d->wizStyle = AeroStyle;
        d->handleAeroStyleChange();
    }
    else if (d->isVistaThemeEnabled()) {
        if (event->type() == QEvent::Resize
                || event->type() == QEvent::LayoutDirectionChange) {
            const int buttonLeft = (layoutDirection() == Qt::RightToLeft
                                    ? width() - d->vistaHelper->backButton()->sizeHint().width()
                                    : 0);

            d->vistaHelper->backButton()->move(buttonLeft,
                                               d->vistaHelper->backButton()->y());
        }

        d->vistaHelper->mouseEvent(event);
    }
#endif
    return QDialog::event(event);
}

/*!
    \reimp
*/
void VectorWizard::resizeEvent(QResizeEvent *event)
{
    // size of installer is fixed to installer background image
    BackgroundWindow::resizeEvent(event);
    return;

    Q_D(VectorWizard);
    int heightOffset = 0;
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    if (d->isVistaThemeEnabled()) {
        heightOffset = d->vistaHelper->topOffset();
        if (d->isVistaThemeEnabled(QVistaHelper::VistaAero))
            heightOffset += d->vistaHelper->titleBarSize();
    }
#endif
    d->antiFlickerWidget->resize(event->size().width(), event->size().height() - heightOffset);
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    if (d->isVistaThemeEnabled())
        d->vistaHelper->resizeEvent(event);
#endif
    QDialog::resizeEvent(event);
}

/*!
    \reimp
*/
void VectorWizard::paintEvent(QPaintEvent * event)
{
    BackgroundWindow::paintEvent(event);
    
    Q_D(VectorWizard);
    if (d->wizStyle == MacStyle && currentPage()) {
        QPixmap backgroundPixmap = currentPage()->pixmap(BackgroundPixmap);
        if (backgroundPixmap.isNull())
            return;

        QPainter painter(this);
        painter.drawPixmap(0, (height() - backgroundPixmap.height()) / 2, backgroundPixmap);
    }
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    else if (d->isVistaThemeEnabled()) {
        if (d->isVistaThemeEnabled(QVistaHelper::VistaBasic)) {
            QPainter painter(this);
            QColor color = d->vistaHelper->basicWindowFrameColor();
            painter.fillRect(0, 0, width(), QVistaHelper::topOffset(), color);
        }
        d->vistaHelper->paintEvent(event);
    }
#else
    Q_UNUSED(event);
#endif
}

#if defined(Q_OS_WIN)
/*!
    \reimp
*/
bool VectorWizard::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#if !defined(QT_NO_STYLE_WINDOWSVISTA)
    Q_D(VectorWizard);
    if (d->isVistaThemeEnabled() && eventType == "windows_generic_MSG") {
        MSG *windowsMessage = static_cast<MSG *>(message);
        const bool winEventResult = d->vistaHelper->handleWinEvent(windowsMessage, result);
        if (QVistaHelper::vistaState() != d->vistaState) {
            d->vistaState = QVistaHelper::vistaState();
            d->vistaStateChanged = true;
            setWizardStyle(AeroStyle);
        }
        return winEventResult;
    } else {
        return QDialog::nativeEvent(eventType, message, result);
    }
#else
    return QDialog::nativeEvent(eventType, message, result);
#endif
}
#endif

/*!
    \reimp
*/
void VectorWizard::done(int result)
{
    Q_D(VectorWizard);
    // canceling leaves the wizard in a known state
    if (result == Rejected) {
        d->reset();
    } else {
        if (!validateCurrentPage())
            return;
    }
    QDialog::done(result);
}

/*!
    \fn void VectorWizard::initializePage(int id)

    This virtual function is called by QWizard to prepare page \a id
    just before it is shown either as a result of VectorWizard::restart()
    being called, or as a result of the user clicking \uicontrol Next. (However, if the \l
    VectorWizard::IndependentPages option is set, this function is only
    called the first time the page is shown.)

    By reimplementing this function, you can ensure that the page's
    fields are properly initialized based on fields from previous
    pages.

    The default implementation calls VectorWizardPage::initializePage() on
    page(\a id).

    \sa VectorWizardPage::initializePage(), cleanupPage()
*/
void VectorWizard::initializePage(int theid)
{
    VectorWizardPage *page = this->page(theid);
    if (page)
        page->initializePage();
}

/*!
    \fn void VectorWizard::cleanupPage(int id)

    This virtual function is called by QWizard to clean up page \a id just before the
    user leaves it by clicking \uicontrol Back (unless the \l VectorWizard::IndependentPages option is set).

    The default implementation calls VectorWizardPage::cleanupPage() on
    page(\a id).

    \sa VectorWizardPage::cleanupPage(), initializePage()
*/
void VectorWizard::cleanupPage(int theid)
{
    VectorWizardPage *page = this->page(theid);
    if (page)
        page->cleanupPage();
}

/*!
    This virtual function is called by QWizard when the user clicks
    \uicontrol Next or \uicontrol Finish to perform some last-minute validation.
    If it returns \c true, the next page is shown (or the wizard
    finishes); otherwise, the current page stays up.

    The default implementation calls VectorWizardPage::validatePage() on
    the currentPage().

    When possible, it is usually better style to disable the \uicontrol
    Next or \uicontrol Finish button (by specifying \l{mandatory fields} or
    by reimplementing VectorWizardPage::isComplete()) than to reimplement
    validateCurrentPage().

    \sa VectorWizardPage::validatePage(), currentPage()
*/
bool VectorWizard::validateCurrentPage()
{
    VectorWizardPage *page = currentPage();
    if (!page)
        return true;

    return page->validatePage();
}

/*!
    This virtual function is called by QWizard to find out which page
    to show when the user clicks the \uicontrol Next button.

    The return value is the ID of the next page, or -1 if no page follows.

    The default implementation calls VectorWizardPage::nextId() on the
    currentPage().

    By reimplementing this function, you can specify a dynamic page
    order.

    \sa VectorWizardPage::nextId(), currentPage()
*/
int VectorWizard::nextId() const
{
    const VectorWizardPage *page = currentPage();
    if (!page)
        return -1;

    return page->nextId();
}

/*!
    \class VectorWizardPage
    \since 4.3
    \brief The VectorWizardPage class is the base class for wizard pages.

    \inmodule QtWidgets

    QWizard represents a wizard. Each page is a VectorWizardPage. When
    you create your own wizards, you can use VectorWizardPage directly,
    or you can subclass it for more control.

    A page has the following attributes, which are rendered by
    QWizard: a \l title, a \l subTitle, and a \l{setPixmap()}{set of
    pixmaps}. See \l{Elements of a Wizard Page} for details. Once a
    page is added to the wizard (using VectorWizard::addPage() or
    VectorWizard::setPage()), wizard() returns a pointer to the
    associated QWizard object.

    Page provides five virtual functions that can be reimplemented to
    provide custom behavior:

    \list
    \li initializePage() is called to initialize the page's contents
       when the user clicks the wizard's \uicontrol Next button. If you
       want to derive the page's default from what the user entered
       on previous pages, this is the function to reimplement.
    \li cleanupPage() is called to reset the page's contents when the
       user clicks the wizard's \uicontrol Back button.
    \li validatePage() validates the page when the user clicks \uicontrol
       Next or \uicontrol Finish. It is often used to show an error message
       if the user has entered incomplete or invalid information.
    \li nextId() returns the ID of the next page. It is useful when
       \l{creating non-linear wizards}, which allow different
       traversal paths based on the information provided by the user.
    \li isComplete() is called to determine whether the \uicontrol Next
       and/or \uicontrol Finish button should be enabled or disabled. If
       you reimplement isComplete(), also make sure that
       completeChanged() is emitted whenever the complete state
       changes.
    \endlist

    Normally, the \uicontrol Next button and the \uicontrol Finish button of a
    wizard are mutually exclusive. If isFinalPage() returns \c true, \uicontrol
    Finish is available; otherwise, \uicontrol Next is available. By
    default, isFinalPage() is true only when nextId() returns -1. If
    you want to show \uicontrol Next and \uicontrol Final simultaneously for a
    page (letting the user perform an "early finish"), call
    setFinalPage(true) on that page. For wizards that support early
    finishes, you might also want to set the
    \l{VectorWizard::}{HaveNextButtonOnLastPage} and
    \l{VectorWizard::}{HaveFinishButtonOnEarlyPages} options on the
    wizard.

    In many wizards, the contents of a page may affect the default
    values of the fields of a later page. To make it easy to
    communicate between pages, QWizard supports a \l{Registering and
    Using Fields}{"field" mechanism} that allows you to register a
    field (e.g., a QLineEdit) on a page and to access its value from
    any page. Fields are global to the entire wizard and make it easy
    for any single page to access information stored by another page,
    without having to put all the logic in QWizard or having the
    pages know explicitly about each other. Fields are registered
    using registerField() and can be accessed at any time using
    field() and setField().

    \sa QWizard, {Class Wizard Example}, {License Wizard Example}
*/

/*!
    Constructs a wizard page with the given \a parent.

    When the page is inserted into a wizard using VectorWizard::addPage()
    or VectorWizard::setPage(), the parent is automatically set to be the
    wizard.

    \sa wizard()
*/
VectorWizardPage::VectorWizardPage(QWidget *parent)
    : QWidget(*new VectorWizardPagePrivate, parent, 0)
{
    connect(this, SIGNAL(completeChanged()), this, SLOT(_q_updateCachedCompleteState()));
}

/*!
    Destructor.
*/
VectorWizardPage::~VectorWizardPage()
{
}

/*!
    \property VectorWizardPage::title
    \brief the title of the page

    The title is shown by the QWizard, above the actual page. All
    pages should have a title.

    The title may be plain text or HTML, depending on the value of the
    \l{VectorWizard::titleFormat} property.

    By default, this property contains an empty string.

    \sa subTitle, {Elements of a Wizard Page}
*/
void VectorWizardPage::setTitle(const QString &title)
{
    Q_D(VectorWizardPage);
    d->title = title;
    if (d->wizard && d->wizard->currentPage() == this)
        d->wizard->d_func()->updateLayout();
}

QString VectorWizardPage::title() const
{
    Q_D(const VectorWizardPage);
    return d->title;
}

/*!
    \property VectorWizardPage::subTitle
    \brief the subtitle of the page

    The subtitle is shown by the QWizard, between the title and the
    actual page. Subtitles are optional. In
    \l{VectorWizard::ClassicStyle}{ClassicStyle} and
    \l{VectorWizard::ModernStyle}{ModernStyle}, using subtitles is
    necessary to make the header appear. In
    \l{VectorWizard::MacStyle}{MacStyle}, the subtitle is shown as a text
    label just above the actual page.

    The subtitle may be plain text or HTML, depending on the value of
    the \l{VectorWizard::subTitleFormat} property.

    By default, this property contains an empty string.

    \sa title, VectorWizard::IgnoreSubTitles, {Elements of a Wizard Page}
*/
void VectorWizardPage::setSubTitle(const QString &subTitle)
{
    Q_D(VectorWizardPage);
    d->subTitle = subTitle;
    if (d->wizard && d->wizard->currentPage() == this)
        d->wizard->d_func()->updateLayout();
}

QString VectorWizardPage::subTitle() const
{
    Q_D(const VectorWizardPage);
    return d->subTitle;
}

/*!
    Sets the pixmap for role \a which to \a pixmap.

    The pixmaps are used by QWizard when displaying a page. Which
    pixmaps are actually used depend on the \l{Wizard Look and
    Feel}{wizard style}.

    Pixmaps can also be set for the entire wizard using
    VectorWizard::setPixmap(), in which case they apply for all pages that
    don't specify a pixmap.

    \sa VectorWizard::setPixmap(), {Elements of a Wizard Page}
*/
void VectorWizardPage::setPixmap(VectorWizard::WizardPixmap which, const QPixmap &pixmap)
{
    Q_D(VectorWizardPage);
    Q_ASSERT(uint(which) < VectorWizard::NPixmaps);
    d->pixmaps[which] = pixmap;
    if (d->wizard && d->wizard->currentPage() == this)
        d->wizard->d_func()->updatePixmap(which);
}

/*!
    Returns the pixmap set for role \a which.

    Pixmaps can also be set for the entire wizard using
    VectorWizard::setPixmap(), in which case they apply for all pages that
    don't specify a pixmap.

    \sa VectorWizard::pixmap(), {Elements of a Wizard Page}
*/
QPixmap VectorWizardPage::pixmap(VectorWizard::WizardPixmap which) const
{
    Q_D(const VectorWizardPage);
    Q_ASSERT(uint(which) < VectorWizard::NPixmaps);

    const QPixmap &pixmap = d->pixmaps[which];
    if (!pixmap.isNull())
        return pixmap;

    if (wizard())
        return wizard()->pixmap(which);

    return pixmap;
}

/*!
    This virtual function is called by VectorWizard::initializePage() to
    prepare the page just before it is shown either as a result of VectorWizard::restart()
    being called, or as a result of the user clicking \uicontrol Next.
    (However, if the \l VectorWizard::IndependentPages option is set, this function is only
    called the first time the page is shown.)

    By reimplementing this function, you can ensure that the page's
    fields are properly initialized based on fields from previous
    pages. For example:

    \snippet dialogs/classwizard/classwizard.cpp 17

    The default implementation does nothing.

    \sa VectorWizard::initializePage(), cleanupPage(), VectorWizard::IndependentPages
*/
void VectorWizardPage::initializePage()
{
}

/*!
    This virtual function is called by VectorWizard::cleanupPage() when
    the user leaves the page by clicking \uicontrol Back (unless the \l VectorWizard::IndependentPages
    option is set).

    The default implementation resets the page's fields to their
    original values (the values they had before initializePage() was
    called).

    \sa VectorWizard::cleanupPage(), initializePage(), VectorWizard::IndependentPages
*/
void VectorWizardPage::cleanupPage()
{
    Q_D(VectorWizardPage);
    if (d->wizard) {
        QVector<QWizardField> &fields = d->wizard->d_func()->fields;
        for (int i = 0; i < fields.count(); ++i) {
            const QWizardField &field = fields.at(i);
            if (field.page == this)
                field.object->setProperty(field.property, field.initialValue);
        }
    }
}

/*!
    This virtual function is called by VectorWizard::validateCurrentPage()
    when the user clicks \uicontrol Next or \uicontrol Finish to perform some
    last-minute validation. If it returns \c true, the next page is shown
    (or the wizard finishes); otherwise, the current page stays up.

    The default implementation returns \c true.

    When possible, it is usually better style to disable the \uicontrol
    Next or \uicontrol Finish button (by specifying \l{mandatory fields} or
    reimplementing isComplete()) than to reimplement validatePage().

    \sa VectorWizard::validateCurrentPage(), isComplete()
*/
bool VectorWizardPage::validatePage()
{
    return true;
}

/*!
    This virtual function is called by QWizard to determine whether
    the \uicontrol Next or \uicontrol Finish button should be enabled or
    disabled.

    The default implementation returns \c true if all \l{mandatory
    fields} are filled; otherwise, it returns \c false.

    If you reimplement this function, make sure to emit completeChanged(),
    from the rest of your implementation, whenever the value of isComplete()
    changes. This ensures that QWizard updates the enabled or disabled state of
    its buttons. An example of the reimplementation is
    available \l{http://doc.qt.io/archives/qq/qq22-qwizard.html#validatebeforeitstoolate}
    {here}.

    \sa completeChanged(), isFinalPage()
*/
bool VectorWizardPage::isComplete() const
{
    Q_D(const VectorWizardPage);

    if (!d->wizard)
        return true;

    const QVector<QWizardField> &wizardFields = d->wizard->d_func()->fields;
    for (int i = wizardFields.count() - 1; i >= 0; --i) {
        const QWizardField &field = wizardFields.at(i);
        if (field.page == this && field.mandatory) {
            QVariant value = field.object->property(field.property);
            if (value == field.initialValue)
                return false;

#ifndef QT_NO_LINEEDIT
            if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(field.object)) {
                if (!lineEdit->hasAcceptableInput())
                    return false;
            }
#endif
#ifndef QT_NO_SPINBOX
            if (QAbstractSpinBox *spinBox = qobject_cast<QAbstractSpinBox *>(field.object)) {
                if (!spinBox->hasAcceptableInput())
                    return false;
            }
#endif
        }
    }
    return true;
}

/*!
    Explicitly sets this page to be final if \a finalPage is true.

    After calling setFinalPage(true), isFinalPage() returns \c true and the \uicontrol
    Finish button is visible (and enabled if isComplete() returns
    true).

    After calling setFinalPage(false), isFinalPage() returns \c true if
    nextId() returns -1; otherwise, it returns \c false.

    \sa isComplete(), VectorWizard::HaveFinishButtonOnEarlyPages
*/
void VectorWizardPage::setFinalPage(bool finalPage)
{
    Q_D(VectorWizardPage);
    d->explicitlyFinal = finalPage;
    VectorWizard *wizard = this->wizard();
    if (wizard && wizard->currentPage() == this)
        wizard->d_func()->updateCurrentPage();
}

/*!
    This function is called by QWizard to determine whether the \uicontrol
    Finish button should be shown for this page or not.

    By default, it returns \c true if there is no next page
    (i.e., nextId() returns -1); otherwise, it returns \c false.

    By explicitly calling setFinalPage(true), you can let the user perform an
    "early finish".

    \sa isComplete(), VectorWizard::HaveFinishButtonOnEarlyPages
*/
bool VectorWizardPage::isFinalPage() const
{
    Q_D(const VectorWizardPage);
    if (d->explicitlyFinal)
        return true;

    VectorWizard *wizard = this->wizard();
    if (wizard && wizard->currentPage() == this) {
        // try to use the QWizard implementation if possible
        return wizard->nextId() == -1;
    } else {
        return nextId() == -1;
    }
}

/*!
    Sets this page to be a commit page if \a commitPage is true; otherwise,
    sets it to be a normal page.

    A commit page is a page that represents an action which cannot be undone
    by clicking \uicontrol Back or \uicontrol Cancel.

    A \uicontrol Commit button replaces the \uicontrol Next button on a commit page. Clicking this
    button simply calls VectorWizard::next() just like clicking \uicontrol Next does.

    A page entered directly from a commit page has its \uicontrol Back button disabled.

    \sa isCommitPage()
*/
void VectorWizardPage::setCommitPage(bool commitPage)
{
    Q_D(VectorWizardPage);
    d->commit = commitPage;
    VectorWizard *wizard = this->wizard();
    if (wizard && wizard->currentPage() == this)
        wizard->d_func()->updateCurrentPage();
}

/*!
    Returns \c true if this page is a commit page; otherwise returns \c false.

    \sa setCommitPage()
*/
bool VectorWizardPage::isCommitPage() const
{
    Q_D(const VectorWizardPage);
    return d->commit;
}

/*!
    Sets the text on button \a which to be \a text on this page.

    By default, the text on buttons depends on the VectorWizard::wizardStyle,
    but may be redefined for the wizard as a whole using VectorWizard::setButtonText().

    \sa buttonText(), VectorWizard::setButtonText(), VectorWizard::buttonText()
*/
void VectorWizardPage::setButtonText(VectorWizard::WizardButton which, const QString &text)
{
    Q_D(VectorWizardPage);
    d->buttonCustomTexts.insert(which, text);
    if (wizard() && wizard()->currentPage() == this && wizard()->d_func()->btns[which])
        wizard()->d_func()->btns[which]->setText(text);
}

/*!
    Returns the text on button \a which on this page.

    If a text has ben set using setButtonText(), this text is returned.
    Otherwise, if a text has been set using VectorWizard::setButtonText(),
    this text is returned.

    By default, the text on buttons depends on the VectorWizard::wizardStyle.
    For example, on OS X, the \uicontrol Next button is called \uicontrol
    Continue.

    \sa setButtonText(), VectorWizard::buttonText(), VectorWizard::setButtonText()
*/
QString VectorWizardPage::buttonText(VectorWizard::WizardButton which) const
{
    Q_D(const VectorWizardPage);

    if (d->buttonCustomTexts.contains(which))
        return d->buttonCustomTexts.value(which);

    if (wizard())
        return wizard()->buttonText(which);

    return QString();
}

/*!
    This virtual function is called by VectorWizard::nextId() to find
    out which page to show when the user clicks the \uicontrol Next button.

    The return value is the ID of the next page, or -1 if no page follows.

    By default, this function returns the lowest ID greater than the ID
    of the current page, or -1 if there is no such ID.

    By reimplementing this function, you can specify a dynamic page
    order. For example:

    \snippet dialogs/licensewizard/licensewizard.cpp 18

    \sa VectorWizard::nextId()
*/
int VectorWizardPage::nextId() const
{
    Q_D(const VectorWizardPage);

    if (!d->wizard)
        return -1;

    bool foundCurrentPage = false;

    const VectorWizardPrivate::PageMap &pageMap = d->wizard->d_func()->pageMap;
    VectorWizardPrivate::PageMap::const_iterator i = pageMap.constBegin();
    VectorWizardPrivate::PageMap::const_iterator end = pageMap.constEnd();

    for (; i != end; ++i) {
        if (i.value() == this) {
            foundCurrentPage = true;
        } else if (foundCurrentPage) {
            return i.key();
        }
    }
    return -1;
}

/*!
    \fn void VectorWizardPage::completeChanged()

    This signal is emitted whenever the complete state of the page
    (i.e., the value of isComplete()) changes.

    If you reimplement isComplete(), make sure to emit
    completeChanged() whenever the value of isComplete() changes, to
    ensure that QWizard updates the enabled or disabled state of its
    buttons.

    \sa isComplete()
*/

/*!
    Sets the value of the field called \a name to \a value.

    This function can be used to set fields on any page of the wizard.
    It is equivalent to calling
    wizard()->\l{VectorWizard::setField()}{setField(\a name, \a value)}.

    \sa VectorWizard::setField(), field(), registerField()
*/
void VectorWizardPage::setField(const QString &name, const QVariant &value)
{
    Q_D(VectorWizardPage);
    if (!d->wizard)
        return;
    d->wizard->setField(name, value);
}

/*!
    Returns the value of the field called \a name.

    This function can be used to access fields on any page of the
    wizard. It is equivalent to calling
    wizard()->\l{VectorWizard::field()}{field(\a name)}.

    Example:

    \snippet dialogs/classwizard/classwizard.cpp 17

    \sa VectorWizard::field(), setField(), registerField()
*/
QVariant VectorWizardPage::field(const QString &name) const
{
    Q_D(const VectorWizardPage);
    if (!d->wizard)
        return QVariant();
    return d->wizard->field(name);
}

/*!
    Creates a field called \a name associated with the given \a
    property of the given \a widget. From then on, that property
    becomes accessible using field() and setField().

    Fields are global to the entire wizard and make it easy for any
    single page to access information stored by another page, without
    having to put all the logic in QWizard or having the pages know
    explicitly about each other.

    If \a name ends with an asterisk (\c *), the field is a mandatory
    field. When a page has mandatory fields, the \uicontrol Next and/or
    \uicontrol Finish buttons are enabled only when all mandatory fields
    are filled. This requires a \a changedSignal to be specified, to
    tell QWizard to recheck the value stored by the mandatory field.

    QWizard knows the most common Qt widgets. For these (or their
    subclasses), you don't need to specify a \a property or a \a
    changedSignal. The table below lists these widgets:

    \table
    \header \li Widget          \li Property                            \li Change Notification Signal
    \row    \li QAbstractButton \li bool \l{QAbstractButton::}{checked} \li \l{QAbstractButton::}{toggled()}
    \row    \li QAbstractSlider \li int \l{QAbstractSlider::}{value}    \li \l{QAbstractSlider::}{valueChanged()}
    \row    \li QComboBox       \li int \l{QComboBox::}{currentIndex}   \li \l{QComboBox::}{currentIndexChanged()}
    \row    \li QDateTimeEdit   \li QDateTime \l{QDateTimeEdit::}{dateTime} \li \l{QDateTimeEdit::}{dateTimeChanged()}
    \row    \li QLineEdit       \li QString \l{QLineEdit::}{text}       \li \l{QLineEdit::}{textChanged()}
    \row    \li QListWidget     \li int \l{QListWidget::}{currentRow}   \li \l{QListWidget::}{currentRowChanged()}
    \row    \li QSpinBox        \li int \l{QSpinBox::}{value}           \li \l{QSpinBox::}{valueChanged()}
    \endtable

    You can use VectorWizard::setDefaultProperty() to add entries to this
    table or to override existing entries.

    To consider a field "filled", QWizard simply checks that their
    current value doesn't equal their original value (the value they
    had before initializePage() was called). For QLineEdit, it also
    checks that
    \l{QLineEdit::hasAcceptableInput()}{hasAcceptableInput()} returns
    true, to honor any validator or mask.

    QWizard's mandatory field mechanism is provided for convenience.
    It can be bypassed by reimplementing VectorWizardPage::isComplete().

    \sa field(), setField(), VectorWizard::setDefaultProperty()
*/
void VectorWizardPage::registerField(const QString &name, QWidget *widget, const char *property,
                                const char *changedSignal)
{
    Q_D(VectorWizardPage);
    QWizardField field(this, name, widget, property, changedSignal);
    if (d->wizard) {
        d->wizard->d_func()->addField(field);
    } else {
        d->pendingFields += field;
    }
}

/*!
    Returns the wizard associated with this page, or 0 if this page
    hasn't been inserted into a QWizard yet.

    \sa VectorWizard::addPage(), VectorWizard::setPage()
*/
VectorWizard *VectorWizardPage::wizard() const
{
    Q_D(const VectorWizardPage);
    return d->wizard;
}

QT_END_NAMESPACE

#ifdef Q_OS_WIN
#include "GeneratedFiles/moc_VectorWizard.cpp"
#else
#include "moc_VectorWizard.cpp"
#endif

#endif // QT_NO_WIZARD
