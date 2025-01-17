#ifndef _CryThread_pthreads_h_
#define _CryThread_pthreads_h_ 1

#ifdef ORBIS
#include "OrbisPThreads.h"
#else
#ifndef __SPU__
#  include <sys/types.h>
#  include <sys/time.h>
#  include <pthread.h>
#  include <semaphore.h>
#  include <sched.h>
#endif
#endif

#include <ISystem.h>
#include <ILog.h>

#if defined(PS3)
	#ifdef __SPU__
		#define pthread_t void*
	#endif			
	#ifndef __SPU__
		#include <time.h>
		#include <sys/ppu_thread.h>
		#include <sys/time_util.h>
		#include <sys/sys_time.h>
 	  #include <sys/synchronization.h>
	#endif
	extern void RegisterThreadName(pthread_t, const char*);
	extern void UnRegisterThreadName(pthread_t);
#elif defined(ORBIS)
	#define PTHREAD_MUTEX_FAST_NP PTHREAD_MUTEX_NORMAL
	#define RegisterThreadName(id,name) scePthreadRename(id, name)
	#define UnRegisterThreadName(id)
#else
	#define RegisterThreadName(id,name)
	#define UnRegisterThreadName(id)
#endif

#if defined(PS3) || defined(APPLE) || defined(ANDROID)
    // PTHREAD_MUTEX_FAST_NP is only defined by Pthreads-w32, thus not on PS3 nor on MAC
	#define PTHREAD_MUTEX_FAST_NP PTHREAD_MUTEX_NORMAL
#endif

// Define LARGE_THREAD_STACK to use larger than normal per-thread stack
#if defined(_DEBUG) && (defined(PS3) || defined(MAC) || defined(LINUX))
#define LARGE_THREAD_STACK
#endif

#if defined(LINUX)
#undef RegisterThreadName
ILINE void RegisterThreadName(threadID id, const char* name)
{
  if(!name) return;
  int ret;
  // pthread names on linux are limited to 16 char
  if (strlen(name) >= 16)
  {
    char thread_name[16];
    memcpy(thread_name, name, 15);
	thread_name[15] = 0;
    ret = pthread_setname_np(id,thread_name);
  }
  else
  {
    ret = pthread_setname_np(id,name);
  }
  if (ret != 0)
    CryLog("Failed to set thread name for %" PRI_THREADID ", name: %s", id, name);
}
#endif

#if !defined _CRYTHREAD_HAVE_LOCK
template<class LockClass> class _PthreadCond;
template<int PthreadMutexType> class _PthreadLockBase;

template<int PthreadMutexType> class _PthreadLockAttr
{
	friend class _PthreadLockBase<PthreadMutexType>;

protected:
	_PthreadLockAttr()
	{
		pthread_mutexattr_init(&m_Attr);
		pthread_mutexattr_settype(&m_Attr, PthreadMutexType);
	}
	~_PthreadLockAttr()
	{
		pthread_mutexattr_destroy(&m_Attr);
	}
  pthread_mutexattr_t m_Attr;
};

template<int PthreadMutexType> class _PthreadLockBase
{
protected:
	static pthread_mutexattr_t &GetAttr() 
	{
		static _PthreadLockAttr<PthreadMutexType> m_Attr;
	 	return m_Attr.m_Attr;
	}
};

template<class LockClass, int PthreadMutexType> class _PthreadLock
  : public _PthreadLockBase<PthreadMutexType>
{
  friend class _PthreadCond<LockClass>;

//#if defined(_DEBUG)
public:
//#endif
  pthread_mutex_t m_Lock;

public:
  _PthreadLock() : LockCount(0)
	{
		pthread_mutex_init(
				&m_Lock,
				&_PthreadLockBase<PthreadMutexType>::GetAttr());
	}
  ~_PthreadLock() { pthread_mutex_destroy(&m_Lock); }

  void Lock() { pthread_mutex_lock(&m_Lock); CryInterlockedIncrement(&LockCount); }
  
  bool TryLock()
  { 
	const int rc = pthread_mutex_trylock(&m_Lock);
	if (0 == rc)
	{
		CryInterlockedIncrement(&LockCount);
		return true;
	}
	return false;
  }
  
  void Unlock() { CryInterlockedDecrement(&LockCount); pthread_mutex_unlock(&m_Lock); }

	// Get the POSIX pthread_mutex_t.
	// Warning:
	// This method will not be available in the Win32 port of CryThread.
	pthread_mutex_t &Get_pthread_mutex_t() { return m_Lock; }

	bool IsLocked()
	{
#if defined(LINUX) || defined(APPLE)
		// implementation taken from CrysisWars
		return LockCount > 0;
#else
		return true;
#endif
	}

private:
	volatile int LockCount;
};

#if defined CRYLOCK_HAVE_FASTLOCK
	#if defined(_DEBUG) && defined(PTHREAD_MUTEX_ERRORCHECK_NP)
	template<> class CryLockT<CRYLOCK_FAST>
		: public _PthreadLock<CryLockT<CRYLOCK_FAST>, PTHREAD_MUTEX_ERRORCHECK_NP>
	#else
	template<> class CryLockT<CRYLOCK_FAST>
		: public _PthreadLock<CryLockT<CRYLOCK_FAST>, PTHREAD_MUTEX_FAST_NP>
	#endif
	{
		CryLockT(const CryLockT<CRYLOCK_FAST>&);
		void operator = (const CryLockT<CRYLOCK_FAST>&);

	public:
		CryLockT() { }
	};
#endif // CRYLOCK_HAVE_FASTLOCK

