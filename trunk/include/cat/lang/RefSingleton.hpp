/*
	Copyright (c) 2011 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CAT_REF_SINGLETON_HPP
#define CAT_REF_SINGLETON_HPP

#include <cat/lang/Singleton.hpp>

namespace cat {

class RefSingletonBase;
class RefSingletons;


// In the H file for the object, derive from this class:
template<class T>
class RefSingleton : public RefSingletonBase
{
	friend class RefSingletonImpl<T>;

	CAT_INLINE virtual ~RefSingleton<T>() {}

protected:
	// Called during initialization, return false to indicate error
	virtual bool OnInitialize() = 0;

	// Called during finalization, return false to indicate error
	virtual bool OnFinalize() = 0;

public:
	static T *ref();
};


// In the C file for the object, use this macro:
#define CAT_REF_SINGLETON(T)				\
static cat::RefSingletonImpl<T> m_T_rss;	\
T *T::ref() { return m_T_rss.GetRef(); }


// Internal class
class RefSingletonBase : public SListItem
{
	CAT_NO_COPY(RefSingletonBase);

	CAT_INLINE RefSingletonBase() {}
	CAT_INLINE virtual ~RefSingletonBase() {}

protected:
	virtual bool OnInitialize() = 0;
	virtual bool OnFinalize() = 0;
};

// Internal free function
Mutex CAT_EXPORT &GetRefSingletonMutex();

// Internal class
template<class T>
class RefSingletonImpl
{
	T _instance;
	bool _init;

public:
	CAT_INLINE T *GetRef()
	{
		if (_init) return &_instance;

		AutoMutex lock(GetRefSingletonMutex());

		if (_init) return &_instance;

		_instance.OnRefSingletonStartup();

		CAT_FENCE_COMPILER;

		_init = true;

		return &_instance;
	}
};

// Internal class
class RefSingletons : public Singleton<RefSingletons>
{
	SList _active_list;
	typedef SList::Iterator<RefSingletonBase> iter;

	template<class T>
	CAT_INLINE void Watch(T *obj)
	{
		AutoMutex lock(GetRefSingletonMutex());

		_active_list.PushFront(obj);
	}

	static void OnExit();

	void OnInitialize();
	void OnFinalize();
};


} // namespace cat

#endif // CAT_REF_SINGLETON_HPP
