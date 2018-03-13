#ifndef __PROGRESSBAR_H
#define __PROGRESSBAR_H

#include <stdint.h>

typedef struct progress{
	const char* text;
	uint64_t count;
	uint64_t max;
}progress;

progress* start_progress(const char* text, uint64_t max);
void inc_progress(progress* p, uint64_t count);
void set_progress(progress* p, uint64_t count);
void finish_progress(progress* p);

#endif
