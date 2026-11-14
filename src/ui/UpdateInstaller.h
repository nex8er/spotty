/**
 * \file UpdateInstaller.h
 * \brief Загрузка и установка опубликованного обновления.
 */
#pragma once

#include <QCoreApplication>

class QWidget;

namespace spotty {
struct ReleaseInfo;

class UpdateInstaller
{
    Q_DECLARE_TR_FUNCTIONS(spotty::UpdateInstaller)
public:
    /// \brief Скачать и подготовить обновление; при успехе закрыть Spotty для замены.
    static void install(QWidget *parent, const ReleaseInfo &release);
};
} // namespace spotty