template<> class CryLockT<CRYLOCK_RECURSIVE>
  : public _PthreadLock<CryLockT<CRYLOCK_RECURSIVE>, PTHREAD_MUTEX_RECURSIVE>
{
	CryLockT(const CryLockT<CRYLOCK_RECURSIVE>&);
	void operator = (const CryLockT<CRYLOCK_RECURSIVE>&);

public:
	CryLockT() { }
};

#if !defined(LINUX) && !defined(APPLE) && !defined(ORBIS)
#if defined CRYLOCK_HAVE_FASTLOCK
class CryMutex : public CryLockT<CRYLOCK_FAST> {};
#else
class CryMutex : public CryLockT<CRYLOCK_RECURSIVE> {};
#endif
#endif // !LINUX !MAC 

template<class LockClass> class _PthreadCond
{
  pthread_cond_t m_Cond;

public:
  _PthreadCond() { pthread_cond_init(&m_Cond, NULL); }
  ~_PthreadCond() { pthread_cond_destroy(&m_Cond); }
  void Notify() { pthread_cond_broadcast(&m_Cond); }
  void NotifySingle() { pthread_cond_signal(&m_Cond); }
  void Wait(LockClass &Lock) { pthread_cond_wait(&m_Cond, &Lock.m_Lock); }
	bool TimedWait(LockClass &Lock, uint32 milliseconds)
	{
		struct timeval now;
    struct timespec timeout;
    int err;

    gettimeofday(&now, NULL);
		while (true)
		{
			timeout.tv_sec = now.tv_sec + milliseconds / 1000;
			uint64 nsec = (uint64)now.tv_usec * 1000 + (uint64)milliseconds * 1000000;
			if (nsec >= 1000000000)
			{
				timeout.tv_sec += (long)(nsec / 1000000000);
				nsec %= 1000000000;
			}
			timeout.tv_nsec = (long)nsec;
#ifdef ORIS_SCE_THREADS
			SceKernelUseconds to;
			to=milliseconds*1000;
			err = pthread_cond_timedwait(&m_Cond, &Lock.m_Lock, to);
			if (err == SCE_KERNEL_ERROR_EINTR)
				continue;
			else if (err == SCE_KERNEL_ERROR_ETIMEDOUT)
				return false;
#else
			err = pthread_cond_timedwait(&m_Cond, &Lock.m_Lock, &timeout);
			if (err == EINTR)
			{
				// Interrupted by a signal.
				continue;
			}
			else if (err == ETIMEDOUT)
			{
				return false;
			}
#endif
			else
				assert(err == 0);
			break;
		}
		return true;
	}

	// Get the POSIX pthread_cont_t.
	// Warning:
	// This method will not be available in the Win32 port of CryThread.
	pthread_cond_t &Get_pthread_cond_t() { return m_Cond; }
};

#if defined(LINUX) || defined(APPLE) || defined(ORBIS)
template <class LockClass> class CryConditionVariableT : public _PthreadCond<LockClass> {};

#if defined CRYLOCK_HAVE_FASTTLOCK
template<>
class CryConditionVariableT< CryLockT<CRYLOCK_FAST> > : public _PthreadCond< CryLockT<CRYLOCK_FAST> >
{
	typedef CryLockT<CRYLOCK_FAST> LockClass;
	CryConditionVariableT(const CryConditionVariableT<LockClass>&);
	CryConditionVariableT<LockClass>& operator = (const CryConditionVariableT<LockClass>&);

public:
	CryConditionVariableT() { }
};
#endif // CRYLOCK_HAVE_FASTLOCK

template<>
class CryConditionVariableT< CryLockT<CRYLOCK_RECURSIVE> > : public _PthreadCond< CryLockT<CRYLOCK_RECURSIVE> >
{
	typedef CryLockT<CRYLOCK_RECURSIVE> LockClass;
	CryConditionVariableT(const CryConditionVariableT<LockClass>&);
	CryConditionVariableT<LockClass>& operator = (const CryConditionVariableT<LockClass>&);

public:
	CryConditionVariableT() { }
};

#if !defined(_CRYTHREAD_CONDLOCK_GLITCH)
typedef CryConditionVariableT< CryLockT<CRYLOCK_RECURSIVE> > CryConditionVariable;
#else
typedef CryConditionVariableT< CryLockT<CRYLOCK_FAST> > CryConditionVariable;
#endif

#define _CRYTHREAD_HAVE_LOCK 1

#else // LINUX MAC

#if defined CRYLOCK_HAVE_FASTLOCK
template<>
class CryConditionVariable : public _PthreadCond< CryLockT<CRYLOCK_FAST> >
{
	typedef CryLockT<CRYLOCK_FAST> LockClass;
	CryConditionVariable(const CryConditionVariable&);
	CryConditionVariable& operator = (const CryConditionVariable&);

public:
	CryConditionVariable() { }
};
#endif // CRYLOCK_HAVE_FASTLOCK

template<>
class CryConditionVariable : public _PthreadCond< CryLockT<CRYLOCK_RECURSIVE> >
{
	typedef CryLockT<CRYLOCK_RECURSIVE> LockClass;
	CryConditionVariable(const CryConditionVariable&);
	CryConditionVariable& operator = (const CryConditionVariable&);

public:
	CryConditionVariable() { }
};

#define _CRYTHREAD_HAVE_LOCK 1

#endif // LINUX MAC
#endif // !defined _CRYTHREAD_HAVE_LOCK

//////////////////////////////////////////////////////////////////////////
// Platform independet wrapper for a counting semaphore
class CrySemaphore
{
public:
	CrySemaphore(int nMaximumCount, int nInitialCount = 0);	
	~CrySemaphore();	

	void Acquire();
	void Release();
	
private:
#if defined(__SPU__)
	void* m_Semaphore;
#else
	sem_t m_Semaphore;
#endif
};

