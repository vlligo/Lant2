#include "mainwindow.h"

#include <iostream>
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
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <utility>

#include "antfieldwidget.h"
#include "QPoint64.h"
#include "QInt64SpinBox.h"

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
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);

    // Create menu bar
    QMenu* fileMenu = menuBar()->addMenu("&File");
    exportAction = fileMenu->addAction("&Export Statistics...");
    fileMenu->addSeparator();
    exitAction = fileMenu->addAction("E&xit");

    QMenu* simulationMenu = menuBar()->addMenu("&Simulation");
    resetStatsAction = simulationMenu->addAction("&Reset Statistics");

    // Status bar - create labels once
    statsLabel = new QLabel();
    coordinateLabel = new QLabel("Mouse: (0, 0)");

    // Add them to status bar
    statusBar()->addPermanentWidget(statsLabel);
    statusBar()->addPermanentWidget(coordinateLabel);
    statusBar()->showMessage("Ready");

    // Control panel
    auto* controlGroup = new QGroupBox("Controls");
    auto* controlLayout = new QGridLayout(controlGroup);

    int row = 0;

    // Rules section
    controlLayout->addWidget(new QLabel("Rules:"), row, 0);
    rulesEdit = new QLineEdit();
    controlLayout->addWidget(rulesEdit, row, 1);

    rulesButton = new QPushButton("Update Rules");
    controlLayout->addWidget(rulesButton, row, 2);

    auto* presetCombo = new QComboBox();
    presetCombo->addItems(
        {"Classic LR", "Symmetric LLRR", "Highway", "Complex"});
    controlLayout->addWidget(presetCombo, row, 3);

    rulesLabel = new QLabel("Rules: LR");
    controlLayout->addWidget(rulesLabel, row, 4, 1, 2);

    row++;

    // Step controls
    stepButton = new QPushButton("Step (1)");
    controlLayout->addWidget(stepButton, row, 0);

    quickStepsSpin = new QInt64SpinBox();
    quickStepsSpin->setRange(1, std::numeric_limits<qint64>::max());   // 9.22e18
    quickStepsSpin->setValue(lastCustomSteps);
    quickStepsSpin->setSingleStep(1000);
    quickStepsSpin->setSuffix(" steps");
    controlLayout->addWidget(quickStepsSpin, row, 1);

    quickStepsButton = new QPushButton("Run");
    controlLayout->addWidget(quickStepsButton, row, 2);

    resetButton = new QPushButton("Reset");
    controlLayout->addWidget(resetButton, row, 3);

    statsCheckBox = new QCheckBox("Track Statistics");
    statsCheckBox->setChecked(true);
    controlLayout->addWidget(statsCheckBox, row, 4);

    row++;

    // View controls
    centerButton = new QPushButton("Center on Ant");
    controlLayout->addWidget(centerButton, row, 0);

    zoomOutButton = new QPushButton("Zoom Out");
    controlLayout->addWidget(zoomOutButton, row, 1);

    zoomInButton = new QPushButton("Zoom In");
    controlLayout->addWidget(zoomInButton, row, 2);

    zoomLabel = new QLabel("Zoom: 1.0x");
    controlLayout->addWidget(zoomLabel, row, 3);

    cellSizeButton = new QPushButton("Cell Size...");
    controlLayout->addWidget(cellSizeButton, row, 4);

    togglePanelButton = new QPushButton("Show Statistics Panel");
    controlLayout->addWidget(togglePanelButton, row, 5);

    // Centering buttons
    centerMostVisitedButton =
        new QPushButton("Center Most Visited");
    controlLayout->addWidget(centerMostVisitedButton, row, 6);

    centerCoordinatesButton =
        new QPushButton("Center Coordinates...");
    controlLayout->addWidget(centerCoordinatesButton, row, 7);

    row++;

    // Navigation buttons
    leftButton = new QPushButton("←");
    leftButton->setFixedSize(40, 30);
    controlLayout->addWidget(leftButton, row, 0);

    upButton = new QPushButton("↑");
    upButton->setFixedSize(40, 30);
    controlLayout->addWidget(upButton, row, 1);

    downButton = new QPushButton("↓");
    downButton->setFixedSize(40, 30);
    controlLayout->addWidget(downButton, row, 2);

    rightButton = new QPushButton("→");
    rightButton->setFixedSize(40, 30);
    controlLayout->addWidget(rightButton, row, 3);

    // Add a button to center on selected table cell
    centerTableButton = new QPushButton("Center Selected");
    controlLayout->addWidget(centerTableButton, row, 4);

    // Add a button to show statistics centering menu
    centerStatsButton = new QPushButton("Center Stats...");
    controlLayout->addWidget(centerStatsButton, row, 5);

    stepsLabel = new QLabel("Total steps: 0");
    controlLayout->addWidget(stepsLabel, row, 6, 1, 2);

    row++;

    // Statistics quick view
    auto* statsGroup = new QGroupBox("Quick Statistics");
    auto* statsLayout = new QGridLayout(statsGroup);

    uniqueCellsLabel = new QLabel("Unique cells: 0");
    mostVisitedLabel = new QLabel("Most visited: (0,0)");
    maxVisitsLabel = new QLabel("Max visits: 0");
    averageVisitsLabel = new QLabel("Average: 0.0");

    statsLayout->addWidget(uniqueCellsLabel, 0, 0);
    statsLayout->addWidget(mostVisitedLabel, 0, 1);
    statsLayout->addWidget(maxVisitsLabel, 0, 2);
    statsLayout->addWidget(averageVisitsLabel, 0, 3);

    // Button to show cell details
    cellDetailsButton = new QPushButton("Show Cell Details");
    statsLayout->addWidget(cellDetailsButton, 0, 4);

    // Button to show detailed statistics dialog
    statsDialogButton = new QPushButton("Detailed Statistics");
    statsLayout->addWidget(statsDialogButton, 0, 5);

    controlLayout->addWidget(statsGroup, row, 0, 1, 6);

    // Style button
    styleButton = new QPushButton("Next Style");
    controlLayout->addWidget(styleButton, row, 7);

    // Ant field
    antField = new AntFieldWidget();
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidget(antField);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    mainLayout->addWidget(controlGroup);
    mainLayout->addWidget(scrollArea, 1);

    // Statistics table - HIDDEN BY DEFAULT
    statsTableGroup = new QGroupBox("Top 20 Most Visited Cells");
    auto* tableLayout = new QVBoxLayout(statsTableGroup);

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
    statusBar()->showMessage("Ready");

}

