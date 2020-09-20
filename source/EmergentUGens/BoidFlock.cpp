#include "Boid.hpp"
#include "BoidFlock.hpp"

// =============================================== //
// ======== Flock Functions from Flock.h ========= //
// =============================================== //

int BoidFlock::getSize() {
  return flock.size();
}

Boid BoidFlock::getBoid(int i) {
  return flock[i];
}

void BoidFlock::addBoid(Boid b) {
  flock.push_back(b);
}

// Runs the run function for every boid in the flock checking against the flock
// itself. Which in turn applies all the rules to the flock.
void BoidFlock::flocking() {
  for (int i = 0; i < flock.size(); i++)
    flock[i].run(flock);
}
