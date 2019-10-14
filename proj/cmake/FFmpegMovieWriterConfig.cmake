if( NOT TARGET FFmpegMovieWriter )
	get_filename_component( FFMPEGMOVIEWRITER_ROOT_PATH
		"${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE )

	list( APPEND FFMPEGMOVIEWRITER_SOURCES
		${FFMPEGMOVIEWRITER_ROOT_PATH}/src/FFmpegMovieWriter.cpp
	)

	add_library( FFmpegMovieWriter ${FFMPEGMOVIEWRITER_SOURCES} )

	list( APPEND FFMPEGMOVIEWRITER_INCLUDE_DIRS
		${FFMPEGMOVIEWRITER_ROOT_PATH}/src
	)

	target_include_directories( FFmpegMovieWriter PUBLIC
		"${FFMPEGMOVIEWRITER_INCLUDE_DIRS}" )

	if( NOT TARGET cinder )
		include( "${CINDER_PATH}/proj/cmake/configure.cmake" )
		find_package( cinder REQUIRED PATHS
			"${CINDER_PATH}/${CINDER_LIB_DIRECTORY}"
			"$ENV{CINDER_PATH}/${CINDER_LIB_DIRECTORY}" )
	endif()

	target_link_libraries( FFmpegMovieWriter PRIVATE cinder )
endif()
