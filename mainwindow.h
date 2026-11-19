#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QHash>
#include "antfieldwidget.h"
#include "QInt64SpinBox.h"

// Forward declarations
class AntFieldWidget;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QTableWidget;
class QGroupBox;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void updateRules();
    void onAntMoved(qint64 x, qint64 y, int direction, qint64 steps) const;
    void updateStatisticsTable();
    void takeStep() const;
    void onQuickStepsClicked();
    void resetSimulation();
    void centerView() const;
    void centerOnMostVisited() const;
    void centerOnCoordinates();
    void moveView(qint64 dx, qint64 dy) const;
    void zoomIn() const;
    void zoomOut() const;
    void onZoomChanged(double zoom) const;
    void changeCellSize();
    void loadPreset(int index);
    void showStatistics();
    void toggleStatistics(bool enabled);
    void saveState();
    void loadState();
    void showCellDetails();
    void centerOnTableCell();
    void centerOnSelectedStatistic();
    void onMouseOverCell(qint64 x, qint64 y) const;
    void changeStyle();
    void showHighwayAnalysis();
    void randomizeArea();
    void startSimulation();
    void stopSimulation();
    void autoStep();

private:
    void loadPresets();
    void setupUI();
    void setupConnections();
    void beginRun(std::optional<qint64> steps);
    void cancelFiniteRun();
    void updateQuickStatistics() const;

    QHash<int, QString> presets;
    AntFieldWidget *antField{};

    // UI widgets
    QLineEdit *rulesEdit{};
    QInt64SpinBox* quickStepsSpin{};
    QPushButton *quickStepsButton{};
    QPushButton *styleButton{};
    QCheckBox *statsCheckBox{};
    QLabel *zoomLabel{};
    QLabel *stepsLabel{};
    QLabel *statsLabel{};
    QWidget *highwayPanel{};
    QLabel *highwayResult{};
    QLabel *uniqueCellsLabel{};
    QLabel *mostVisitedLabel{};
    QLabel *maxVisitsLabel{};
    QLabel *averageVisitsLabel{};
    QLabel *rulesLabel{};
    QGroupBox *statsTableGroup{};
    QTableWidget *statsTable{};
    QLabel *coordinateLabel{};
    QPushButton *cellDetailsButton{};
    QPushButton *statsDialogButton{};
    QPushButton *togglePanelButton{};
    QPushButton *rulesButton{};
    QPushButton *stepButton{};
    QPushButton *resetButton{};
    QPushButton *centerButton{};
    QPushButton *zoomOutButton{};
    QPushButton *zoomInButton{};
    QPushButton *cellSizeButton{};
    QPushButton *centerMostVisitedButton{};
    QPushButton *centerCoordinatesButton{};
    QPushButton *centerTableButton{};
    QPushButton *centerStatsButton{};
    QPushButton *leftButton{};
    QPushButton *upButton{};
    QPushButton *downButton{};
    QPushButton *rightButton{};
    QSpinBox *radiusSpin{};
    QPushButton *randomizeButton{};
    QAction *exportAction{};
    QAction *resetStatsAction{};
    QAction *exitAction{};
    QPushButton *startButton{};
    QPushButton *stopButton{};
    QTimer *autoRunTimer{};
    QAction *saveAction{};
    QAction *loadAction{};
    QPushButton *saveButton{};
    QPushButton *loadButton{};

    // An empty count represents continuous mode while the run timer is active.
    std::optional<qint64> remainingSteps;
    QElapsedTimer lastFrame;
    QVector<QPair<QPoint64, qint64>> displayedTopCells;
    qint64 lastCustomSteps = 1000;
    int currentStyleIndex = 0;
};

#endif // MAINWINDOW_H