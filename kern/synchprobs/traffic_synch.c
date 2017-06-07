#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

// 0 - North, 1 - East, 2 - South, 3 - West
typedef struct Vehicles {
  Direction origin;
  Direction destination;
}Vehicle;

// Declarations
static struct lock *intersectionLock;
static struct cv *cvNorth;
static struct cv *cvEast;
static struct cv *cvSouth;
static struct cv *cvWest;
struct array *waitingVehicles;
struct array *enteredVehicles;

// Forward declartions
bool is_cv_empty(void);
bool check_collision(Vehicle *v);
bool right_turn(Vehicle *v);


// Helper functions
bool is_cv_empty() {
  return (cvNorth == NULL || cvEast == NULL || cvSouth == NULL || cvWest == NULL);
}

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  enteredVehicles = array_create();
  waitingVehicles = array_create();
  intersectionLock = lock_create("intersectionLock"); 
  cvNorth = cv_create("0");
  cvEast  = cv_create("1");
  cvSouth = cv_create("2");
  cvWest  = cv_create("3");

  if (intersectionLock == NULL) {
    panic("could not create intersection lock");
  }
  if (is_cv_empty()) {
    panic("could not create intersection condition variable");
  }
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(intersectionLock != NULL);
  KASSERT(is_cv_empty() == false);
  KASSERT(enteredVehicles != NULL);
  KASSERT(waitingVehicles != NULL);

  lock_destroy(intersectionLock);
  cv_destroy(cvNorth);
  cv_destroy(cvEast);
  cv_destroy(cvSouth);
  cv_destroy(cvWest);
  array_destroy(waitingVehicles);
  array_destroy(enteredVehicles);
}

/*
 * Function to predicate that checks whether a vehicle is 
 * making a right turn. (Refered from traffic.c)
 */
bool right_turn(Vehicle *v) {
  KASSERT(v != NULL);
  if (((v->origin == west) && (v->destination == south)) ||
      ((v->origin == south) && (v->destination == east)) ||
      ((v->origin == east) && (v->destination == north)) ||
      ((v->origin == north) && (v->destination == west))) {
    return true;
  } else {
    return false;
  }
}

/*
 * Checks whether the incoming vehicle can enter the intersection
 * by making comparison with previous vehicles already in the intersection
 */
bool check_collision(Vehicle *v) {
  Vehicle *eVehicle;
  for(unsigned int i = 0; i < array_num(enteredVehicles); i++) {
    eVehicle = array_get(enteredVehicles,i);
    if (v->origin == eVehicle->origin) {
      continue;
    }
    else if (v->origin == eVehicle->destination && 
             v->destination == eVehicle->origin) {
      continue;
    }
    else if ((v->destination != eVehicle->destination) &&
              (right_turn(v) || right_turn(eVehicle))) {
      continue;
    }
    else {
      return true;
    }
  }
  return false;
}

/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  KASSERT(intersectionLock != NULL && is_cv_empty() == false);
  KASSERT(enteredVehicles  != NULL && waitingVehicles != NULL);

  lock_acquire(intersectionLock);
  Vehicle *nVehicle = kmalloc(sizeof(struct Vehicles));
  nVehicle->origin = origin;
  nVehicle->destination = destination;
  // add the vehicle in the waiting list
  array_add(waitingVehicles,nVehicle,NULL);
  while(check_collision(nVehicle)) {
    switch (nVehicle->origin) {
      case north:
        cv_wait(cvNorth,intersectionLock);
        break;
      case east:
        cv_wait(cvEast,intersectionLock);
        break;
      case south:
        cv_wait(cvSouth,intersectionLock);
        break;
      case west:
        cv_wait(cvWest,intersectionLock);
        break;
    }
  }
  // add the vehicle into intersection
  array_add(enteredVehicles, nVehicle, NULL);
  // remove the vehicle from the waiting list
  for (unsigned int i=0; i < array_num(waitingVehicles); i++){
    if (nVehicle == array_get(waitingVehicles,i)){
      array_remove(waitingVehicles, i);
      break;
    }
  }
  lock_release(intersectionLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  KASSERT(intersectionLock != NULL && is_cv_empty() == false);
  KASSERT(enteredVehicles  != NULL && waitingVehicles != NULL);
  
  lock_acquire(intersectionLock);
  // remove vehicle from the list of vehicles in the intersection
  Vehicle *eVehicle;
  for (unsigned int i = 0; i < array_num(enteredVehicles); i++){
    eVehicle = array_get(enteredVehicles, i);
    if (eVehicle->origin == origin && eVehicle->destination == destination){
      array_remove(enteredVehicles, i);
      break;
    }
  }
  // signal some vehicles to enter
  Vehicle *wVehicle;
  for (unsigned int i = 0; i < array_num(waitingVehicles); i++){
    wVehicle = array_get(waitingVehicles, i);
    if (check_collision(wVehicle) == false){
      switch (wVehicle->origin) {
        case north:
          cv_signal(cvNorth,intersectionLock);
          break;
        case east:
          cv_signal(cvEast,intersectionLock);
          break;
        case south:
          cv_signal(cvSouth,intersectionLock);
          break;
        case west:
          cv_signal(cvWest,intersectionLock);
          break;
      }
    }
  }
  lock_release(intersectionLock);
}
