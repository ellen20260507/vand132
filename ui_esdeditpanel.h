/********************************************************************************
** Form generated from reading UI file 'esdeditpanel.ui'
**
** Created by: Qt User Interface Compiler version 5.14.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ESDEDITPANEL_H
#define UI_ESDEDITPANEL_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_EsdEditPanel
{
public:
    QHBoxLayout *mainHorizontalLayout;
    QWidget *leftPanel;
    QVBoxLayout *leftPanelLayout;
    QWidget *queryAddrPanel;
    QHBoxLayout *queryAddrRowLayout;
    QLabel *queryAddrTitleLabel;
    QLineEdit *displayAddressLineEdit;
    QPushButton *queryButton;
    QWidget *modifyAddrPanel;
    QHBoxLayout *modifyAddrRowLayout;
    QLabel *modifyAddrTitleLabel;
    QLineEdit *inputAddressLineEdit;
    QPushButton *applyButton;
    QTabWidget *tabWidget;
    QWidget *tab1;
    QVBoxLayout *tab1Layout;
    QWidget *channelSettingsPanel;
    QVBoxLayout *channelSettingsLayout;
    QLabel *channelSettingsTitleLabel;
    QGridLayout *deviceEditGrid;
    QComboBox *typeComboBox;
    QLineEdit *valueInputLineEdit;
    QLabel *resultLabel;
    QLineEdit *triggerDisplayLineEdit;
    QPushButton *setChannelButton;
    QLabel *wristbandLabel;
    QComboBox *triggerComboBox;
    QPushButton *applyChannelButton;
    QLabel *upperLimitLabel;
    QLineEdit *upperLimitLineEdit;
    QHBoxLayout *upperLimitBtnLayout;
    QPushButton *readUpperLimitButton;
    QPushButton *applyUpperLimitButton;
    QLabel *lowerLimitLabel;
    QLineEdit *lowerLimitLineEdit;
    QHBoxLayout *lowerLimitBtnLayout;
    QPushButton *readLowerLimitButton;
    QPushButton *applyLowerLimitButton;
    QLabel *internalResistanceLabel;
    QLineEdit *internalResistanceLineEdit;
    QHBoxLayout *internalResistanceBtnLayout;
    QPushButton *readInternalResistanceButton;
    QPushButton *applyInternalResistanceButton;
    QWidget *channelGridContainer;
    QVBoxLayout *channelGridOuterLayout;
    QLabel *channelGridTitleLabel;
    QWidget *channelGridHost;
    QWidget *tab3;
    QVBoxLayout *tab3Layout;
    QWidget *delayParamPanel;
    QHBoxLayout *delayRowLayout;
    QLabel *delayParamTitleLabel;
    QComboBox *delayTypeComboBox;
    QLineEdit *delayTimeLineEdit;
    QPushButton *applyDelayButton;
    QWidget *groundParamPanel;
    QHBoxLayout *groundRowLayout;
    QLabel *groundParamTitleLabel;
    QComboBox *groundTypeComboBox;
    QLineEdit *groundValueLineEdit;
    QPushButton *readGroundButton;
    QPushButton *applyGroundButton;
    QWidget *tempParamPanel;
    QHBoxLayout *tempRowLayout;
    QLabel *tempParamTitleLabel;
    QComboBox *tempTypeComboBox;
    QLineEdit *tempValueLineEdit;
    QPushButton *readTempButton;
    QPushButton *applyTempButton;
    QWidget *timeModifyPanel;
    QVBoxLayout *timeModifyPanelLayout;
    QHBoxLayout *timeModifyHeaderLayout;
    QLabel *timeModifyTitleLabel;
    QSpacerItem *timeModifyHeaderSpacer;
    QPushButton *applyTimeButton;
    QPushButton *syncPcTimeButton;
    QHBoxLayout *timeModifyFieldsLayout;
    QLineEdit *timeYearLineEdit;
    QLabel *timeYearLabel;
    QLineEdit *timeMonthLineEdit;
    QLabel *timeMonthLabel;
    QLineEdit *timeDayLineEdit;
    QLabel *timeDayLabel;
    QLineEdit *timeWeekdayLineEdit;
    QLabel *timeWeekdayLabel;
    QLineEdit *timeHourLineEdit;
    QLabel *timeHourLabel;
    QLineEdit *timeMinuteLineEdit;
    QLabel *timeMinuteLabel;
    QLineEdit *timeSecondLineEdit;
    QLabel *timeSecondLabel;
    QSpacerItem *timeModifyFieldsSpacer;
    QWidget *referencePanel;
    QVBoxLayout *referencePanelLayout;
    QLabel *referenceTitleLabel;
    QTextEdit *referenceInfoTextEdit;

    void setupUi(QWidget *EsdEditPanel)
    {
        if (EsdEditPanel->objectName().isEmpty())
            EsdEditPanel->setObjectName(QString::fromUtf8("EsdEditPanel"));
        EsdEditPanel->resize(1200, 720);
        mainHorizontalLayout = new QHBoxLayout(EsdEditPanel);
        mainHorizontalLayout->setSpacing(16);
        mainHorizontalLayout->setObjectName(QString::fromUtf8("mainHorizontalLayout"));
        mainHorizontalLayout->setContentsMargins(8, 8, 8, 8);
        leftPanel = new QWidget(EsdEditPanel);
        leftPanel->setObjectName(QString::fromUtf8("leftPanel"));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(3);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(leftPanel->sizePolicy().hasHeightForWidth());
        leftPanel->setSizePolicy(sizePolicy);
        leftPanelLayout = new QVBoxLayout(leftPanel);
        leftPanelLayout->setSpacing(10);
        leftPanelLayout->setObjectName(QString::fromUtf8("leftPanelLayout"));
        leftPanelLayout->setContentsMargins(0, 0, 0, 0);
        queryAddrPanel = new QWidget(leftPanel);
        queryAddrPanel->setObjectName(QString::fromUtf8("queryAddrPanel"));
        queryAddrRowLayout = new QHBoxLayout(queryAddrPanel);
        queryAddrRowLayout->setSpacing(10);
        queryAddrRowLayout->setObjectName(QString::fromUtf8("queryAddrRowLayout"));
        queryAddrRowLayout->setContentsMargins(12, 10, 12, 10);
        queryAddrTitleLabel = new QLabel(queryAddrPanel);
        queryAddrTitleLabel->setObjectName(QString::fromUtf8("queryAddrTitleLabel"));
        queryAddrTitleLabel->setMinimumSize(QSize(72, 0));

        queryAddrRowLayout->addWidget(queryAddrTitleLabel);

        displayAddressLineEdit = new QLineEdit(queryAddrPanel);
        displayAddressLineEdit->setObjectName(QString::fromUtf8("displayAddressLineEdit"));

        queryAddrRowLayout->addWidget(displayAddressLineEdit);

        queryButton = new QPushButton(queryAddrPanel);
        queryButton->setObjectName(QString::fromUtf8("queryButton"));
        queryButton->setMinimumSize(QSize(88, 0));

        queryAddrRowLayout->addWidget(queryButton);


        leftPanelLayout->addWidget(queryAddrPanel);

        modifyAddrPanel = new QWidget(leftPanel);
        modifyAddrPanel->setObjectName(QString::fromUtf8("modifyAddrPanel"));
        modifyAddrRowLayout = new QHBoxLayout(modifyAddrPanel);
        modifyAddrRowLayout->setSpacing(10);
        modifyAddrRowLayout->setObjectName(QString::fromUtf8("modifyAddrRowLayout"));
        modifyAddrRowLayout->setContentsMargins(12, 10, 12, 10);
        modifyAddrTitleLabel = new QLabel(modifyAddrPanel);
        modifyAddrTitleLabel->setObjectName(QString::fromUtf8("modifyAddrTitleLabel"));
        modifyAddrTitleLabel->setMinimumSize(QSize(72, 0));

        modifyAddrRowLayout->addWidget(modifyAddrTitleLabel);

        inputAddressLineEdit = new QLineEdit(modifyAddrPanel);
        inputAddressLineEdit->setObjectName(QString::fromUtf8("inputAddressLineEdit"));

        modifyAddrRowLayout->addWidget(inputAddressLineEdit);

        applyButton = new QPushButton(modifyAddrPanel);
        applyButton->setObjectName(QString::fromUtf8("applyButton"));
        applyButton->setMinimumSize(QSize(88, 0));

        modifyAddrRowLayout->addWidget(applyButton);


        leftPanelLayout->addWidget(modifyAddrPanel);

        tabWidget = new QTabWidget(leftPanel);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));
        sizePolicy.setHeightForWidth(tabWidget->sizePolicy().hasHeightForWidth());
        tabWidget->setSizePolicy(sizePolicy);
        tab1 = new QWidget();
        tab1->setObjectName(QString::fromUtf8("tab1"));
        tab1Layout = new QVBoxLayout(tab1);
        tab1Layout->setSpacing(10);
        tab1Layout->setObjectName(QString::fromUtf8("tab1Layout"));
        tab1Layout->setContentsMargins(12, 12, 12, 12);
        channelSettingsPanel = new QWidget(tab1);
        channelSettingsPanel->setObjectName(QString::fromUtf8("channelSettingsPanel"));
        channelSettingsLayout = new QVBoxLayout(channelSettingsPanel);
        channelSettingsLayout->setSpacing(8);
        channelSettingsLayout->setObjectName(QString::fromUtf8("channelSettingsLayout"));
        channelSettingsLayout->setContentsMargins(12, 10, 12, 10);
        channelSettingsTitleLabel = new QLabel(channelSettingsPanel);
        channelSettingsTitleLabel->setObjectName(QString::fromUtf8("channelSettingsTitleLabel"));

        channelSettingsLayout->addWidget(channelSettingsTitleLabel);

        deviceEditGrid = new QGridLayout();
        deviceEditGrid->setObjectName(QString::fromUtf8("deviceEditGrid"));
        deviceEditGrid->setHorizontalSpacing(10);
        deviceEditGrid->setVerticalSpacing(16);
        typeComboBox = new QComboBox(channelSettingsPanel);
        typeComboBox->setObjectName(QString::fromUtf8("typeComboBox"));
        typeComboBox->setMinimumSize(QSize(100, 0));

        deviceEditGrid->addWidget(typeComboBox, 0, 0, 1, 1);

        valueInputLineEdit = new QLineEdit(channelSettingsPanel);
        valueInputLineEdit->setObjectName(QString::fromUtf8("valueInputLineEdit"));

        deviceEditGrid->addWidget(valueInputLineEdit, 0, 1, 1, 1);

        resultLabel = new QLabel(channelSettingsPanel);
        resultLabel->setObjectName(QString::fromUtf8("resultLabel"));
        resultLabel->setMinimumSize(QSize(56, 0));
        resultLabel->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        deviceEditGrid->addWidget(resultLabel, 0, 2, 1, 1);

        triggerDisplayLineEdit = new QLineEdit(channelSettingsPanel);
        triggerDisplayLineEdit->setObjectName(QString::fromUtf8("triggerDisplayLineEdit"));
        triggerDisplayLineEdit->setReadOnly(true);

        deviceEditGrid->addWidget(triggerDisplayLineEdit, 0, 3, 1, 1);

        setChannelButton = new QPushButton(channelSettingsPanel);
        setChannelButton->setObjectName(QString::fromUtf8("setChannelButton"));
        setChannelButton->setMinimumSize(QSize(88, 0));

        deviceEditGrid->addWidget(setChannelButton, 0, 4, 1, 1);

        wristbandLabel = new QLabel(channelSettingsPanel);
        wristbandLabel->setObjectName(QString::fromUtf8("wristbandLabel"));
        wristbandLabel->setMinimumSize(QSize(56, 0));
        wristbandLabel->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        deviceEditGrid->addWidget(wristbandLabel, 1, 2, 1, 1);

        triggerComboBox = new QComboBox(channelSettingsPanel);
        triggerComboBox->setObjectName(QString::fromUtf8("triggerComboBox"));
        triggerComboBox->setMinimumSize(QSize(160, 0));

        deviceEditGrid->addWidget(triggerComboBox, 1, 3, 1, 1);

        applyChannelButton = new QPushButton(channelSettingsPanel);
        applyChannelButton->setObjectName(QString::fromUtf8("applyChannelButton"));
        applyChannelButton->setMinimumSize(QSize(88, 0));

        deviceEditGrid->addWidget(applyChannelButton, 1, 4, 1, 1);

        upperLimitLabel = new QLabel(channelSettingsPanel);
        upperLimitLabel->setObjectName(QString::fromUtf8("upperLimitLabel"));
        upperLimitLabel->setMinimumSize(QSize(56, 0));
        upperLimitLabel->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        deviceEditGrid->addWidget(upperLimitLabel, 2, 2, 1, 1);

        upperLimitLineEdit = new QLineEdit(channelSettingsPanel);
        upperLimitLineEdit->setObjectName(QString::fromUtf8("upperLimitLineEdit"));

        deviceEditGrid->addWidget(upperLimitLineEdit, 2, 3, 1, 1);

        upperLimitBtnLayout = new QHBoxLayout();
        upperLimitBtnLayout->setSpacing(8);
        upperLimitBtnLayout->setObjectName(QString::fromUtf8("upperLimitBtnLayout"));
        readUpperLimitButton = new QPushButton(channelSettingsPanel);
        readUpperLimitButton->setObjectName(QString::fromUtf8("readUpperLimitButton"));
        readUpperLimitButton->setMinimumSize(QSize(88, 0));

        upperLimitBtnLayout->addWidget(readUpperLimitButton);

        applyUpperLimitButton = new QPushButton(channelSettingsPanel);
        applyUpperLimitButton->setObjectName(QString::fromUtf8("applyUpperLimitButton"));
        applyUpperLimitButton->setMinimumSize(QSize(88, 0));

        upperLimitBtnLayout->addWidget(applyUpperLimitButton);


        deviceEditGrid->addLayout(upperLimitBtnLayout, 2, 4, 1, 1);

        lowerLimitLabel = new QLabel(channelSettingsPanel);
        lowerLimitLabel->setObjectName(QString::fromUtf8("lowerLimitLabel"));
        lowerLimitLabel->setMinimumSize(QSize(56, 0));
        lowerLimitLabel->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        deviceEditGrid->addWidget(lowerLimitLabel, 3, 2, 1, 1);

        lowerLimitLineEdit = new QLineEdit(channelSettingsPanel);
        lowerLimitLineEdit->setObjectName(QString::fromUtf8("lowerLimitLineEdit"));

        deviceEditGrid->addWidget(lowerLimitLineEdit, 3, 3, 1, 1);

        lowerLimitBtnLayout = new QHBoxLayout();
        lowerLimitBtnLayout->setSpacing(8);
        lowerLimitBtnLayout->setObjectName(QString::fromUtf8("lowerLimitBtnLayout"));
        readLowerLimitButton = new QPushButton(channelSettingsPanel);
        readLowerLimitButton->setObjectName(QString::fromUtf8("readLowerLimitButton"));
        readLowerLimitButton->setMinimumSize(QSize(88, 0));

        lowerLimitBtnLayout->addWidget(readLowerLimitButton);

        applyLowerLimitButton = new QPushButton(channelSettingsPanel);
        applyLowerLimitButton->setObjectName(QString::fromUtf8("applyLowerLimitButton"));
        applyLowerLimitButton->setMinimumSize(QSize(88, 0));

        lowerLimitBtnLayout->addWidget(applyLowerLimitButton);


        deviceEditGrid->addLayout(lowerLimitBtnLayout, 3, 4, 1, 1);

        internalResistanceLabel = new QLabel(channelSettingsPanel);
        internalResistanceLabel->setObjectName(QString::fromUtf8("internalResistanceLabel"));
        internalResistanceLabel->setMinimumSize(QSize(56, 0));
        internalResistanceLabel->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        deviceEditGrid->addWidget(internalResistanceLabel, 4, 2, 1, 1);

        internalResistanceLineEdit = new QLineEdit(channelSettingsPanel);
        internalResistanceLineEdit->setObjectName(QString::fromUtf8("internalResistanceLineEdit"));

        deviceEditGrid->addWidget(internalResistanceLineEdit, 4, 3, 1, 1);

        internalResistanceBtnLayout = new QHBoxLayout();
        internalResistanceBtnLayout->setSpacing(8);
        internalResistanceBtnLayout->setObjectName(QString::fromUtf8("internalResistanceBtnLayout"));
        readInternalResistanceButton = new QPushButton(channelSettingsPanel);
        readInternalResistanceButton->setObjectName(QString::fromUtf8("readInternalResistanceButton"));
        readInternalResistanceButton->setMinimumSize(QSize(88, 0));

        internalResistanceBtnLayout->addWidget(readInternalResistanceButton);

        applyInternalResistanceButton = new QPushButton(channelSettingsPanel);
        applyInternalResistanceButton->setObjectName(QString::fromUtf8("applyInternalResistanceButton"));
        applyInternalResistanceButton->setMinimumSize(QSize(88, 0));

        internalResistanceBtnLayout->addWidget(applyInternalResistanceButton);


        deviceEditGrid->addLayout(internalResistanceBtnLayout, 4, 4, 1, 1);


        channelSettingsLayout->addLayout(deviceEditGrid);


        tab1Layout->addWidget(channelSettingsPanel);

        channelGridContainer = new QWidget(tab1);
        channelGridContainer->setObjectName(QString::fromUtf8("channelGridContainer"));
        channelGridOuterLayout = new QVBoxLayout(channelGridContainer);
        channelGridOuterLayout->setSpacing(6);
        channelGridOuterLayout->setObjectName(QString::fromUtf8("channelGridOuterLayout"));
        channelGridOuterLayout->setContentsMargins(12, 10, 12, 10);
        channelGridTitleLabel = new QLabel(channelGridContainer);
        channelGridTitleLabel->setObjectName(QString::fromUtf8("channelGridTitleLabel"));

        channelGridOuterLayout->addWidget(channelGridTitleLabel);

        channelGridHost = new QWidget(channelGridContainer);
        channelGridHost->setObjectName(QString::fromUtf8("channelGridHost"));

        channelGridOuterLayout->addWidget(channelGridHost);


        tab1Layout->addWidget(channelGridContainer);

        tabWidget->addTab(tab1, QString());
        tab3 = new QWidget();
        tab3->setObjectName(QString::fromUtf8("tab3"));
        tab3Layout = new QVBoxLayout(tab3);
        tab3Layout->setSpacing(10);
        tab3Layout->setObjectName(QString::fromUtf8("tab3Layout"));
        tab3Layout->setContentsMargins(12, 12, 12, 12);
        delayParamPanel = new QWidget(tab3);
        delayParamPanel->setObjectName(QString::fromUtf8("delayParamPanel"));
        delayRowLayout = new QHBoxLayout(delayParamPanel);
        delayRowLayout->setSpacing(10);
        delayRowLayout->setObjectName(QString::fromUtf8("delayRowLayout"));
        delayRowLayout->setContentsMargins(12, 10, 12, 10);
        delayParamTitleLabel = new QLabel(delayParamPanel);
        delayParamTitleLabel->setObjectName(QString::fromUtf8("delayParamTitleLabel"));
        delayParamTitleLabel->setMinimumSize(QSize(72, 0));

        delayRowLayout->addWidget(delayParamTitleLabel);

        delayTypeComboBox = new QComboBox(delayParamPanel);
        delayTypeComboBox->setObjectName(QString::fromUtf8("delayTypeComboBox"));
        delayTypeComboBox->setMinimumSize(QSize(150, 0));

        delayRowLayout->addWidget(delayTypeComboBox);

        delayTimeLineEdit = new QLineEdit(delayParamPanel);
        delayTimeLineEdit->setObjectName(QString::fromUtf8("delayTimeLineEdit"));

        delayRowLayout->addWidget(delayTimeLineEdit);

        applyDelayButton = new QPushButton(delayParamPanel);
        applyDelayButton->setObjectName(QString::fromUtf8("applyDelayButton"));
        applyDelayButton->setMinimumSize(QSize(88, 0));

        delayRowLayout->addWidget(applyDelayButton);


        tab3Layout->addWidget(delayParamPanel);

        groundParamPanel = new QWidget(tab3);
        groundParamPanel->setObjectName(QString::fromUtf8("groundParamPanel"));
        groundRowLayout = new QHBoxLayout(groundParamPanel);
        groundRowLayout->setSpacing(10);
        groundRowLayout->setObjectName(QString::fromUtf8("groundRowLayout"));
        groundRowLayout->setContentsMargins(12, 10, 12, 10);
        groundParamTitleLabel = new QLabel(groundParamPanel);
        groundParamTitleLabel->setObjectName(QString::fromUtf8("groundParamTitleLabel"));
        groundParamTitleLabel->setMinimumSize(QSize(72, 0));

        groundRowLayout->addWidget(groundParamTitleLabel);

        groundTypeComboBox = new QComboBox(groundParamPanel);
        groundTypeComboBox->setObjectName(QString::fromUtf8("groundTypeComboBox"));
        groundTypeComboBox->setMinimumSize(QSize(150, 0));

        groundRowLayout->addWidget(groundTypeComboBox);

        groundValueLineEdit = new QLineEdit(groundParamPanel);
        groundValueLineEdit->setObjectName(QString::fromUtf8("groundValueLineEdit"));

        groundRowLayout->addWidget(groundValueLineEdit);

        readGroundButton = new QPushButton(groundParamPanel);
        readGroundButton->setObjectName(QString::fromUtf8("readGroundButton"));
        readGroundButton->setMinimumSize(QSize(88, 0));

        groundRowLayout->addWidget(readGroundButton);

        applyGroundButton = new QPushButton(groundParamPanel);
        applyGroundButton->setObjectName(QString::fromUtf8("applyGroundButton"));
        applyGroundButton->setMinimumSize(QSize(88, 0));

        groundRowLayout->addWidget(applyGroundButton);


        tab3Layout->addWidget(groundParamPanel);

        tempParamPanel = new QWidget(tab3);
        tempParamPanel->setObjectName(QString::fromUtf8("tempParamPanel"));
        tempRowLayout = new QHBoxLayout(tempParamPanel);
        tempRowLayout->setSpacing(10);
        tempRowLayout->setObjectName(QString::fromUtf8("tempRowLayout"));
        tempRowLayout->setContentsMargins(12, 10, 12, 10);
        tempParamTitleLabel = new QLabel(tempParamPanel);
        tempParamTitleLabel->setObjectName(QString::fromUtf8("tempParamTitleLabel"));
        tempParamTitleLabel->setMinimumSize(QSize(72, 0));

        tempRowLayout->addWidget(tempParamTitleLabel);

        tempTypeComboBox = new QComboBox(tempParamPanel);
        tempTypeComboBox->setObjectName(QString::fromUtf8("tempTypeComboBox"));
        tempTypeComboBox->setMinimumSize(QSize(150, 0));

        tempRowLayout->addWidget(tempTypeComboBox);

        tempValueLineEdit = new QLineEdit(tempParamPanel);
        tempValueLineEdit->setObjectName(QString::fromUtf8("tempValueLineEdit"));

        tempRowLayout->addWidget(tempValueLineEdit);

        readTempButton = new QPushButton(tempParamPanel);
        readTempButton->setObjectName(QString::fromUtf8("readTempButton"));
        readTempButton->setMinimumSize(QSize(88, 0));

        tempRowLayout->addWidget(readTempButton);

        applyTempButton = new QPushButton(tempParamPanel);
        applyTempButton->setObjectName(QString::fromUtf8("applyTempButton"));
        applyTempButton->setMinimumSize(QSize(88, 0));

        tempRowLayout->addWidget(applyTempButton);


        tab3Layout->addWidget(tempParamPanel);

        timeModifyPanel = new QWidget(tab3);
        timeModifyPanel->setObjectName(QString::fromUtf8("timeModifyPanel"));
        timeModifyPanelLayout = new QVBoxLayout(timeModifyPanel);
        timeModifyPanelLayout->setSpacing(10);
        timeModifyPanelLayout->setObjectName(QString::fromUtf8("timeModifyPanelLayout"));
        timeModifyPanelLayout->setContentsMargins(12, 10, 12, 10);
        timeModifyHeaderLayout = new QHBoxLayout();
        timeModifyHeaderLayout->setSpacing(10);
        timeModifyHeaderLayout->setObjectName(QString::fromUtf8("timeModifyHeaderLayout"));
        timeModifyTitleLabel = new QLabel(timeModifyPanel);
        timeModifyTitleLabel->setObjectName(QString::fromUtf8("timeModifyTitleLabel"));

        timeModifyHeaderLayout->addWidget(timeModifyTitleLabel);

        timeModifyHeaderSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        timeModifyHeaderLayout->addItem(timeModifyHeaderSpacer);

        applyTimeButton = new QPushButton(timeModifyPanel);
        applyTimeButton->setObjectName(QString::fromUtf8("applyTimeButton"));
        applyTimeButton->setMinimumSize(QSize(88, 0));

        timeModifyHeaderLayout->addWidget(applyTimeButton);

        syncPcTimeButton = new QPushButton(timeModifyPanel);
        syncPcTimeButton->setObjectName(QString::fromUtf8("syncPcTimeButton"));
        syncPcTimeButton->setMinimumSize(QSize(156, 0));

        timeModifyHeaderLayout->addWidget(syncPcTimeButton);


        timeModifyPanelLayout->addLayout(timeModifyHeaderLayout);

        timeModifyFieldsLayout = new QHBoxLayout();
        timeModifyFieldsLayout->setSpacing(6);
        timeModifyFieldsLayout->setObjectName(QString::fromUtf8("timeModifyFieldsLayout"));
        timeYearLineEdit = new QLineEdit(timeModifyPanel);
        timeYearLineEdit->setObjectName(QString::fromUtf8("timeYearLineEdit"));
        timeYearLineEdit->setMinimumSize(QSize(72, 0));
        timeYearLineEdit->setMaximumSize(QSize(80, 16777215));
        timeYearLineEdit->setAlignment(Qt::AlignCenter);

        timeModifyFieldsLayout->addWidget(timeYearLineEdit);

        timeYearLabel = new QLabel(timeModifyPanel);
        timeYearLabel->setObjectName(QString::fromUtf8("timeYearLabel"));

        timeModifyFieldsLayout->addWidget(timeYearLabel);

        timeMonthLineEdit = new QLineEdit(timeModifyPanel);
        timeMonthLineEdit->setObjectName(QString::fromUtf8("timeMonthLineEdit"));
        timeMonthLineEdit->setMinimumSize(QSize(40, 0));
        timeMonthLineEdit->setMaximumSize(QSize(48, 16777215));
        timeMonthLineEdit->setAlignment(Qt::AlignCenter);

        timeModifyFieldsLayout->addWidget(timeMonthLineEdit);

        timeMonthLabel = new QLabel(timeModifyPanel);
        timeMonthLabel->setObjectName(QString::fromUtf8("timeMonthLabel"));

        timeModifyFieldsLayout->addWidget(timeMonthLabel);

        timeDayLineEdit = new QLineEdit(timeModifyPanel);
        timeDayLineEdit->setObjectName(QString::fromUtf8("timeDayLineEdit"));
        timeDayLineEdit->setMinimumSize(QSize(40, 0));
        timeDayLineEdit->setMaximumSize(QSize(48, 16777215));
        timeDayLineEdit->setAlignment(Qt::AlignCenter);

        timeModifyFieldsLayout->addWidget(timeDayLineEdit);

        timeDayLabel = new QLabel(timeModifyPanel);
        timeDayLabel->setObjectName(QString::fromUtf8("timeDayLabel"));

        timeModifyFieldsLayout->addWidget(timeDayLabel);

        timeWeekdayLineEdit = new QLineEdit(timeModifyPanel);
        timeWeekdayLineEdit->setObjectName(QString::fromUtf8("timeWeekdayLineEdit"));
        timeWeekdayLineEdit->setMinimumSize(QSize(40, 0));
        timeWeekdayLineEdit->setMaximumSize(QSize(48, 16777215));
        timeWeekdayLineEdit->setAlignment(Qt::AlignCenter);

        timeModifyFieldsLayout->addWidget(timeWeekdayLineEdit);

        timeWeekdayLabel = new QLabel(timeModifyPanel);
        timeWeekdayLabel->setObjectName(QString::fromUtf8("timeWeekdayLabel"));

        timeModifyFieldsLayout->addWidget(timeWeekdayLabel);

        timeHourLineEdit = new QLineEdit(timeModifyPanel);
        timeHourLineEdit->setObjectName(QString::fromUtf8("timeHourLineEdit"));
        timeHourLineEdit->setMinimumSize(QSize(40, 0));
        timeHourLineEdit->setMaximumSize(QSize(48, 16777215));
        timeHourLineEdit->setAlignment(Qt::AlignCenter);

        timeModifyFieldsLayout->addWidget(timeHourLineEdit);

        timeHourLabel = new QLabel(timeModifyPanel);
        timeHourLabel->setObjectName(QString::fromUtf8("timeHourLabel"));

        timeModifyFieldsLayout->addWidget(timeHourLabel);

        timeMinuteLineEdit = new QLineEdit(timeModifyPanel);
        timeMinuteLineEdit->setObjectName(QString::fromUtf8("timeMinuteLineEdit"));
        timeMinuteLineEdit->setMinimumSize(QSize(40, 0));
        timeMinuteLineEdit->setMaximumSize(QSize(48, 16777215));
        timeMinuteLineEdit->setAlignment(Qt::AlignCenter);

        timeModifyFieldsLayout->addWidget(timeMinuteLineEdit);

        timeMinuteLabel = new QLabel(timeModifyPanel);
        timeMinuteLabel->setObjectName(QString::fromUtf8("timeMinuteLabel"));

        timeModifyFieldsLayout->addWidget(timeMinuteLabel);

        timeSecondLineEdit = new QLineEdit(timeModifyPanel);
        timeSecondLineEdit->setObjectName(QString::fromUtf8("timeSecondLineEdit"));
        timeSecondLineEdit->setMinimumSize(QSize(40, 0));
        timeSecondLineEdit->setMaximumSize(QSize(48, 16777215));
        timeSecondLineEdit->setAlignment(Qt::AlignCenter);

        timeModifyFieldsLayout->addWidget(timeSecondLineEdit);

        timeSecondLabel = new QLabel(timeModifyPanel);
        timeSecondLabel->setObjectName(QString::fromUtf8("timeSecondLabel"));

        timeModifyFieldsLayout->addWidget(timeSecondLabel);

        timeModifyFieldsSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        timeModifyFieldsLayout->addItem(timeModifyFieldsSpacer);


        timeModifyPanelLayout->addLayout(timeModifyFieldsLayout);


        tab3Layout->addWidget(timeModifyPanel);

        tabWidget->addTab(tab3, QString());

        leftPanelLayout->addWidget(tabWidget);


        mainHorizontalLayout->addWidget(leftPanel);

        referencePanel = new QWidget(EsdEditPanel);
        referencePanel->setObjectName(QString::fromUtf8("referencePanel"));
        referencePanelLayout = new QVBoxLayout(referencePanel);
        referencePanelLayout->setSpacing(8);
        referencePanelLayout->setObjectName(QString::fromUtf8("referencePanelLayout"));
        referencePanelLayout->setContentsMargins(12, 10, 12, 10);
        referenceTitleLabel = new QLabel(referencePanel);
        referenceTitleLabel->setObjectName(QString::fromUtf8("referenceTitleLabel"));

        referencePanelLayout->addWidget(referenceTitleLabel);

        referenceInfoTextEdit = new QTextEdit(referencePanel);
        referenceInfoTextEdit->setObjectName(QString::fromUtf8("referenceInfoTextEdit"));
        QSizePolicy sizePolicy1(QSizePolicy::Preferred, QSizePolicy::Expanding);
        sizePolicy1.setHorizontalStretch(1);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(referenceInfoTextEdit->sizePolicy().hasHeightForWidth());
        referenceInfoTextEdit->setSizePolicy(sizePolicy1);
        referenceInfoTextEdit->setMinimumSize(QSize(260, 0));
        referenceInfoTextEdit->setMaximumSize(QSize(320, 16777215));
        referenceInfoTextEdit->setReadOnly(true);

        referencePanelLayout->addWidget(referenceInfoTextEdit);


        mainHorizontalLayout->addWidget(referencePanel);


        retranslateUi(EsdEditPanel);

        tabWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(EsdEditPanel);
    } // setupUi

    void retranslateUi(QWidget *EsdEditPanel)
    {
        queryAddrTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\346\237\245\350\257\242\345\234\260\345\235\200", nullptr));
        displayAddressLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\346\230\276\347\244\272\345\234\260\345\235\200", nullptr));
        queryButton->setText(QCoreApplication::translate("EsdEditPanel", "\346\237\245\350\257\242", nullptr));
        modifyAddrTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\344\277\256\346\224\271\345\234\260\345\235\200", nullptr));
        inputAddressLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\350\276\223\345\205\245\346\226\260\345\234\260\345\235\200", nullptr));
        applyButton->setText(QCoreApplication::translate("EsdEditPanel", "\345\272\224\347\224\250", nullptr));
        channelSettingsTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\351\200\232\351\201\223\344\270\216\344\270\212\344\270\213\351\231\220", nullptr));
        valueInputLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\351\200\232\351\201\223(1-8\346\210\2261-3)", nullptr));
        resultLabel->setText(QCoreApplication::translate("EsdEditPanel", "\347\273\223\346\236\234", nullptr));
        triggerDisplayLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\350\257\273\345\217\226\347\273\223\346\236\234", nullptr));
        setChannelButton->setText(QCoreApplication::translate("EsdEditPanel", "\350\257\273\345\217\226", nullptr));
        wristbandLabel->setText(QCoreApplication::translate("EsdEditPanel", "\350\205\225\345\270\246", nullptr));
        applyChannelButton->setText(QCoreApplication::translate("EsdEditPanel", "\345\272\224\347\224\250", nullptr));
        upperLimitLabel->setText(QCoreApplication::translate("EsdEditPanel", "\344\270\212\351\231\220", nullptr));
        upperLimitLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\344\270\212\351\231\220", nullptr));
        readUpperLimitButton->setText(QCoreApplication::translate("EsdEditPanel", "\350\257\273\345\217\226", nullptr));
        applyUpperLimitButton->setText(QCoreApplication::translate("EsdEditPanel", "\345\272\224\347\224\250", nullptr));
        lowerLimitLabel->setText(QCoreApplication::translate("EsdEditPanel", "\344\270\213\351\231\220", nullptr));
        lowerLimitLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\344\270\213\351\231\220", nullptr));
        readLowerLimitButton->setText(QCoreApplication::translate("EsdEditPanel", "\350\257\273\345\217\226", nullptr));
        applyLowerLimitButton->setText(QCoreApplication::translate("EsdEditPanel", "\345\272\224\347\224\250", nullptr));
        internalResistanceLabel->setText(QCoreApplication::translate("EsdEditPanel", "\345\206\205\351\230\273", nullptr));
        internalResistanceLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\345\206\205\351\230\273", nullptr));
        readInternalResistanceButton->setText(QCoreApplication::translate("EsdEditPanel", "\350\257\273\345\217\226", nullptr));
        applyInternalResistanceButton->setText(QCoreApplication::translate("EsdEditPanel", "\345\272\224\347\224\250", nullptr));
        channelGridTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\345\220\204\351\200\232\351\201\223\345\200\274", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab1), QCoreApplication::translate("EsdEditPanel", "\344\277\256\346\224\271\350\256\276\345\244\207\350\256\276\347\275\256", nullptr));
        delayParamTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\345\273\266\350\277\237\350\256\276\347\275\256", nullptr));
        delayTimeLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\345\273\266\350\277\237\346\227\266\351\227\264", nullptr));
        applyDelayButton->setText(QCoreApplication::translate("EsdEditPanel", "\344\277\256\346\224\271", nullptr));
        groundParamTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\346\216\245\345\234\260\347\224\265\345\216\213", nullptr));
        groundValueLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\346\216\245\345\234\260\347\224\265\345\216\213\344\270\212\351\231\220", nullptr));
        readGroundButton->setText(QCoreApplication::translate("EsdEditPanel", "\350\257\273\345\217\226", nullptr));
        applyGroundButton->setText(QCoreApplication::translate("EsdEditPanel", "\345\272\224\347\224\250", nullptr));
        tempParamTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\346\270\251\345\272\246\344\270\212\351\231\220", nullptr));
        tempValueLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "\346\270\251\345\272\246\344\270\212\351\231\220", nullptr));
        readTempButton->setText(QCoreApplication::translate("EsdEditPanel", "\350\257\273\345\217\226", nullptr));
        applyTempButton->setText(QCoreApplication::translate("EsdEditPanel", "\345\272\224\347\224\250", nullptr));
        timeModifyTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\344\277\256\346\224\271\346\227\266\351\227\264", nullptr));
        applyTimeButton->setText(QCoreApplication::translate("EsdEditPanel", "\344\277\256\346\224\271", nullptr));
        syncPcTimeButton->setText(QCoreApplication::translate("EsdEditPanel", "\345\220\214\346\255\245\347\224\265\350\204\221\346\227\266\351\227\264", nullptr));
        timeYearLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "2026", nullptr));
        timeYearLabel->setText(QCoreApplication::translate("EsdEditPanel", "\345\271\264", nullptr));
        timeMonthLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "7", nullptr));
        timeMonthLabel->setText(QCoreApplication::translate("EsdEditPanel", "\346\234\210", nullptr));
        timeDayLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "8", nullptr));
        timeDayLabel->setText(QCoreApplication::translate("EsdEditPanel", "\346\227\245", nullptr));
        timeWeekdayLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "3", nullptr));
        timeWeekdayLabel->setText(QCoreApplication::translate("EsdEditPanel", "\346\230\237\346\234\237", nullptr));
        timeHourLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "12", nullptr));
        timeHourLabel->setText(QCoreApplication::translate("EsdEditPanel", "\346\227\266", nullptr));
        timeMinuteLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "30", nullptr));
        timeMinuteLabel->setText(QCoreApplication::translate("EsdEditPanel", "\345\210\206", nullptr));
        timeSecondLineEdit->setPlaceholderText(QCoreApplication::translate("EsdEditPanel", "45", nullptr));
        timeSecondLabel->setText(QCoreApplication::translate("EsdEditPanel", "\347\247\222", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab3), QCoreApplication::translate("EsdEditPanel", "\344\277\256\346\224\271\350\256\276\345\244\207\345\217\202\346\225\260", nullptr));
        referenceTitleLabel->setText(QCoreApplication::translate("EsdEditPanel", "\345\217\202\346\225\260\345\217\202\350\200\203", nullptr));
        (void)EsdEditPanel;
    } // retranslateUi

};

namespace Ui {
    class EsdEditPanel: public Ui_EsdEditPanel {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ESDEDITPANEL_H
