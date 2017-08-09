#pragma once

#if defined(COSIX_USERLAND)
#include <ctype.h>
#else

int
isupper(int c);

int
islower(int c);

int
isalpha(int c);

int
isdigit(int c);

#endif
