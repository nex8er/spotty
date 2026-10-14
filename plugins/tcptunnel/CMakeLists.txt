# Проброс UART удалённой машины по raw TCP.
#
# Клиентская часть (канал, сканер сети) зависит только от Qt6::Network. Установка агента по ssh —
# от libssh2: без неё плагин собирается и остаётся рабочим клиентом уже установленного агента, а
# кнопки установки отвечают, что сборка без ssh. Поэтому libssh2 здесь не обязательна.
#
# Бинарники агента и скрипты установки (resources/agent, agent/) уходят в ресурсы плагина: они и
# есть «агент в составе Spotty».
option(SPOTTY_WITH_SSH "Build the ssh installer of the tcptunnel plugin (needs libssh2)" ON)

# Отсутствие libssh2 по умолчанию — предупреждение, а не ошибка: у разработчика без неё всё должно
# собираться. Но сборка, которая идёт в релиз, не вправе молча выйти без установщика: CI и упаковка
# включают этот параметр, и пропажа библиотеки (сменилось имя пакета, не встал vcpkg) падает сразу и
# с понятной причиной, а не обнаруживается пользователем по исчезнувшим кнопкам.
option(SPOTTY_REQUIRE_SSH "Fail the configuration if libssh2 is not found" OFF)

set(SPOTTY_TCPTUNNEL_SOURCES
    TcpTunnelPlugin.cpp
    TcpTunnelPlugin.h
    TcpTunnelChannel.cpp
    TcpTunnelChannel.h
    TcpTunnelEndpoint.h
    TcpTunnelSettings.h
    NetworkScanner.cpp
    NetworkScanner.h
    RemoteHost.cpp
    RemoteHost.h
    KnownHosts.cpp
    KnownHosts.h
    TabFileStore.h
    HostNames.cpp
    HostNames.h
    AgentPackage.cpp
    AgentPackage.h
    ResourceAgentPackage.cpp
    SshSession.h
    RemoteInstaller.cpp
    RemoteInstaller.h
    RemoteTasks.cpp
    RemoteTasks.h
)

set(SPOTTY_TCPTUNNEL_LINK Qt6::Network)

if(SPOTTY_WITH_SSH)
    find_package(Libssh2 QUIET)
    if(TARGET Libssh2::libssh2)
        list(APPEND SPOTTY_TCPTUNNEL_SOURCES Libssh2Session.cpp Libssh2Session.h)
        list(APPEND SPOTTY_TCPTUNNEL_LINK Libssh2::libssh2)
        message(STATUS "tcptunnel: ssh installer enabled (libssh2 found)")
    elseif(SPOTTY_REQUIRE_SSH)
        message(FATAL_ERROR "libssh2 not found, but SPOTTY_REQUIRE_SSH is ON. Install it (apt: "
                            "libssh2-1-dev, brew: libssh2, vcpkg: libssh2) and point CMAKE_PREFIX_PATH "
                            "at it, or configure with -DSPOTTY_WITH_SSH=OFF.")
    else()
        message(WARNING "libssh2 not found - the tcptunnel plugin is built without the ssh installer "
                        "(-DSPOTTY_WITH_SSH=OFF silences this)")
    endif()
elseif(SPOTTY_REQUIRE_SSH)
    message(FATAL_ERROR "SPOTTY_REQUIRE_SSH is ON while SPOTTY_WITH_SSH is OFF: the options contradict each other.")
endif()

spotty_add_plugin(spotty-plugin-tcptunnel
    CLASS_NAME TcpTunnelPlugin
    SOURCES ${SPOTTY_TCPTUNNEL_SOURCES}
    LINK ${SPOTTY_TCPTUNNEL_LINK}
)

if(TARGET Libssh2::libssh2)
    target_compile_definitions(spotty-plugin-tcptunnel PRIVATE SPOTTY_HAVE_LIBSSH2=1)
    if(WIN32)
        # Заголовок libssh2 помечает функции dllimport, если собирается DLL (_WINDLL), а плагин в
        # обычной сборке — именно DLL. Библиотека же из vcpkg статическая (x64-windows-static-md), и
        # dllimport потребовал бы символов __imp_libssh2_*, которых у неё нет. Пустое LIBSSH2_API
        # годится и для статической, и для разделяемой: вызов пойдёт через заглушку импорта.
        target_compile_definitions(spotty-plugin-tcptunnel PRIVATE LIBSSH2_API=)
    endif()
endif()

# Ресурсы двумя наборами: бинарники и версия (resources/agent), скрипты и шаблоны (agent/). Имена
# наборов совпадают с Q_INIT_RESOURCE в ResourceAgentPackage.cpp.
include("${CMAKE_SOURCE_DIR}/cmake/SpottyAgentResources.cmake")
spotty_embed_agent_resources(spotty-plugin-tcptunnel)
