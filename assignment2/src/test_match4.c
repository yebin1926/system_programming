// ====== TESTS FOR match() supporting literals, x*, (group) and (group)* ======
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>

const char *find_close(const char *p) { //function that returns pointer to closing bracket )
  int depth = 1;                      // start when seeing one '('
  for (p = p + 1; *p; p++) {
      if (*p == '(') depth++;
      else if (*p == ')') {
          depth--;
          if (depth == 0) return p;   // if all ( are closed with ), return that pointer
      }
  }
  return NULL;                        // no match
}

const char *check_repetition_match(const char *s, const char *p, int unit_len)
{
  printf("Check Repetition: %c, %c\n", *s, *p);
    const char *end_of_run = s;                     // end of run: how far we've scanned so far within s
    int run_count = 0;                              // how many repetitions we've been through
    while(1) {
        int matches_count = 0;                      // counter for how many matches there were so far
        const char *s_current = end_of_run;         // current position within s
        const char *p_current = p;                  // current position within p
        // Try to match one full copy of the unit
        while (matches_count < unit_len && *s_current && *p_current && (*s_current == *p_current || *p_current == '?')) {  //if we haven't matched all of the pattern yet, but we still have remaining characters to match, continue
          ++matches_count;
          ++s_current;
          ++p_current;
        }
        if (matches_count == unit_len) {            //if all were matched, move end_of_run to check for the next copy
          run_count++;
          end_of_run = s_current;
        } else {
          if(matches_count != 0 && run_count == 0){
            printf("***no match***\n");
            return NULL;
          }
          return end_of_run;                      //else, that was the maximum matches we could find, so return
        }
    }
}

static int submatch(const char *s, const char *p, int is_only_group){
  printf("Submatch for: %c, %c, %d\n", (char)*s, (char)*p, is_only_group);
  while (*p != '\0'){       

    if (*s == '\0') {
      // if search keyword is "", and if pattern can be skipped (like (abc)* or x* repeated), treat as match. Else, return 0
      while (*p) {
          // if (*(p + 1) == '*') p += 2;
          if (*p == '*') { p += 1; }
          else if (*p == '(') {
              const char *close = find_close(p);
              if (close && *(close + 1) == '*')
                  p = close + 2; // skip (group)*
              else
                  return 0;
          } else
              return 0;
      }
    return 1;
    }   

    if (*p == '*') {
      if(*(p+1) == '*') return 2;                       //two *s in a row - invalid
      if (submatch(s, p + 1, is_only_group)) return 1;  //if 
      // try consuming one character and stay on '*'
      return (*s && submatch(s + 1, p, is_only_group)) ? 1 : 0;
    } else if (*(p + 1) == '*') {
      char c = *p;                 // repeat this literal
      const char *rest = p + 2;    // pattern after the 'x*'

      // try zero copies
      if (submatch(s, rest, is_only_group)) return 1;

      // try one-or-more copies
      while (*s == c) {
        s++;
        if (submatch(s, rest, is_only_group)) return 1;
      }
      return 0;
    }
    else if (*p == '('){
      //endless repetition here
      const char* p_closed = find_close(p);        // find pointer to closing braket )
      if (p_closed == NULL) return 0; 
      int len = (int)(p_closed - (p + 1));   // get size of substring
      if (len <= 0) {
        // Empty group like "()" is invalid → not a match
        return 0;
      }
      const char *after = p_closed + 1;            // first char after ')'

      printf("Detected (, p: %c\tp_closed: %c\tlen: %d\n", (char)*p, (char)*p_closed, len);
      if (*after == '*'){
        printf("Detected * after )\n");
        const char *end = check_repetition_match(s, p + 1, len); // consume (group)*
        if(end == NULL) return 2;
        const char next = *(p_closed + 2);
        if (next && next != '(' && next != ')' && next != '*' && next != '?') {
            if (*end != next) return 0;
        }
        return submatch(end, p_closed + 2, is_only_group);       // continue after the '*'
      } else{
        //what should i put here
        int k = 0;
        const char *ts = s, *tp = p + 1;               // compare inner literally
        while (k < len && *ts && *tp && (*tp == '?' || *ts == *tp)) { k++; ts++; tp++; }
        if (k != len) return 0;                        // inner didn't match once

        // advance both: we consumed the group once
        s = ts;                                        // move input past inner
        p = after;                                     // move pattern past ')'
        continue;                                      // continue the while-loop in submatch
      }
    } else if (*s != *p && *p != '?') return 0;          // if it's not a star case: must match literally or be '?'. If not, return 0
    else {
      s++;
      p++;
    }
  }
  while (*p == '*') p += 1;           // handling trailing x*

  return (*p == '\0');  //if p = "", return true
}

static int match(const char *str, const char *pattern){
  if (*pattern == '*') return 0;            //if it starts with *, return false
  //TODO: print invalid pattern syntax
  for (const char *q = pattern; *q; ++q) {
    if (*q == '*' && q[1] == '*') return 0;
  }
  //check if the whole search keyword is a group
  const char* p_closed = find_close(pattern);
  //const char *after = p_closed + 1;
  int is_only_group = 0;
  if (*pattern == '(') {
    p_closed = find_close(pattern);        // may be NULL for unbalanced '('
    if (p_closed && *(p_closed + 1) == '*' && *(p_closed + 2) == '\0') {
      is_only_group = 1;
    }
  }
  if(is_only_group && *str == '\0'){
    return 1;
  }
  do {
    int result = submatch(str, pattern, is_only_group);
    switch(result){
      case 1:
        return 1;
      case 2:
        return 0;
      default:
        break;
    }
    // if (submatch(str, pattern, is_only_group)) return 1;
  } while (*str++);
  return 0;
}

