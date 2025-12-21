#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>

class QLineEdit;
class QLabel;
class QPushButton;
class QComboBox;
class QSpinBox;
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
    void moveView(int dx, int dy);
    void zoomIn();
    void zoomOut();
    void changeCellSize();
    void loadPreset(int index);
    void onZoomChanged(double zoom);
    void onQuickStepsClicked();

private:
    void setupUI();
    void setupConnections();
    void loadPresets();

    AntFieldWidget *antField = nullptr;
    QLineEdit *rulesEdit = nullptr;
    QLabel *rulesLabel = nullptr;
    QLabel *zoomLabel = nullptr;
    QLabel *stepsLabel = nullptr;
    QSpinBox *quickStepsSpin = nullptr;
    QPushButton *quickStepsButton = nullptr;

    int lastCustomSteps = 10000;
    QMap<int, QString> presets;
};

#endif // MAINWINDOW_H
