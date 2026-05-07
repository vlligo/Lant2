#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

#include "antfieldwidget.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    loadPresets();
    setupUI();
    setupConnections();

    rulesEdit->setText("LR");
    updateRules();

    setWindowTitle("Langton's Ant with Statistics");
    resize(1200, 800);
}

void MainWindow::loadPresets() {
    presets = {
        {0, "LR"},
        {1, "LLRR"},
        {2, "LLRRRLRLRLLR"},
        {3, "LRRRRRLLR"},
    };
}

void MainWindow::setupUI() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);

    // Create menu bar
    QMenu* fileMenu = menuBar()->addMenu("&File");
    QAction* exportAction = fileMenu->addAction("&Export Statistics...");
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction("E&xit");

    QMenu* viewMenu = menuBar()->addMenu("&View");
    QAction* statsAction = viewMenu->addAction("&Show Statistics Panel");
    statsAction->setCheckable(true);
    statsAction->setChecked(false);  // Hidden by default

    QMenu* simulationMenu = menuBar()->addMenu("&Simulation");
    QAction* resetStatsAction = simulationMenu->addAction("&Reset Statistics");

    // Status bar - create labels once
    statsLabel = new QLabel();
    coordinateLabel = new QLabel("Mouse: (0, 0)");

    // Add them to status bar
    statusBar()->addPermanentWidget(statsLabel);
    statusBar()->addPermanentWidget(coordinateLabel);
    statusBar()->showMessage("Ready");

    // Control panel
    QGroupBox* controlGroup = new QGroupBox("Controls");
    QGridLayout* controlLayout = new QGridLayout(controlGroup);

    int row = 0;

    // Rules section
    controlLayout->addWidget(new QLabel("Rules:"), row, 0);
    rulesEdit = new QLineEdit();
    controlLayout->addWidget(rulesEdit, row, 1);

    QPushButton* rulesButton = new QPushButton("Update Rules");
    controlLayout->addWidget(rulesButton, row, 2);

    QComboBox* presetCombo = new QComboBox();
    presetCombo->addItems(
        {"Classic LR", "Symmetric LLRR", "Highway", "Complex"});
    controlLayout->addWidget(presetCombo, row, 3);

    rulesLabel = new QLabel("Rules: LR");
    controlLayout->addWidget(rulesLabel, row, 4, 1, 2);

    row++;

    // Step controls
    QPushButton* stepButton = new QPushButton("Step (1)");
    controlLayout->addWidget(stepButton, row, 0);

    quickStepsSpin = new QSpinBox();
    quickStepsSpin->setRange(1, INT_MAX);
    quickStepsSpin->setValue(lastCustomSteps);
    quickStepsSpin->setSingleStep(1000);
    quickStepsSpin->setSuffix(" steps");
    controlLayout->addWidget(quickStepsSpin, row, 1);

    quickStepsButton = new QPushButton("Run");
    controlLayout->addWidget(quickStepsButton, row, 2);

    QPushButton* resetButton = new QPushButton("Reset");
    controlLayout->addWidget(resetButton, row, 3);

    statsCheckBox = new QCheckBox("Track Statistics");
    statsCheckBox->setChecked(true);
    controlLayout->addWidget(statsCheckBox, row, 4);

    row++;

    // View controls
    QPushButton* centerButton = new QPushButton("Center on Ant");
    controlLayout->addWidget(centerButton, row, 0);

    QPushButton* zoomOutButton = new QPushButton("Zoom Out");
    controlLayout->addWidget(zoomOutButton, row, 1);

    QPushButton* zoomInButton = new QPushButton("Zoom In");
    controlLayout->addWidget(zoomInButton, row, 2);

    zoomLabel = new QLabel("Zoom: 1.0x");
    controlLayout->addWidget(zoomLabel, row, 3);

    QPushButton* cellSizeButton = new QPushButton("Cell Size...");
    controlLayout->addWidget(cellSizeButton, row, 4);

    QPushButton* togglePanelButton = new QPushButton("Show Statistics Panel");
    controlLayout->addWidget(togglePanelButton, row, 5);

    // New centering buttons
    QPushButton* centerMostVisitedButton =
        new QPushButton("Center Most Visited");
    controlLayout->addWidget(centerMostVisitedButton, row, 6);

    QPushButton* centerCoordinatesButton =
        new QPushButton("Center Coordinates...");
    controlLayout->addWidget(centerCoordinatesButton, row, 7);

    row++;

    // Navigation buttons
    QPushButton* leftButton = new QPushButton("←");
    leftButton->setFixedSize(40, 30);
    controlLayout->addWidget(leftButton, row, 0);

    QPushButton* upButton = new QPushButton("↑");
    upButton->setFixedSize(40, 30);
    controlLayout->addWidget(upButton, row, 1);

    QPushButton* downButton = new QPushButton("↓");
    downButton->setFixedSize(40, 30);
    controlLayout->addWidget(downButton, row, 2);

    QPushButton* rightButton = new QPushButton("→");
    rightButton->setFixedSize(40, 30);
    controlLayout->addWidget(rightButton, row, 3);

    // Add a button to center on selected table cell
    QPushButton* centerTableButton = new QPushButton("Center Selected");
    controlLayout->addWidget(centerTableButton, row, 4);

    // Add a button to show statistics centering menu
    QPushButton* centerStatsButton = new QPushButton("Center Stats...");
    controlLayout->addWidget(centerStatsButton, row, 5);

    stepsLabel = new QLabel("Total steps: 0");
    controlLayout->addWidget(stepsLabel, row, 6, 1, 2);

    row++;

    // Statistics quick view
    QGroupBox* statsGroup = new QGroupBox("Quick Statistics");
    QGridLayout* statsLayout = new QGridLayout(statsGroup);

    uniqueCellsLabel = new QLabel("Unique cells: 0");
    mostVisitedLabel = new QLabel("Most visited: (0,0)");
    maxVisitsLabel = new QLabel("Max visits: 0");
    averageVisitsLabel = new QLabel("Average: 0.0");

    statsLayout->addWidget(uniqueCellsLabel, 0, 0);
    statsLayout->addWidget(mostVisitedLabel, 0, 1);
    statsLayout->addWidget(maxVisitsLabel, 0, 2);
    statsLayout->addWidget(averageVisitsLabel, 0, 3);

    // Add button to show cell details
    QPushButton* cellDetailsButton = new QPushButton("Show Cell Details");
    statsLayout->addWidget(cellDetailsButton, 0, 4);

    // Add button to show detailed statistics dialog
    QPushButton* statsDialogButton = new QPushButton("Detailed Statistics");
    statsLayout->addWidget(statsDialogButton, 0, 5);

    controlLayout->addWidget(statsGroup, row, 0, 1, 6);

    // Style button
    styleButton = new QPushButton("Next Style");
    controlLayout->addWidget(styleButton, row, 7);

    // Ant field
    antField = new AntFieldWidget();
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(antField);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    mainLayout->addWidget(controlGroup);
    mainLayout->addWidget(scrollArea, 1);

    // Statistics table - HIDDEN BY DEFAULT
    statsTableGroup = new QGroupBox("Top 20 Most Visited Cells");
    QVBoxLayout* tableLayout = new QVBoxLayout(statsTableGroup);

    statsTable = new QTableWidget();
    statsTable->setColumnCount(2);
    statsTable->setHorizontalHeaderLabels({"Cell (X,Y)", "Visits"});
    statsTable->horizontalHeader()->setStretchLastSection(true);
    statsTable->setMaximumHeight(200);
    tableLayout->addWidget(statsTable);

    // Hide the panel by default
    statsTableGroup->setVisible(false);

    mainLayout->addWidget(statsTableGroup);

    // Status bar
    statsLabel = new QLabel();
    statusBar()->addPermanentWidget(statsLabel);
    statusBar()->showMessage("Ready");

    // Connect menu actions
    connect(exportAction, &QAction::triggered, this,
            &MainWindow::exportStatistics);
    connect(statsAction, &QAction::triggered, [this, togglePanelButton]() {
        bool visible = !statsTableGroup->isVisible();
        statsTableGroup->setVisible(visible);
        togglePanelButton->setText(visible ? "Hide Statistics Panel"
                                           : "Show Statistics Panel");
    });
    connect(resetStatsAction, &QAction::triggered, [this]() {
        antField->resetStatistics();
        updateQuickStatistics();  // Update quick stats immediately
        updateStatisticsTable();
    });
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
    connect(cellDetailsButton, &QPushButton::clicked, this,
            &MainWindow::showCellDetails);
    connect(statsDialogButton, &QPushButton::clicked, this,
            &MainWindow::showStatistics);
    connect(togglePanelButton, &QPushButton::clicked,
            [this, togglePanelButton]() {
                bool visible = !statsTableGroup->isVisible();
                statsTableGroup->setVisible(visible);
                togglePanelButton->setText(visible ? "Hide Statistics Panel"
                                                   : "Show Statistics Panel");
            });
}

