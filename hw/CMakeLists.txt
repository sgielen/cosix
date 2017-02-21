if(BAREMETAL_ENABLED)
	set(EXTRA_HW_SOURCES segments.s interrupt.S)
	set_source_files_properties(segments.s interrupt.S PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
else()
	set(EXTRA_HW_SOURCES test/segments_stub.c test/interrupt_stub.c)
endif()

add_library(hw
	vga.cpp vga.hpp
	vga_stream.hpp vga_stream.cpp
	multiboot.hpp multiboot.cpp
	segments.hpp segments.cpp
	interrupt_table.hpp interrupt_table.cpp
	interrupt.hpp interrupt.cpp
	sse.hpp sse.cpp
	pci_bus.hpp pci_bus.cpp
	driver.hpp
	driver_store.hpp driver_store.cpp
	device.hpp device.cpp
	root_device.hpp root_device.cpp
	net/virtio.hpp net/virtio.cpp
	net/ethernet_device.hpp net/ethernet_device.cpp
	arch/x86/x86.hpp arch/x86/x86.cpp
	arch/x86/x86_pit.hpp arch/x86/x86_pit.cpp
	arch/x86/x86_kbd.hpp arch/x86/x86_kbd.cpp
	arch/x86/x86_serial.hpp arch/x86/x86_serial.cpp
	${EXTRA_HW_SOURCES})
list(APPEND hw_tests
	test/test_vga.cpp
	test/test_vga_stream.cpp
	test/test_multiboot.cpp
	test/test_segments.cpp)
target_link_libraries(hw oslibc)

if(TESTING_ENABLED)
	add_executable(hw_test ${hw_tests} test/test_main.cpp)
	target_include_directories(hw_test PRIVATE ${TESTING_CATCH_INCLUDE})
	target_link_libraries(hw_test hw)
	add_test(NAME hw_test WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND hw_test)
endif()
