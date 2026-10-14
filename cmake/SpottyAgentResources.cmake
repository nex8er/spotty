# spotty_embed_agent_resources(<цель>)
#
# Кладёт в ресурсы цели всё, что Spotty заливает на удалённую машину: бинарники агента под
# каждую архитектуру, версию, скрипты установки, шаблоны конфига и сервиса. Пути в ресурсах
# читает AgentPackage (`:/agent/bin/<арх>/spotty-uartd`, `:/agent/pkg/<файл>`).
#
# Список архитектур берётся из каталога resources/agent, а не зашит: добавить цель в
# agent/build-agents.sh достаточно, чтобы она попала в комплект.
function(spotty_embed_agent_resources target)
    set(bin_root "${CMAKE_SOURCE_DIR}/resources/agent")
    set(pkg_root "${CMAKE_SOURCE_DIR}/agent")

    file(GLOB arch_dirs LIST_DIRECTORIES true RELATIVE "${bin_root}" "${bin_root}/*")
    set(bin_files "")
    foreach(entry IN LISTS arch_dirs)
        if(IS_DIRECTORY "${bin_root}/${entry}" AND EXISTS "${bin_root}/${entry}/spotty-uartd")
            list(APPEND bin_files "${bin_root}/${entry}/spotty-uartd")
        endif()
    endforeach()
    if(NOT bin_files)
        message(WARNING "No agent binaries in ${bin_root}: run agent/build-agents.sh. "
                        "The ssh installer will have nothing to upload.")
    endif()
    list(APPEND bin_files "${bin_root}/VERSION")

    set(pkg_files "")
    foreach(name detect.sh install.sh uninstall.sh spotty-uartd@.service uartd.conf.template)
        list(APPEND pkg_files "${pkg_root}/${name}")
    endforeach()

    # Пересборка при смене файлов: CMake не следит за ресурсами, перечисленными по списку.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${bin_root}/VERSION")

    qt_add_resources(${target} tcptunnel_agent_bin
        PREFIX "/agent/bin"
        BASE "${bin_root}"
        FILES ${bin_files})
    qt_add_resources(${target} tcptunnel_agent_pkg
        PREFIX "/agent/pkg"
        BASE "${pkg_root}"
        FILES ${pkg_files})
endfunction()