void MainWindow::setupConnections() {
    // Ant field signals
    connect(antField, &AntFieldWidget::antMoved, this, &MainWindow::onAntMoved);
    connect(antField, &AntFieldWidget::zoomChanged, this,
            &MainWindow::onZoomChanged);
    connect(antField, &AntFieldWidget::stepsChanged, this, [this](qint64 steps) {
        stepsLabel->setText(QString("Total steps: %1").arg(QLocale().toString(steps)));
    });

    // Connect the mouse coordinate signal
    connect(antField, &AntFieldWidget::mouseOverCell, this,
            &MainWindow::onMouseOverCell);

    // Remove the cellVisited connection since we're using timer-based updates

    // Buttons
    QList<QPushButton*> buttons = findChildren<QPushButton*>();
    for (QPushButton* btn : buttons) {
        const QString text = btn->text();

        if (text == "Update Rules") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::updateRules);
        } else if (text == "Step (1)") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::takeStep);
        } else if (text == "Run") {
            connect(btn, &QPushButton::clicked, this,
                    &MainWindow::onQuickStepsClicked);
        } else if (text == "Reset") {
            connect(btn, &QPushButton::clicked, this,
                    &MainWindow::resetSimulation);
        } else if (text == "Center on Ant") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::centerView);
        } else if (text == "Center Most Visited") {
            connect(btn, &QPushButton::clicked, this,
                    &MainWindow::centerOnMostVisited);
        } else if (text == "Center Coordinates...") {
            connect(btn, &QPushButton::clicked, this,
                    &MainWindow::centerOnCoordinates);
        } else if (text == "Center Selected") {
            connect(btn, &QPushButton::clicked, this,
                    &MainWindow::centerOnTableCell);
        } else if (text == "Center Stats...") {
            connect(btn, &QPushButton::clicked, this,
                    &MainWindow::centerOnSelectedStatistic);
        } else if (text == "Zoom In") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::zoomIn);
        } else if (text == "Zoom Out") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::zoomOut);
        } else if (text == "Cell Size...") {
            connect(btn, &QPushButton::clicked, this,
                    &MainWindow::changeCellSize);
        } else if (text == "←") {
            connect(btn, &QPushButton::clicked, this,
                    [this]() { moveView(50, 0); });
        } else if (text == "→") {
            connect(btn, &QPushButton::clicked, this,
                    [this]() { moveView(-50, 0); });
        } else if (text == "↑") {
            connect(btn, &QPushButton::clicked, this,
                    [this]() { moveView(0, 50); });
        } else if (text == "↓") {
            connect(btn, &QPushButton::clicked, this,
                    [this]() { moveView(0, -50); });
        }
    }

    // Style button
    connect(styleButton, &QPushButton::clicked, this, &MainWindow::changeStyle);

    // Preset combo
    QComboBox* presetCombo = findChild<QComboBox*>();
    if (presetCombo) {
        connect(presetCombo, QOverload<int>::of(&QComboBox::activated), this,
                &MainWindow::loadPreset);
    }

    // Rules edit
    connect(rulesEdit, &QLineEdit::returnPressed, this,
            &MainWindow::updateRules);

    // Statistics checkbox
    connect(statsCheckBox, &QCheckBox::toggled, this,
            &MainWindow::toggleStatistics);

    // Update statistics periodically
    QTimer* statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, [this]() {
        if (statsCheckBox->isChecked()) {
            updateQuickStatistics();  // Always update quick stats when enabled
            if (statsTableGroup->isVisible()) {
                updateStatisticsTable();  // Only update table if visible
            }
        }
    });
    statsTimer->start(500);  // Update every 500ms
}

