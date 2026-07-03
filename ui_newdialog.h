/********************************************************************************
** Form generated from reading UI file 'newdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.14.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_NEWDIALOG_H
#define UI_NEWDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_newdialog
{
public:
    QWidget *layoutWidget;
    QVBoxLayout *verticalLayout;
    QTableWidget *tableWidget;
    QHBoxLayout *horizontalLayout;
    QPushButton *addRowBtn;
    QPushButton *deleteRowBtn;
    QPushButton *saveBtn;

    void setupUi(QDialog *newdialog)
    {
        if (newdialog->objectName().isEmpty())
            newdialog->setObjectName(QString::fromUtf8("newdialog"));
        newdialog->resize(900, 500);
        layoutWidget = new QWidget(newdialog);
        layoutWidget->setObjectName(QString::fromUtf8("layoutWidget"));
        layoutWidget->setGeometry(QRect(20, 10, 860, 441));
        verticalLayout = new QVBoxLayout(layoutWidget);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        tableWidget = new QTableWidget(layoutWidget);
        if (tableWidget->columnCount() < 6)
            tableWidget->setColumnCount(6);
        QTableWidgetItem *__qtablewidgetitem = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(0, __qtablewidgetitem);
        QTableWidgetItem *__qtablewidgetitem1 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(1, __qtablewidgetitem1);
        QTableWidgetItem *__qtablewidgetitem2 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(2, __qtablewidgetitem2);
        QTableWidgetItem *__qtablewidgetitem3 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(3, __qtablewidgetitem3);
        QTableWidgetItem *__qtablewidgetitem4 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(4, __qtablewidgetitem4);
        QTableWidgetItem *__qtablewidgetitem5 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(5, __qtablewidgetitem5);
        if (tableWidget->rowCount() < 1)
            tableWidget->setRowCount(1);
        tableWidget->setObjectName(QString::fromUtf8("tableWidget"));
        tableWidget->setRowCount(1);
        tableWidget->setColumnCount(6);
        tableWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        tableWidget->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);

        verticalLayout->addWidget(tableWidget);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        addRowBtn = new QPushButton(layoutWidget);
        addRowBtn->setObjectName(QString::fromUtf8("addRowBtn"));

        horizontalLayout->addWidget(addRowBtn);

        deleteRowBtn = new QPushButton(layoutWidget);
        deleteRowBtn->setObjectName(QString::fromUtf8("deleteRowBtn"));

        horizontalLayout->addWidget(deleteRowBtn);

        saveBtn = new QPushButton(layoutWidget);
        saveBtn->setObjectName(QString::fromUtf8("saveBtn"));

        horizontalLayout->addWidget(saveBtn);


        verticalLayout->addLayout(horizontalLayout);


        retranslateUi(newdialog);

        QMetaObject::connectSlotsByName(newdialog);
    } // setupUi

    void retranslateUi(QDialog *newdialog)
    {
        newdialog->setWindowTitle(QCoreApplication::translate("newdialog", "Dialog", nullptr));
        QTableWidgetItem *___qtablewidgetitem = tableWidget->horizontalHeaderItem(0);
        ___qtablewidgetitem->setText(QCoreApplication::translate("newdialog", "\345\234\260\345\235\200", nullptr));
        QTableWidgetItem *___qtablewidgetitem1 = tableWidget->horizontalHeaderItem(1);
        ___qtablewidgetitem1->setText(QCoreApplication::translate("newdialog", "\350\205\225\345\270\246", nullptr));
        QTableWidgetItem *___qtablewidgetitem2 = tableWidget->horizontalHeaderItem(2);
        ___qtablewidgetitem2->setText(QCoreApplication::translate("newdialog", "\345\217\260\345\236\253", nullptr));
        QTableWidgetItem *___qtablewidgetitem3 = tableWidget->horizontalHeaderItem(3);
        ___qtablewidgetitem3->setText(QCoreApplication::translate("newdialog", "\350\256\276\345\244\207", nullptr));
        QTableWidgetItem *___qtablewidgetitem4 = tableWidget->horizontalHeaderItem(4);
        ___qtablewidgetitem4->setText(QCoreApplication::translate("newdialog", "\345\260\230\345\237\203", nullptr));
        QTableWidgetItem *___qtablewidgetitem5 = tableWidget->horizontalHeaderItem(5);
        ___qtablewidgetitem5->setText(QCoreApplication::translate("newdialog", "\347\246\273\345\255\220\351\243\216\346\234\272", nullptr));
        addRowBtn->setText(QCoreApplication::translate("newdialog", "\346\267\273\345\212\240\350\241\214", nullptr));
        deleteRowBtn->setText(QCoreApplication::translate("newdialog", "\345\210\240\351\231\244\350\241\214", nullptr));
        saveBtn->setText(QCoreApplication::translate("newdialog", "\344\277\235\345\255\230\351\205\215\347\275\256", nullptr));
    } // retranslateUi

};

namespace Ui {
    class newdialog: public Ui_newdialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_NEWDIALOG_H
