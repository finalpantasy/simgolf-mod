#include "port/ui/ui_dialog_text.h"

#include <stdio.h>
#include <string.h>

static int digit(char c) { return c >= '0' && c <= '9'; }

static int parse_number(const char **p, int min_digits, int max_digits, int *out) {
  int n = 0, v = 0;
  while (digit(**p) && n < max_digits) {
    v = v * 10 + (**p - '0');
    ++*p;
    ++n;
  }
  if (n < min_digits || digit(**p)) return 0;
  *out = v;
  return 1;
}

static int leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

int ui_dialog_format_date_hint(const char *src, char *dst, size_t cap) {
  static const char *const months[12] = {"January", "February", "March", "April", "May", "June",
                                         "July", "August", "September", "October", "November", "December"};
  if (!src || !dst || !cap) return 0;
  const char *p = src;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  int month = 0, day = 0, year = 0;
  if (!parse_number(&p, 1, 2, &month)) return 0;
  char sep = *p++;
  if (sep != '\'' && sep != '/' && sep != '-') return 0;
  if (!parse_number(&p, 1, 2, &day) || *p++ != sep || !parse_number(&p, 4, 4, &year)) return 0;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  if (*p || month < 1 || month > 12 || year < 1) return 0;
  static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int max_day = days[month - 1] + (month == 2 && leap(year));
  if (day < 1 || day > max_day) return 0;
  char formatted[40];
  int n = snprintf(formatted, sizeof formatted, "%s %d, %04d", months[month - 1], day, year);
  if (n < 0 || (size_t)n >= sizeof formatted || (size_t)n >= cap) return 0;
  memcpy(dst, formatted, (size_t)n + 1);
  return 1;
}

const char *ui_dialog_choice_label(const char *const *items, int count, int choice) {
  if (!items || count != 3 || choice != 2 || !items[0] || !items[1] || !items[2]) return NULL;
  if (strcmp(items[0], "Wait, I don't want to quit yet!") == 0 &&
      strcmp(items[1], "I'd like to save this game first.") == 0 &&
      strcmp(items[2], "So long for now.") == 0)
    return "Return to Main Menu";
  return NULL;
}

float ui_dialog_hint_width(float text_width, float pad, float portrait_width, float portrait_gap,
                          float min_width, float max_width, float available_width) {
  if (available_width <= 0) return 0;
  float low = min_width < 0 ? 0 : min_width;
  float high = max_width < 0 ? 0 : max_width;
  if (low > available_width) low = available_width;
  if (high > available_width) high = available_width;
  if (high < low) low = high;
  float natural = text_width + 2 * pad;
  if (portrait_width > 0) natural += portrait_width + portrait_gap;
  if (natural < low) return low;
  if (natural > high) return high;
  return natural;
}
