SET(ShortcutStyles_Files
        "SolidWorks.txt"
)

ADD_CUSTOM_TARGET(ShortcutStyles_data ALL
        SOURCES ${ShortcutStyles_Files}
)

fc_copy_sources(ShortcutStyles_data "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Gui/ShortcutStyles"
        ${ShortcutStyles_Files})

INSTALL(
        FILES
        ${ShortcutStyles_Files}
        DESTINATION
        ${CMAKE_INSTALL_DATADIR}/Gui/ShortcutStyles
)
