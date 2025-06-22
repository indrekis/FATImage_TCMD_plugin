/*
* Floppy disk images unpack plugin for the Total Commander.
* Copyright (c) 2022-2025, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
*/

#pragma once

#ifndef MINIMAL_FIXED_STRING_H_INCLUDED
#define MINIMAL_FIXED_STRING_H_INCLUDED

#include <cstddef>
#include <array>
#include <cstring>

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
	constexpr const char& back() const { return data_m[data_m.size()-1]; }
	constexpr       char& back() { return data_m[data_m.size() - 1]; }
	constexpr const char& front() const { return data_m[0]; }
	constexpr       char& front() { return data_m[0]; }
	bool push_back(char c) {
		if (size() == capacity()) return false;
		data_m[size_m++] = c;
		data_m[size_m] = '\0';
		return true;
	}
	bool push_back(const char* str) {
		data_m[size_m] = '\0';
		// +1 -- for strcpy_s, note it we have place for 0 anyway
		auto res = !strcpy_s(data() + size(), capacity() - size() + 1, str); 
		size_m += strnlen_s(data() + size(), capacity() - size());
		return res;
	}
	template<size_t M>
	bool push_back(const minimal_fixed_string_t<M>& fixed_str) {
		data_m[size_m] = '\0';
		auto res = !strcpy_s(data() + size(), capacity() - size() + 1, fixed_str.data());
		size_m += strnlen_s(data() + size(), capacity() - size());
		return res;
	}
    void erase(size_t beg, size_t number) {
		if (beg >= size_m || number == 0) return;
		if (beg + number >= size_m) {
			size_m = beg;
			data_m[size_m] = '\0';
			return;
		}
		std::memmove(data_m.data() + beg, data_m.data() + beg + number, size_m - beg - number + 1);
		size_m -= number;
		data_m[size_m] = '\0';
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

	minimal_fixed_string_t& operator+=(const minimal_fixed_string_t& rhs) {
		push_back(rhs);
		return *this;
	}

	minimal_fixed_string_t& operator+=(const char* rhs) {
		push_back(rhs);
		return *this;
	}
}; 

template<size_t N>
minimal_fixed_string_t<N> operator+(minimal_fixed_string_t<N> lhs, const char* rhs) {
    lhs.push_back(rhs);
    return lhs;
}

template<size_t N>
minimal_fixed_string_t<N> operator+(minimal_fixed_string_t<N> lhs, const minimal_fixed_string_t<N>& rhs) {
	lhs.push_back(rhs);
	return lhs;
}

#endif 