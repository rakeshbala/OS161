/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code for whale mating problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>
#include <wchan.h>
/*
 * 08 Feb 2012 : GWA : Driver code is in kern/synchprobs/driver.c. We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.

 /************ RB: Make the match here ************/
static struct cv *maleCv;
static struct cv *femaleCv;
static struct cv *matchMakerCv;
static struct lock *whaleLock;

void whalemating_init() {
  maleCv = cv_create("male");
  femaleCv = cv_create("female");
  matchMakerCv = cv_create("matchMaker");
  whaleLock = lock_create("whaleLock");
  return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void whalemating_cleanup() {
  lock_destroy(whaleLock);
  cv_destroy(maleCv);
  cv_destroy(femaleCv);
  cv_destroy(matchMakerCv);
  return;
}


void
male(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;

  male_start();
  lock_acquire(whaleLock);
	// Implement this function
  if (wchan_isempty(femaleCv->cv_wchan) || wchan_isempty(matchMakerCv->cv_wchan))
  {
    cv_wait(maleCv,whaleLock);
  }else{
   cv_signal(femaleCv,whaleLock);
   cv_signal(matchMakerCv,whaleLock);
  }
  lock_release(whaleLock);
  male_end();

  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}

void
female(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;

  female_start();
	// Implement this function
   lock_acquire(whaleLock);
  // Implement this function
  if (wchan_isempty(maleCv->cv_wchan) || wchan_isempty(matchMakerCv->cv_wchan))
  {
      cv_wait(femaleCv,whaleLock);
  }else{
       cv_signal(maleCv,whaleLock);
       cv_signal(matchMakerCv,whaleLock);
  }
  lock_release(whaleLock);
  female_end();

  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}





void
matchmaker(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;

  matchmaker_start();
   lock_acquire(whaleLock);
  if (wchan_isempty(femaleCv->cv_wchan) || wchan_isempty(maleCv->cv_wchan))
  {
      cv_wait(matchMakerCv,whaleLock);
  }else{
       cv_signal(femaleCv,whaleLock);
       cv_signal(maleCv,whaleLock);
  }
  lock_release(whaleLock);
  matchmaker_end();

  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}

/*
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is,
 * of course, stable under rotation)
 *
 *   | 0 |
 * --     --
 *    0 1
 * 3       1
 *    3 2
 * --     --
 *   | 2 |
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X
 * first.
 *
 * You will probably want to write some helper functions to assist
 * with the mappings. Modular arithmetic can help, e.g. a car passing
 * straight through the intersection entering from direction X will leave to
 * direction (X + 2) % 4 and pass through quadrants X and (X + 3) % 4.
 * Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in drivers.c.
 */
typedef enum{
  SL_LEFT,
  SL_RIGHT,
  SL_STRAIGHT
} SL_DIRECTION;

 static struct lock *zero;
 static struct lock *one;
 static struct lock *two;
 static struct lock *three;
 static struct lock *big_lock;
 void move_to_quadrants(SL_DIRECTION turn, unsigned long direction);
 void release_quadrants(SL_DIRECTION turn, unsigned long direction);
 void lock_quadrants(SL_DIRECTION turn, unsigned long direction);
 struct lock *get_lock_from_number(int number);
// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.

void stoplight_init() {
  zero = lock_create("zero");
  one = lock_create("one");
  two = lock_create("two");
  three = lock_create("three");
  big_lock = lock_create("big");
  return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void stoplight_cleanup() {
  lock_destroy(zero);
  lock_destroy(one);
  lock_destroy(two);
  lock_destroy(three);
  lock_destroy(big_lock);
  return;
}

void
gostraight(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
  (void)direction;

  lock_acquire(big_lock);
  lock_quadrants(SL_STRAIGHT,direction);
  lock_release(big_lock);
  move_to_quadrants(SL_STRAIGHT,direction);
  leaveIntersection();
  release_quadrants(SL_STRAIGHT,direction);
  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}

void
turnleft(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
  (void)direction;


  lock_acquire(big_lock);
  lock_quadrants(SL_LEFT,direction);
  lock_release(big_lock);
  move_to_quadrants(SL_LEFT,direction);
  leaveIntersection();
  release_quadrants(SL_LEFT,direction);
  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}

void
turnright(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
  (void)direction;


  lock_acquire(big_lock);
  lock_quadrants(SL_RIGHT,direction);
  lock_release(big_lock);
  move_to_quadrants(SL_RIGHT,direction);
  leaveIntersection();
  release_quadrants(SL_RIGHT,direction);
  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}

void
lock_quadrants(SL_DIRECTION turn, unsigned long direction){
  switch (turn) {
    case SL_LEFT:{
       struct lock * first,*second,*third;
       first = get_lock_from_number(direction);
       second = get_lock_from_number((direction+3)%4);
       third = get_lock_from_number((direction + 2)%4);
       lock_acquire(first);
       lock_acquire(second);
       lock_acquire(third);
       break;
    }
    case SL_RIGHT:{
      struct lock *me = get_lock_from_number(direction);
      lock_acquire(me);
      break;
    }
    case SL_STRAIGHT:{
      struct lock *first,*second;
      first = get_lock_from_number(direction);
      second = get_lock_from_number((direction+3)%4);
      lock_acquire(first);
      lock_acquire(second);
      break;
    }
    default: break;
  }
}

void
move_to_quadrants(SL_DIRECTION turn, unsigned long direction)
{
  switch (turn) {
    case SL_LEFT:{
      inQuadrant(direction);
      inQuadrant((direction+3)%4);
      inQuadrant((direction + 2)%4);
      break;
    }
    case SL_RIGHT:{
      inQuadrant(direction);
      break;
    }
    case SL_STRAIGHT:{
      inQuadrant(direction);
      inQuadrant((direction+3)%4);
      break;
    }
    default: break;
  }
}

void
release_quadrants(SL_DIRECTION turn, unsigned long direction)
{
  switch (turn) {
    case SL_LEFT:{
       struct lock * first, *second, *third;
       first = get_lock_from_number(direction);
       second = get_lock_from_number((direction+3)%4);
       third = get_lock_from_number((direction + 2)%4);
       lock_release(first);
       lock_release(second);
       lock_release(third);
       break;
    }
    case SL_RIGHT:{
      struct lock *me = get_lock_from_number(direction);
      lock_release(me);
      break;
    }
    case SL_STRAIGHT:{
      struct lock *first, *second;
      first = get_lock_from_number(direction);
      second = get_lock_from_number((direction+3)%4);
      lock_release(first);
      lock_release(second);
      break;
    }
    default: break;
  }
}

struct lock *get_lock_from_number(int number)
{
  switch (number){
    case 0: return zero;
    case 1: return one;
    case 2: return two;
    case 3: return three;
    default: return NULL;
  }
}
