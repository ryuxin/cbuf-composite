#include <pthread.h>

extern int printc(const char *fmt, ...);

int pthread_once(pthread_once_t *c, void (*init)(void))
{
	printc("pthread_once not implemented!\n");
	return 0;
}

int pthread_mutex_init(pthread_mutex_t *__restrict m, const pthread_mutexattr_t *__restrict a)
{
	//printc("pthread_mutex_init not implemented!\n");
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
	//printc("pthread_mutex_lock not implemented!\n");
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
	//printc("pthread_mutex_unlock not implemented!\n");
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
	printc("pthread_mutex_trylock not implemented!\n");
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
	//printc("pthread_mutex_destroy not implemented!\n");
	return 0;
}

int pthread_key_create(pthread_key_t *k, void (*dtor)(void *))
{
	printc("pthread_key_create not implemented!\n");
	return 0;
}

void *pthread_getspecific(pthread_key_t k)
{
	printc("pthread_getspecific not implemented!\n");
	return NULL;
}

int pthread_setspecific(pthread_key_t k, const void *x)
{
	printc("pthread_setspecific not implemented!\n");
	return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *a)
{
	//printc("pthread_mutexattr_init not implemented!\n");
	return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a)
{
	printc("pthread_mutexattr_destroy not implemented!\n");
	return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type)
{
	printc("pthread_mutexattr_settype not implemented!\n");
	return 0;
}


