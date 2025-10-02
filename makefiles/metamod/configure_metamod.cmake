if (WIN32)
	set(ACCELERATORCSS_VDF_PLATFORM "win64")
else()
	set(ACCELERATORCSS_VDF_PLATFORM "linuxsteamrt64")
endif()

configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/AcceleratorCSS.vdf.in
	${PROJECT_SOURCE_DIR}/configs/addons/metamod/AcceleratorCSS.vdf
)