void MainWindow::changeStyle() {
    currentStyleIndex = (currentStyleIndex + 1) % 3;
    auto newStyle = static_cast<AntFieldWidget::DisplayStyle>(currentStyleIndex);
    antField->setDisplayStyle(newStyle);
}

void MainWindow::onMouseOverCell(int x, int y) {
    if (x == INT_MAX && y == INT_MAX) {
        coordinateLabel->setText("Mouse: --");
    } else {
        QString pattern = ((x + y) % 2 == 0) ? "Vertical" : "Horizontal";
        coordinateLabel->setText(
            QString("Mouse: (%1, %2) [%3]").arg(x).arg(y).arg(pattern));
    }
}

void MainWindow::updateQuickStatistics() const {
    if (!statsCheckBox->isChecked()) {
        uniqueCellsLabel->setText("Unique cells: 0");
        mostVisitedLabel->setText("Most visited: (0,0)");
        maxVisitsLabel->setText("Max visits: 0");
        averageVisitsLabel->setText("Average: 0.0");
        statsLabel->clear();  // Clear the status bar label
        return;
    }

    auto summary = antField->getStatisticsSummary();
    uniqueCellsLabel->setText(
        QString("Unique cells: %1").arg(QLocale().toString(summary.uniqueCellsVisited)));
    mostVisitedLabel->setText(QString("Most visited: (%1, %2)")
                                  .arg(summary.mostVisitedCell.x())
                                  .arg(summary.mostVisitedCell.y()));
    maxVisitsLabel->setText(
        QString("Max visits: %1").arg(QLocale().toString(summary.maxVisitsPerCell)));
    averageVisitsLabel->setText(
        QString("Average: %1").arg(summary.averageVisits, 0, 'f', 2));

    // Update status bar with most visited cell info
    if (summary.maxVisitsPerCell > 0) {
        statsLabel->setText(QString("Most visited: (%1, %2) = %3 times")
                                .arg(summary.mostVisitedCell.x())
                                .arg(summary.mostVisitedCell.y())
                                .arg(QLocale().toString(summary.maxVisitsPerCell)));
    }
}

