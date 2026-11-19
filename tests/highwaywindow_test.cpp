#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QtMath>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using Cell = std::pair<int, int>;
using Board = std::map<Cell, uint32_t>;

static void require(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}

struct Fixture { int onset, period; };
struct ExpectedWindow { Cell head; Board cells; };

// Forward-only reference ant. Freeze the entire board at the known onset and
// intersect its history with future reads, independently of production rewind.
static ExpectedWindow reference(const std::string &rule, const Fixture fixture, int editColor = -1) {
    Board board, atOnset;
    ExpectedWindow expected;
    Cell pos{0, 0};
    int dir = 0;
    const int dx[]{0, 1, 0, -1}, dy[]{-1, 0, 1, 0};
    std::vector<std::pair<int, char>> history;
    for (int i = 0; i < fixture.onset + highwayPreviewSteps; ++i) {
        if (editColor >= 0 && i == 40) board.at(pos) = editColor;
        if (i == fixture.onset) {
            expected.head = pos;
            atOnset = board;
        }
        if (i >= fixture.onset && atOnset.count(pos)) expected.cells.emplace(pos, atOnset.at(pos));
        auto &color = board[pos];
        const char turn = rule[color];
        history.emplace_back(dir, turn);
        if (i >= fixture.onset + fixture.period)
            require(history[i] == history[i - fixture.period], "reference fixture stopped repeating");
        dir = (dir + (turn == 'L' ? 3 : 1)) % 4;
        color = (color + 1) % rule.size();
        pos.first += dx[dir];
        pos.second += dy[dir];
    }
    if (fixture.onset != 40 || editColor < 0)
        require(history[fixture.onset - 1] != history[fixture.onset + fixture.period - 1], "incorrect onset fixture");
    return expected;
}

static void verify(const AntFieldWidget &field, const Fixture fixture, const ExpectedWindow &expected) {
    auto result = field.analyzeHighway();
    require(result && result->period == fixture.period, "incorrect period");
    require(result->onsetStep == fixture.onset, "incorrect onset step");
    require(result->onsetX == expected.head.first && result->onsetY == expected.head.second, "incorrect onset position");
    Board actual;
    for (const auto &c : result->window) actual.emplace(Cell{c.x, c.y}, c.state);
    require(!actual.empty() && actual == expected.cells && actual.size() == result->window.size(),
            "incorrect onset window membership or state");
}

static void click(MainWindow &w, const QString &text) {
    for (auto *button : w.findChildren<QPushButton*>()) {
        if (button->text() == text && button->isVisible() && button->isEnabled()) {
            button->click();
            return;
        }
    }
    throw std::runtime_error(("missing button " + text).toStdString());
}

static bool shows(MainWindow &w, const QString &text) {
    QElapsedTimer timeout;
    timeout.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        for (const auto *label : w.findChildren<QLabel*>())
            if (label->isVisible() && label->text() == text) return true;
    } while (timeout.elapsed() < 5000);
    return false;
}

static void verifyRendering(AntFieldWidget &field, const ExpectedWindow &expected,
                            const QImage &marked, const QImage &plain) {
    int left = expected.head.first, right = left, top = expected.head.second, bottom = top;
    for (const auto &[pos, color] : expected.cells) {
        left = std::min(left, pos.first); right = std::max(right, pos.first);
        top = std::min(top, pos.second); bottom = std::max(bottom, pos.second);
    }
    const int size = qRound(field.getZoom() * field.getCellSize());
    require(size == 32 && marked.size() == plain.size(), "unexpected framing");
    const int originX = marked.width() / 2 - (left + (right - left) / 2) * size;
    const int originY = marked.height() / 2 - (top + (bottom - top) / 2) * size;
    std::set<Cell> visible;
    for (int y = 0; y < marked.height(); ++y) {
        for (int x = 0; x < marked.width(); ++x) {
            if (marked.pixel(x, y) == plain.pixel(x, y)) continue;
            const int gx = qFloor(double(x - originX) / size), gy = qFloor(double(y - originY) / size);
            require(expected.cells.count({gx, gy}), "changed a pixel outside the onset window");
            const int px = x - (originX + gx * size), py = y - (originY + gy * size);
            require(px < 4 || py < 4 || px >= size - 4 || py >= size - 4, "changed a cell interior");
            visible.emplace(gx, gy);
        }
    }
    require(visible.size() == expected.cells.size(), "missing outline on a window cell");
}