void MainWindow::setupConnections() {
    // Connect menu actions
    connect(exportAction, &QAction::triggered, this,
            &MainWindow::exportStatistics);
    connect(resetStatsAction, &QAction::triggered, [this]() {
        antField->resetStatistics();
        updateQuickStatistics();  // Update quick stats immediately
        updateStatisticsTable();
    });
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);

    // Ant field signals
    connect(antField, &AntFieldWidget::antMoved, this, &MainWindow::onAntMoved);
    connect(antField, &AntFieldWidget::zoomChanged, this,
            &MainWindow::onZoomChanged);
    connect(antField, &AntFieldWidget::stepsChanged, this, [this](const qint64 steps) {
        stepsLabel->setText(QString("Total steps: %1").arg(QLocale().toString(steps)));
    });

    // Connect the mouse coordinate signal
    connect(antField, &AntFieldWidget::mouseOverCell, this,
            &MainWindow::onMouseOverCell);

    // Buttons
    connect(rulesButton, &QPushButton::clicked, this, &MainWindow::updateRules);
    connect(stepButton, &QPushButton::clicked, this, &MainWindow::takeStep);
    connect(quickStepsButton, &QPushButton::clicked, this, &MainWindow::onQuickStepsClicked);
    connect(resetButton, &QPushButton::clicked, this, &MainWindow::resetSimulation);
    connect(centerButton, &QPushButton::clicked, this, &MainWindow::centerView);
    connect(centerMostVisitedButton, &QPushButton::clicked, this, &MainWindow::centerOnMostVisited);
    connect(centerCoordinatesButton, &QPushButton::clicked, this, &MainWindow::centerOnCoordinates);
    connect(centerTableButton, &QPushButton::clicked, this, &MainWindow::centerOnTableCell);
    connect(centerStatsButton, &QPushButton::clicked, this, &MainWindow::centerOnSelectedStatistic);
    connect(zoomInButton, &QPushButton::clicked, this, &MainWindow::zoomIn);
    connect(zoomOutButton, &QPushButton::clicked, this, &MainWindow::zoomOut);
    connect(cellSizeButton, &QPushButton::clicked, this, &MainWindow::changeCellSize);

    connect(leftButton,  &QPushButton::clicked, this, [this]() { moveView( 50,   0); });
    connect(rightButton, &QPushButton::clicked, this, [this]() { moveView(-50,   0); });
    connect(upButton,    &QPushButton::clicked, this, [this]() { moveView(  0,  50); });
    connect(downButton,  &QPushButton::clicked, this, [this]() { moveView(  0, -50); });

    // Style button
    connect(styleButton, &QPushButton::clicked, this, &MainWindow::changeStyle);

    // Preset combo
    auto* presetCombo = findChild<QComboBox*>();
    if (presetCombo) {
        connect(presetCombo, QOverload<int>::of(&QComboBox::activated), this,
                &MainWindow::loadPreset);
    }

    // Rules edit
    connect(rulesEdit, &QLineEdit::returnPressed, this,
            &MainWindow::updateRules);

    // Statistics checkbox
    connect(cellDetailsButton, &QPushButton::clicked, this,
            &MainWindow::showCellDetails);
    connect(statsDialogButton, &QPushButton::clicked, this,
            &MainWindow::showStatistics);
    connect(togglePanelButton, &QPushButton::clicked,
            [this]() {
                const bool visible = !statsTableGroup->isVisible();
                statsTableGroup->setVisible(visible);
                togglePanelButton->setText(visible ? "Hide Statistics Panel"
                                                   : "Show Statistics Panel");
            });
    connect(statsCheckBox, &QCheckBox::toggled, this,
            &MainWindow::toggleStatistics);

    // Update statistics periodically
    auto* statsTimer = new QTimer(this);
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
    const auto newStyle = static_cast<AntFieldWidget::DisplayStyle>(currentStyleIndex);
    antField->setDisplayStyle(newStyle);
}

