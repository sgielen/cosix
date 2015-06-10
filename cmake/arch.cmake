function(get_gcc_raw_triplet TRIPLET_VAR)
	# TODO: this only works on gcc & clang
	execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine
		OUTPUT_VARIABLE TRIPLET_OUT
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET)
	execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
		OUTPUT_VARIABLE TRIPLET_CXX_OUT
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET)

	if(NOT ${TRIPLET_OUT} MATCHES ${TRIPLET_CXX_OUT})
		message("Warning: triplets are different for your C and C++ compiler. You'll most likely run into problems.")
	endif()

	set(${TRIPLET_VAR} ${TRIPLET_OUT} PARENT_SCOPE)
endfunction()

function(is_triplet_system SYSTEM OUTPUT)
	set(${OUTPUT} FALSE PARENT_SCOPE)
	if(${SYSTEM} MATCHES "^linux$" OR ${SYSTEM} MATCHES "^darwin" OR ${SYSTEM} MATCHES "win32")
		set(${OUTPUT} TRUE PARENT_SCOPE)
	endif()
endfunction()

function(get_compiler_triplet ARCHVAR VENDORVAR SYSTEMVAR ABIVAR)
	set(${ARCHVAR} "" PARENT_SCOPE)
	set(${VENDORVAR} "" PARENT_SCOPE)
	set(${SYSTEMVAR} "" PARENT_SCOPE)
	set(${ABIVAR} "" PARENT_SCOPE)

	# The base form of a triplet is <arch>-<vendor>-<system>.  However,
	# vendor is optional, so <arch>-<system> is also fine. But, to make
	# things complicated, the format <arch>-<vendor>-<system>-<abi> is also
	# valid, and since vendor is optional, so is <arch>-<system>-<abi>.
	# Ergo, a three-part triplet is ambiguous, and to correctly parse it,
	# we need to be able to recognise valid system entries.
	get_gcc_raw_triplet(TRIPLET)

	# since cmakes's REGEX MATCHALL does not work with empty substrings,
	# clang's i686---elf will be parsed as "i686;elf" instead of "i686;;;elf"
	# so make sure there's always something in between dashes
	string(REGEX REPLACE "--" "-__empty__-" TRIPLET "${TRIPLET}")
	string(REGEX REPLACE "--" "-__empty__-" TRIPLET "${TRIPLET}")
	string(REGEX MATCHALL "[^-]+" TRIPLET_LIST "${TRIPLET}")
	list(LENGTH TRIPLET_LIST TRIPLET_LENGTH)
	if(${TRIPLET_LENGTH} LESS 2)
		message("Warning: compiler-reported triplet '${TRIPLET}' is invalid")
		set(${ARCHVAR} ${TRIPLET} PARENT_SCOPE)
		return()
	endif()

	list(GET TRIPLET_LIST 0 TRIPLET_ARCH)
	list(REMOVE_AT TRIPLET_LIST 0)

	if(${TRIPLET_LENGTH} EQUAL 2)
		list(GET TRIPLET_LIST 0 TRIPLET_SYSTEM)
	elseif(${TRIPLET_LENGTH} GREATER 3)
		list(GET TRIPLET_LIST 0 TRIPLET_VENDOR)
		list(GET TRIPLET_LIST 1 TRIPLET_SYSTEM)
		list(GET TRIPLET_LIST 2 TRIPLET_ABI)
	else()
		# triplet-length is 3, so it could be <arch>-<vendor>-<system> or
		# <arch>-<system>-<abi>; to figure it out, we will try recognising if the
		# second part is a known system
		list(GET TRIPLET_LIST 0 TRIPLET_SECOND)
		list(GET TRIPLET_LIST 1 TRIPLET_THIRD)
		is_triplet_system(${TRIPLET_SECOND} IS_SYSTEM)
		if(${IS_SYSTEM})
			set(TRIPLET_SYSTEM ${TRIPLET_SECOND})
			set(TRIPLET_ABI ${TRIPLET_THIRD})
		else()
			set(TRIPLET_VENDOR ${TRIPLET_SECOND})
			set(TRIPLET_SYSTEM ${TRIPLET_THIRD})
		endif()
	endif()

	if(${TRIPLET_VENDOR} MATCHES "^__empty__$")
		set(TRIPLET_VENDOR "")
	endif()
	if(${TRIPLET_SYSTEM} MATCHES "^__empty__$")
		set(TRIPLET_SYSTEM "")
	endif()

	set(${ARCHVAR} ${TRIPLET_ARCH} PARENT_SCOPE)
	set(${VENDORVAR} ${TRIPLET_VENDOR} PARENT_SCOPE)
	set(${SYSTEMVAR} ${TRIPLET_SYSTEM} PARENT_SCOPE)
	set(${ABIVAR} ${TRIPLET_ABI} PARENT_SCOPE)
endfunction()

function(get_testing_baremetal_enabled TESTING_ENABLED_VAR BAREMETAL_ENABLED_VAR)
	get_compiler_triplet(ARCH VENDOR SYS ABI)
	if("${ABI}" MATCHES "^$")
		set(PRINT_ABI "")
	else()
		set(PRINT_ABI "-${ABI}")
	endif()

	if(${CMAKE_CROSSCOMPILING})
		message("** Cross compiling for ${ARCH}-${VENDOR}-${SYS}${PRINT_ABI} -- tests disabled, bare metal build enabled")
		set(${TESTING_ENABLED_VAR} FALSE PARENT_SCOPE)
		set(${BAREMETAL_ENABLED_VAR} TRUE PARENT_SCOPE)
	else()
		message("** Native compilation for ${ARCH}-${VENDOR}-${SYS}${PRINT_ABI} -- tests enabled, bare metal build disabled")
		set(${TESTING_ENABLED_VAR} TRUE PARENT_SCOPE)
		set(${BAREMETAL_ENABLED_VAR} FALSE PARENT_SCOPE)
	endif()
endfunction()
