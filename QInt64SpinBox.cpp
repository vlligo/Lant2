#include "QInt64SpinBox.h"
#include <limits>

QInt64SpinBox::QInt64SpinBox(QWidget *parent)
    : QAbstractSpinBox(parent)
    , m_value(0)
    , m_minimum(0)
    , m_maximum(std::numeric_limits<qint64>::max())
    , m_singleStep(1)
{
    // Update line edit when user finishes typing
    connect(lineEdit(), &QLineEdit::editingFinished,
            this, &QInt64SpinBox::onEditingFinished);

    // Show initial value
    lineEdit()->setText(textFromValue(m_value));
}

qint64 QInt64SpinBox::value() const
{
    return m_value;
}

void QInt64SpinBox::setValue(qint64 val)
{
    val = qBound(m_minimum, val, m_maximum);
    if (val != m_value) {
        m_value = val;
        lineEdit()->setText(textFromValue(m_value));
        emit valueChanged(m_value);
    }
}

qint64 QInt64SpinBox::minimum() const
{
    return m_minimum;
}

void QInt64SpinBox::setMinimum(const qint64 min)
{
    m_minimum = min;
    if (m_maximum < m_minimum)
        m_maximum = m_minimum;
    if (m_value < m_minimum)
        setValue(m_minimum);
}

qint64 QInt64SpinBox::maximum() const
{
    return m_maximum;
}

void QInt64SpinBox::setMaximum(qint64 max)
{
    m_maximum = max;
    if (m_minimum > m_maximum)
        m_minimum = m_maximum;
    if (m_value > m_maximum)
        setValue(m_maximum);
}

void QInt64SpinBox::setRange(qint64 min, qint64 max)
{
    m_minimum = min;
    m_maximum = max;
    // Re-clamp current value
    setValue(m_value);
}

qint64 QInt64SpinBox::singleStep() const
{
    return m_singleStep;
}

void QInt64SpinBox::setSingleStep(const qint64 step)
{
    if (step > 0)
        m_singleStep = step;
}

QString QInt64SpinBox::suffix() const
{
    return m_suffix;
}

void QInt64SpinBox::setSuffix(const QString &suffix)
{
    m_suffix = suffix;
    // Redisplay the value with the new suffix
    lineEdit()->setText(textFromValue(m_value));
}

void QInt64SpinBox::stepBy(const qint64 steps)
{
    if (steps == 0)
        return;
    const qint64 newVal = m_value + static_cast<qint64>(steps) * m_singleStep;
    setValue(newVal);
}

QAbstractSpinBox::StepEnabled QInt64SpinBox::stepEnabled() const
{
    StepEnabled flags;
    if (m_value < m_maximum)
        flags |= StepUpEnabled;
    if (m_value > m_minimum)
        flags |= StepDownEnabled;
    return flags;
}

QValidator::State QInt64SpinBox::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);
    QString text = input.trimmed();

    // Remove suffix if present (case‑sensitive)
    if (!m_suffix.isEmpty() && text.endsWith(m_suffix))
        text.chop(m_suffix.length());
    else if (!m_suffix.isEmpty())
        return QValidator::Invalid;

    bool ok;
    qint64 val = text.remove(QLocale().groupSeparator()).toLongLong(&ok);
    if (!ok)
        return QValidator::Invalid;
    if (val < m_minimum || val > m_maximum)
        return QValidator::Intermediate;
    return QValidator::Acceptable;
}

void QInt64SpinBox::fixup(QString &input) const
{
    // Strip suffix and extra spaces, then try to fix the number
    QString text = input.trimmed();
    if (!m_suffix.isEmpty() && text.endsWith(m_suffix))
        text.chop(m_suffix.length());
    text = text.trimmed();

    bool ok;
    qint64 val = text.remove(QLocale().groupSeparator()).toLongLong(&ok);
    if (!ok)
        val = m_minimum;
    else
        val = qBound(m_minimum, val, m_maximum);

    input = textFromValue(val);
}

QString QInt64SpinBox::textFromValue(qint64 val) const
{
    if (m_suffix.isEmpty())
        return QString::number(val);
    return QLocale().toString(val) + m_suffix;
}

qint64 QInt64SpinBox::valueFromText(const QString &text) const
{
    QString t = text.trimmed();
    if (!m_suffix.isEmpty() && t.endsWith(m_suffix))
        t.chop(m_suffix.length());
    return t.remove(QLocale().groupSeparator()).toLongLong();
}

void QInt64SpinBox::onEditingFinished()
{
    QString currentText = lineEdit()->text();
    int dummy = 0;
    if (validate(currentText, dummy) == QValidator::Acceptable) {
        qint64 newVal = valueFromText(currentText);
        setValue(newVal);
    } else {
        // Reset to current value with suffix
        lineEdit()->setText(textFromValue(m_value));
    }
}