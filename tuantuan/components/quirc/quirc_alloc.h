#pragma once
#include <stdlib.h>
void *companion_quirc_malloc(size_t size);
void *companion_quirc_calloc(size_t count,size_t size);
#define malloc companion_quirc_malloc
#define calloc companion_quirc_calloc
