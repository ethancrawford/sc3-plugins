#include <iostream>
#include <vector>
#include "Boid.hpp"
#include "RTAllocator.hpp"

#ifndef FLOCK_H_
#define FLOCK_H_

// Brief description of Flock Class:
// This file contains the class needed to create a flock of boids. It utilizes
// the boids class and initializes boid flocks with parameters that can be
// specified.

class BoidFlock {
public:
  vector<Boid, rt_allocator<Boid>> flock;
  // Accessor functions
  int getSize();
  Boid getBoid(int i);
  // Mutator Functions
  void addBoid(Boid b);
  void flocking();
};

#endif
