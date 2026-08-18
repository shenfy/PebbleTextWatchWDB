#pragma once

#include <stddef.h>

void time_to_3words(int hours, int minutes, char *first, char *second,
                    char *third, size_t line_size);
