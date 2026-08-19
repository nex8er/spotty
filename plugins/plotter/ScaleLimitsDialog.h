/**
 * \file ScaleLimitsDialog.h
 * \brief Окно ввода пределов шкалы ряда.
 */
#pragma once

#include <QDialog>

class QLineEdit;

namespace spotty {

/**
 * \class ScaleLimitsDialog
 * \brief Спрашивает нижний и верхний пределы шкалы разом.
 *
 * \par Почему одно окно, а не два подряд
 *
 * Пределы задают парой: осмысленно только их соотношение, и ввести один, не видя второго,
 * нельзя. Два QInputDialog подряд к тому же не давали отменить целиком — отказ во втором
 * оставлял первый уже введённым.
 *
 * Поля текстовые, а не счётчики: телеметрия ходит и в наносекундах, и в мегагерцах, а
 * QDoubleSpinBox с фиксированным числом знаков после запятой показал бы 0.000000 вместо
 * 1.2e-9. Здесь принимается всё, что понимает QString::toDouble, включая запись с
 * экспонентой.
 */
class ScaleLimitsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * \param seriesName Имя ряда — уходит в заголовок, чтобы не гадать, чью шкалу правят.
     * \param minimum Что показать в поле нижнего предела.
     * \param maximum Что показать в поле верхнего.
     * \param measured Измеренные пределы ряда; показываются подсказкой и кнопкой возврата.
     * \param hasMeasured Есть ли измеренные значения вообще.
     */
    ScaleLimitsDialog(const QString &seriesName, double minimum, double maximum,
                      double measuredMinimum, double measuredMaximum, bool hasMeasured,
                      QWidget *parent = nullptr);

    double minimum() const;
    double maximum() const;

private:
    /// \brief Разобрать оба поля; при неудаче гасит кнопку подтверждения.
    void validate();

    QLineEdit *m_minimum = nullptr;
    QLineEdit *m_maximum = nullptr;
    QPushButton *m_accept = nullptr;
};

} // namespace spotty
