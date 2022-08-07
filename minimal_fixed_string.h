#pragma once

#ifndef MINIMAL_FIXED_STRING_H_INCLUDED
#define MINIMAL_FIXED_STRING_H_INCLUDED

#include <cstddef>
#include <array>

template<size_t N>
class minimal_fixed_string_t {
	std::array<char, N> data_m = { '\0' }; // Should always be a C-string -- with '\0'
	size_t size_m = 0;
public:
	static const size_t npos = -1;
	minimal_fixed_string_t() = default;
	minimal_fixed_string_t(const minimal_fixed_string_t&) = default;
	minimal_fixed_string_t(const char* str) {
		push_back(str);
	}
	constexpr size_t size() const { return size_m; }
	constexpr size_t capacity() const { return data_m.size() - 1; }
	constexpr bool is_empty() const { return size_m == 0; }
	constexpr       char& operator[](size_t idx) { return data_m[idx]; }
	constexpr const char& operator[](size_t idx) const { return data_m[idx]; }
	constexpr const char* data() const { return data_m.data(); }
	constexpr       char* data() { return data_m.data(); }
	bool push_back(char c) {
		if (size() == capacity()) return false;
		data_m[size_m++] = c;
		data_m[size_m] = '\0';
		return true;
	}
	bool push_back(const char* str) {
		data_m[size_m] = '\0';
		auto res = !strcpy_s(data() + size(), capacity() - size(), str);
		size_m += strnlen_s(data() + size(), capacity() - size());
		return res;
	}
	template<size_t M>
	bool push_back(const minimal_fixed_string_t<M>& fixed_str) {
		data_m[size_m] = '\0';
		auto res = !strcpy_s(data() + size(), capacity() - size(), fixed_str.data());
		size_m += strnlen_s(data() + size(), capacity() - size());
		return res;
	}
	void clear() {
		size_m = 0;
		data_m[size_m] = '\0';
	}
	void shrink_to(size_t new_size) {
		if (new_size > size_m)
			return;
		size_m = new_size;
		data_m[size_m] = '\0';
	}
	void pop_back() {
		if (size_m  == 0)
			return;
		--size_m;
		data_m[size_m] = '\0';
	}
	size_t find_last(char c) const {
		const char* last = strrchr(data(), c);
		if (last == nullptr)
			return npos;
		else
			return last - data();
	}
}; 

#endif 