// At step 40 of LLR the head is on a previously read cell. Every possible
// replacement color yields a distinct pose at step 1600, so the independent
// reference can check the actual random outcome without controlling the RNG.
static void verifyEditedBoard(AntFieldWidget &field, const QString &path) {
    field.reset();
    field.nextStep(40);
    field.randomizeArea(0);
    Cell end;
    const auto connection = QObject::connect(&field, &AntFieldWidget::antMoved,
        [&](qint64 x, qint64 y, int, qint64) { end = {x, y}; });
    field.nextStep(1560);
    QObject::disconnect(connection);
    const std::map<Cell, std::pair<int, int>> outcomes{
        {{-84, 90}, {0, 49}}, {{-70, -62}, {1, 406}}, {{-86, 88}, {2, 40}}
    };
    require(outcomes.count(end), "unexpected trajectory after board edit");
    const auto [color, onset] = outcomes.at(end);
    const Fixture edited{onset, 18};
    const auto expected = reference("LLR", edited, color);
    verify(field, edited, expected);
    require(field.saveState(path) && field.loadState(path), "edited board save/load failed");
    verify(field, edited, expected);
}

static void waitForSteps(AntFieldWidget &field, qint64 target) {
    QElapsedTimer timeout;
    timeout.start();
    while (field.getTotalVisitedCells() < target && timeout.elapsed() < 5000)
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    require(field.getTotalVisitedCells() == target, "finite run missed its target");
}

static void verifyRunsAndRankings() {
    MainWindow window;
    window.show();
    QApplication::processEvents();
    auto &field = *window.findChild<AntFieldWidget *>();
    auto *steps = window.findChild<QInt64SpinBox *>();
    auto checkRanking = [&] {
        // Requests above the cache capacity use a full scan of the visit counters.
        const auto expected = field.getTopVisitedCells(21).mid(0, 20);
        require(field.getTopVisitedCells(20) == expected, "incorrect top 20");
        require(field.getTopVisitedCells(7) == expected.mid(0, 7), "incorrect cached prefix");
    };
    steps->setValue(1000123);
    click(window, "Run");
    waitForSteps(field, steps->value());
    checkRanking();
    QTemporaryDir dir;
    const auto path = dir.filePath("rankings.ant");
    // A small snapshot also exercises rebuilding the cache from the save format.
    click(window, "Reset");
    require(field.getTopVisitedCells(20).isEmpty(), "reset retained old ranking");
    field.nextStep(18000);
    checkRanking();
    const auto saved = field.getTopVisitedCells(20);
    require(field.saveState(path), "ranking snapshot save failed");
    field.nextStep(12345);
    checkRanking();
    field.resetStatistics();
    require(field.getTopVisitedCells(20).isEmpty(), "statistics reset retained old ranking");
    require(field.loadState(path) && field.getTopVisitedCells(20) == saved, "incorrect restored ranking");
    field.setStatisticsEnabled(false);
    field.nextStep(1000);
    require(field.getTopVisitedCells(20).isEmpty(), "disabled tracking retained old ranking");
    field.setStatisticsEnabled(true);
    field.nextStep(20000);
    checkRanking();

    steps->setValue(5000000);
    const auto target = field.getTotalVisitedCells() + steps->value();
    click(window, "Run");
    click(window, "Stop");
    const auto stopped = field.getTotalVisitedCells();
    require(stopped < target, "large Run blocked until completion");
    QApplication::processEvents();
    require(field.getTotalVisitedCells() == stopped, "finite run continued after Stop");
    click(window, "Start");
    click(window, "Stop");
    require(field.getTotalVisitedCells() > stopped, "continuous run failed to advance");
    click(window, "Run");
    click(window, "Reset");
    QApplication::processEvents();
    require(field.getTotalVisitedCells() == 0, "pending run survived a reset");
}

static QByteArray contents(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "cannot read test output");
    return file.readAll();
}

static void verifyAuditFixes() {
    QTemporaryDir dir;
    AntFieldWidget field;
    field.resize(640, 480);
    field.setRules("LR");
    field.nextStep(20000);
    field.resetStatistics();
    field.nextStep();
    require(field.getStatisticsSummary().averageVisits == 1, "average includes untracked steps");
    require(field.estimateRandomizeAreaBytes(1) >= 4 * qint64(sizeof(Chunk)), "chunk alignment missing");

    const auto path = dir.filePath("source.ant");
    require(field.saveState(path), "could not save initial state");
    const auto original = contents(path);
    const auto partial = dir.filePath("partial.ant");
    const auto roundTrip = dir.filePath("current.ant");
    for (int length : {0, 10, int(original.size() / 2), int(original.size() - 1)}) {
        QFile file(partial);
        require(file.open(QIODevice::WriteOnly) && file.write(original.left(length)) == length,
                "could not simulate interrupted write");
        file.close();
        require(!field.loadState(partial), "incomplete snapshot accepted");
        require(field.saveState(roundTrip) && contents(roundTrip) == original, "failed load changed live board");
    }
    field.nextStep(300);
    require(field.loadState(path) && field.getStatisticsSummary().averageVisits == 1, "lost tracked visit count");
    const auto csv = dir.filePath("visits.csv");
    require(field.exportStatistics(csv), "CSV export failed");
    const auto lines = contents(csv).trimmed().split('\n');
    require(lines.size() == 2 && lines[1].split(',').value(2) == "1", "unexpected CSV rows");

    field.setRules("LLRRLR");
    field.randomizeArea(96);
    field.setZoom(0.071);
    field.centerOnPoint(0, 0);
    const auto still = field.grab().toImage();
    for (int i = 0; i < 12; ++i) {
        field.centerOnPoint(0, 0);
        require(field.grab().toImage() == still, "stationary board flickers");
    }
    field.setZoom(1.0);
    qint64 mouseX = 0, mouseY = 0;
    QObject::connect(&field, &AntFieldWidget::mouseOverCell, [&](qint64 x, qint64 y) {
        mouseX = x; mouseY = y;
    });
    for (qint64 value : {std::numeric_limits<qint64>::min(), std::numeric_limits<qint64>::max()}) {
        field.centerOnPoint(value, value);
        require(field.saveState(path), "camera save failed");
        field.centerOnPoint(0, 0);
        require(field.loadState(path), "camera restore failed");
        field.grab();
        const QPointF center(field.width() / 2, field.height() / 2);
        QMouseEvent move(QEvent::MouseMove, center, center, center, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&field, &move);
        require(mouseX == value && mouseY == value, "lost 64-bit camera precision");
    }

    field.setRules("LR");
    field.setStatisticsEnabled(false);
    field.nextStep(1000000);
    int completions = 0;
    QObject::connect(&field, &AntFieldWidget::highwayChecked, [&](int) { ++completions; });
    field.startHighwayCheck();
    field.reset();
    QApplication::processEvents();
    require(completions == 0 && field.getTotalVisitedCells() == 0, "stale analysis survived reset");
}

