#pragma once

template <typename T>
T ntoh(T n) {
	// TODO: return n if network byte order == host byte order
	T r;
	uint8_t *net = reinterpret_cast<uint8_t*>(&n);
	uint8_t *res = reinterpret_cast<uint8_t*>(&r);
	for(size_t i = 0; i < sizeof(T); ++i) {
		res[i] = net[sizeof(T)-i-1];
	}
	return r;
}

template <typename T>
T hton(T n) {
	return ntoh(n);
}
