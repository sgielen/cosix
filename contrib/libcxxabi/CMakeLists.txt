if(BAREMETAL_ENABLED)
	add_library(cxxabi_rtti
		private_typeinfo.cpp private_typeinfo.h
		stdlib_typeinfo.cpp
		stdlib_exception.cpp
	)
	target_compile_options(cxxabi_rtti PRIVATE -Wno-zero-as-null-pointer-constant)
endif()
