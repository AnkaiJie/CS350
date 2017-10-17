#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <queue.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

static struct lock * mainLk;

static struct cv * northCv;
static struct cv * southCv;
static struct cv * eastCv;
static struct cv * westCv;

static int carTracker[4][4];

struct vehicle {
  Direction origin;
  Direction destination;
};

static struct queue *carQueue;
static struct queue *carQueue1;
static struct queue *carQueue2;

//Function declarations
struct cv *getDestCv(Direction destination);
// 1 for straight, 2 for right, 3 for left
int getPathType(Direction origin, Direction destination);
// struct queue *plusXq(Direction target, int plusx);
Direction plusXdir(Direction target, int plusx);
struct cv *blocksFirstQueue(Direction origin, Direction destination);
struct cv *waitOnCv(Direction origin, Direction destination);
struct cv *plusXcv(Direction target, int plusx);
// void printQueueDetails (struct queue *q, const char *action);
unsigned int directionFilledBy(Direction target);
bool directionFilled(Direction target);
void removeFromQueue(struct vehicle *toremove);
void printQueueDetails(struct queue *q, const char *action);


void printQueueDetails(struct queue *q, const char *action) {
  kprintf("carQ after %s, ", action);


  int length = q_len(q);

  kprintf("Length: %d, ", length);
  if (length > 0) {
    struct vehicle *first = (struct vehicle *)q_peek(q);
    kprintf("Peek: %d, %d\n", (int)first->origin, (int)first->destination);

  }

  
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
  /* replace this default implementation with your own implementation */

  // intersectionSem = sem_create("intersectionSem",1);
  // if (intersectionSem == NULL) {
  //   panic("could not create intersection semaphore");
  // }

  northCv = cv_create("northCv");
  southCv = cv_create("southCv");
  eastCv = cv_create("eastCv");
  westCv = cv_create("westCv");


  mainLk = lock_create("mainLk");

  carQueue1 = q_create(4);
  carQueue2 = q_create(4);
  carQueue = carQueue1;

  for (int i=0; i<4; ++i) {
    for (int j=0; j<4; ++j) {
      carTracker[i][j] = 0;
    }
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
  cv_destroy(northCv);
  cv_destroy(southCv);
  cv_destroy(eastCv);
  cv_destroy(westCv);

  lock_destroy(mainLk);
  printQueueDetails(carQueue, "inspect");
  // q_destroy(carQueue);
}


struct cv *getDestCv (Direction destination) {
  struct cv *toreturn;
  switch (destination) {
    case north:
      toreturn = northCv;
      break;
    case east:
      toreturn = eastCv;
      break;
    case south:
      toreturn = southCv;
      break;
    case west:
      toreturn = westCv;
      break;
  }
  return toreturn;
}


// 1 for straight, 2 for right, 3 for left
int getPathType(Direction origin, Direction destination) {
  if ((((int)destination - (int)origin) % 2) == 0) {
    return 1;
  } else if ((int)destination - (int)origin == -1 || (int)destination - (int)origin == 3) {
    return 2;
  } else {
    return 3;
  }
}


Direction plusXdir(Direction target, int plusx) {
  Direction dir = (target + plusx) % 4;
  return dir;
}

struct cv *plusXcv(Direction target, int plusx) {
  Direction dir = (target + plusx) % 4;
  return getDestCv(dir);
}

// 10 means its unfilled
unsigned int directionFilledBy(Direction target) {
  if (carTracker[0][target] > 0) {
    return 0;
  } else if (carTracker[1][target] > 0) {
    return 1;
  } else if (carTracker[2][target] > 0) {
    return 2;
  } else if (carTracker[3][target] > 0) {
    return 3;
  } else {
    return 10;
  }
}

bool directionFilled(Direction target) {
  return (carTracker[0][target] > 0 || carTracker[1][target] > 0 || carTracker[2][target] > 0 || carTracker[3][target] > 0);
}


struct cv *blocksFirstQueue(Direction origin, Direction destination) {
  if (!q_empty(carQueue)) {
    struct vehicle *firstCar = (struct vehicle *)q_peek(carQueue);
    Direction fo = firstCar->origin;
    Direction fd = firstCar->destination;
    if (!((fo == origin) || (fd == origin && fo == destination) || 
        (fd != destination && (getPathType(fo, fd) == 2 || getPathType(origin, destination) == 2)))) {
      return getDestCv(fd);
    } 
  }

  return NULL;
}


struct cv *waitOnCv(Direction origin, Direction destination) {
  int type = getPathType(origin, destination);

  struct cv *thisCv = getDestCv(destination);

  Direction plus1dir = plusXdir(destination, 1);
  struct cv *plus1cv = plusXcv(destination, 1);

  Direction plus2dir = plusXdir(destination, 2);
  struct cv *plus2cv = plusXcv(destination, 2);

  Direction plus3dir = plusXdir(destination, 3);
  struct cv *plus3cv = plusXcv(destination, 3);

  struct cv *toreturn = NULL;
  
  if (directionFilled(destination) && directionFilledBy(destination) != origin) {
    // kprintf("Triggered 00\n");
    toreturn = thisCv;
  } else if (type == 1) {
    if (directionFilled(plus1dir) && directionFilledBy(plus1dir) != origin) {
      // kprintf("Triggered 11\n");
      toreturn = plus1cv;
    } else if (directionFilled(plus2dir) && directionFilledBy(plus2dir) != destination && directionFilledBy(plus2dir) != plus3dir) {
      // kprintf("Triggered 12\n");
      toreturn = plus2cv;
    } else if (directionFilled(plus3dir) && directionFilledBy(plus3dir) != destination && directionFilledBy(plus3dir) != origin) {
      // kprintf("Triggered 13\n");
      toreturn = plus3cv;
    }
  } else if (type == 3) {
    if (directionFilled(plus1dir) && directionFilledBy(plus1dir) != origin && directionFilledBy(plus1dir) != plus2dir) {
      // kprintf("Triggered 31\n");
      toreturn = plus1cv;
    } else if (directionFilled(plus2dir) && directionFilledBy(plus2dir) != origin) {
      // kprintf("Triggered 32\n");
      toreturn = plus2cv;
    } else if (directionFilled(plus3dir) && directionFilledBy(plus3dir) != destination) {
      // kprintf("Triggered 33\n");
      toreturn = plus3cv;
    }
  } else {
    toreturn = blocksFirstQueue(origin, destination);
  }
  return toreturn;
}

void removeFromQueue(struct vehicle *toremove) {
  struct queue *change = carQueue1;
  if (change == carQueue) {
    change = carQueue2;
  }
  while (!q_empty(carQueue)) {
    struct vehicle *removed = (struct vehicle *)q_remhead(carQueue);
    if (removed != toremove) {
      q_addtail(change, removed);
    }
  }

  carQueue = change;
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

  lock_acquire(mainLk);
  // kprintf("PRE ENTER: %d, %d\n", origin , destination);
  struct cv *testCv = waitOnCv(origin, destination);
  struct vehicle *toadd = (struct vehicle *)kmalloc(sizeof(struct vehicle));
  toadd->origin = origin;
  toadd->destination = destination;
  bool waited = false;
  if (testCv != NULL) {
    waited = true;
    q_addtail(carQueue, toadd);
    // printQueueDetails(carQueue, "add tail");
  }

  while (testCv != NULL) {
    cv_wait(testCv, mainLk);
    // printQueueDetails(carQueue, "after wait");
    testCv = waitOnCv(origin, destination);
  }

  // kprintf("ENTERING: %d, %d\n", origin , destination);
  if (!q_empty(carQueue) && ((struct vehicle *)q_peek(carQueue)) == toadd) {
    // kprintf("MATCH ADDRESS WITH FRONT: %d, %d\n", origin , destination);
    struct vehicle *thisRem = (struct vehicle *)q_remhead(carQueue);
    kfree(thisRem);
  } else if (waited) {
    // kprintf("IN QUEUE BUT NOT FRONT: %d, %d\n", origin , destination);
    removeFromQueue(toadd);
    kfree(toadd);
  } else {
    // kprintf("NOT PART OF QUEUE\n");
    kfree(toadd);
  }
  
  carTracker[origin][destination] += 1;
  // printQueueDetails(carQueue, "after ENTRANCE");

  lock_release(mainLk);
  
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

  lock_acquire(mainLk);
  struct cv *destCv = getDestCv(destination);
  // kprintf("LEAVING: %d, %d\n", origin , destination);
  KASSERT(carTracker[origin][destination] > 0);
  carTracker[origin][destination] -= 1;

  if (carTracker[origin][destination] == 0) {  
    cv_broadcast(destCv, mainLk);
  }
  lock_release(mainLk);
}
