#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QHash>
#include <QPoint>
#include "antfieldwidget.h"

// Forward declarations
class AntFieldWidget;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QTableWidget;
class QGroupBox;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void updateRules();
    void onAntMoved(int x, int y, int direction, int steps);
    void updateStatisticsTable();
    void takeStep();
    void onQuickStepsClicked();
    void resetSimulation();
    void centerView();
    void centerOnMostVisited();
    void centerOnCoordinates();
    void moveView(int dx, int dy);
    void zoomIn();
    void zoomOut();
    void onZoomChanged(double zoom);
    void changeCellSize();
    void loadPreset(int index);
    void showStatistics();
    void toggleStatistics(bool enabled);
    void exportStatistics();
    void showCellDetails();
    void centerOnTableCell();
    void centerOnSelectedStatistic();
    void onMouseOverCell(int x, int y);
    void changeStyle();

private:
    void loadPresets();
    void setupUI();
    void setupConnections();
    void updateQuickStatistics();

    QHash<int, QString> presets;
    AntFieldWidget *antField;

    // UI widgets
    QLineEdit *rulesEdit;
    QSpinBox *quickStepsSpin;
    QPushButton *quickStepsButton;
    QPushButton *styleButton;
    QCheckBox *statsCheckBox;
    QLabel *zoomLabel;
    QLabel *stepsLabel;
    QLabel *statsLabel;
    QLabel *uniqueCellsLabel;
    QLabel *mostVisitedLabel;
    QLabel *maxVisitsLabel;
    QLabel *averageVisitsLabel;
    QLabel *rulesLabel;
    QGroupBox *statsTableGroup;
    QTableWidget *statsTable;
    QLabel *coordinateLabel;
    QLabel *mouseCoordinateLabel;

    int lastCustomSteps = 1000;
    int currentStyleIndex = 0;
};

#endif // MAINWINDOW_H