void MainWindow::updateRules() {
    QString rulesText = rulesEdit->text().toUpper().trimmed();
    QString expandedRules;
    QString compressedRules;

    for (QChar ch : rulesText) {
        if (ch.isLetter() && !QString("LR").contains(ch)) {
            QMessageBox::warning(this, "Invalid Rules",
                                 "Rules can only contain L or R letters.");
            return;
        }
    }
    for (int i = 0; i < rulesText.length();) {
        QChar currentChar;
        int count = 1;

        if (i < rulesText.length() && rulesText[i].isLetter()) {
            currentChar = rulesText[i];
            i++;

            while (i < rulesText.length() && rulesText[i] == ' ') {
                i++;
            }
            QString numStr;
            while (i < rulesText.length() && rulesText[i].isDigit()) {
                numStr += rulesText[i];
                i++;
            }

            if (!numStr.isEmpty()) {
                count = qBound(1, numStr.toInt(), 1000);
            }

            if (count == 1) {
                compressedRules += currentChar;
            } else {
                compressedRules += currentChar + QString::number(count);
            }

            expandedRules += QString(count, currentChar);
        } else {
            i++;
        }
    }

    if (expandedRules.isEmpty()) {
        expandedRules = "LR";
        compressedRules = "L1R1";
    }

    if (rulesEdit->text() != compressedRules) {
        rulesEdit->setText(compressedRules);
    }

    antField->setRules(expandedRules);
    rulesLabel->setText("Rules: " + compressedRules);

    // Update statistics after resetting
    updateQuickStatistics();
}

void MainWindow::onAntMoved(int x, int y, int direction, qint64 steps) {
    static const QString dirSymbols[] = {"↑", "→", "↓", "←"};
    QString dirStr =
        (direction >= 0 && direction < 4) ? dirSymbols[direction] : "?";

    statusBar()->showMessage(QString("Ant: (%1, %2) %3 | Steps: %4")
                                 .arg(x)
                                 .arg(y)
                                 .arg(dirStr)
                                 .arg(steps));
}

// void MainWindow::onCellVisited(const QPoint &cell, int visitCount) {
//     // Optional: show visit count in status bar for the current cell
//     QPoint mostVisited = antField->getMostVisitedCell();
//     if (cell.x() == mostVisited.x() &&
//         cell.y() == mostVisited.y()) {
//         statsLabel->setText(QString("Most visited: (%1, %2) = %3 times")
//                                 .arg(cell.x()).arg(cell.y()).arg(visitCount));
//     }
// }

