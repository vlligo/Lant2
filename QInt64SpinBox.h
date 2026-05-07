#ifndef LANT_QINT64SPINBOX_H
#define LANT_QINT64SPINBOX_H


#include <QAbstractSpinBox>
#include <QLineEdit>

class QInt64SpinBox : public QAbstractSpinBox
{
    Q_OBJECT

    Q_PROPERTY(qint64 value READ value WRITE setValue NOTIFY valueChanged USER true)
    Q_PROPERTY(qint64 minimum READ minimum WRITE setMinimum)
    Q_PROPERTY(qint64 maximum READ maximum WRITE setMaximum)
    Q_PROPERTY(qint64 singleStep READ singleStep WRITE setSingleStep)
    Q_PROPERTY(QString suffix READ suffix WRITE setSuffix)

public:
    explicit QInt64SpinBox(QWidget *parent = nullptr);

    [[nodiscard]] qint64 value() const;
    void setValue(qint64 val);

    [[nodiscard]] qint64 minimum() const;
    void setMinimum(qint64 min);

    [[nodiscard]] qint64 maximum() const;
    void setMaximum(qint64 max);

    void setRange(qint64 min, qint64 max);

    [[nodiscard]] qint64 singleStep() const;
    void setSingleStep(qint64 step);

    [[nodiscard]] QString suffix() const;
    void setSuffix(const QString &suffix);

    signals:
        void valueChanged(qint64 value);

protected:
    void stepBy(qint64 steps);
    [[nodiscard]] StepEnabled stepEnabled() const override;

    // Handling of manually typed text
    QValidator::State validate(QString &input, int &pos) const override;
    void fixup(QString &input) const override;

    // Convert between internal value and displayed text (number + suffix)
    [[nodiscard]] QString textFromValue(qint64 val) const;
    [[nodiscard]] qint64 valueFromText(const QString &text) const;

private slots:
    void onEditingFinished();

private:
    qint64 m_value;
    qint64 m_minimum;
    qint64 m_maximum;
    qint64 m_singleStep;
    QString m_suffix;
};


#endif //LANT_QINT64SPINBOX_H