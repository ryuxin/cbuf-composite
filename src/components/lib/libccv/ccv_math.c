#include <limits.h>
#include <assert.h>
#include "ccv_math.h"

/*

code comes from http://www.cs.tut.fi/~jkorpela/round.html

*/
long
lround(double x)
{
      assert(x >= LONG_MIN-0.5);
      assert(x <= LONG_MAX+0.5);
      if (x >= 0)
         return (long) (x+0.5);
      return (long) (x-0.5);
}

double
round(double x)
{
      assert(x >= LONG_MIN-0.5);
      assert(x <= LONG_MAX+0.5);
      if (x >= 0)
         return x+0.5;
      return x-0.5;
}
