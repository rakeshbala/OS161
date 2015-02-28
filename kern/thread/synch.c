/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, int initial_count)
{
        struct semaphore *sem;

        KASSERT(initial_count >= 0);

        sem = kmalloc(sizeof(struct semaphore));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 * Bridge to the wchan lock, so if someone else comes
		 * along in V right this instant the wakeup can't go
		 * through on the wchan until we've finished going to
		 * sleep. Note that wchan_sleep unlocks the wchan.
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_lock(sem->sem_wchan);
		spinlock_release(&sem->sem_lock);
                wchan_sleep(sem->sem_wchan);

		spinlock_acquire(&sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(struct lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

        /************ RB:Allocate wchan************/
        lock->lock_wchan = wchan_create(lock->lk_name);
        if (lock->lock_wchan == NULL) {
            kfree(lock->lk_name);
            kfree(lock);
            return NULL;
        }
        /************ RB:Init spin locks ************/
        spinlock_init(&lock->lock_lk);

        lock->hold_count = 0;
        lock->lock_holder = NULL;
        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        // add stuff here as needed
        spinlock_cleanup(&lock->lock_lk);
        wchan_destroy(lock->lock_wchan);
        kfree(lock->lk_name);
        kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
        // Write this
        // (void)lock;  // suppress warning until code gets written
        /************ RB:Acquire lock before assigning holder ************/
        KASSERT(lock != NULL);


        spinlock_acquire(&lock->lock_lk);
        /************ RB:Go to sleep if already acquired ************/
        while(lock->lock_holder != NULL)
        {
            /************ RB:Make it recursive ************/
            if (lock_do_i_hold(lock)){
                lock->hold_count++;
                spinlock_release(&lock->lock_lk);
                return;
            }
            wchan_lock(lock->lock_wchan);
            /************ RB:Relase lock before sleeping ************/
            spinlock_release(&lock->lock_lk);
                wchan_sleep(lock->lock_wchan);
            spinlock_acquire(&lock->lock_lk);
        }
        lock->lock_holder = curthread;
        lock->hold_count++;
        spinlock_release(&lock->lock_lk);

}

void
lock_release(struct lock *lock)
{
        // Write this
        /************ RB:Lock and NULLify the holder field ************/
        KASSERT(lock != NULL);


        spinlock_acquire(&lock->lock_lk);
        if (lock_do_i_hold(lock))
        {
            /************ RB:Recursive ************/
            lock->hold_count--;
            if (lock->hold_count >0)
            {
                spinlock_release(&lock->lock_lk);
                return;
            }
        }
        lock->lock_holder = NULL;
        wchan_wakeone(lock->lock_wchan);
        spinlock_release(&lock->lock_lk);

        // (void)lock;  // suppress warning until code gets written
}

bool
lock_do_i_hold(struct lock *lock)
{
        // Write this
        if (lock->lock_holder == curthread)
        {
            return true;
        }else{
            return false;
        }

        // (void)lock;  // suppress warning until code gets written

        // return true; // dummy until code gets written
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(struct cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }

        /*********** RR: creating wchan for CV ***********/

        cv->cv_wchan = wchan_create(cv->cv_name);
        if(cv->cv_wchan == NULL) {
            kfree(cv->cv_name);
            kfree(cv);
            return NULL;
        }
        return cv;
}

void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        /*********** RR: free wchan memory ***********/
        wchan_destroy(cv->cv_wchan);
        kfree(cv->cv_name);
        kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{

    /***********************************************
    RR: lock the wchan,release the lock passed,
    sleep on wchan,when woken up, reacquire the lock
    ************************************************/
    KASSERT(lock != NULL);
    KASSERT(cv != NULL);
    wchan_lock(cv->cv_wchan);
    lock_release(lock);
    wchan_sleep(cv->cv_wchan);
    lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    /****************************************************
    RR: acquire lock on wchan, wake one signal,
    release lock on wchan
    *****************************************************/
    KASSERT(lock != NULL);
    KASSERT(cv != NULL);
    KASSERT(curthread == lock->lock_holder);
    wchan_wakeone(cv->cv_wchan);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	/******************************************************
    RR: acquire lock on wchan, wake all signals,
    release lock on wchan
    *******************************************************/
    KASSERT(lock != NULL);
    KASSERT(cv != NULL);
    KASSERT(curthread == lock->lock_holder);
    wchan_wakeall(cv->cv_wchan);
}


/************ RB: Read write locks ************/
/* Ref: http://jhshi.me/2013/04/05/os161-synchronization-primitives-rwlock/ */
struct rwlock * rwlock_create(const char * name){
        struct rwlock * rw_lock;

        rw_lock = kmalloc(sizeof(struct rwlock));
        if (rw_lock == NULL)
        {
            return NULL;
        }

        rw_lock->rwlock_name = kstrdup(name);
        if (rw_lock->rwlock_name == NULL)
        {
            kfree(rw_lock);
            return NULL;
        }
        rw_lock->rw_maxread=5;;
        rw_lock->rwlock_sem = sem_create(rw_lock->rwlock_name, rw_lock->rw_maxread);
        if (rw_lock->rwlock_sem == NULL)
        {
            kfree(rw_lock->rwlock_name);
            kfree(rw_lock);
            return NULL;
        }

        // rw_lock->rwlock_wchan = wchan_create(rw_lock->rwlock_name);
        // if (rw_lock->rwlock_wchan == NULL)
        // {
        //     sem_destroy(rw_lock->rwlock_sem);
        //     kfree(rw_lock->rwlock_name);
        //     kfree(rw_lock);
        //     return NULL;
        // }

        rw_lock->rwlock_lock = lock_create(rw_lock->rwlock_name);
        if (rw_lock->rwlock_lock == NULL)
        {
            sem_destroy(rw_lock->rwlock_sem);
            kfree(rw_lock->rwlock_name);
            kfree(rw_lock);
            return NULL;
        }



        return  rw_lock;
}
void rwlock_destroy(struct rwlock * rwlk){

        KASSERT(rwlk != NULL);
        kfree(rwlk->rwlock_name);
        sem_destroy(rwlk->rwlock_sem);
        lock_destroy(rwlk->rwlock_lock);
        kfree(rwlk);
}

void rwlock_acquire_read(struct rwlock * rwlk){
        lock_acquire(rwlk->rwlock_lock);
        P(rwlk->rwlock_sem);
        lock_release(rwlk->rwlock_lock);
}
void rwlock_release_read(struct rwlock * rwlk){
        V(rwlk->rwlock_sem);
}
void rwlock_acquire_write(struct rwlock * rwlk){
        lock_acquire(rwlk->rwlock_lock);
        for (int i = 0; i < rwlk->rw_maxread; ++i)
        {
            P(rwlk->rwlock_sem);
        }
        lock_release(rwlk->rwlock_lock);

}
void rwlock_release_write(struct rwlock * rwlk){
        for (int i = 0; i < rwlk->rw_maxread; ++i)
        {
            V(rwlk->rwlock_sem);
        }
}
