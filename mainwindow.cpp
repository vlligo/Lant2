#include "mainwindow.h"
#include "antfieldwidget.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QStatusBar>
#include <QInputDialog>
#include <QScrollArea>
#include <QLineEdit>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    loadPresets();
    setupUI();
    setupConnections();

    rulesEdit->setText("LR");
    updateRules();

    setWindowTitle("Langton's Ant");
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

void MainWindow::updateRules() {
    QString rulesText = rulesEdit->text().toUpper().trimmed();
    QString expandedRules;
    QString compressedRules;

    // Parse compressed format
    for (int i = 0; i < rulesText.length(); ) {
        QChar currentChar;
        int count = 1;

        if (i < rulesText.length() && rulesText[i].isLetter()) {
            currentChar = rulesText[i];
            i++;

            // Parse number
            QString numStr;
            while (i < rulesText.length() && rulesText[i].isDigit()) {
                numStr += rulesText[i];
                i++;
            }

            if (!numStr.isEmpty()) {
                count = qBound(1, numStr.toInt(), 1000);
            }

            // Build compressed and expanded rules
            if (count == 1) {
                compressedRules += currentChar;
            } else {
                compressedRules += currentChar + QString::number(count);
            }

            expandedRules += QString(count, currentChar);
        } else {
            i++; // Skip invalid
        }
    }

    if (expandedRules.isEmpty()) {
        expandedRules = "LR";
        compressedRules = "L1R1";
    }

    // Update UI
    if (rulesEdit->text() != compressedRules) {
        rulesEdit->setText(compressedRules);
    }

    antField->setRules(expandedRules);
    rulesLabel->setText("Rules: " + compressedRules);
}

void MainWindow::onAntMoved(int x, int y, int direction, int steps) {
    static const QString dirSymbols[] = {"↑", "→", "↓", "←"};
    QString dirStr = (direction >= 0 && direction < 4) ? dirSymbols[direction] : "?";

    statusBar()->showMessage(QString("Ant: (%1, %2) %3 | Steps: %4")
                                 .arg(x).arg(y).arg(dirStr).arg(steps));
}

void MainWindow::takeStep() {
    antField->nextStep(1);
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
                                    "Enter cell size (pixels):",
                                    currentSize, 1, 50, 1, &ok);
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

void MainWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Control panel
    QGroupBox *controlGroup = new QGroupBox("Controls");
    QGridLayout *controlLayout = new QGridLayout(controlGroup);

    int row = 0;

    // Rules section
    controlLayout->addWidget(new QLabel("Rules:"), row, 0);
    rulesEdit = new QLineEdit();
    controlLayout->addWidget(rulesEdit, row, 1);

    QPushButton *rulesButton = new QPushButton("Update Rules");
    controlLayout->addWidget(rulesButton, row, 2);

    QComboBox *presetCombo = new QComboBox();
    presetCombo->addItems({"Classic LR", "Symmetric LLRR", "Highway", "Complex"});
    controlLayout->addWidget(presetCombo, row, 3);

    rulesLabel = new QLabel("Rules: LR");
    controlLayout->addWidget(rulesLabel, row, 4, 1, 2);

    row++;

    // Step controls
    QPushButton *stepButton = new QPushButton("Step (1)");
    controlLayout->addWidget(stepButton, row, 0);

    quickStepsSpin = new QSpinBox();
    quickStepsSpin->setRange(1, INT_MAX);
    quickStepsSpin->setValue(lastCustomSteps);
    quickStepsSpin->setSingleStep(1000);
    quickStepsSpin->setSuffix(" steps");
    controlLayout->addWidget(quickStepsSpin, row, 1);

    quickStepsButton = new QPushButton("Run");
    controlLayout->addWidget(quickStepsButton, row, 2);

    QPushButton *resetButton = new QPushButton("Reset");
    controlLayout->addWidget(resetButton, row, 3);

    row++;

    // View controls
    QPushButton *centerButton = new QPushButton("Center on Ant");
    controlLayout->addWidget(centerButton, row, 0);

    QPushButton *zoomOutButton = new QPushButton("Zoom Out");
    controlLayout->addWidget(zoomOutButton, row, 1);

    QPushButton *zoomInButton = new QPushButton("Zoom In");
    controlLayout->addWidget(zoomInButton, row, 2);

    zoomLabel = new QLabel("Zoom: 1.0x");
    controlLayout->addWidget(zoomLabel, row, 3);

    QPushButton *cellSizeButton = new QPushButton("Cell Size...");
    controlLayout->addWidget(cellSizeButton, row, 4);

    row++;

    // Navigation buttons
    QPushButton *leftButton = new QPushButton("←");
    leftButton->setFixedSize(40, 30);
    controlLayout->addWidget(leftButton, row, 0);

    QPushButton *upButton = new QPushButton("↑");
    upButton->setFixedSize(40, 30);
    controlLayout->addWidget(upButton, row, 1);

    QPushButton *downButton = new QPushButton("↓");
    downButton->setFixedSize(40, 30);
    controlLayout->addWidget(downButton, row, 2);

    QPushButton *rightButton = new QPushButton("→");
    rightButton->setFixedSize(40, 30);
    controlLayout->addWidget(rightButton, row, 3);

    stepsLabel = new QLabel("Total steps: 0");
    controlLayout->addWidget(stepsLabel, row, 4);

    // Ant field
    antField = new AntFieldWidget();
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(antField);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    mainLayout->addWidget(controlGroup);
    mainLayout->addWidget(scrollArea, 1);

    statusBar()->showMessage("Ready");
}

void MainWindow::setupConnections() {
    // Ant field signals
    connect(antField, &AntFieldWidget::antMoved,
            this, &MainWindow::onAntMoved);
    connect(antField, &AntFieldWidget::zoomChanged,
            this, &MainWindow::onZoomChanged);
    connect(antField, &AntFieldWidget::stepsChanged,
            this, [this](int steps) {
                stepsLabel->setText(QString("Total steps: %1").arg(steps));
            });

    // Buttons
    QList<QPushButton*> buttons = findChildren<QPushButton*>();
    for (QPushButton *btn : buttons) {
        const QString text = btn->text();

        if (text == "Update Rules") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::updateRules);
        } else if (text == "Step (1)") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::takeStep);
        } else if (text == "Run") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::onQuickStepsClicked);
        } else if (text == "Reset") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::resetSimulation);
        } else if (text == "Center on Ant") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::centerView);
        } else if (text == "Zoom In") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::zoomIn);
        } else if (text == "Zoom Out") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::zoomOut);
        } else if (text == "Cell Size...") {
            connect(btn, &QPushButton::clicked, this, &MainWindow::changeCellSize);
        } else if (text == "←") {
            connect(btn, &QPushButton::clicked, this, [this]() { moveView(50, 0); });
        } else if (text == "→") {
            connect(btn, &QPushButton::clicked, this, [this]() { moveView(-50, 0); });
        } else if (text == "↑") {
            connect(btn, &QPushButton::clicked, this, [this]() { moveView(0, 50); });
        } else if (text == "↓") {
            connect(btn, &QPushButton::clicked, this, [this]() { moveView(0, -50); });
        }
    }

    // Preset combo
    QComboBox *presetCombo = findChild<QComboBox*>();
    if (presetCombo) {
        connect(presetCombo, QOverload<int>::of(&QComboBox::activated),
                this, &MainWindow::loadPreset);
    }

    // Rules edit - update on Enter key
    connect(rulesEdit, &QLineEdit::returnPressed,
            this, &MainWindow::updateRules);
}
