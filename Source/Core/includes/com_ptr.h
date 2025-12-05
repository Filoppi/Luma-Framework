#pragma once

#include <cstddef>

template<typename T>
class ComPtr
{
public:

	ComPtr() noexcept
		: ptr(nullptr)
	{}

	ComPtr(T* const other) noexcept
		: ptr(other)
	{
		if (ptr)
		{
			ptr->AddRef();
		}
	}

	ComPtr(const ComPtr<T>& other) noexcept
		: ptr(other.ptr)
	{
		if (ptr)
		{
			ptr->AddRef();
		}
	}

	ComPtr(ComPtr<T>&& other) noexcept
		: ptr(other.ptr)
	{
		other.ptr = nullptr;
	}

	~ComPtr() noexcept
	{
		if (ptr)
		{
			ptr->Release();
		}
	}

public:

	ComPtr<T>& operator=(T* const other) noexcept
	{
		if (ptr != other)
		{
			if (other)
			{
				other->AddRef();
			}
			
			if (ptr)
			{
				ptr->Release();
			}
			
			ptr = other;
		}

		return *this;
	}

	ComPtr<T>& operator=(const ComPtr<T>& other) noexcept
	{
		if (ptr != other.ptr)
		{
			if (other.ptr)
			{
				other.ptr->AddRef();
			}
			
			if (ptr)
			{
				ptr->Release();
			}
			
			ptr = other.ptr;
		}

		return *this;
	}

	ComPtr<T>& operator=(ComPtr<T>&& other) noexcept
	{
		if (ptr != other.ptr)
		{
			if (ptr)
			{
				ptr->Release();
			}
			
			ptr = other.ptr;
			other.ptr = nullptr;
		}

		return *this;
	}

public:

	T* operator->() const noexcept
	{
		return ptr;
	}

	T& operator*() const noexcept
	{
		return *ptr;
	}

	T*const* operator&() const noexcept
	{
		return &ptr;
	}

	explicit operator bool() const noexcept
	{
		return ptr != nullptr;
	}

public:

	T* get() const noexcept
	{
		return ptr;
	}

	T*const* get_address() const noexcept
	{
		return &ptr;
	}

	T** put() noexcept
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}

		return &ptr;
	}

	void attach(T* const other) noexcept
	{
		if (ptr)
		{
			ptr->Release();
		}

		ptr = other;
	}

	T* detach() noexcept
	{
		T* tmp = ptr;
		ptr = nullptr;
		return tmp;
	}

	void reset() noexcept
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}

private:

	T* ptr;
};

template<typename T, typename U>
bool operator==(const ComPtr<T>& left, const ComPtr<U>& right) noexcept
{
	return left.get() == right.get();
}

template<typename T, typename U>
bool operator==(const ComPtr<T>& left, const U* const right) noexcept
{
	return left.get() == right;
}

template<typename T, typename U>
bool operator==(const T* const left, const ComPtr<U>& right) noexcept
{
	return left == right.get();
}

template<typename T>
bool operator==(const ComPtr<T>& left, std::nullptr_t) noexcept
{
	return left.get() == nullptr;
}

template<typename T>
bool operator==(std::nullptr_t, const ComPtr<T>& right) noexcept
{
	return nullptr == right.get();
}

template<typename T, typename U>
bool operator!=(const ComPtr<T>& left, const ComPtr<U>& right) noexcept
{
	return left.get() != right.get();
}

template<typename T, typename U>
bool operator!=(const ComPtr<T>& left, const U* const right) noexcept
{
	return left.get() != right;
}

template<typename T, typename U>
bool operator!=(const T* const left, const ComPtr<U>& right) noexcept
{
	return left != right.get();
}

template<typename T>
bool operator!=(const ComPtr<T>& left, std::nullptr_t) noexcept
{
	return left.get() != nullptr;
}

template<typename T>
bool operator!=(std::nullptr_t, const ComPtr<T>& right) noexcept
{
	return nullptr != right.get();
}

template<typename T, typename U>
bool operator<(const ComPtr<T>& left, const ComPtr<U>& right) noexcept
{
	return left.get() < right.get();
}
