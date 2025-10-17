class ReadWriteLock
{
public:
  ReadWriteLock() = default;
  ~ReadWriteLock() = default;

  ReadWriteLock(const ReadWriteLock& rhs) = delete;
  ReadWriteLock(ReadWriteLock&& rhs) = delete;
  ReadWriteLock& operator=(ReadWriteLock&&) = delete;
  ReadWriteLock& operator=(const ReadWriteLock& rhs) = delete;

  std::shared_mutex& getLock()
  {
    return d_lock;
  }

private:
  std::shared_mutex d_lock;
};

class ReadLock
{
public:
  ReadLock(ReadWriteLock& lock) :
    ReadLock(lock.getLock())
  {
  }

  ReadLock(ReadWriteLock* lock) :
    ReadLock(lock->getLock())
  {
  }

  ~ReadLock() = default;
  ReadLock(const ReadLock& rhs) = delete;
  ReadLock& operator=(const ReadLock& rhs) = delete;
  ReadLock& operator=(ReadLock&&) = delete;

  ReadLock(ReadLock&& rhs) noexcept :
    d_lock(std::move(rhs.d_lock))
  {
  }

private:
  ReadLock(std::shared_mutex& lock) :
    d_lock(lock)
  {
  }

  std::shared_lock<std::shared_mutex> d_lock;
};

class WriteLock
{
public:
  WriteLock(ReadWriteLock& lock) :
    WriteLock(lock.getLock())
  {
  }

  WriteLock(ReadWriteLock* lock) :
    WriteLock(lock->getLock())
  {
  }

  ~WriteLock() = default;
  WriteLock(const WriteLock& rhs) = delete;
  WriteLock& operator=(const WriteLock& rhs) = delete;
  WriteLock& operator=(WriteLock&&) = delete;

  WriteLock(WriteLock&& rhs) noexcept :
    d_lock(std::move(rhs.d_lock))
  {
  }

private:
  WriteLock(std::shared_mutex& lock) :
    d_lock(lock)
  {
  }

  std::unique_lock<std::shared_mutex> d_lock;
};

class TryReadLock
{
public:
  TryReadLock(ReadWriteLock& lock) :
    TryReadLock(lock.getLock())
  {
  }

  TryReadLock(ReadWriteLock* lock) :
    TryReadLock(lock->getLock())
  {
  }

  ~TryReadLock() = default;
  TryReadLock(const TryReadLock& rhs) = delete;
  TryReadLock(TryReadLock&&) = delete;
  TryReadLock& operator=(const TryReadLock& rhs) = delete;
  TryReadLock& operator=(TryReadLock&&) = delete;

  [[nodiscard]] bool gotIt() const
  {
    return d_lock.owns_lock();
  }

private:
  TryReadLock(std::shared_mutex& lock) :
    d_lock(lock, std::try_to_lock)
  {
  }

  std::shared_lock<std::shared_mutex> d_lock;
};

class TryWriteLock
{
public:
  TryWriteLock(ReadWriteLock& lock) :
    TryWriteLock(lock.getLock())
  {
  }

  TryWriteLock(ReadWriteLock* lock) :
    TryWriteLock(lock->getLock())
  {
  }

  ~TryWriteLock() = default;
  TryWriteLock(const TryWriteLock& rhs) = delete;
  TryWriteLock(TryWriteLock&&) = delete;
  TryWriteLock& operator=(const TryWriteLock& rhs) = delete;
  TryWriteLock& operator=(TryWriteLock&&) = delete;

  [[nodiscard]] bool gotIt() const
  {
    return d_lock.owns_lock();
  }

private:
  TryWriteLock(std::shared_mutex& lock) :
    d_lock(lock, std::try_to_lock)
  {
  }

  std::unique_lock<std::shared_mutex> d_lock;
};

template <typename T>
class LockGuardedHolder
{
public:
  explicit LockGuardedHolder(T& value, std::mutex& mutex) :
    d_lock(mutex), d_value(value)
  {
  }

