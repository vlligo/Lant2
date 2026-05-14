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

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void updateRules();
    void onAntMoved(qint64 x, qint64 y, int direction, qint64 steps) const;
    void updateStatisticsTable() const;
    void takeStep() const;
    void onQuickStepsClicked();
    void resetSimulation() const;
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
    void toggleStatistics(bool enabled) const;
    void exportStatistics();
    void showCellDetails();
    void centerOnTableCell();
    void centerOnSelectedStatistic();
    void onMouseOverCell(qint64 x, qint64 y) const;
    void changeStyle();

private:
    void loadPresets();
    void setupUI();
    void setupConnections();
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
    QPushButton *zoomOutButton;
    QPushButton *zoomInButton;
    QPushButton *cellSizeButton{};
    QPushButton *centerMostVisitedButton{};
    QPushButton *centerCoordinatesButton{};
    QPushButton *centerTableButton{};
    QPushButton *centerStatsButton{};
    QPushButton *leftButton{};
    QPushButton *upButton{};
    QPushButton *downButton{};
    QPushButton *rightButton{};
    QAction *exportAction{};
    QAction *resetStatsAction{};
    QAction *exitAction{};

    qint64 lastCustomSteps = 1000;
    int currentStyleIndex = 0;
};

#endif // MAINWINDOW_H