void MainWindow::onMouseOverCell(const qint64 x, const qint64 y) const {
        QString pattern = ((x + y) % 2 == 0) ? "Vertical" : "Horizontal";
    coordinateLabel->setText(
        QString("Mouse: (%1; %2) [%3]").arg(QLocale().toString(x), QLocale().toString((y)), pattern));
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

    const auto summary = antField->getStatisticsSummary();
    uniqueCellsLabel->setText(
        QString("Unique cells: %1").arg(QLocale().toString(summary.uniqueCellsVisited)));
    mostVisitedLabel->setText(QString("Most visited: (%1, %2)")
                                  .arg(QLocale().toString(summary.mostVisitedCell.x()),
                                      QLocale().toString(summary.mostVisitedCell.y())));
    maxVisitsLabel->setText(
        QString("Max visits: %1").arg(QLocale().toString(summary.maxVisitsPerCell)));
    averageVisitsLabel->setText(
        QString("Average: %1").arg(QLocale().toString(summary.averageVisits, 'f', 2)));

    // Update status bar with most visited cell info
    if (summary.maxVisitsPerCell > 0) {
        statsLabel->setText(QString("Most visited: (%1, %2) = %3 times")
                                .arg(QLocale().toString(summary.mostVisitedCell.x()),
                                    QLocale().toString(summary.mostVisitedCell.y()),
                                    QLocale().toString(summary.maxVisitsPerCell)));
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
        if (i < rulesText.length() && rulesText[i].isLetter()) {
            int count = 1;
            const QChar currentChar = rulesText[i];
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
                count = qBound(1, numStr.toInt(), 1000000);
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

void MainWindow::onAntMoved(const qint64 x, const qint64 y, const int direction, const qint64 steps) const {
    static const QString dirSymbols[] = {"↑", "→", "↓", "←"};
    const QString dirStr =
        (direction >= 0 && direction < 4) ? dirSymbols[direction] : "?";

    statusBar()->showMessage(QString("Ant: (%1, %2) %3 | Steps: %4")
                                 .arg(QLocale().toString(x), QLocale().toString(y), dirStr,
                                     QLocale().toString(steps)));
}

void MainWindow::updateStatisticsTable() const {
    if (!statsCheckBox->isChecked()) {
        statsTable->clearContents();
        statsTable->setRowCount(0);
        return;
    }

    auto topCells = antField->getTopVisitedCells(20);
    statsTable->setRowCount(topCells.size());

    for (int i = 0; i < topCells.size(); ++i) {
        const auto& cell = topCells[i];
        const int visitCount = cell.second;

        statsTable->setItem(
            i, 0,
            new QTableWidgetItem(
                QString("(%1, %2)").arg(QLocale().toString(cell.first.x()),
                    QLocale().toString(cell.first.y()))));
        statsTable->setItem(i, 1,
                            new QTableWidgetItem(QString::number(visitCount)));

        // For now, just show the visit count
    }
}

void MainWindow::takeStep() const {
    antField->nextStep(1);
    updateQuickStatistics();  // Update stats immediately after step
}

void MainWindow::onQuickStepsClicked() {
    const qint64 steps = quickStepsSpin->value();
    lastCustomSteps = steps;
    antField->nextStep(steps);
    updateQuickStatistics();  // Update stats immediately after steps
}

void MainWindow::resetSimulation() const {
    antField->reset();
    updateQuickStatistics();  // Update quick stats immediately
    updateStatisticsTable();
}

void MainWindow::centerView() const {
    antField->centerOnAnt();
}

void MainWindow::centerOnMostVisited() const {
    const QPoint64 mostVisited = antField->getMostVisitedCell();
    antField->centerOnPoint(mostVisited);
}

void MainWindow::centerOnCoordinates() {
    bool ok;
    const QString text = QInputDialog::getText(
        this, "Center on Coordinates",
        "Enter coordinates to center on (x,y):", QLineEdit::Normal, "0,0", &ok);
    if (ok && !text.isEmpty()) {
        QStringList coords = text.split(',');
        if (coords.size() == 2) {
            bool xOk, yOk;
            const long long x = coords[0].trimmed().toLongLong(&xOk);
            const long long y = coords[1].trimmed().toLongLong(&yOk);
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
                long long x = coords[0].trimmed().toLongLong(&xOk);
                long long y = coords[1].trimmed().toLongLong(&yOk);
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
                           .arg(QLocale().toString(cell.first.x()),
                               QLocale().toString(cell.first.y()),
                               QLocale().toString(cell.second));
        QAction* action = menu.addAction(text);
        action->setData(QVariant::fromValue(cell.first));
    }

    const QAction* selected = menu.exec(QCursor::pos());
    if (selected) {
        const auto point = selected->data().value<QPoint64>();
        antField->centerOnPoint(point);
    }
}

void MainWindow::moveView(qint64 dx, qint64 dy) const {
    antField->moveView(dx, dy);
}

void MainWindow::zoomIn() const {
    antField->setZoom(antField->getZoom() * 1.2);
}

void MainWindow::zoomOut() const {
    antField->setZoom(antField->getZoom() / 1.2);
}

void MainWindow::onZoomChanged(double zoom) const {
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
        QString("Unique Cells Visited: %1\n").arg(QLocale().toString(summary.uniqueCellsVisited));
    statsText += QString("Most Visited Cell: (%1, %2) [%3 times]\n")
                     .arg(QLocale().toString(summary.mostVisitedCell.x()),
                         QLocale().toString(summary.mostVisitedCell.y()),
                         QLocale().toString(summary.maxVisitsPerCell));
    statsText += QString("Average Visits per Cell: %1\n")
                     .arg(QLocale().toString(summary.averageVisits, 'f', 2));
    statsText +=
        QString("Simulation Time: %1 ms\n").arg(summary.simulationTimeMs);
    statsText += QString("\nTop 20 Most Visited Cells:\n");

    for (int i = 0; i < topCells.size(); ++i) {
        const auto& cell = topCells[i];
        statsText += QString("%1. (%2, %3): %4 visits\n")
                         .arg(i + 1)
                         .arg(QLocale().toString(cell.first.x()), QLocale().toString((cell.first.y())))
                         .arg(QLocale().toString(cell.second));
    }

    QMessageBox::information(this, "Detailed Statistics", statsText);
}

void MainWindow::toggleStatistics(bool enabled) const {
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
    const QString text = QInputDialog::getText(
        this, "Check Cell", "Enter cell coordinates (x,y):", QLineEdit::Normal,
        "0,0", &ok);
    if (ok && !text.isEmpty()) {
        QStringList coords = text.split(',');
        if (coords.size() == 2) {
            bool xOk, yOk;
            long long x = coords[0].trimmed().toLongLong(&xOk);
            long long y = coords[1].trimmed().toLongLong(&yOk);
            if (xOk && yOk) {
                long long visits = antField->getVisitCount(x, y);
                QMessageBox::information(
                    this, "Cell Details",
                    QString("Cell (%1, %2) has been visited %3 times")
                        .arg(QLocale().toString(x),
                            QLocale().toString((y)),
                            QLocale().toString(visits)));
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
