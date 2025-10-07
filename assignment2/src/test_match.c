// match_tests.c
#include <stdio.h>

/* =========================
   Matcher: regex-style x* only
   - '*' repeats the preceding character (zero or more)
   - No () or ?
   - match(s,p) returns 1 if ANY substring of s matches p
   - returns 0 otherwise
   ========================= */

static int submatch(const char *s, const char *p){
  while (*s != '\0' && *p != '\0'){
    if(*p == '*') return 0;                 
    if (*(p + 1) == '*'){ 
      const char c = *p;                  // remember the preceding character
      const char *rest = p + 2;           // skip over 'x*'

      if (submatch(s, rest)) return 1;    // if zero repetition of p is a match, return true

      while (*s == c) {                   // check if one or more repetition of c is a match
        s++;
        if (submatch(s, rest)) return 1;
      }
      return 0;
    }
    else if (*s != *p) return 0;          // if it's not a star case: must match literally, return 0
    else {
      s++;
      p++;
    }
  }
  while (*(p + 1) == '*') p += 2;           // handling trailing x*

  return (*p == '\0');
}

static int match(const char *str, const char *pattern){
  if (*pattern == '*') return 0;            //if it starts with *, return false
  do {
      if (submatch(str, pattern)) return 1;
  } while (*str++);
  return 0;
}

/* =========================
   Test harness
   ========================= */

#define CHECK(S,P,EXP) do { \
    int got = match((S),(P)); \
    total++; \
    passed += (got == (EXP)); \
    printf("%s match(\"%s\", \"%s\") -> %s (expected %s)\n", \
        (got==(EXP)) ? "[ OK ]" : "[FAIL]", \
        (S), (P), got ? "true" : "false", (EXP) ? "true" : "false"); \
} while (0)

int main(void) {
    int total = 0, passed = 0;

    // 1) Empty / trivial
    CHECK("",     "",     1);
    CHECK("a",    "",     1);
    CHECK("",     "a",    0);

    // 2) Plain literals (no stars), substring semantics
    CHECK("hello", "he",    1);
    CHECK("hello", "ell",   1);
    CHECK("hello", "lo",    1);
    CHECK("hello", "world", 0);

    // 3) x* (repeat previous char), substring semantics
    CHECK("abbbc",    "ab*c",  1);
    CHECK("xxabbbc",  "ab*c",  1);
    CHECK("abXc",     "ab*c",  0);

    // zero-or-more cases
    CHECK("ac",       "ab*c",  1);
    CHECK("ab",       "ab*",   1);
    CHECK("zzz",      "z*",    1);
    CHECK("zzza",     "z*",    1);  // substring "zzz" matches

    // 4) Multiple starred atoms
    CHECK("aaabbb",   "a*b*",   1);
    CHECK("abba",     "a*b*",   1); // substring "abb" matches
    CHECK("bbaaa",    "a*b*",   1); // a*=0, b* matches "bb"
    CHECK("xyz",      "a*b*",   1); // a*=0, b*=0 (empty) matches somewhere
    CHECK("ccccc",    "a*b*c*", 1); // c* consumes run

    // 5) Invalid patterns for x* language (lone '*' or '**' -> expect 0)
    CHECK("abc",      "*abc",  0);
    CHECK("abc",      "a**bc", 0);
    CHECK("abc",      "ab**",  0);

    // 6) Mismatches
    CHECK("abdc",     "ab*c",  0);
    CHECK("aaaa",     "aaab",  0);
    CHECK("cabbbcd",  "ab*cd", 1);  // substring "abbbcd" matches

    // 7) Matches at different positions
    CHECK("xxacxx",   "ab*c",  1);  // matches at pos 2 ("ac")
    CHECK("xxabxx",   "ab*",   1);  // matches at pos 2 ("ab")
    CHECK("xxxx",     "y*",    1);  // y* matches empty at any position

    // 8) Distinguish from full-string matching
    CHECK("abca",     "ab*c",  1);  // substring "abc" matches
    CHECK("aaaab",    "a*a",   1);  // substring "aaaa" matches "a*a"

    printf("\nSummary: %d/%d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}