void MainWindow::updateStatisticsTable() {
    if (!statsCheckBox->isChecked()) {
        statsTable->clearContents();
        statsTable->setRowCount(0);
        return;
    }

    auto topCells = antField->getTopVisitedCells(20);
    statsTable->setRowCount(topCells.size());

    for (int i = 0; i < topCells.size(); ++i) {
        const auto& cell = topCells[i];
        int visitCount = cell.second;

        statsTable->setItem(
            i, 0,
            new QTableWidgetItem(
                QString("(%1, %2)").arg(cell.first.x()).arg(cell.first.y())));
        statsTable->setItem(i, 1,
                            new QTableWidgetItem(QString::number(visitCount)));

        // For now, just show the visit count
        // statsTable->setItem(i, 2, new
        // QTableWidgetItem(QString::number(visitCount)));
    }
}

void MainWindow::takeStep() {
    antField->nextStep(1);
    updateQuickStatistics();  // Update stats immediately after step
}

void MainWindow::onQuickStepsClicked() {
    int steps = quickStepsSpin->value();
    lastCustomSteps = steps;
    antField->nextStep(steps);
    updateQuickStatistics();  // Update stats immediately after steps
}

void MainWindow::resetSimulation() {
    antField->reset();
    updateQuickStatistics();  // Update quick stats immediately
    updateStatisticsTable();
}

void MainWindow::centerView() {
    antField->centerOnAnt();
}

void MainWindow::centerOnMostVisited() {
    QPoint mostVisited = antField->getMostVisitedCell();
    antField->centerOnPoint(mostVisited);
}

void MainWindow::centerOnCoordinates() {
    bool ok;
    QString text = QInputDialog::getText(
        this, "Center on Coordinates",
        "Enter coordinates to center on (x,y):", QLineEdit::Normal, "0,0", &ok);
    if (ok && !text.isEmpty()) {
        QStringList coords = text.split(',');
        if (coords.size() == 2) {
            bool xOk, yOk;
            int x = coords[0].trimmed().toInt(&xOk);
            int y = coords[1].trimmed().toInt(&yOk);
            if (xOk && yOk) {
                antField->centerOnPoint(x, y);
            } else {
                QMessageBox::warning(
                    this, "Invalid Input",
                    "Please enter valid integers for coordinates.");
            }
        } else {
            QMessageBox::warning(this, "Invalid Format",
                                 "Please enter coordinates in the format: x,y");
        }
    }
}

void MainWindow::centerOnTableCell() {
    int row = statsTable->currentRow();
    if (row >= 0) {
        QTableWidgetItem* item = statsTable->item(row, 0);
        if (item) {
            QString text = item->text();
            // Extract coordinates from format like "(x, y)"
            text = text.mid(1, text.length() - 2);  // Remove parentheses
            QStringList coords = text.split(',');
            if (coords.size() == 2) {
                bool xOk, yOk;
                int x = coords[0].trimmed().toInt(&xOk);
                int y = coords[1].trimmed().toInt(&yOk);
                if (xOk && yOk) {
                    antField->centerOnPoint(x, y);
                }
            }
        }
    } else {
        QMessageBox::information(
            this, "No Selection",
            "Please select a cell from the statistics table first.");
    }
}

void MainWindow::centerOnSelectedStatistic() {
    // Get the currently selected statistic (most visited, second most visited,
    // etc.) We'll create a menu to choose which statistic to center on
    QMenu menu(this);

    // Get top 5 visited cells
    auto topCells = antField->getTopVisitedCells(5);

    if (topCells.isEmpty()) {
        QMessageBox::information(this, "No Statistics",
                                 "No statistics available yet.");
        return;
    }

    for (int i = 0; i < topCells.size(); ++i) {
        const auto& cell = topCells[i];
        QString text = QString("%1. (%2, %3) - %4 visits")
                           .arg(i + 1)
                           .arg(cell.first.x())
                           .arg(cell.first.y())
                           .arg(cell.second);
        QAction* action = menu.addAction(text);
        action->setData(QVariant::fromValue(cell.first));
    }

    QAction* selected = menu.exec(QCursor::pos());
    if (selected) {
        QPoint point = selected->data().value<QPoint>();
        antField->centerOnPoint(point);
    }
}

