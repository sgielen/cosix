#include "reverse_fd.hpp"
#include <fd/pseudo_fd.hpp>

using namespace cloudos;

reversefd_t::reversefd_t(cloudabi_filetype_t type, cloudabi_fdflags_t flags, const char *name)
: unixsock(type, flags, name)
{
}

reversefd_t::~reversefd_t()
{
}

void reversefd_t::subscribe_fd_read_events(shared_ptr<pseudo_fd> fd)
{
	assert(fd->get_reverse_fd().get() == this);
	append(&pseudos, allocate<pseudo_list>(fd));
}

shared_ptr<pseudo_fd> reversefd_t::get_pseudo(reverse_proto::pseudofd_t pseudo_id)
{
	// TODO: roll the below loops into one
	// remove all pseudos that are expired
	remove_all(&pseudos, [](pseudo_list *item) -> bool {
		return !item->data.lock();
	});
	// find this pseudo
	for(auto it = pseudos; it != nullptr; it = it->next) {
		auto pseudo = it->data.lock();
		if(pseudo && pseudo->get_pseudo_id() == pseudo_id) {
			return pseudo;
		}
	}
	return {};
}

void reversefd_t::handle_gratituous_message()
{
	assert(message.gratituous);
	pseudofd_t pseudo_id = message.result;
	shared_ptr<pseudo_fd> pseudo = get_pseudo(pseudo_id);
	if(!pseudo) {
		// pseudo FD is already closed
		return;
	}
	if(message.flags == 1) {
		// pseudo became readable
		pseudo->became_readable();
	}
}

void reversefd_t::have_bytes_received()
{
	// more bytes came in; it could be a gratituous message or a response
	while(bytes_readable() > 0) {
		// if we haven't read a full header yet, try that
		if (bytes_read < sizeof(message)) {
			size_t remaining = sizeof(message) - bytes_read;
			size_t readable = bytes_readable();
			if(remaining > readable) {
				remaining = readable;
			}
			if(remaining == 0) {
				// need additional data to be able to read()
				assert(bytes_readable() == 0);
				return;
			}
			char *msg = reinterpret_cast<char*>(&message);
			bytes_read += read(msg + bytes_read, remaining);
			// read() exactly the amount of bytes_readable() should never lead to an error
			assert(error == 0);
			if(bytes_read < sizeof(message)) {
				// we are still awaiting more data
				assert(bytes_readable() == 0);
				return;
			}
			assert(bytes_read == sizeof(message));
		}

		// we have a full header, do we have a full body?
		if(bytes_read < (sizeof(message) + message.send_length)) {
			if(recv_data.ptr == nullptr) {
				recv_data = allocate(message.send_length);
			}
			assert(recv_data.size == message.send_length);

			size_t remaining = sizeof(message) + message.send_length - bytes_read;
			size_t readable = bytes_readable();
			if(remaining > readable) {
				remaining = readable;
			}
			if(remaining == 0) {
				// need additional data to be able to read()
				assert(bytes_readable() == 0);
				return;
			}
			char *msg = reinterpret_cast<char*>(recv_data.ptr);
			bytes_read += read(msg + (bytes_read - sizeof(message)), remaining);
			// read() exactly the amount of bytes_readable() should never lead to an error
			assert(error == 0);
			if(bytes_read < (sizeof(message) + message.send_length)) {
				// we are still awaiting more data
				assert(bytes_readable() == 0);
				return;
			}
			assert(bytes_read == (sizeof(message) + message.send_length));
		}

		// we have a full message, what kind is it?
		if(message.gratituous) {
			// gratituous messages never have additional data
			assert(recv_data.ptr == nullptr);
			handle_gratituous_message();
			bytes_read = 0;
		} else {
			// it's a response, wake up the thread waiting for a
			// response, it takes ownership of recv_data, set
			// bytes_read to 0 and call have_bytes_received() after
			// this if necessary
			assert(sending_request);
			response_arrived_cv.notify();
			return;
		}
	}
}

Blk reversefd_t::send_request(reverse_request_t *request, const char *buffer, reverse_response_t *response) {
	while(sending_request) {
		// Lock the reverse_fd. Multiple pseudo FD's may have a reference to
		// this reverse_fd, and another one may have an outstanding request
		// already.
		request_done_cv.wait();
	}
	sending_request = true;

	assert(type == CLOUDABI_FILETYPE_SOCKET_STREAM);

	char *msg = reinterpret_cast<char*>(request);
	if(write(msg, sizeof(reverse_request_t)) != sizeof(reverse_request_t) || error != 0) {
		return {};
	}
	if(request->send_length > 0) {
		if(write(buffer, request->send_length) != request->send_length || error != 0) {
			return {};
		}
	}

	// wait for a response
	response_arrived_cv.wait();
	assert(bytes_read == sizeof(message) + message.send_length);

	// take the response into our buffers (the caller takes ownership over
	// the buffer in recv_data)
	memcpy(response, &message, sizeof(message));
	Blk res = recv_data;
	recv_data = {};

	bytes_read = 0;
	sending_request = false;
	request_done_cv.notify();

	// handle any gratituous messages that might have come in after this
	// response but before this function reading it?
	have_bytes_received();

	return res;
}
