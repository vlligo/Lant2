#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QStatusBar>
#include <QScrollArea>
#include <QSpinBox>

class AntFieldWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

private slots:
    void updateRules();
    void onAntMoved(int x, int y, int direction, int steps);
    void takeStep();
    void resetSimulation();
    void centerView();
    void moveViewLeft();
    void moveViewRight();
    void moveViewUp();
    void moveViewDown();
    void zoomIn();
    void zoomOut();
    void changeCellSize();
    void loadPreset(int index);
    void onZoomChanged(double zoom);
    void onQuickStepsClicked();

private:
    void setupUI();
    void setupConnections();

    AntFieldWidget *antField;
    QLineEdit *rulesEdit;
    QLabel *rulesLabel;
    QLabel *zoomLabel;
    QLabel *stepsLabel;
    QSpinBox *quickStepsSpin;
    QPushButton *quickStepsButton;
    int lastCustomSteps = 10000;
};

#endif // MAINWINDOW_H
