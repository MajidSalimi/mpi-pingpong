#ifndef TIME_SUB_H
#define TIME_SUB_H

#define NANOS (1000*1000*1000L)

#define TIME_UNITS_S  0
#define TIME_UNITS_MS 1
#define TIME_UNITS_US 2
#define TIME_UNITS_NS 3

long timespec_to_nsec(const struct timespec *ts) {
	return ts->tv_sec * NANOS + ts->tv_nsec;
}

double nsec_to_double(long nsec, int units) {
    return (double)nsec / (
        units == TIME_UNITS_S  ? 1000*1000*1000 : 
        units == TIME_UNITS_MS ? 1000*1000 :
        units == TIME_UNITS_US ? 1000 :
        1
    );
}

int timespec_subtract(struct timespec * result, const struct timespec *x, const struct timespec *y)
{
	long nsec_x = timespec_to_nsec(x);
	long nsec_y = timespec_to_nsec(y);
	long nsec_diff = nsec_x - nsec_y;

	result->tv_sec = nsec_diff / NANOS;
	result->tv_nsec = nsec_diff % NANOS;

	/* Return 1 if result is negative. */
	return nsec_diff < 0 ? 1 : 0;
}

#endif // TIME_SUB_H
