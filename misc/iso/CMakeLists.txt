if(BAREMETAL_ENABLED)
	add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/cosix.iso"
		COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/produce_iso.sh"
			"${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "cosix.iso"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
		DEPENDS cloudkernel
	)
	add_custom_target(iso
		DEPENDS "${CMAKE_BINARY_DIR}/cosix.iso"
	)
	add_custom_target(isoboot
		COMMAND qemu-system-i386 -boot d -cdrom "${CMAKE_BINARY_DIR}/cosix.iso" ${QEMU_ARGS}
		DEPENDS "${CMAKE_BINARY_DIR}/cosix.iso"
	)
endif()