//////////////////////////////////////////////////////////////////////////
inline CrySemaphore::CrySemaphore(int nMaximumCount, int nInitialCount)
{
#if defined(__SPU__)
	snPause(); // not supported on SPU
#elif defined(ORIS_SCE_THREADS)
	ORBIS_TO_IMPLEMENT;
#else	
	sem_init(&m_Semaphore,0,nInitialCount);
#endif
}

//////////////////////////////////////////////////////////////////////////
inline CrySemaphore::~CrySemaphore()
{
#if defined(__SPU__)
	snPause(); // not supported on SPU
#elif defined(ORIS_SCE_THREADS)
	ORBIS_TO_IMPLEMENT;
#else	
	sem_destroy(&m_Semaphore);
#endif
}

//////////////////////////////////////////////////////////////////////////
inline void CrySemaphore::Acquire()
{
#if defined(__SPU__)
	__spu_send_ppu_event(JOBMANAGER_SPU_REQUEST_ACQUIRE_SEMAPHORE, alias_cast<uint32>(this) );
#elif defined(ORIS_SCE_THREADS)
	ORBIS_TO_IMPLEMENT;
#else	
	sem_wait(&m_Semaphore);
#endif
}

//////////////////////////////////////////////////////////////////////////
inline void CrySemaphore::Release()
{
#if defined(__SPU__)
	__spu_send_ppu_event(JOBMANAGER_SPU_REQUEST_RELEASE_SEMAPHORE, alias_cast<uint32>(this) );
#elif defined(ORIS_SCE_THREADS)
	ORBIS_TO_IMPLEMENT;
#else	
	sem_post(&m_Semaphore);
#endif
}

//////////////////////////////////////////////////////////////////////////
// Platform independet wrapper for a counting semaphore
// except that this version uses C-A-S only until a blocking call is needed
// -> No kernel call if there are object in the semaphore

// forward declarations needed for friend function
namespace JobManager {
	class CJobDelegator;
	struct SJobStringHandle;
	typedef SJobStringHandle* TJobHandle;
}

class CryFastSemaphore
{
public:
	CryFastSemaphore(int nMaximumCount, int nInitialCount = 0);	
	~CryFastSemaphore();
	void Acquire();	
	void Release();	

private:
	// allow SPU job submission function access to internal functions to not need to expose then for all clients
	friend void AddJobtoJobQueueJobs(JobManager::CJobDelegator& crJob, const unsigned int cOpMode, const JobManager::TJobHandle cJobHandle);

	CrySemaphore m_Semaphore;
	volatile int32 m_nCounter;	
};

//////////////////////////////////////////////////////////////////////////
inline CryFastSemaphore::CryFastSemaphore(int nMaximumCount, int nInitialCount) :
	m_Semaphore(nMaximumCount),
	m_nCounter(nInitialCount)
{
}
	
//////////////////////////////////////////////////////////////////////////
inline CryFastSemaphore::~CryFastSemaphore()
{
}
	
/////////////////////////////////////////////////////////////////////////
inline void CryFastSemaphore::Acquire()
{
	int nCount = ~0;
	do
	{
		nCount = *const_cast<volatile int*>(&m_nCounter);
	}while( CryInterlockedCompareExchange( alias_cast<volatile LONG*>(&m_nCounter), nCount - 1, nCount) != nCount );
	
	// if the count would have been 0 or below, go to kernel semaphore
	if( (nCount - 1)  < 0 )
		m_Semaphore.Acquire();
}
	
//////////////////////////////////////////////////////////////////////////
inline void CryFastSemaphore::Release()
{
	int nCount = ~0;
	do
	{
		nCount = *const_cast<volatile int*>(&m_nCounter);

	}while( CryInterlockedCompareExchange( alias_cast<volatile LONG*>(&m_nCounter), nCount + 1, nCount) != nCount );

	// wake up kernel semaphore if we have waiter
	if( nCount < 0 )
		m_Semaphore.Release();
}

//////////////////////////////////////////////////////////////////////////
#if !defined _CRYTHREAD_HAVE_RWLOCK && !defined __SPU__
class CryRWLock
{
	pthread_rwlock_t m_Lock;

	CryRWLock(const CryRWLock &);
	CryRWLock &operator= (const CryRWLock &);

public:
	CryRWLock() { pthread_rwlock_init(&m_Lock, NULL); }
	~CryRWLock() { pthread_rwlock_destroy(&m_Lock); }
	void RLock() { pthread_rwlock_rdlock(&m_Lock); }
	bool TryRLock() {
#ifdef ORIS_SCE_THREADS
		return pthread_rwlock_tryrdlock(&m_Lock) != SCE_KERNEL_ERROR_EBUSY;
#else
		return pthread_rwlock_tryrdlock(&m_Lock) != EBUSY;
#endif
	}
	void RUnlock() { Unlock(); }
	void WLock() { pthread_rwlock_wrlock(&m_Lock); }
	bool TryWLock() {
#ifdef ORIS_SCE_THREADS
		return pthread_rwlock_trywrlock(&m_Lock) != SCE_KERNEL_ERROR_EBUSY;
#else
		return pthread_rwlock_trywrlock(&m_Lock) != EBUSY;
#endif
	}
	void WUnlock() { Unlock(); }
	void Lock() { WLock(); }
	bool TryLock() { return TryWLock(); }
	void Unlock() { pthread_rwlock_unlock(&m_Lock); }
};

// Indicate that this implementation header provides an implementation for
// CryRWLock.
#define _CRYTHREAD_HAVE_RWLOCK 1
#endif // !defined _CRYTHREAD_HAVE_RWLOCK


////////////////////////////////////////////////////////////////////////////////
// Provide TLS implementation using pthreads for those platforms without __thread
////////////////////////////////////////////////////////////////////////////////

