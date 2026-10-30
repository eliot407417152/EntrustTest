#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_EntrustTest.h"

class EntrustTest : public QMainWindow
{
    Q_OBJECT

public:
    EntrustTest(QWidget *parent = nullptr);
    ~EntrustTest();

private:
    Ui::EntrustTestClass ui;
};