void MainWindow::moveView(int dx, int dy) {
    antField->moveView(dx, dy);
}

void MainWindow::zoomIn() {
    antField->setZoom(antField->getZoom() * 1.2);
}

void MainWindow::zoomOut() {
    antField->setZoom(antField->getZoom() / 1.2);
}

void MainWindow::onZoomChanged(double zoom) {
    zoomLabel->setText(QString("Zoom: %1x").arg(zoom, 0, 'f', 1));
}

void MainWindow::changeCellSize() {
    bool ok;
    int currentSize = antField->getCellSize();
    int size = QInputDialog::getInt(this, "Cell Size",
                                    "Enter cell size (pixels):", currentSize, 1,
                                    50, 1, &ok);
    if (ok) {
        antField->setCellSize(size);
    }
}

void MainWindow::loadPreset(int index) {
    if (presets.contains(index)) {
        rulesEdit->setText(presets[index]);
        updateRules();
    }
}

void MainWindow::showStatistics() {
    auto summary = antField->getStatisticsSummary();
    auto topCells = antField->getTopVisitedCells(20);

    QString statsText;
    statsText += QString("Simulation Statistics\n");
    statsText += QString("====================\n");
    statsText += QString("Total Steps: %1\n").arg(QLocale().toString(summary.totalCellsVisited));
    statsText +=
        QString("Unique Cells Visited: %1\n").arg(summary.uniqueCellsVisited);
    statsText += QString("Most Visited Cell: (%1, %2) [%3 times]\n")
                     .arg(summary.mostVisitedCell.x())
                     .arg(summary.mostVisitedCell.y())
                     .arg(QLocale().toString(summary.maxVisitsPerCell));
    statsText += QString("Average Visits per Cell: %1\n")
                     .arg(summary.averageVisits, 0, 'f', 2);
    statsText +=
        QString("Simulation Time: %1 ms\n").arg(summary.simulationTimeMs);
    statsText += QString("\nTop 20 Most Visited Cells:\n");

    for (int i = 0; i < topCells.size(); ++i) {
        const auto& cell = topCells[i];
        statsText += QString("%1. (%2, %3): %4 visits\n")
                         .arg(i + 1)
                         .arg(cell.first.x())
                         .arg(cell.first.y())
                         .arg(QLocale().toString(cell.second));
    }

    QMessageBox::information(this, "Detailed Statistics", statsText);
}

void MainWindow::toggleStatistics(bool enabled) {
    antField->setStatisticsEnabled(enabled);
    updateQuickStatistics();  // Update immediately when toggled

    if (!enabled) {
        statsLabel->clear();
        if (statsTableGroup->isVisible()) {
            statsTable->clearContents();
            statsTable->setRowCount(0);
        }
    }
}

void MainWindow::exportStatistics() {
    QString fileName = QFileDialog::getSaveFileName(
        this, "Export Statistics", "ant_statistics.csv", "CSV Files (*.csv)");
    if (!fileName.isEmpty()) {
        try {
            antField->exportStatisticsToCSV(fileName);
            statusBar()->showMessage("Statistics exported to " + fileName,
                                     3000);
        } catch (const std::exception& e) {
            QMessageBox::critical(
                this, "Export Error",
                QString("Failed to export: %1").arg(e.what()));
        }
    }
}

void MainWindow::showCellDetails() {
    bool ok;
    QString text = QInputDialog::getText(
        this, "Check Cell", "Enter cell coordinates (x,y):", QLineEdit::Normal,
        "0,0", &ok);
    if (ok && !text.isEmpty()) {
        QStringList coords = text.split(',');
        if (coords.size() == 2) {
            bool xOk, yOk;
            int x = coords[0].trimmed().toInt(&xOk);
            int y = coords[1].trimmed().toInt(&yOk);
            if (xOk && yOk) {
                int visits = antField->getVisitCount(x, y);
                QMessageBox::information(
                    this, "Cell Details",
                    QString("Cell (%1, %2) has been visited %3 times")
                        .arg(x)
                        .arg(y)
                        .arg(visits));
            } else {
                QMessageBox::warning(
                    this, "Invalid Input",
                    "Please enter valid integers for coordinates.");
            }
        } else {
            QMessageBox::warning(this, "Invalid Format",
                                 "Please enter coordinates in the format: x,y");
        }
    }
}