struct SCryPthreadTLSBase
{
	SCryPthreadTLSBase(void (*pDestructor)(void*))
	{
		pthread_key_create(&m_kKey, pDestructor);
	}
	
	~SCryPthreadTLSBase()
	{
		pthread_key_delete(m_kKey);
	}
	
	void* GetSpecific()
	{
		return pthread_getspecific(m_kKey);
	}
	
	void SetSpecific(const void* pValue)
	{
		pthread_setspecific(m_kKey, pValue);
	}
	
	pthread_key_t m_kKey;
};

template <typename T, bool bDirect> struct SCryPthreadTLSImpl{};

template <typename T> struct SCryPthreadTLSImpl<T, true> : private SCryPthreadTLSBase
{
	SCryPthreadTLSImpl()
	:	SCryPthreadTLSBase(NULL)
	{
	}
	
	T Get()
	{
		void* pSpecific(GetSpecific());
		return *reinterpret_cast<const T*>(&pSpecific);
	}
	
	void Set(const T& kValue)
	{
		SetSpecific(*reinterpret_cast<const void* const *>(&kValue));
	}
};

template <typename T> struct SCryPthreadTLSImpl<T, false> : private SCryPthreadTLSBase
{
	SCryPthreadTLSImpl()
	:	SCryPthreadTLSBase(&Destroy)
	{
	}
	
	T* GetPtr()
	{
		T* pPtr(static_cast<T*>(GetSpecific()));
		if (pPtr == NULL)
		{
			pPtr = new T();
			SetSpecific(pPtr);
		}
		return pPtr;
	}
	
	static void Destroy(void* pPointer)
	{
		delete static_cast<T*>(pPointer);
	}
	
	const T& Get()
	{
		return *GetPtr();
	}
	
	void Set(const T& kValue)
	{
		*GetPtr() = kValue;
	}
};

template <typename T>
struct SCryPthreadTLS : SCryPthreadTLSImpl<T, sizeof(T) <= sizeof(void*)>
{
};


//////////////////////////////////////////////////////////////////////////
// CryEvent(Timed) represent a synchronization event
//////////////////////////////////////////////////////////////////////////
class CryEventTimed
{
public:
	ILINE CryEventTimed(){m_flag=false;}
	ILINE ~CryEventTimed(){}

	// Reset the event to the unsignalled state.
	void Reset();
	// Set the event to the signalled state.
	void Set();
	// Access a HANDLE to wait on.
	void* GetHandle() const { return NULL; };
	// Wait indefinitely for the object to become signalled.
	void Wait();
	// Wait, with a time limit, for the object to become signalled. 
	bool Wait( const uint32 timeoutMillis );

private:
	// Lock for synchronization of notifications.
	CryCriticalSection m_lockNotify;
#if defined(LINUX) || defined(APPLE)
	CryConditionVariableT< CryLockT<CRYLOCK_RECURSIVE> > m_cond;
#else
	CryConditionVariable m_cond;
#endif
	volatile bool m_flag;
};

#define PS3_USE_SEMAPHORE	1

#if PS3 && PS3_USE_SEMAPHORE
//a semaphore based implementation, do not use on spu executed code
class CryEvent
{
public:
	ILINE CryEvent() {Init();}
	ILINE ~CryEvent()
	{
#if !defined( __SPU__ )
		if( CELL_OK != sys_semaphore_destroy(m_semaphore) )
#endif
			snPause();
	}

	// Reset the event to the unsignalled state.
	ILINE void Reset() {}

	// Set the event to the signalled state.
	ILINE void Set( int count = 1 )
	{
#if !defined( __SPU__ )
		if( CELL_OK != sys_semaphore_post(m_semaphore, (sys_semaphore_value_t)count) )
		{
			snPause();
		}
#else
		if (count != 1)
		{
			snPause();
		}
		__spu_send_ppu_event(JOBMANAGER_SPU_REQUEST_SET_EVENT, alias_cast<uint32>(this) );
#endif
	}
	
	// Wait indefinitely for the object to become signalled.
	ILINE void Wait()
	{
#if !defined( __SPU__ )
		if( CELL_OK != sys_semaphore_wait(m_semaphore, 0 ) )
#endif
			snPause();
	}
	
	// Wait with timeout for the object to become signalled.
	ILINE bool Wait(const uint32 timeoutMillis)
	{
		bool bInTime = true;
#if !defined( __SPU__ )
		usecond_t microTimeout = timeoutMillis*1000;
		int ret = sys_semaphore_wait(m_semaphore, microTimeout );
		if (ret == ETIMEDOUT)
		{	
			//-- To be consistent with CryEventTimed, return false if the wait timed out.
			bInTime = false;
		}
		if( CELL_OK != ret && ETIMEDOUT != ret )
#endif
			snPause();
		return bInTime;
	}

private:
	sys_semaphore_t m_semaphore;

	NO_INLINE void Init()
	{
#if !defined( __SPU__ )
		sys_semaphore_attribute_t attr;
		sys_semaphore_attribute_initialize(attr);
		sys_semaphore_attribute_name_set(attr.name, "Semaphore" );
		if( CELL_OK != sys_semaphore_create(&m_semaphore, &attr, 0, 0x7fffffff) )
#endif
			snPause();
	}
};
#else
	typedef CryEventTimed CryEvent;
#endif

#if !PLATFORM_SUPPORTS_THREADLOCAL
TLS_DECLARE(class CrySimpleThreadSelf *, g_CrySimpleThreadSelf);
#endif

class CrySimpleThreadSelf
{
protected:
#if PLATFORM_SUPPORTS_THREADLOCAL
	
	static CrySimpleThreadSelf* GetSelf()
	{
		return m_Self;
	}

