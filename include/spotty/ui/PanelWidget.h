/**
 * \file PanelWidget.h
 * \brief Необязательная основа для виджета панели.
 */
#pragma once

#include <spotty/api/ChannelState.h>
#include <spotty/ui/SpottyUiApiExport.h>

#include <QWidget>

class QVBoxLayout;

namespace spotty {

class IPanelHost;

/**
 * \class PanelWidget
 * \brief Основа панели: заголовок, поля раскладки и отклики на события хоста.
 *
 * \par Зачем
 *
 * Панель можно унаследовать и от голого QWidget — SDK этого не запрещает. Но четыре
 * встроенные панели показали, что у каждой повторяется одно и то же: заголовок с
 * `objectName("panelTitle")` первым в раскладке, поля `6,10,6,10` с шагом `8`, подписка на
 * смену темы ради перекраски значков и метод вида `setSendEnabled(bool)`, вызываемый
 * окном при смене состояния канала. Всё это здесь и сделано один раз.
 *
 * \par Отличие от прежнего устройства
 *
 * Раньше окно дёргало у каждой панели свой метод: `setSendEnabled()` у макросов и
 * генератора, `setInterfaceOpen()` у журнала. Теперь состояние приходит одним
 * channelStateChanged() — окну не нужно знать, какая панель что из него извлечёт.
 */
class SPOTTY_UI_API_EXPORT PanelWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * \param host Живёт дольше панели; хранится по ссылке.
     * \param parent Родитель в дереве виджетов.
     */
    explicit PanelWidget(IPanelHost *host, QWidget *parent = nullptr);
    ~PanelWidget() override;

    IPanelHost *host() const { return m_host; }

protected:
    /**
     * \brief Задать заголовок панели.
     *
     * Создаёт подпись с `objectName("panelTitle")` первой в раскладке — тот же вид, что у
     * остальных панелей окна. Повторный вызов меняет текст.
     */
    void setPanelTitle(const QString &title);

    /**
     * \brief Раскладка для содержимого панели.
     *
     * Поля и шаг уже выставлены. Добавлять свои виджеты нужно сюда, а не в новую
     * раскладку поверх: иначе панель встанет с чужими отступами.
     */
    QVBoxLayout *content() const { return m_content; }

    /**
     * \brief Тема сменилась.
     *
     * Здесь пересобираются значки: QIcon хранит уже отрисованное изображение, и после
     * смены темы старый значок остаётся прежнего цвета. Вызывается и из конструктора
     * производного класса не будет — вызовите свою перерисовку сами, как обычно делают с
     * виртуальными методами.
     */
    virtual void themeChanged() {}

    /// \brief Состояние канала изменилось: разрешить или запретить отправку, остановиться.
    virtual void channelStateChanged(ChannelState state) { Q_UNUSED(state); }

    /**
     * \brief Настройки приложения сброшены к умолчаниям.
     *
     * Панель обязана перечитать своё состояние. Без этого сброс выглядит выполненным
     * наполовину: файл настроек чист, а панель показывает то, что успела прочитать при
     * запуске.
     */
    virtual void settingsReset() {}

    /// \brief Окно закрывается: дописать файлы, остановить запись, сохранить состояние.
    virtual void aboutToClose() {}

private:
    IPanelHost *m_host = nullptr;
    QVBoxLayout *m_content = nullptr;
    QWidget *m_title = nullptr;
};

} // namespace spotty