  T& operator*() const noexcept
  {
    return d_value;
  }

  T* operator->() const noexcept
  {
    return &d_value;
  }

private:
  std::scoped_lock<std::mutex> d_lock;
  T& d_value;
};

template <typename T>
class LockGuardedTryHolder
{
public:
  explicit LockGuardedTryHolder(T& value, std::mutex& mutex) :
    d_lock(mutex, std::try_to_lock), d_value(value)
  {
  }

  T& operator*() const
  {
    if (!owns_lock()) {
      throw std::runtime_error("Trying to access data protected by a mutex while the lock has not been acquired");
    }
    return d_value;
  }

  T* operator->() const
  {
    if (!owns_lock()) {
      throw std::runtime_error("Trying to access data protected by a mutex while the lock has not been acquired");
    }
    return &d_value;
  }

  operator bool() const noexcept
  {
    return d_lock.owns_lock();
  }

  [[nodiscard]] bool owns_lock() const noexcept
  {
    return d_lock.owns_lock();
  }

  void lock()
  {
    d_lock.lock();
  }

private:
  std::unique_lock<std::mutex> d_lock;
  T& d_value;
};

template <typename T>
class LockGuarded
{
public:
  explicit LockGuarded(const T& value) :
    d_value(value)
  {
  }

  explicit LockGuarded(T&& value) :
    d_value(std::move(value))
  {
  }

  explicit LockGuarded() = default;

  LockGuardedTryHolder<T> try_lock()
  {
    return LockGuardedTryHolder<T>(d_value, d_mutex);
  }

  LockGuardedHolder<T> lock()
  {
    return LockGuardedHolder<T>(d_value, d_mutex);
  }

  LockGuardedHolder<const T> read_only_lock()
  {
    return LockGuardedHolder<const T>(d_value, d_mutex);
  }

private:
  std::mutex d_mutex;
  T d_value;
};

template <typename T>
class RecursiveLockGuardedHolder
{
public:
  explicit RecursiveLockGuardedHolder(T& value, std::recursive_mutex& mutex) :
    d_lock(mutex), d_value(value)
  {
  }

  T& operator*() const noexcept
  {
    return d_value;
  }

  T* operator->() const noexcept
  {
    return &d_value;
  }

private:
  std::scoped_lock<std::recursive_mutex> d_lock;
  T& d_value;
};

template <typename T>
class RecursiveLockGuardedTryHolder
{
public:
  explicit RecursiveLockGuardedTryHolder(T& value, std::recursive_mutex& mutex) :
    d_lock(mutex, std::try_to_lock), d_value(value)
  {
  }

  T& operator*() const
  {
    if (!owns_lock()) {
      throw std::runtime_error("Trying to access data protected by a mutex while the lock has not been acquired");
    }
    return d_value;
  }

  T* operator->() const
  {
    if (!owns_lock()) {
      throw std::runtime_error("Trying to access data protected by a mutex while the lock has not been acquired");
    }
    return &d_value;
  }

  operator bool() const noexcept
  {
    return d_lock.owns_lock();
  }

  [[nodiscard]] bool owns_lock() const noexcept
  {
    return d_lock.owns_lock();
  }

  void lock()
  {
    d_lock.lock();
  }

private:
  std::unique_lock<std::recursive_mutex> d_lock;
  T& d_value;
};

template <typename T>
class RecursiveLockGuarded
{
public:
  explicit RecursiveLockGuarded(const T& value) :
    d_value(value)
  {
  }

  explicit RecursiveLockGuarded(T&& value) :
    d_value(std::move(value))
  {
  }

  explicit RecursiveLockGuarded() = default;

  RecursiveLockGuardedTryHolder<T> try_lock()
  {
    return RecursiveLockGuardedTryHolder<T>(d_value, d_mutex);
  }

  RecursiveLockGuardedHolder<T> lock()
  {
    return RecursiveLockGuardedHolder<T>(d_value, d_mutex);
  }