	static void SetSelf(CrySimpleThreadSelf* pSelf)
	{
		m_Self = pSelf;
	}
private:
	static THREADLOCAL CrySimpleThreadSelf *m_Self;
	
#else
	
	static CrySimpleThreadSelf* GetSelf()
	{
		return TLS_GET(CrySimpleThreadSelf*, g_CrySimpleThreadSelf);
	}
	
	static void SetSelf(CrySimpleThreadSelf* pSelf)
	{
		TLS_SET(g_CrySimpleThreadSelf, pSelf);
	}
	
#endif
};

template<class Runnable>
class CrySimpleThread
	: public CryRunnable,
		protected CrySimpleThreadSelf
{
public:
	typedef void (*ThreadFunction)(void *);
	typedef CryRunnable RunnableT;

private:
#if !defined(NO_THREADINFO)
	CryThreadInfo m_Info;
#endif
	pthread_t m_ThreadID;
	unsigned m_CpuMask;
	Runnable *m_Runnable;
	struct
	{
		ThreadFunction m_ThreadFunction;
		void *m_ThreadParameter;
	} m_ThreadFunction;
	volatile bool m_bIsStarted;
	volatile bool m_bIsRunning;

protected:
	virtual void Terminate()
	{
		// This method must be empty.
		// Derived classes overriding Terminate() are not required to call this
		// method.
	}

private:
#ifndef __SPU__
#if !defined(NO_THREADINFO)
	static void SetThreadInfo(CrySimpleThread<Runnable> *self)
	{
		pthread_t thread = pthread_self();
		self->m_Info.m_ID = (uint32)(TRUNCATE_PTR)thread;
#if defined(APPLE)
		pthread_setname_np(self->m_Info.m_Name.c_str());
#endif
	}
#else
	static void SetThreadInfo(CrySimpleThread<Runnable> *self) { }
#endif

  static inline void SetThreadLocalData()
  {
#if defined(PS3)
    sys_ppu_thread_stack_t stack_info;
    sys_ppu_thread_get_stack_information(&stack_info); 
#if defined PS3_CRYENGINE
    __thread_stacksize = stack_info.pst_size;
    __thread_stackptr = (void*)stack_info.pst_addr;
#endif
#endif
  }

	static void *PthreadRunRunnable(void *thisPtr)
	{
		CrySimpleThread<Runnable> *const self = (CrySimpleThread<Runnable> *)thisPtr;
		SetSelf(self);
		self->m_bIsStarted = true;
		self->m_bIsRunning = true;
		SetThreadInfo(self);
		self->m_Runnable->Run();
		self->m_bIsRunning = false;
		self->Terminate();
		SetSelf(NULL);
    SetThreadLocalData();
		return NULL;
	}

	static void *PthreadRunThis(void *thisPtr)
	{
		CrySimpleThread<Runnable> *const self = (CrySimpleThread<Runnable> *)thisPtr;
		SetSelf(self);
		self->m_bIsStarted = true;
		self->m_bIsRunning = true;
		SetThreadInfo(self);
		self->Run();
		self->m_bIsRunning = false;
		self->Terminate();
		SetSelf(NULL);
    SetThreadLocalData();
		return NULL;
	}
#endif
	CrySimpleThread(const CrySimpleThread<Runnable>&);
	void operator = (const CrySimpleThread<Runnable>&);

public:
  CrySimpleThread()
		: m_CpuMask(0), m_bIsStarted(false), m_bIsRunning(false)
	{
		m_ThreadFunction.m_ThreadFunction = NULL;
		m_ThreadFunction.m_ThreadParameter = NULL;
#if !defined(NO_THREADINFO)
#if !defined(__SPU__)
		m_Info.m_Name = "<Thread>";
#endif
		m_Info.m_ID = 0;
#endif
		memset(&m_ThreadID, 0, sizeof m_ThreadID);
		m_Runnable = NULL;
	}

  virtual ~CrySimpleThread()
	{
#ifndef __SPU__
		if (IsStarted())
		{
			// Note: We don't want to cache a pointer to ISystem and/or ILog to
			// gain more freedom on when the threading classes are used (e.g.
			// threads may be started very early in the initialization).
			ISystem *pSystem = GetISystem();
			ILog *pLog = NULL;
			if (pSystem != NULL)
				pLog = pSystem->GetILog();
#if defined(PS3_CRYENGINE)
			if (pLog != NULL)
				pLog->LogError("Runaway thread %s", GetName());
#endif
			Cancel();
			WaitForThread();
		}
#endif
	}

#if !defined(NO_THREADINFO)
	CryThreadInfo &GetInfo() { return m_Info; }
	const char *GetName()
	{
#if !defined(__SPU__)
		return m_Info.m_Name.c_str();
#else
		return "";
#endif
	}

	// Set the name of the called thread.
	//
	// WIN32:
	// If the thread is started, then the VC debugger is informed about the new
	// thread name.  If the thread is not started, then the VC debugger will be
	// informed lated when the thread is started through one of the Start()
	// methods.
	//
	// If the parameter Name is NULL, then the name of the thread is kept
	// unchanged.  This may be used to sent the current thread name to the VC
	// debugger.
	void SetName(const char *Name)
	{
		CryThreadSetName(THREADID_NULL, Name);
#if !defined(__SPU__)
		if (Name != NULL)
		{
			m_Info.m_Name = Name;
		}
#endif
#if defined(WIN32)
		if (IsStarted())
		{
			// The VC debugger gets the information about a thread's name through
			// the exception 0x406D1388.
			struct
			{
				DWORD Type;
				const char *Name;
				DWORD ID;
				DWORD Flags;
			} Info = { 0x1000, NULL, 0, 0 };
			Info.ID = (DWORD)m_Info.m_ID;
			__try
			{
				RaiseException(
						0x406D1388, 0, sizeof Info / sizeof(DWORD), (ULONG_PTR *)&Info);
			}
			__except (EXCEPTION_CONTINUE_EXECUTION)
			{
			}
		}
#endif
	}
#else
#if !defined(NO_THREADINFO)
	CryThreadInfo &GetInfo()
	{
		static CryThreadInfo dummyInfo = { "<dummy>", 0 };
		return dummyInfo;
	}
#endif
	const char *GetName() { return "<dummy>"; }
	void SetName(const char *Name) { }
#endif

  virtual void Run()
	{
		// This Run() implementation supports the void StartFunction() method.
		// However, code using this class (or derived classes) should eventually
		// be refactored to use one of the other Start() methods.  This code will
		// be removed some day and the default implementation of Run() will be
		// empty.
		if (m_ThreadFunction.m_ThreadFunction != NULL)
		{
			m_ThreadFunction.m_ThreadFunction(m_ThreadFunction.m_ThreadParameter);
		}
	}

	// Cancel the running thread.
	//
	// If the thread class is implemented as a derived class of CrySimpleThread,
	// then the derived class should provide an appropriate implementation for
	// this method.  Calling the base class implementation is _not_ required.
	//
	// If the thread was started by specifying a Runnable (template argument),
	// then the Cancel() call is passed on to the specified runnable.
	//
	// If the thread was started using the StartFunction() method, then the
	// caller must find other means to inform the thread about the cancellation
	// request.
	virtual void Cancel()
	{
		if (IsStarted() && m_Runnable != NULL)
		{
			UnRegisterThreadName(m_ThreadID);
			m_Runnable->Cancel();
		}
	}

	virtual void Start(Runnable &runnable, unsigned cpuMask = 0, const char* name = NULL, int32 StackSize=(SIMPLE_THREAD_STACK_SIZE_KB*1024))
  {
#if defined(LARGE_THREAD_STACK)
		StackSize *= 4;//debug code needs a lot more than profile 
#endif
#ifndef __SPU__
    assert(m_ThreadID == 0);
    pthread_attr_t threadAttr;
    pthread_attr_init(&threadAttr);
    pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&threadAttr, StackSize);
		if(name)
			this->m_Info.m_Name = name;
#if !defined(LINUX) && !defined(APPLE) && !defined(ORBIS)
		threadAttr.name = (char*)name;
#endif
		m_CpuMask = cpuMask;
#if defined(PTHREAD_NPTL)
		if (cpuMask != ~0 && cpuMask != 0)
		{
			cpu_set_t cpuSet;
			CPU_ZERO(&cpuSet);
			for (int cpu = 0; cpu < sizeof(cpuMask) * 8; ++cpu)
			{
				if (cpuMask & (1 << cpu))
				{
					CPU_SET(cpu, &cpuSet);
				}
			}
			pthread_attr_setaffinity_np(&threadAttr, sizeof cpuSet, &cpuSet);
		}
#elif defined(ORBIS)
		scePthreadAttrSetaffinity(&threadAttr, cpuMask ? (cpuMask & SCE_KERNEL_CPUMASK_USER_ALL) : SCE_KERNEL_CPUMASK_USER_ALL);
#endif
		m_Runnable = &runnable;
    int err = pthread_create(
				&m_ThreadID,
				&threadAttr,
				PthreadRunRunnable,
				this);
		RegisterThreadName(m_ThreadID, name);
    assert(err == 0);
#endif
  }

	virtual void Start(unsigned cpuMask = 0, const char* name = NULL,int32 Priority=THREAD_PRIORITY_NORMAL, int32 StackSize=(SIMPLE_THREAD_STACK_SIZE_KB*1024))
	{
#if defined(LARGE_THREAD_STACK)
		StackSize *= 4;//debug code needs a lot more than profile 
#endif
#ifndef __SPU__
    assert(m_ThreadID == 0);
    pthread_attr_t threadAttr;
		sched_param schedParam;
    pthread_attr_init(&threadAttr);
		pthread_attr_getschedparam(&threadAttr,&schedParam);
		schedParam.sched_priority	=	Priority;
		pthread_attr_setschedparam(&threadAttr,&schedParam);
    pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&threadAttr, StackSize);
		if(name)
			this->m_Info.m_Name = name;
