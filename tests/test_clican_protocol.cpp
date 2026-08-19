/**
 * \file test_clican_protocol.cpp
 * \brief Тесты адресации и учёта узлов туннеля CLI поверх CAN.
 *
 * Адаптер, драйвер PEAK и шина здесь не нужны: вся арифметика протокола вынесена в
 * заголовок без единого обращения к железу ровно ради этой проверки. Всё, что требует
 * шины, проверяется запуском — см. AGENTS.md.
 */
#include "../plugins/clican/CliCanProtocol.h"

#include <gtest/gtest.h>

using namespace spotty::clican;

TEST(CliCanProtocol, AddressesNodesAsTheSpecificationSays)
{
    // Пример из описания протокола: узел 5 принимает на 0x40A, отвечает с 0x40B.
    EXPECT_EQ(requestId(5), 0x40Au);
    EXPECT_EQ(responseId(5), 0x40Bu);

    // Узел 1 — первый после зарезервированного нулевого.
    EXPECT_EQ(requestId(1), 0x402u);
    EXPECT_EQ(responseId(1), 0x403u);
}

TEST(CliCanProtocol, RejectsNodesOutsideTheAddressableRange)
{
    EXPECT_FALSE(isValidNode(0));
    EXPECT_TRUE(isValidNode(1));
    EXPECT_TRUE(isValidNode(kMaxNode));

    // Узел 447 описание называет допустимым, но его пара идентификаторов — 0x77E и 0x77F,
    // объявленные там же зарезервированными. Адресовать его нечем.
    EXPECT_EQ(requestId(447), 0x77Eu);
    EXPECT_FALSE(isValidNode(447));
}

TEST(CliCanProtocol, RecognisesResponsesOnlyFromRealNodes)
{
    EXPECT_EQ(nodeFromResponseId(0x40Bu), 5);
    EXPECT_EQ(nodeFromResponseId(0x403u), 1);

    // Чётный идентификатор — это команда узлу, а не его ответ. Приняв его за ответ, мы
    // засчитали бы за «узел на шине» собственную же посылку, вернувшуюся эхом.
    EXPECT_EQ(nodeFromResponseId(0x40Au), 0);

    // Служебные: широковещание, управление шлюзами и зарезервированный хвост диапазона.
    EXPECT_EQ(nodeFromResponseId(kBroadcastId), 0);
    EXPECT_EQ(nodeFromResponseId(kGatewayControlId), 0);
    EXPECT_EQ(nodeFromResponseId(0x77Fu), 0);

    // Вне выделенного туннелю диапазона — чужой трафик шины.
    EXPECT_EQ(nodeFromResponseId(0x123u), 0);
    EXPECT_EQ(nodeFromResponseId(0x800u), 0);
}

TEST(CliCanProtocol, TellsFlowControlFromPresenceQuery)
{
    // Один байт на широковещательном идентификаторе — просьба шлюза придержать отправку.
    EXPECT_EQ(flowControlPauseMs(kBroadcastId, QByteArray(1, char(50))), 50);
    EXPECT_EQ(flowControlPauseMs(kBroadcastId, QByteArray(1, char(0xFF))), 255);

    // Пустой пакет на том же идентификаторе — запрос присутствия, а не пауза. Отличаются
    // они только длиной, и спутать их значило бы замирать на каждом опросе шины.
    EXPECT_EQ(flowControlPauseMs(kBroadcastId, QByteArray()), -1);

    // На адресном идентификаторе один байт — обычные данные CLI.
    EXPECT_EQ(flowControlPauseMs(requestId(5), QByteArray(1, char(50))), -1);
}

TEST(CliCanProtocol, SplitsStreamIntoFrameSizedChunks)
{
    const QList<QByteArray> chunks = splitPayload(QByteArrayLiteral("0123456789"));
    ASSERT_EQ(chunks.size(), 2);
    EXPECT_EQ(chunks.at(0), QByteArrayLiteral("01234567"));
    // Последний кусок короче восьми — это законно: длина кадра от 1 до 8.
    EXPECT_EQ(chunks.at(1), QByteArrayLiteral("89"));

    EXPECT_EQ(splitPayload(QByteArrayLiteral("12345678")).size(), 1);
    // Пустая посылка не даёт ни одного кадра: пустой пакет — это удержание туннеля, и
    // порождать его из пустой записи пользователя было бы подменой смысла.
    EXPECT_TRUE(splitPayload(QByteArray()).isEmpty());
}

TEST(NodeDirectory, ForgetsNodesThatStoppedAnswering)
{
    NodeDirectory directory;
    directory.noteSeen(5, 1000);
    directory.noteSeen(12, 1000);
    directory.noteSeen(3, 4000);

    // Порядок — по возрастанию номера, а не по времени ответа: список читает человек.
    EXPECT_EQ(directory.nodes(4000, 5000), QList<int>({3, 5, 12}));

    // Узел не сообщает об уходе, он просто перестаёт отвечать: 5 и 12 молчат дольше
    // допустимого, 3 ответил недавно.
    EXPECT_EQ(directory.nodes(7000, 5000), QList<int>({3}));

    // Ответивший снова возвращается в список — плата могла перезагружаться.
    directory.noteSeen(5, 7500);
    EXPECT_EQ(directory.nodes(7500, 5000), QList<int>({3, 5}));
}

TEST(NodeDirectory, IgnoresNodesItCouldNotAddress)
{
    NodeDirectory directory;
    directory.noteSeen(0, 100);
    directory.noteSeen(447, 100);
    directory.noteSeen(-1, 100);

    EXPECT_TRUE(directory.nodes(100, 5000).isEmpty());
}
