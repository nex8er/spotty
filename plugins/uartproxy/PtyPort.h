/**
 * \file PtyPort.h
 * \brief Виртуальный порт на псевдотерминале (POSIX).
 */
#pragma once

#include "VirtualPort.h"

class QSocketNotifier;

namespace spotty {

/**
 * \class PtyPort
 * \brief Создаёт псевдотерминал и отдаёт чужой программе его подчинённый конец.
 *
 * Программа открывает `/dev/ttysNNN` (macOS) или `/dev/pts/N` (Linux) как обычный
 * последовательный порт, Spotty читает и пишет ведущий конец. Номер меняется при каждом
 * запуске, поэтому при наличии настройки рядом кладётся ссылка с постоянным путём.
 *
 * \par Режим линии
 *
 * Подчинённый конец сразу переводится в «сырой» режим. Иначе драйвер терминала
 * по умолчанию отражает ввод (`ECHO`) — а всё, что Spotty пишет в ведущий конец, для него
 * ввод, — и ответ устройства возвращался бы в ведущий конец как «отправка программой»,
 * после чего уходил обратно устройству: петля, которую не отличить от настоящего обмена.
 * Программа, настраивающая порт сама (а любая серьёзная так и делает), перекрывает режим
 * своим, и это нормально.
 *
 * \par Зачем держать подчинённый конец открытым
 *
 * Пока ни одного процесса на нём нет, чтение ведущего конца на Linux завершается ошибкой
 * `EIO`, а запись теряет данные. Собственный дескриптор подчинённого конца, открытый до
 * закрытия порта, снимает оба случая: программа может приходить и уходить.
 *
 * \note Скорость, чётность и стоповые биты на псевдотерминале ничего не значат: программа
 *       их выставит, но на частоту обмена они не повлияют. Реальные параметры задаются у
 *       реального порта.
 */
class PtyPort : public VirtualPort
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param linkPath Имя, под которым порт увидит чужая программа; пустая строка — без
     *        имени, и тогда открывать придётся путь самого pty.
     *
     * Путь вне `/dev` — обычная ссылка прямо на pty, права не нужны, но в списках портов
     * других программ её нет: путь им приходится вводить руками. Путь в `/dev` в такие
     * списки попадает, и ссылка там создаётся с правами администратора через
     * промежуточную (см. spotty::uartproxy::bridgePath()) — один раз за загрузку системы.
     */
    explicit PtyPort(QString linkPath, QObject *parent = nullptr);
    ~PtyPort() override;

    bool open(const QVariantMap &settings, QString *error) override;
    void close() override;
    void write(const QByteArray &data) override;

    /// \brief Постоянный путь, если он задан, иначе путь подчинённого конца.
    QString description() const override;
    QString details() const override;

    /// \brief Путь подчинённого конца, который открывает программа; пусто, пока порт закрыт.
    QString slavePath() const { return m_slavePath; }

private:
    /// \brief Прочитать всё доступное из ведущего конца.
    void readAvailable();

    /// \brief Дописать очередь в ведущий конец, сколько он примет сейчас.
    void flushPending();

    /// \brief Положить ссылку \p path на подчинённый конец; при отказе записывает причину.
    bool createLink(const QString &path, QString *error);

    /// \brief Убрать ссылку, которую положил createLink(), если она всё ещё наша.
    void removeLink();

    /// \brief Имя, заказанное пользователем.
    QString m_linkPath;

    /// \brief Ссылка прямо на pty: заказанное имя либо промежуточная для имени в `/dev`.
    QString m_ownLink;

    QString m_slavePath;

    /// \brief Заказанное имя готово к открытию чужой программой.
    bool m_linkCreated = false;

    int m_master = -1;
    int m_slave = -1;

    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;

    /// \brief Не принятое ведущим концом: программа читает медленнее, чем отвечает устройство.
    QByteArray m_pending;
    bool m_overflowReported = false;
};

} // namespace spotty
