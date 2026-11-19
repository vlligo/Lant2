#include "diagonalcolordialog.h"
#include "antfieldwidget.h"

#include <QAbstractListModel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// Rules can contain millions of steps. Only materialize visible rows.
class DiagonalColorModel final : public QAbstractListModel {
public:
    explicit DiagonalColorModel(const AntFieldWidget &field)
        : field(field), selected(field.diagonalStates()) {}

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : selected.size();
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) return {};
        const int step = index.row() + 1;
        const int state = step % selected.size();
        switch (role) {
        case Qt::DisplayRole:
            return QString("Step %1 (%2) → color %3")
                .arg(step).arg(field.expandedRules().at(index.row())).arg(state);
        case Qt::DecorationRole: return field.cellColor(state);
        case Qt::CheckStateRole: return selected.testBit(state) ? Qt::Checked : Qt::Unchecked;
        default: return {};
        }
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        return QAbstractListModel::flags(index) | Qt::ItemIsUserCheckable;
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override {
        if (!index.isValid() || role != Qt::CheckStateRole) return false;
        selected.setBit((index.row() + 1) % selected.size(), value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {role});
        return true;
    }

    void selectAll(bool enabled) {
        if (selected.isEmpty()) return;
        selected.fill(enabled);
        emit dataChanged(index(0), index(rowCount() - 1), {Qt::CheckStateRole});
    }

    const AntFieldWidget &field;
    QBitArray selected;
};
}

void chooseDiagonalColors(AntFieldWidget &field, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Diagonal colors");
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel("Mark the color left on a cell after each rule step. "
                            "Diagonals appear on tracked visits at 4 pixels per cell or larger.");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    DiagonalColorModel model(field);
    auto *list = new QListView;
    list->setUniformItemSizes(true);
    list->setModel(&model);
    layout->addWidget(list);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    for (bool enabled : {true, false}) {
        auto *button = buttons->addButton(enabled ? "Select all" : "Clear all", QDialogButtonBox::ActionRole);
        QObject::connect(button, &QPushButton::clicked, &model, [&model, enabled] { model.selectAll(enabled); });
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() == QDialog::Accepted) field.setDiagonalStates(model.selected);
}
