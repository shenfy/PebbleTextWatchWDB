#include "num2words-en.h"

#include <stdio.h>
#include <string.h>

static const char *const ONES[] = {
  "o'clock",
  "one",
  "two",
  "three",
  "four",
  "five",
  "six",
  "seven",
  "eight",
  "nine"
};

static const char *const TEENS[] = {
  "",
  "eleven",
  "twelve",
  "thirteen",
  "fourteen",
  "fifteen",
  "sixteen",
  "seventeen",
  "eighteen",
  "nineteen"
};

static const char *const TENS[] = {
  "",
  "ten",
  "twenty",
  "thirty",
  "forty",
  "fifty"
};

static void copy_word(char *destination, size_t destination_size,
                      const char *source) {
  if (destination_size == 0) {
    return;
  }

  size_t length = strlen(source);
  if (length >= destination_size) {
    length = destination_size - 1;
  }
  memcpy(destination, source, length);
  destination[length] = '\0';
}

static void split_long_teen(const char *word, char *second, char *third,
                            size_t line_size) {
  const char *suffix = strstr(word, "teen");
  if (!suffix) {
    copy_word(second, line_size, word);
    return;
  }

  size_t prefix_length = (size_t)(suffix - word);
  if (line_size > 0) {
    if (prefix_length >= line_size) {
      prefix_length = line_size - 1;
    }
    memcpy(second, word, prefix_length);
    second[prefix_length] = '\0';
  }
  copy_word(third, line_size, suffix);
}

void time_to_3words(int hours, int minutes, char *first, char *second,
                    char *third, size_t line_size) {
  if (line_size == 0) {
    return;
  }

  first[0] = '\0';
  second[0] = '\0';
  third[0] = '\0';

  int hour = hours % 12;
  if (hour == 0) {
    copy_word(first, line_size, TEENS[2]);
  } else if (hour < 10) {
    copy_word(first, line_size, ONES[hour]);
  } else if (hour == 10) {
    copy_word(first, line_size, TENS[1]);
  } else {
    copy_word(first, line_size, TEENS[1]);
  }

  if (minutes < 0 || minutes > 59) {
    return;
  }
  if (minutes == 0) {
    copy_word(second, line_size, ONES[0]);
    return;
  }
  if (minutes < 10) {
    snprintf(second, line_size, "o'%s", ONES[minutes]);
    return;
  }
  if (minutes == 10) {
    copy_word(second, line_size, TENS[1]);
    return;
  }
  if (minutes < 20) {
    const char *teen = TEENS[minutes % 10];
    if (minutes != 13 && strlen(teen) > 7) {
      split_long_teen(teen, second, third, line_size);
    } else {
      copy_word(second, line_size, teen);
    }
    return;
  }

  copy_word(second, line_size, TENS[minutes / 10]);
  if (minutes % 10 != 0) {
    copy_word(third, line_size, ONES[minutes % 10]);
  }
}
