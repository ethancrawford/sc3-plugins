#include "SC_World.h"

#ifndef _STLALLOCATOR_H
#define _STLALLOCATOR_H

extern World* g_pWorld;

template <typename T>
class stl_allocator
{
public:
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T* pointer;
  typedef T value_type;
  stl_allocator() {}
  ~stl_allocator() {}
  template <class U> stl_allocator(const stl_allocator<U>&) {}

  pointer allocate(size_type n)
  {
    return (pointer)RTAlloc(g_pWorld, n * sizeof(T));
  }
  void deallocate(pointer p, size_type n)
  {
    (void)&n;
    RTFree(g_pWorld, p);
  }
};

#endif