#if !defined(LINUX) && !defined(APPLE) && !defined(ORBIS)
		threadAttr.name = (char*)name;
#endif
		m_CpuMask = cpuMask;
#if defined(PTHREAD_NPTL)
		if (cpuMask != ~0 && cpuMask != 0)
		{
			cpu_set_t cpuSet;
			CPU_ZERO(&cpuSet);
			for (int cpu = 0; cpu < sizeof(cpuMask) * 8; ++cpu)
			{
				if (cpuMask & (1 << cpu))
				{
					CPU_SET(cpu, &cpuSet);
				}
			}
			pthread_attr_setaffinity_np(&threadAttr, sizeof cpuSet, &cpuSet);
		}
#elif defined(ORBIS)
		scePthreadAttrSetaffinity(&threadAttr, cpuMask ? (cpuMask & SCE_KERNEL_CPUMASK_USER_ALL) : SCE_KERNEL_CPUMASK_USER_ALL);
#endif
    int err = pthread_create(
				&m_ThreadID,
				&threadAttr,
				PthreadRunThis,
				this);
		RegisterThreadName(m_ThreadID, name);
#if defined(ORBIS)
		assert(THREAD_PRIORITY_IDLE >= Priority && Priority >= THREAD_PRIORITY_TIME_CRITICAL);
		err = pthread_setprio(m_ThreadID, Priority);