static void run(const std::string &rule) {
    const std::map<std::string, Fixture> fixtures{{"LR",{9977,104}}, {"RL",{9977,104}}, {"LLR",{39,18}},
                                                {"RRL",{39,18}}, {"LLLR",{79,52}}, {"LRLR",{9977,104}}};
    MainWindow w;
    w.show();
    QApplication::processEvents();
    auto *field = w.findChild<AntFieldWidget*>();
    field->setFixedSize(960, 420);
    for (auto *edit : w.findChildren<QLineEdit*>())
        if (edit->text() == "LR") { edit->setText(QString::fromStdString(rule)); break; }
    click(w, "Update Rules");
    require(field->expandedRules().toStdString() == rule, "rule was not applied");
    auto runSteps = [&](int steps) {
        w.findChild<QInt64SpinBox*>()->setValue(steps);
        const auto target = field->getTotalVisitedCells() + steps;
        click(w, "Run");
        waitForSteps(*field, target);
    };
    if (!fixtures.count(rule)) {
        runSteps(15000);
        click(w, "Check Highway");
        require(shows(w, "No highway detected") && !field->analyzeHighway(), "false highway");
        return;
    }
    const auto fixture = fixtures.at(rule);
    const auto expected = reference(rule, fixture);
    runSteps(fixture.onset - 1);
    require(!field->analyzeHighway(), "detected a highway one step early");
    click(w, "Step (1)");
    verify(*field, fixture, expected);
    runSteps(12000 + fixture.period / 2);
    verify(*field, fixture, expected);
    const qint64 checked = field->getTotalVisitedCells();
    // Click from each style and also switch styles while the window is shown.
    std::vector<QImage> styles;
    for (int style = 0; style < 5; ++style) {
        click(w, "Check Highway");
        require(shows(w, QString("Period: %1 steps").arg(fixture.period)), "unexpected result text");
        click(w, "Next Style");
        const auto marked = field->grab().toImage();
        click(w, "Dismiss");
        auto plain = field->grab().toImage();
        verifyRendering(*field, expected, marked, plain);
        require(std::find(styles.begin(), styles.end(), plain) == styles.end(), "style did not change");
        styles.push_back(std::move(plain));
    }
    require(field->getTotalVisitedCells() == checked, "checking advanced the live ant");
    field->setStatisticsEnabled(false);
    field->nextStep(109);
    verify(*field, fixture, expected);
    QTemporaryDir dir;
    const auto path = dir.filePath("window.ant");
    require(field->saveState(path) && field->loadState(path), "save/load failed");
    verify(*field, fixture, expected);
    QFile legacy(path);
    require(legacy.open(QIODevice::ReadWrite), "cannot open legacy fixture");
    const auto end = legacy.readAll().lastIndexOf("WIN1");
    require(end >= 0 && legacy.resize(end), "cannot build legacy fixture");
    legacy.close();
    require(field->loadState(path), "legacy load failed");
    verify(*field, fixture, expected);
    if (rule == "LLR") verifyEditedBoard(*field, path);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        require(argc == 2, "missing rule");
        if (std::string(argv[1]) == "runs-and-rankings") verifyRunsAndRankings();
        else if (std::string(argv[1]) == "audit-fixes") verifyAuditFixes();
        else run(argv[1]);
    } catch (const std::exception &e) {
        std::cerr << argv[argc - 1] << ": " << e.what() << '\n';
        return 1;
    }
}
