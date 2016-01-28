#pragma once

namespace cloudos {

enum class error_t {
	no_error = 0,           //! No error
	not_permitted = 1,      //! Operation not permitted
	no_entity = 2,          //! No such file or directory
	no_process = 3,         //! No such process
	interrupted = 4,        //! Interrupted system call
	input_output = 5,       //! Input/output error
	not_configured = 6,     //! Device not configured

	no_memory = 12,         //! Cannot allocate memory

	dev_not_supported = 19, //! Operation not supported by device
};

}