#endif
    assert(err == 0);
#endif
	}

	void StartFunction(
			ThreadFunction threadFunction,
			void *threadParameter = NULL,
			unsigned cpuMask = 0
			)
	{
		m_ThreadFunction.m_ThreadFunction = threadFunction;
		m_ThreadFunction.m_ThreadParameter = threadParameter;
		Start(cpuMask);
	}

	static CrySimpleThread<Runnable> *Self()
	{
		return reinterpret_cast<CrySimpleThread<Runnable> *>(GetSelf());
	}

  void Exit()
  {
#ifndef __SPU__
    assert(m_ThreadID == pthread_self());
		m_bIsRunning = false;
		Terminate();
		SetSelf(NULL);
    pthread_exit(NULL);
		UnRegisterThreadName(m_ThreadID);
#endif
  }

  void WaitForThread()
  {
#ifndef __SPU__
		if( pthread_self() != m_ThreadID )
		{
			int err = pthread_join(m_ThreadID, NULL);
			assert(err == 0);
		}
		m_bIsStarted = false;
		memset(&m_ThreadID, 0, sizeof m_ThreadID);
#endif
  }

	unsigned SetCpuMask(unsigned cpuMask)
	{
		int oldCpuMask = m_CpuMask;
		if (cpuMask == m_CpuMask)
			return oldCpuMask;
		m_CpuMask = cpuMask;
#if defined(PTHREAD_NPTL)
		cpu_set_t cpuSet;
		CPU_ZERO(&cpuSet);
		if (cpuMask != ~0 && cpuMask != 0)
		{
			for (int cpu = 0; cpu < sizeof(cpuMask) * 8; ++cpu)
			{
				if (cpuMask & (1 << cpu))
				{
					CPU_SET(cpu, &cpuSet);
				}
			}
		else
		{
			CPU_ZERO(&cpuSet);
			for (int cpu = 0; i < sizeof(cpuSet) * 8; ++cpu)
			{
				CPU_SET(cpu, &cpuSet);
			}
		}
		pthread_attr_setaffinity_np(&threadAttr, sizeof cpuSet, &cpuSet);
#elif defined(ORBIS)
		if (cpuMask==0)
		{
			cpuMask=~0;
		}
		scePthreadSetaffinity(scePthreadSelf(), cpuMask);
#endif
		return oldCpuMask;
	}

	unsigned GetCpuMask() { return m_CpuMask; }

	void Stop()
	{
		m_bIsStarted = false;
	}

	bool IsStarted() const { return m_bIsStarted; }
	bool IsRunning() const { return m_bIsRunning; }
};

#include "MemoryAccess.h"

///////////////////////////////////////////////////////////////////////////////
// base class for lock less Producer/Consumer queue, due platforms specific they
// are implemeted in CryThead_platform.h
namespace CryMT {
namespace detail {

	///////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////
	class SingleProducerSingleConsumerQueueBase
	{
	public:
		SingleProducerSingleConsumerQueueBase()
		{}

		void Push( void *pObj, volatile uint32 &rProducerIndex, volatile uint32 &rComsumerIndex, uint32 nBufferSize, void *arrBuffer, uint32 nObjectSize );
		void Pop( void *pObj, volatile uint32 &rProducerIndex, volatile uint32 &rComsumerIndex, uint32 nBufferSize, void *arrBuffer, uint32 nObjectSize );
	};

	///////////////////////////////////////////////////////////////////////////////
	inline void SingleProducerSingleConsumerQueueBase::Push( void *pObj, volatile uint32 &rProducerIndex, volatile uint32 &rComsumerIndex, uint32 nBufferSize, void *arrBuffer, uint32 nObjectSize )
	{
#if !defined(__SPU__)
		MemoryBarrier();
		// spin if queue is full
		int iter = 0;
		while(rProducerIndex - rComsumerIndex == nBufferSize) 
		{ 
			Sleep(iter++ > 10 ? 1 : 0); 
		}

		char *pBuffer = alias_cast<char*>(arrBuffer);
		uint32 nIndex = rProducerIndex % nBufferSize;
#if defined(PS3)
		IF( IsAligned( nObjectSize, 16 ), 1)		
			mymemcpy16( pBuffer + (nIndex * nObjectSize), pObj, nObjectSize );
		else
#endif
			memcpy( pBuffer + (nIndex * nObjectSize), pObj, nObjectSize );

		MemoryBarrier();
		rProducerIndex += 1;
		MemoryBarrier();
#endif
	}

	///////////////////////////////////////////////////////////////////////////////
	inline void SingleProducerSingleConsumerQueueBase::Pop( void *pObj, volatile uint32 &rProducerIndex, volatile uint32 &rComsumerIndex, uint32 nBufferSize, void *arrBuffer, uint32 nObjectSize )
	{
#if !defined(__SPU__)
		MemoryBarrier();
		// busy-loop if queue is empty
		int iter = 0;
		while(rProducerIndex - rComsumerIndex == 0) { Sleep(iter++ > 10 ? 1 : 0); }		

		char *pBuffer = alias_cast<char*>(arrBuffer);
		uint32 nIndex = rComsumerIndex % nBufferSize;

#if defined(PS3)
		IF( IsAligned( nObjectSize, 16 ), 1)		
			mymemcpy16( pObj, pBuffer + (nIndex * nObjectSize), nObjectSize );		
		else
#endif
			memcpy( pObj, pBuffer + (nIndex * nObjectSize), nObjectSize );		

		MemoryBarrier();
		rComsumerIndex += 1;
		MemoryBarrier();
#endif
	}


	///////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////
	class N_ProducerSingleConsumerQueueBase
	{
	public:
		N_ProducerSingleConsumerQueueBase()
		{
			CryInitializeSListHead(fallbackList);
		}

