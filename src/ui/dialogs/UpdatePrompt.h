/**
 * \file UpdatePrompt.h
 * \brief Окно с предложением обновиться.
 */
#pragma once

#include <UpdateChecker.h>

#include <QCoreApplication>

class QWidget;

namespace spotty {

/**
 * \class UpdatePrompt
 * \brief Сообщает о новой версии и спрашивает, что с этим делать.
 */
class UpdatePrompt
{
    Q_DECLARE_TR_FUNCTIONS(spotty::UpdatePrompt)

public:
    /// \brief Ответ пользователя.
    enum class Choice {
        Download, ///< Открыть пакет (или страницу релиза) в браузере.
        Later,    ///< Напомнить при следующем запуске.
        Disable,  ///< Больше не проверять; вернуть можно в настройках.
    };

    /**
     * \brief Показать окно и дождаться ответа.
     * \param parent Родитель окна.
     * \param currentVersion Запущенная версия.
     * \param release Найденный релиз.
     *
     * Закрытие окна крестиком или Esc — «Позже», а не «Отключить»: случайный жест не
     * должен навсегда выключать проверку.
     */
    static Choice ask(QWidget *parent, const QString &currentVersion, const ReleaseInfo &release);
};

} // namespace spotty
