/**
 * \file SpottyApiExport.h
 * \brief Макрос экспорта символов SDK.
 */
#pragma once

#include <QtCore/qglobal.h>

/**
 * \def SPOTTY_API_EXPORT
 * \brief Управление видимостью символов библиотеки SDK.
 *
 * \par Почему SDK — разделяемая библиотека
 *
 * Это не вопрос упаковки, а требование корректности. spotty::IInterfaceChannel — класс
 * с Q_OBJECT, а Qt сопоставляет сигналы и слоты, сравнивая **указатели** на QMetaObject:
 *
 * \code
 * bool QMetaObject::inherits(const QMetaObject *metaObject) const noexcept
 * {
 *     const QMetaObject *m = this;
 *     do {
 *         if (metaObject == m)   // сравнение указателей, не имён классов
 *             return true;
 *     } while ((m = m->d.superdata));
 *     return false;
 * }
 * \endcode
 *
 * Если бы исполняемый файл и каждый плагин несли собственную копию
 * `IInterfaceChannel::staticMetaObject`, то `connect()` из приложения к сигналу канала,
 * созданного плагином, **молча не сработал бы**: проверка типа не прошла бы, объект
 * подключён не был бы, данные бы не приходили. Ошибки сборки при этом нет, предупреждения
 * тоже — только неработающая программа.
 *
 * Отсюда единственная разделяемая копия: libspotty-api содержит и метаобъект, и таблицу
 * виртуальных функций, и typeinfo класса ровно в одном экземпляре.
 *
 * \par Статическая сборка
 *
 * При CMake-опции `SPOTTY_STATIC_PLUGINS=ON` всё собирается в один двоичный файл, делить
 * между модулями нечего, SDK становится статической библиотекой, а макрос раскрывается в
 * пустоту.
 *
 * \see cmake/SpottyPlugin.cmake
 * \see docs/PLUGIN_API.md
 */
#if defined(SPOTTY_API_STATIC)
#  define SPOTTY_API_EXPORT
#elif defined(SPOTTY_API_BUILD)
#  define SPOTTY_API_EXPORT Q_DECL_EXPORT
#else
#  define SPOTTY_API_EXPORT Q_DECL_IMPORT
#endif
