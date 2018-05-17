#pragma once

namespace cloudos {

#define NUM_ELEMENTS(x) (sizeof(x) / sizeof(x[0]))

template <class T> struct remove_reference      { typedef T type; };
template <class T> struct remove_reference<T&>  { typedef T type; };
template <class T> struct remove_reference<T&&> { typedef T type; };

template <class T>
typename remove_reference<T>::type&& move(T&& t) {
	return static_cast<typename remove_reference<T>::type&&>(t);
}

template <class T, T v>
struct integral_constant
{
	static constexpr T value = v;
	typedef T value_type;
	typedef integral_constant type;
	constexpr operator value_type() const noexcept { return value; }
	constexpr value_type operator()() const noexcept { return value; }
};

template <bool B>
using bool_constant = integral_constant<bool, B>;

typedef bool_constant<true> true_type;
typedef bool_constant<false> false_type;

template<class T, class U>
struct is_same : false_type {};

template <class T>
struct is_same<T, T> : true_type {};

template <bool B, class T = void>
struct enable_if {};

template<class T>
struct enable_if<true, T> { typedef T type; };

template<typename T>
struct is_unsigned : integral_constant<bool, T(0) < T(-1)> {};

template <typename T1, typename T2>
struct pair {
	typedef T1 first_type;
	typedef T2 second_type;
	first_type first;
	second_type second;

	explicit constexpr pair() = default;
	explicit constexpr pair(const T1 &x, const T2 &y) : first(x), second(y) {}

	template <typename U1, typename U2>
	explicit constexpr pair(const pair<U1, U2> &p) : first(p.first), second(p.second) {}

	pair(const pair&) = default;
};

template <typename T1, typename T2>
pair<T1, T2> make_pair(T1 t1, T2 t2) {
	return pair<T1, T2>(t1, t2);
}

template <typename T>
constexpr const T& min(const T& a, const T& b)
{
	return a < b ? a : b;
}

template <typename T>
constexpr const T& max(const T& a, const T& b)
{
	return a > b ? a : b;
}

}