		void Push( void *pObj, volatile uint32 &rProducerIndex, volatile uint32 &rComsumerIndex, volatile uint32 &rRunning, void *arrBuffer, uint32 nBufferSize, uint32 nObjectSize, volatile uint32*	arrStates );
		bool Pop( void *pObj, volatile uint32 &rProducerIndex, volatile uint32 &rComsumerIndex, volatile uint32 &rRunning, void *arrBuffer, uint32 nBufferSize, uint32 nObjectSize, volatile uint32*	arrStates );
	
		SLockFreeSingleLinkedListHeader fallbackList;
		struct SFallbackList
		{
			SLockFreeSingleLinkedListEntry nextEntry;
			char alignment_padding[128-sizeof(SLockFreeSingleLinkedListEntry)];
			char object[1]; // struct will be overallocated with enough memory for the object
		};
	};

	///////////////////////////////////////////////////////////////////////////////
	inline void N_ProducerSingleConsumerQueueBase::Push( void *pObj, volatile uint32 &rProducerIndex, volatile uint32 &rComsumerIndex, volatile uint32 &rRunning, void *arrBuffer, uint32 nBufferSize, uint32 nObjectSize, volatile uint32*	arrStates )
	{
#if !defined(__SPU__)
		MemoryBarrier();
		uint32 nProducerIndex;
		uint32 nComsumerIndex;
			
		int iter = 0;
		do
		{
			nProducerIndex = rProducerIndex;
			nComsumerIndex = rComsumerIndex;
			
			if( nProducerIndex - nComsumerIndex == nBufferSize)
			{
				Sleep(iter++ > 10 ? 1 : 0); 
				if(iter > 20 ) // 10 spins + 10 ms wait
				{
					uint32 nSizeToAlloc = sizeof(SFallbackList) + nObjectSize - 1;
					SFallbackList *pFallbackEntry = (SFallbackList *)CryModuleMemalign(nSizeToAlloc,128);
					memcpy(pFallbackEntry->object, pObj, nObjectSize);
					CryInterlockedPushEntrySList( fallbackList,  pFallbackEntry->nextEntry );
					return;
				}
				continue;
			}

			if(CryInterlockedCompareExchange( alias_cast<volatile LONG*>(&rProducerIndex), nProducerIndex + 1, nProducerIndex ) == nProducerIndex )
				break;
		}while(true);		

		char *pBuffer = alias_cast<char*>(arrBuffer);
		uint32 nIndex = nProducerIndex % nBufferSize;

#if defined(PS3)
		IF( IsAligned( nObjectSize, 16 ), 1)		
			mymemcpy16( pBuffer + (nIndex * nObjectSize), pObj, nObjectSize);
		else
#endif
			memcpy( pBuffer + (nIndex * nObjectSize), pObj, nObjectSize);

		MemoryBarrier();
		arrStates[nIndex] = 1;
		MemoryBarrier();
#endif
	}

	///////////////////////////////////////////////////////////////////////////////
	inline bool N_ProducerSingleConsumerQueueBase::Pop( void *pObj, volatile uint32 &rProducerIndex, volatile uint32 &rComsumerIndex, volatile uint32 &rRunning, void *arrBuffer, uint32 nBufferSize, uint32 nObjectSize, volatile uint32 *arrStates )
	{
#if !defined(__SPU__)
		MemoryBarrier();		
		// busy-loop if queue is empty
		int iter = 0;
#if EMBED_PHYSICS_AS_FIBER
		if (rRunning && rProducerIndex - rComsumerIndex == 0)
			JobManager::Fiber::SwitchFiberDirect();
#endif
		do		
		{ 
			SFallbackList *pFallback = (SFallbackList *)CryInterlockedPopEntrySList(fallbackList);
			IF(pFallback ,0)
			{
				memcpy( pObj, pFallback->object, nObjectSize );	
				CryModuleMemalignFree(pFallback);
				return true;
			}

			if(iter>10)
				Sleep(iter > 100 ? 1 : 0);
			iter++;
		}while(rRunning && rProducerIndex - rComsumerIndex == 0); 

		if( rRunning == 0 && rProducerIndex - rComsumerIndex == 0)
		{
			// if the queue was empty, make sure we really are empty
			SFallbackList *pFallback = (SFallbackList *)CryInterlockedPopEntrySList(fallbackList);
			IF(pFallback ,0)
			{
				memcpy( pObj, pFallback->object, nObjectSize );	
				CryModuleMemalignFree(pFallback);
				return true;
			}
			return false;
		}

#if EMBED_PHYSICS_AS_FIBER
		if (rRunning && rProducerIndex - rComsumerIndex == 0)
			JobManager::Fiber::SwitchFiberDirect();
#endif
		iter = 0;
		while(arrStates[rComsumerIndex % nBufferSize] == 0) {
			Sleep(iter++ > 10 ? 1 : 0);
		}
		
		char *pBuffer = alias_cast<char*>(arrBuffer);
		uint32 nIndex = rComsumerIndex % nBufferSize;

#if defined(PS3)
		IF( IsAligned( nObjectSize, 16 ), 1)
			mymemcpy16(pObj, pBuffer + (nIndex * nObjectSize), nObjectSize);		
		else
#endif
			memcpy(pObj, pBuffer + (nIndex * nObjectSize), nObjectSize);		

		MemoryBarrier();
		arrStates[nIndex] = 0;
		MemoryBarrier();
		rComsumerIndex += 1;
		MemoryBarrier();
#endif
		return true;
	}

} // namespace detail
} // namespace CryMT

#ifdef __SPU__
	#include "CryThreadImpl_ps3.h"
#endif

#endif

// vim:ts=2

