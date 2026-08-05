/**
 * \file InterfaceSettingsDialog.h
 * \brief Диалог настроек интерфейса — тонкая обёртка вокруг InterfaceSettingsPanel.
 */
#pragma once

#include <QDialog>

namespace spotty {

class InterfaceRegistry;
class InterfaceSettingsPanel;
class PluginManager;

/**
 * \class InterfaceSettingsDialog
 * \brief Отдельное окно вокруг spotty::InterfaceSettingsPanel.
 *
 * Вся логика — в панели; диалог лишь даёт ей модальное окно с кнопкой закрытия и
 * выбирает устройство, ради которого его открыли. Тот же widget встраивается страницей в
 * spotty::SettingsDialog — одна реализация настроек интерфейса, а не две.
 *
 * Правки применяются немедленно самой панелью, поэтому кнопки «Отмена» здесь нет: закрыть
 * окно, отменив уже применённое, было бы нечем.
 */
class InterfaceSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param registry Реестр интерфейсов; должен пережить диалог.
     * \param plugins Менеджер плагинов; должен пережить диалог.
     * \param initialId Устройство, которое нужно показать выбранным сразу.
     * \param parent Родительский виджет.
     */
    InterfaceSettingsDialog(InterfaceRegistry *registry, PluginManager *plugins,
                            const QString &initialId, QWidget *parent = nullptr);

    /// \return Панель внутри диалога — чтобы подключиться к её settingsApplied().
    InterfaceSettingsPanel *panel() const { return m_panel; }

private:
    InterfaceSettingsPanel *m_panel = nullptr;
};

} // namespace spotty
