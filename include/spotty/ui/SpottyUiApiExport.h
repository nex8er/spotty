/**
 * \file SpottyUiApiExport.h
 * \brief Макрос видимости символов панельного SDK.
 */
#pragma once

#include <QtGlobal>

/**
 * \def SPOTTY_UI_API_EXPORT
 * \brief Помечает то, что видно за границей библиотеки `spotty-ui-api`.
 *
 * \par Почему библиотека обязана быть разделяемой
 *
 * Ровно по той же причине, что и `spotty-api`, и с теми же последствиями: в панельном SDK
 * есть классы с Q_OBJECT (spotty::IPanelHost, spotty::PanelWidget), а Qt сопоставляет
 * сигналы и приводит типы, сравнивая **указатели** на QMetaObject. Отдельная копия
 * `staticMetaObject` в приложении и в плагине приводит к тому, что connect() и
 * qobject_cast() молча перестают работать: ни ошибки сборки, ни предупреждения.
 *
 * \warning Обе библиотеки SDK обязаны быть **одного вида**. Разделяемая `spotty-api` со
 *          статической `spotty-ui-api` — это как раз тот случай, когда метаобъекты
 *          размножаются, и симптом («панель не получает данные») не указывает на причину.
 *          Согласованность обеспечивает единственная опция SPOTTY_STATIC_PLUGINS, которой
 *          подчиняются оба каталога; проверка стоит в src/uiapi/CMakeLists.txt.
 */
#if defined(SPOTTY_UI_API_STATIC)
#  define SPOTTY_UI_API_EXPORT
#elif defined(SPOTTY_UI_API_BUILD)
#  define SPOTTY_UI_API_EXPORT Q_DECL_EXPORT
#else
#  define SPOTTY_UI_API_EXPORT Q_DECL_IMPORT
#endif
