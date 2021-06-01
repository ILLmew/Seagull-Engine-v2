#ifndef STRING_VIEW_H
#define STRING_VIEW_H

#ifdef _MSC_VER
#	pragma once
#endif

#include "internal/iterator.h"
#include "algorithm.h"
#include "utility.h"

namespace SG
{

	//! Build on stack string_view, compile-time confirmed
	template<class T>
	class basic_string_view
	{
	public:
		typedef basic_string_view<T>       this_type;
		typedef T					       value_type;
		typedef T*				           pointer;
		typedef const T*                   const_pointer;
		typedef T&					       reference;
		typedef const T&			       const_reference;
		typedef T*                         iterator;
		typedef const T*                   const_iterator;
		typedef SG::reverse_iterator<iterator> reverse_iterator;
		typedef SG::reverse_iterator<const_iterator> const_reverse_iterator;
		typedef Size                 size_type;
		typedef Diff                 difference_type;

		//! npos indicates a non-valid position of string
		static const SG_CONSTEXPR size_type npos = size_type(-1);
	protected:
		const_pointer mBegin = nullptr;
		size_type     mCount = 0;
	public:
		SG_CONSTEXPR basic_string_view() noexcept : mBegin(nullptr), mCount(0) {}
		SG_CONSTEXPR basic_string_view(const basic_string_view& rhs) noexcept = default;
		SG_CONSTEXPR basic_string_view(const_pointer ptr, size_type n) : mBegin(ptr), mCount(n) {}
		SG_CONSTEXPR basic_string_view(const_pointer ptr) : mBegin(ptr), mCount(len_of_char_str(ptr)) {}
		basic_string_view& operator=(const basic_string_view& view) = default;

		SG_CONSTEXPR void swap(basic_string_view& rhs)
		{
			SG::swap(mBegin, rhs.mBegin);
			SG::swap(mCount, rhs.mCount);
		}

		SG_CONSTEXPR static int compare(const T* pBeg1, const T* pEnd1, const T* pBeg2, const T* pEnd2)
		{
			const difference_type n1 = pEnd1 - pBeg1;
			const difference_type n2 = pEnd2 - pBeg2;
			const size_type nMin = (size_type)SG::smin(n1, n2);

			const int res = SG::compare(pBeg1, pBeg2, nMin);

			if (res != 0)
				return res;
			else if (n1 < n2) // if the common part of the string is same, compare the length of the string
				return -1;
			else if (n1 > n2)
				return 1;
			else
				return 0;
		}
		SG_CONSTEXPR int compare(basic_string_view sw) const
		{
			return compare(mBegin, mBegin + mCount, sw.mBegin, sw.mBegin + sw.mCount);
		}

		SG_CONSTEXPR const_iterator begin()  const noexcept { return mBegin; }
		SG_CONSTEXPR const_iterator cbegin() const noexcept { return mBegin; }

		SG_CONSTEXPR const_iterator end()  const noexcept { return mBegin + mCount; }
		SG_CONSTEXPR const_iterator cend() const noexcept { return mBegin + mCount; }

		SG_CONSTEXPR const_reverse_iterator rbegin()  const noexcept { return const_reverse_iterator(mBegin + mCount); }
		SG_CONSTEXPR const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(mBegin + mCount); }

		SG_CONSTEXPR const_reverse_iterator rend()  const noexcept { return const_reverse_iterator(mBegin); }
		SG_CONSTEXPR const_reverse_iterator crend() const noexcept { return const_reverse_iterator(mBegin); }

		SG_CONSTEXPR const_pointer data()   const { return mBegin; }
		SG_CONSTEXPR size_type     length() const { return mCount; }
		SG_CONSTEXPR bool          empty()  const { return mCount == 0; }

		SG_CONSTEXPR const_reference operator[](size_type pos) const { return mBegin[pos]; }

		SG_CONSTEXPR basic_string_view substr(size_type pos, size_type count = basic_string_view::npos)
		{
			SG_COMPILE_ASSERT(pos < mCount && pos >= 0);
			// if the required str is not long enough, just return the rest of them
			count = min(count, mCount - pos);
			return this_type(mBegin + pos, count);
		}

		// TODO: implemented find_first_of() find_first_not_of() find_last_of() find_last_not_of() find()
	};

	template<class T>
	SG_INLINE SG_CONSTEXPR bool operator==(basic_string_view<T> lhs, basic_string_view<T> rhs)
	{
		return (lhs.size() == rhs.size()) && (lhs.compare(rhs) == 0);
	}

	template<class T>
	SG_INLINE SG_CONSTEXPR bool operator!=(basic_string_view<T> lhs, basic_string_view<T> rhs)
	{
		return !(lhs == rhs);
	}

	template<class T>
	SG_INLINE SG_CONSTEXPR bool operator<(basic_string_view<T> lhs, basic_string_view<T> rhs)
	{
		return lhs.compare(rhs) < 0;
	}

	template<class T>
	SG_INLINE SG_CONSTEXPR bool operator<=(basic_string_view<T> lhs, basic_string_view<T> rhs)
	{
		return !(rhs < lhs);
	}

	template<class T>
	SG_INLINE SG_CONSTEXPR bool operator>(basic_string_view<T> lhs, basic_string_view<T> rhs)
	{
		return rhs < lhs;
	}

	template<class T>
	SG_INLINE SG_CONSTEXPR bool operator>=(basic_string_view<T> lhs, basic_string_view<T> rhs)
	{
		return !(lhs < rhs);
	}

	// user side string_view
	typedef basic_string_view<Char> string_view;
	typedef basic_string_view<WChar> wstring_view;

	typedef basic_string_view<Char8>  u8string_view;
	typedef basic_string_view<Char16> u16string_view;
	typedef basic_string_view<Char32> u32string_view;

	SG_DISABLE_MSVC_WARNING(4455) // disable warning: 'operator ""sv': literal suffix identifiers that do not start with an underscore are reserved.
	// inline namespace literals
	inline namespace literals
	{
		inline namespace string_view_literals
		{
			SG_CONSTEXPR SG_INLINE string_view    operator ""sv(const Char*  str,  Size len) noexcept { return {str, len}; }
			SG_CONSTEXPR SG_INLINE wstring_view   operator ""sv(const WChar* str,  Size len) noexcept { return {str, len}; }
			SG_CONSTEXPR SG_INLINE u16string_view operator ""sv(const Char16* str, Size len) noexcept { return {str, len}; }
			SG_CONSTEXPR SG_INLINE u32string_view operator ""sv(const Char32* str, Size len) noexcept { return {str, len}; }
		}
	}
	SG_RESTORE_MSVC_WARNING()

}

#endif // STRING_VIEW_H