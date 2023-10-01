// by wei-mj 20231001

#ifndef IU_OBJECT_INCLUDED
#define IU_OBJECT_INCLUDED

//#include <atomic>
#include <algorithm>
#include <string>

#ifdef _WIN32
#define IUCALL __cdecl
#define IUEXPORT(ret) extern "C" __declspec(dllexport) ret IUCALL
#else
#define IUCALL
#define IUEXPORT(ret) extern "C" __attribute__ ((visibility ("default"))) ret IUCALL
#endif
#define IUEXPORT_(ret)	typedef ret (IUCALL *type)
#define IUMETHOD(ret) virtual ret IUCALL
#define IUMETHOD_(ret,decl) IUMETHOD(ret) decl = 0
#define IUMETHODIMPL_(ret,decl) ret IUCALL decl override

namespace base
{
	template<typename T>
	struct CIUObjectT
	{
	};

	struct CIUObject
	{
		IUMETHOD_(int, AddRef());
		IUMETHOD_(int, Release());
	};
	template<typename Interface>
	class CIUObjectImplT : public Interface
	{
	public:
		CIUObjectImplT()
			:m_refcount(0)
		{
		}
		CIUObjectImplT(const CIUObjectImplT&) = delete;
		CIUObjectImplT& operator=(const CIUObjectImplT&) = delete;
		CIUObjectImplT(CIUObjectImplT&&) = delete;
		CIUObjectImplT& operator=(CIUObjectImplT&&) = delete;
		virtual ~CIUObjectImplT()
		{
		}
		IUMETHODIMPL_(int, AddRef())
		{
			return __sync_add_and_fetch(&m_refcount, 1);
		}
		IUMETHODIMPL_(int, Release())
		{
			if (int ret = __sync_sub_and_fetch(&m_refcount, 1)) return ret;
			delete this;
			return 0;
		}
	private:
		int m_refcount;
	};
	template<typename Interface>
	class CIUObjectPtrT
	{
	public:
		CIUObjectPtrT()
			:m_ptr(nullptr)
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
		}
		explicit CIUObjectPtrT(Interface* ptr)
			:m_ptr(ptr)
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
			if (m_ptr != nullptr)m_ptr->AddRef();
		}
		explicit CIUObjectPtrT(Interface* ptr, int) // no AddRef, for receiving a released pointer
			:m_ptr(ptr)
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
		}
		CIUObjectPtrT(const CIUObjectPtrT& other)
			:m_ptr(other.m_ptr)
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
			if (m_ptr != nullptr)m_ptr->AddRef();
		}
		CIUObjectPtrT(CIUObjectPtrT&& other)
			:m_ptr(other.release())
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
		}
		CIUObjectPtrT& operator=(const CIUObjectPtrT& other)
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
			CIUObjectPtrT(other).swap(*this);
			return *this;
		}
		CIUObjectPtrT& operator=(CIUObjectPtrT&& other)
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
			CIUObjectPtrT(std::move(other)).swap(*this);
			return *this;
		}
		~CIUObjectPtrT()
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
			if (m_ptr != nullptr)m_ptr->Release();
		}
		Interface* operator->() const
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
			return m_ptr;
		}
		void swap(CIUObjectPtrT& other)
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
			std::swap(m_ptr, other.m_ptr);
		}
		Interface* release()
		{
			static_assert(sizeof(m_ptr) == sizeof(*this), "");
			Interface* ret = m_ptr;
			m_ptr = nullptr;
			return ret;
		}
		Interface* get() const
		{
			return operator->();
		}
		operator Interface*() const
		{
			return operator->();
		}
	private:
		Interface* m_ptr;
	};
	typedef CIUObjectPtrT<CIUObject> CIUObjectPtr;
}

#endif
