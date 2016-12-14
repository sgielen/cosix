#pragma once

#include <oslibc/assert.hpp>
#include <memory/allocation.hpp>
#include <memory/allocator.hpp>
#include <oslibc/utility.hpp>

namespace cloudos {

// TODO: make this entire struct respect atomics before enabling kernel
// preemption or SMP
struct shared_control_block {
	shared_control_block(Blk b) : shared_count(1), weak_count(0), block(b) {}

	void shared_increment() {
		assert(shared_count > 0);
		shared_count++;
	}

	void weak_increment() {
		weak_count++;
	}

	bool shared_decrement() {
		assert(shared_count > 0);
		shared_count--;
		return shared_count == 0;
	}

	void weak_decrement() {
		assert(weak_count > 0);
		weak_count--;
	}

	bool expired() {
		return shared_count == 0;
	}

	bool unreferenced() {
		return shared_count == 0 && weak_count == 0;
	}

	void deallocate() {
		::cloudos::deallocate(block);
	}

	uint64_t use_count() {
		return shared_count;
	}

	uint64_t weak_use_count() {
		return weak_count;
	}

private:
	uint64_t shared_count;
	uint64_t weak_count;
	// allocation whose lifetime is controlled by this control block;
	// the ptr members of the shared ptrs and weak ptrs with this control
	// block most likely point somewhere inside this block
	Blk block;
};

template <typename T>
struct weak_ptr;

template <typename T>
struct shared_ptr;

template <typename T>
void initialize_enable_shared_ptr(shared_ptr<T>&);

template <typename T>
struct shared_ptr {
	shared_ptr() : ptr(nullptr) {}
	shared_ptr(nullptr_t) : shared_ptr() {}

	shared_ptr(shared_ptr &r) : shared_ptr() {
		*this = r;
	}

	shared_ptr(shared_ptr &&o) : shared_ptr() {
		*this = move(o);
	}

	// U must be convertible to T
	template <typename U>
	shared_ptr(shared_ptr<U> &r) : shared_ptr() {
		*this = r;
	}

	template <typename U>
	shared_ptr(shared_ptr<U> &&o) : shared_ptr() {
		*this = move(o);
	}

	template <class... Args>
	shared_ptr(Blk b, Args&&... args) : shared_ptr() {
		assert(b.ptr != nullptr);
		assert(b.size >= sizeof(T));
		new (b.ptr) T(args...);

		control_block = allocate(sizeof(shared_control_block));
		new (control_block.ptr) shared_control_block(b);

		ptr = reinterpret_cast<T*>(b.ptr);
		assert(control()->use_count() == 1);
		assert(control()->weak_use_count() == 0);

		initialize_enable_shared_ptr(*this);
	}

	template <typename U>
	shared_ptr<U> reinterpret_as() {
		shared_ptr<U> res;
		res.control_block = control_block;
		res.ptr = reinterpret_cast<U*>(ptr);
		return res;
	}

	~shared_ptr() {
		reset();
	}

	uint64_t use_count() {
		return control() ? control()->use_count() : 0;
	}

	uint64_t weak_use_count() {
		return control() ? control()->weak_use_count() : 0;
	}

	void operator=(shared_ptr &r) {
		control_block = r.control_block;
		ptr = r.ptr;
		if(control()) {
			control()->shared_increment();
		}
	}

	void operator=(shared_ptr &&o) {
		control_block = o.control_block;
		ptr = o.ptr;
		if(control()) {
			control()->shared_increment();
		}
		o.reset();
	}

	template <typename U>
	void operator=(shared_ptr<U> &r) {
		control_block = r.control_block;
		ptr = r.ptr;
		if(control()) {
			control()->shared_increment();
		}
	}

	template <typename U>
	void operator=(shared_ptr<U> &&o) {
		control_block = o.control_block;
		ptr = o.ptr;
		if(control()) {
			control()->shared_increment();
		}
		o.reset();
	}

	void reset() {
		shared_control_block *c = control();
		if(c) {
			if(c->shared_decrement()) {
				ptr->~T();
				c->deallocate();
			}
			if(c->unreferenced()) {
				deallocate(control_block);
			}
		}
		control_block.ptr = nullptr;
		ptr = nullptr;
	}

	T* get() {
		return ptr;
	}

	const T* get() const {
		return ptr;
	}