  RecursiveLockGuardedHolder<const T> read_only_lock()
  {
    return RecursiveLockGuardedHolder<const T>(d_value, d_mutex);
  }

private:
  std::recursive_mutex d_mutex;
  T d_value;
};

template <typename T>
class SharedLockGuardedHolder
{
public:
  explicit SharedLockGuardedHolder(T& value, std::shared_mutex& mutex) :
    d_lock(mutex), d_value(value)
  {
  }

  T& operator*() const noexcept
  {
    return d_value;
  }

  T* operator->() const noexcept
  {
    return &d_value;
  }

private:
  std::scoped_lock<std::shared_mutex> d_lock;
  T& d_value;
};

template <typename T>
class SharedLockGuardedTryHolder
{
public:
  explicit SharedLockGuardedTryHolder(T& value, std::shared_mutex& mutex) :
    d_lock(mutex, std::try_to_lock), d_value(value)
  {
  }

  T& operator*() const
  {
    if (!owns_lock()) {
      throw std::runtime_error("Trying to access data protected by a mutex while the lock has not been acquired");
    }
    return d_value;
  }

  T* operator->() const
  {
    if (!owns_lock()) {
      throw std::runtime_error("Trying to access data protected by a mutex while the lock has not been acquired");
    }
    return &d_value;
  }

  operator bool() const noexcept
  {
    return d_lock.owns_lock();
  }

  [[nodiscard]] bool owns_lock() const noexcept
  {
    return d_lock.owns_lock();
  }

private:
  std::unique_lock<std::shared_mutex> d_lock;
  T& d_value;
};

template <typename T>
class SharedLockGuardedNonExclusiveHolder
{
public:
  explicit SharedLockGuardedNonExclusiveHolder(const T& value, std::shared_mutex& mutex) :
    d_lock(mutex), d_value(value)
  {
  }

  const T& operator*() const noexcept
  {
    return d_value;
  }

  const T* operator->() const noexcept
  {
    return &d_value;
  }

private:
  std::shared_lock<std::shared_mutex> d_lock;
  const T& d_value;
};

template <typename T>
class SharedLockGuardedNonExclusiveTryHolder
{
public:
  explicit SharedLockGuardedNonExclusiveTryHolder(const T& value, std::shared_mutex& mutex) :
    d_lock(mutex, std::try_to_lock), d_value(value)
  {
  }

  const T& operator*() const
  {
    if (!owns_lock()) {
      throw std::runtime_error("Trying to access data protected by a mutex while the lock has not been acquired");
    }
    return d_value;
  }

  const T* operator->() const
  {
    if (!owns_lock()) {
      throw std::runtime_error("Trying to access data protected by a mutex while the lock has not been acquired");
    }
    return &d_value;
  }

  operator bool() const noexcept
  {
    return d_lock.owns_lock();
  }

  [[nodiscard]] bool owns_lock() const noexcept
  {
    return d_lock.owns_lock();
  }

private:
  std::shared_lock<std::shared_mutex> d_lock;
  const T& d_value;
};

template <typename T>
class SharedLockGuarded
{
public:
  explicit SharedLockGuarded(const T& value) :
    d_value(value)
  {
  }

  explicit SharedLockGuarded(T&& value) :
    d_value(std::move(value))
  {
  }

  explicit SharedLockGuarded() = default;

  SharedLockGuardedTryHolder<T> try_write_lock()
  {
    return SharedLockGuardedTryHolder<T>(d_value, d_mutex);
  }

  SharedLockGuardedHolder<T> write_lock()
  {
    return SharedLockGuardedHolder<T>(d_value, d_mutex);
  }

  SharedLockGuardedNonExclusiveTryHolder<T> try_read_lock()
  {
    return SharedLockGuardedNonExclusiveTryHolder<T>(d_value, d_mutex);
  }

  SharedLockGuardedNonExclusiveHolder<T> read_lock()
  {
    return SharedLockGuardedNonExclusiveHolder<T>(d_value, d_mutex);
  }

private:
  std::shared_mutex d_mutex;
  T d_value;
};