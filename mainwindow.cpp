#include "mainwindow.h"
#include "antfieldwidget.h"
#include <QApplication>
#include <QMainWindow>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QStatusBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QToolBar>
#include <cmath>
#include <map>
#include <vector>
#include <string>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUI();
    setupConnections();

    // Set default rules
    rulesEdit->setText("LR");
    updateRules();

    setWindowTitle("Langton's Ant");
    resize(1200, 800);
}

void MainWindow::updateRules() {
    QString rulesText = rulesEdit->text();
    QString expandedRules;
    QString compressedRules;

    // Parse rules in compressed format like "L5R3" or "L3R1"
    int i = 0;
    while (i < rulesText.length()) {
        QChar currentChar;
        int count = 1;

        // Get the direction character
        if (i < rulesText.length() && rulesText[i].isLetter() &&
            (rulesText[i] == 'L' || rulesText[i] == 'R' || rulesText[i] == 'F' || rulesText[i] == 'B')) {
            currentChar = rulesText[i];
            i++;

            // Check if there's a number following
            QString numStr;
            while (i < rulesText.length() && rulesText[i].isDigit()) {
                numStr += rulesText[i];
                i++;
            }

            if (!numStr.isEmpty()) {
                count = numStr.toInt();
                // Validate count
                if (count <= 0) count = 1;
                if (count > 1000) count = 1000; // Reasonable limit
            }

            // Add to compressed rules
            if (count == 1) {
                compressedRules += currentChar;
            } else {
                compressedRules += currentChar + QString::number(count);
            }

            // Expand for internal use
            for (int j = 0; j < count; j++) {
                expandedRules += currentChar;
            }
        } else {
            // Skip invalid characters
            i++;
        }
    }

    if (expandedRules.isEmpty()) {
        expandedRules = "LR"; // Default rule
        compressedRules = "L1R1";
    }

    // Update the rules edit with compressed format
    if (rulesEdit->text() != compressedRules) {
        rulesEdit->setText(compressedRules);
    }

    antField->setRules(expandedRules);
    rulesLabel->setText("Rules: " + compressedRules);
}

void MainWindow::onAntMoved(int x, int y, int direction, int steps) {
    QString dirStr;
    switch (direction) {
    case 0: dirStr = "↑"; break;
    case 1: dirStr = "→"; break;
    case 2: dirStr = "↓"; break;
    case 3: dirStr = "←"; break;
    }
    statusBar()->showMessage(QString("Ant: (%1, %2) %3 | Steps: %4")
                                 .arg(x).arg(y).arg(dirStr).arg(steps));
}

void MainWindow::takeStep() {
    antField->nextStep();
}

void MainWindow::onQuickStepsClicked() {
    int steps = quickStepsSpin->value();
    lastCustomSteps = steps;
    antField->nextStep(steps);
}

void MainWindow::resetSimulation() {
    antField->reset();
}

void MainWindow::centerView() {
    antField->centerOnAnt();
}

void MainWindow::moveViewLeft() { antField->moveView(50, 0); }
void MainWindow::moveViewRight() { antField->moveView(-50, 0); }
void MainWindow::moveViewUp() { antField->moveView(0, 50); }
void MainWindow::moveViewDown() { antField->moveView(0, -50); }

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
    int size = QInputDialog::getInt(this, "Cell Size",
                                    "Enter cell size (pixels):",
                                    antField->getCellSize(),
                                    1, 50, 1, &ok);
    if (ok) {
        antField->setCellSize(size);
    }
}

void MainWindow::loadPreset(int index) {
    static const QMap<int, QString> presets = {
        {0, "LR"},           // Classic Langton's Ant
        {1, "LLRR"},         // Symmetric
        {2, "LLRRRLRLRLLR"},    // Highway
        {3, "LRRRRRLLR"}, // Complex pattern
    };

    if (presets.contains(index)) {
        rulesEdit->setText(presets[index]);
        updateRules();
    }
}

void MainWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Control panel
    QGroupBox *controlGroup = new QGroupBox("Controls");
    QGridLayout *controlLayout = new QGridLayout(controlGroup);

    // Rules section (row 0)
    controlLayout->addWidget(new QLabel("Rules:"), 0, 0);
    rulesEdit = new QLineEdit();
    controlLayout->addWidget(rulesEdit, 0, 1);

    QPushButton *rulesButton = new QPushButton("Update Rules");
    controlLayout->addWidget(rulesButton, 0, 2);

    QComboBox *presetCombo = new QComboBox();
    presetCombo->addItems({"Classic LR", "Symmetric LLRR", "Highway", "Complex"});
    controlLayout->addWidget(presetCombo, 0, 3);

    rulesLabel = new QLabel("Rules: LR");
    controlLayout->addWidget(rulesLabel, 0, 4, 1, 2);

    // Step controls (row 1)
    QPushButton *stepButton = new QPushButton("Step (1)");
    controlLayout->addWidget(stepButton, 1, 0);

    // Quick steps control
    quickStepsSpin = new QSpinBox();
    quickStepsSpin->setRange(1, 2147483647); // Max int value
    quickStepsSpin->setValue(lastCustomSteps);
    quickStepsSpin->setSingleStep(1000);
    quickStepsSpin->setSuffix(" steps");
    controlLayout->addWidget(quickStepsSpin, 1, 1);

    quickStepsButton = new QPushButton("Run");
    controlLayout->addWidget(quickStepsButton, 1, 2);

    QPushButton *resetButton = new QPushButton("Reset");
    controlLayout->addWidget(resetButton, 1, 3);

    // View controls (row 2)
    QPushButton *centerButton = new QPushButton("Center on Ant");
    controlLayout->addWidget(centerButton, 2, 0);

    QPushButton *zoomOutButton = new QPushButton("Zoom Out");
    controlLayout->addWidget(zoomOutButton, 2, 1);

    QPushButton *zoomInButton = new QPushButton("Zoom In");
    controlLayout->addWidget(zoomInButton, 2, 2);

    zoomLabel = new QLabel("Zoom: 1.0x");
    controlLayout->addWidget(zoomLabel, 2, 3);

    QPushButton *cellSizeButton = new QPushButton("Cell Size...");
    controlLayout->addWidget(cellSizeButton, 2, 4);

    // Navigation buttons (row 3)
    QPushButton *upButton = new QPushButton("↑");
    upButton->setFixedSize(40, 30);
    controlLayout->addWidget(upButton, 3, 1);

    QPushButton *downButton = new QPushButton("↓");
    downButton->setFixedSize(40, 30);
    controlLayout->addWidget(downButton, 3, 3);

    QPushButton *leftButton = new QPushButton("←");
    leftButton->setFixedSize(40, 30);
    controlLayout->addWidget(leftButton, 3, 0);

    QPushButton *rightButton = new QPushButton("→");
    rightButton->setFixedSize(40, 30);
    controlLayout->addWidget(rightButton, 3, 2);

    stepsLabel = new QLabel("Total steps: 0");
    controlLayout->addWidget(stepsLabel, 3, 4);

    // Ant field
    antField = new AntFieldWidget();
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(antField);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    mainLayout->addWidget(controlGroup);
    mainLayout->addWidget(scrollArea, 1);

    // Status bar
    statusBar()->showMessage("Ready");
}

void MainWindow::setupConnections() {
    connect(antField, &AntFieldWidget::antMoved, this, &MainWindow::onAntMoved);
    connect(antField, &AntFieldWidget::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(antField, &AntFieldWidget::stepsChanged, this, [this](int steps) {
        stepsLabel->setText(QString("Total steps: %1").arg(steps));
    });

    // Find buttons by their text or position and connect them
    QList<QPushButton*> buttons = findChildren<QPushButton*>();
    for (QPushButton *btn : buttons) {
        if (btn->text() == "Update Rules") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::updateRules);
        } else if (btn->text() == "Step (1)") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::takeStep);
        } else if (btn->text() == "Reset") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::resetSimulation);
        } else if (btn->text() == "Center on Ant") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::centerView);
        } else if (btn->text() == "↑") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::moveViewUp);
        } else if (btn->text() == "↓") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::moveViewDown);
        } else if (btn->text() == "←") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::moveViewLeft);
        } else if (btn->text() == "→") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::moveViewRight);
        } else if (btn->text() == "Zoom In") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::zoomIn);
        } else if (btn->text() == "Zoom Out") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::zoomOut);
        } else if (btn->text() == "Cell Size...") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::changeCellSize);
        } else if (btn->text() == "Run") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::onQuickStepsClicked);
        }
    }

    QComboBox *presetCombo = findChild<QComboBox*>();
    if (presetCombo) {
        connect(presetCombo, QOverload<int>::of(&QComboBox::activated),
                this, &MainWindow::loadPreset);
    }
}