	T& operator*() {
		assert(get() != nullptr);
		return *get();
	}

	const T& operator*() const {
		assert(get() != nullptr);
		return *get();
	}

	T* operator->() {
		assert(get() != nullptr);
		return get();
	}

	const T* operator->() const {
		assert(get() != nullptr);
		return get();
	}

	bool is_initialized() const {
		return get() != nullptr;
	}

	explicit operator bool() const {
		return is_initialized();
	}

	bool operator==(shared_ptr const &o) const {
		if(ptr == o.ptr) {
			assert(control_block.ptr == o.control_block.ptr);
		}
		return ptr == o.ptr;
	}

private:
	template <typename U>
	friend struct weak_ptr;
	template <typename U>
	friend struct shared_ptr;

	shared_control_block *control() {
		return reinterpret_cast<shared_control_block*>(control_block.ptr);
	}

	Blk control_block;
	T *ptr;
};

template <typename T>
struct weak_ptr {
	weak_ptr() : ptr(nullptr) {}

	weak_ptr(weak_ptr &r) : weak_ptr() {
		*this = r;
	}

	weak_ptr(weak_ptr &&r) : weak_ptr() {
		*this = move(r);
	}

	template <typename U>
	weak_ptr(shared_ptr<U> &r) : weak_ptr() {
		*this = r;
	}

	~weak_ptr() {
		reset();
	}

	void operator=(weak_ptr &r) {
		control_block = r.control_block;
		ptr = r.ptr;
		if(control()) {
			control()->weak_increment();
		}
	}

	void operator=(weak_ptr &&o) {
		// swap contents
		Blk c = o.control_block;
		T *p = o.ptr;

		o.control_block = control_block;
		o.ptr = ptr;

		control_block = c;
		ptr = p;
	}

	template <typename U>
	void operator=(shared_ptr<U> &r) {
		control_block = r.control_block;
		ptr = r.ptr;
		if(control()) {
			control()->weak_increment();
		}
	}

	void reset() {
		shared_control_block *c = control();
		if(c) {
			c->weak_decrement();
			if(c->unreferenced()) {
				deallocate(control_block);
			}
		}
		control_block.ptr = nullptr;
		ptr = nullptr;
	}

	/* if this weak_ptr is not expired before or during this call, returns
	 * a shared_ptr to the same object; otherwise, returns an empty
	 * shared_ptr.
	 */
	shared_ptr<T> lock() {
		shared_ptr<T> res;
		auto *c = control();
		if(c && !c->expired()) {
			// TODO: use an atomic c->lock()
			c->shared_increment();
			res.control_block = control_block;
			res.ptr = ptr;
		}
		return res;
	}

	uint64_t use_count() {
		return control() ? control()->use_count() : 0;
	}

	uint64_t weak_use_count() {
		return control() ? control()->weak_use_count() : 0;
	}

	bool expired() {
		return use_count() == 0;
	}

private:
	shared_control_block *control() {
		return reinterpret_cast<shared_control_block*>(control_block.ptr);
	}

	Blk control_block;
	T *ptr;
};

template <typename T, class... Args>
shared_ptr<T> make_shared(Args&&... args) {
	return shared_ptr<T>(allocate(sizeof(T)), args...);
}

template <typename T>
struct enable_shared_from_this
{
	shared_ptr<T> shared_from_this() {
		assert(!weak_this.expired());
		auto res = weak_this.lock();
		assert(res);
		return res;
	}

	weak_ptr<T> weak_from_this() {
		assert(!weak_this.expired());
		return weak_this;
	}

private:
	weak_ptr<T> weak_this;

	template <typename U, typename V>
	friend void initialize_enable_shared_ptr(shared_ptr<U> &ptr, enable_shared_from_this<V> *f);
};

template <typename U, typename V>
void initialize_enable_shared_ptr(shared_ptr<U> &ptr, enable_shared_from_this<V> *f) {
	assert(&ptr->weak_this == &f->weak_this);
	f->weak_this = ptr;
}

template <typename U>
void initialize_enable_shared_ptr(shared_ptr<U> const &, ...) {}

template <typename T>
void initialize_enable_shared_ptr(shared_ptr<T> &ptr) {
	assert(ptr.is_initialized());
	initialize_enable_shared_ptr(ptr, ptr.get());
}

}