#define CHECK(S,P,EXP) do { \
    int got = match((S),(P)); \
    total++; \
    passed += (got == (EXP)); \
    printf("%s  match(\"%s\", \"%s\") -> %s (expected %s)\n", \
        (got==(EXP)) ? "[ OK ]" : "[FAIL]", \
        (S), (P), got ? "true" : "false", (EXP) ? "true" : "false"); \
} while (0)

int main(void) {
    int total = 0, passed = 0;

/* 1) Empty / trivial */
    CHECK("",      "",         1);
    CHECK("a",     "",         1);  /* empty pattern matches at every position (partial) */
    CHECK("",      "a",        0);

    /* 2) Plain literals, partial basename match */
    CHECK("hello", "he",       1);
    CHECK("hello", "ell",      1);
    CHECK("hello", "lo",       1);
    CHECK("hello", "world",    0);

    /* 3) Single-character wildcard '?' (exactly one char) */
    CHECK("a",     "?",        1);
    CHECK("",      "?",        0);
    CHECK("ab",    "?",        1);
    CHECK("abc",   "a?c",      1);  /* 'b' consumed by '?' */
    CHECK("acc",   "a?c",      1);  /* 'c' consumed by '?' */
    CHECK("ac",    "a?c",      0);  /* too short for '?' */
    CHECK("xaZc",  "a?c",      1);  /* partial on substring "aZc" */

    /* 4) Kleene star '*' (zero or more of ANY chars, partial) */
    CHECK("a",       "a*",      1);
    CHECK("abbb",    "ab*",     1);
    CHECK("ac",      "ab*",     1);  /* zero b's */
    CHECK("xyz",     "*",       0);  /* matches anything */
    CHECK("abcbc",   "a*c",     1);  /* "a...c" */
    CHECK("ac",      "a*c",     1);
    CHECK("abbc",    "ab*c",    1);
    CHECK("abc",     "ab*c",    1);
    CHECK("ac",      "ab*c",    1);
    CHECK("abbbd",   "ab*c",    0);  /* no trailing c */
    CHECK("zzza",    "z*",      1);  /* substring "zzz" */

    /* 5) Groups '()' without repetition */
    CHECK("xabcx",   "(abc)",   1);  /* middle substring */
    CHECK("abc",     "(abc)",   1);
    CHECK("ab",      "(abc)",   0);
    CHECK("xabbc",   "(abc)",   0);

    /* 6) Group repetition '(...) *' */
    CHECK("",          "(abc)*",   1);  /* zero repeats matches empty substring */
    CHECK("abc",       "(abc)*",   1);
    CHECK("abcabc",    "(abc)*",   1);
    CHECK("xabcx",     "(abc)*",   1);  /* partial match on "abc" */
    CHECK("abca",      "(abc)*",   1);  /* "abc" matches; trailing 'a' ignored */
    CHECK("ab",        "(abc)*",   0);  /* incomplete group */

    /* 7) Groups mixed with literals */
    CHECK("xxabcdy",   "x(abc)*dy", 1);
    CHECK("xxdy",      "x(abc)*dy", 1); /* zero group repeats */
    CHECK("xabdy",     "x(abc)*dy", 0);

    // /* 8) Groups + '?' inside or next to them */
    // CHECK("abcXd",     "a(b?)d",    1);  /* 'b?' == 'b' + one arbitrary char -> matches 'cX' */
    // CHECK("abXd",      "a(b?)d",    1);  /* 'bX' fits */
    // CHECK("abd",       "a(b?)d",    0);  /* needs exactly one after 'b' */

    /* 9) '*' with '?' */
    CHECK("abcde",     "a*b?e",     0);  /* '*' eats "bc", '?' eats 'd' */
    CHECK("abe",       "a*b?e",     0);  /* '?' must consume one char */
    CHECK("axxce",     "a*c?e",     0);  /* '*' eats "xx", '?' eats 'c' */

    /* 10) Repetition of group next to literal */
    CHECK("abcabcX",   "(abc)*X",   1);
    CHECK("X",         "(abc)*X",   1);
    CHECK("abX",       "(abc)*X",   0);

    /* 11) Nested/adjacent groups (no true nesting if your code doesn’t support it; still good stress) */
    CHECK("ababcd",    "(ab)*(cd)", 1);  /* "(ab)" twice then "(cd)" */
    CHECK("cd",        "(ab)*(cd)", 1);  /* zero repeats of (ab) */
    CHECK("abacd",     "(ab)*(cd)", 0);  /* garbage 'a' before cd */

    /* 12) More mixes and edge-ish partials */
    CHECK("zzabzzczz", "ab*c",      0);  /* matches "abzzc" slice */
    CHECK("axxxbc",    "a*bc",      1);
    CHECK("xbc",       "a*bc",      1);  /* needs an 'a' before bc if '*' is after 'a' */
    CHECK("aQQQ",      "a??",       1);  /* exactly two chars after 'a' */
    CHECK("aQ",        "a??",       0);
    CHECK("Q",         "??",        0);
    CHECK("QQ",        "??",        1);

    /* 13) A couple of invalid patterns treated as non-matching (if you don’t have a validator) */
    CHECK("abc",      "*abc",     0);  /* leading '*' invalid -> expect non-match */
    CHECK("abc",      "a**bc",    0);  /* consecutive '*' invalid */
    CHECK("abc",      "ab**",     0);  /* consecutive '*' invalid */
    CHECK("x",        "(ab",      0);  /* unclosed group */
    CHECK("x",        "()",       0);  /* empty group (invalid) */

    printf("\nSummary: %d/%d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}