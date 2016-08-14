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
	list_too_long = 7,      //! Argument list too long
	exec_format = 8,        //! Exec format error
	bad_fd = 9,             //! Bad file descriptor
	// ECHILD won't be used
	deadlock_avoided = 11,  //! Resource deadlock avoided
	no_memory = 12,         //! Cannot allocate memory
	permission_denied = 13, //! Permission denied
	bad_address = 14,       //! Bad address
	// ENOTBLK won't be used
	busy = 16,              //! Device or resource busy
	file_exists = 17,       //! File exists
	// EXDEV won't be used
	dev_not_supported = 19, //! Operation not supported by device
	not_a_directory = 20,   //! Not a directory
	is_a_directory = 21,    //! Is a directory
	invalid_argument = 22,  //! Invalid argument
	// TODO: errors 23 to 44
	not_supported = 45,     //! Operation not supported
	not_capable = 46,       //! Capabilities insufficient
	resource_exhausted = 47,//! Resource exhausted
};

/**
 * This method returns a pointer to static memory containing a human-readable
 * string for the given error_t.
 */
inline const char *to_string(error_t e) {
	switch(e) {
	case error_t::no_error: return "No error";
	case error_t::not_permitted: return "Operation not permitted";
	case error_t::no_entity: return "No such file or directory";
	case error_t::no_process: return "No such process";
	case error_t::interrupted: return "Interrupted system call";
	case error_t::input_output: return "Input/output error";
	case error_t::not_configured: return "Device not configured";
	case error_t::list_too_long: return "Argument list too long";
	case error_t::exec_format: return "Exec format error";
	case error_t::bad_fd: return "Bad file descriptor";
	case error_t::deadlock_avoided: return "Resource deadlock avoided";
	case error_t::no_memory: return "Cannot allocate memory";
	case error_t::permission_denied: return "Permission denied";
	case error_t::bad_address: return "Bad address";
	case error_t::busy: return "Device or resource busy";
	case error_t::file_exists: return "File exists";
	case error_t::dev_not_supported: return "Operation not supported by device";
	case error_t::not_a_directory: return "Not a directory";
	case error_t::is_a_directory: return "Is a directory";
	case error_t::invalid_argument: return "Invalid argument";
	case error_t::not_supported: return "Operation not supported";
	case error_t::not_capable: return "Capabilities insufficient";
	case error_t::resource_exhausted: return "Resource exhausted";
	}

	return "(Invalid error_t)";
}

}
