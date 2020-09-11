#include "SC_World.h"

#ifndef _STLALLOCATOR_H
#define _STLALLOCATOR_H

template <typename T, World* world>
class stl_allocator
{
public:
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T* pointer;
  typedef T value_type;
  stl_allocator() {}
  ~stl_allocator() {}
  template <class U, World* world> stl_allocator(const stl_allocator<U, world>&) {}
  pointer allocate(size_type n)
  {
    return (pointer)RTAlloc(world, n * sizeof(T));
  }
  void deallocate(pointer p, size_type n)
  {
    // Free knows how big the block is
    (void)&n;
    RTFree(world, p);
  }
};

#endif