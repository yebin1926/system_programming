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

const char *check_repetition_match(const char *s, const char *p, int substr_size){
  for(int i=0; i<substr_size; i++){
    printf("s: %c\tp: %c\t i: %d\tsize: %d\n", (char)*s, (char)*p, i, substr_size);
    if(*s != *p && i != 0){               //if they don't match
      return NULL;
    } else if (*s != *p && i == 0) {
      return s;
    }
    if((*s == *p) && i == (substr_size-1)){   //(first condition is actually not necessary)
      p -= (substr_size-1);
      i = -1;
    } else {
      p++;
    }
    s++;
  }
  return NULL;
}

static int submatch(const char *s, const char *p, int is_only_group){
  printf("Submatch for: %c, %c, %d\n", (char)*s, (char)*p, is_only_group);
  while (*p != '\0'){     
    if(*s == '\0') return 0;     
    if (*(p + 1) == '*'){ 
      const char c = *p;                  // remember the preceding character
      const char *rest = p + 2;           // skip over 'x*'

      if (submatch(s, rest, is_only_group)) return 1;    // if zero repetition of p is a match, return true

      while (*s == c) {                   // check if one or more repetition of c is a match
        s++;
        if (submatch(s, rest, is_only_group)) return 1;
      }
      return 0;
    }
    else if (*p == '('){
      //endless repetition here
      const char* p_closed = find_close(p);        // find pointer to closing braket )
      if (!p_closed) return 0; 
      int len = (int)(p_closed - (p + 1));   // get size of substring
      const char *after = p_closed + 1;            // first char after ')'

      printf("p: %c\tp_closed: %c\tlen: %d\n", (char)*p, (char)*p_closed, len);
      if (*after == '*'){
        // char *substr = malloc(len + 1);     // store substring in substr
        // if (!substr) { return 0;}
        // memcpy(substr, inner, len);
        //doesn't even get here

        if (check_repetition_match(s, p+1, len)){
          printf("ss: %c\tp_closed+2: %c\n", (char)*s, (char)*(p_closed+2));
          if(*(p_closed+2) == '\0' && *s != '\0' && is_only_group){
            return 0;
          }
          return submatch(s, (p_closed+2), is_only_group);
        } else{
          printf("****no match****\n");
          return 0;
        }
      } else{
        //what should i put here
        int k = 0;
        const char *ts = s, *tp = p + 1;               // compare inner literally
        while (k < len && *ts && *tp && *ts == *tp) { k++; ts++; tp++; }
        if (k != len) return 0;                        // inner didn't match once

        // advance both: we consumed the group once
        s = ts;                                        // move input past inner
        p = after;                                     // move pattern past ')'
        continue;                                      // continue the while-loop in submatch
      }
    } else if (*s != *p) return 0;          // if it's not a star case: must match literally, return 0
    else {
      s++;
      p++;
    }
  }
  while (*p && p[1] == '*') p += 2;           // handling trailing x*

  return (*p == '\0');
}

static int match(const char *str, const char *pattern){
  if (*pattern == '*') return 0;            //if it starts with *, return false
  //check if the whole search keyword is a group
  const char* p_closed = find_close(pattern);
  const char *after = p_closed + 1;
  int is_only_group = 0;
  if(*pattern == '(' && *p_closed == ')' && *after == '*' && *(after+1) == '\0'){
    is_only_group = 1;
  }
  if(is_only_group && *str == '\0'){
    return 1;
  }
  do {
      if (submatch(str, pattern, is_only_group)) return 1;
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

    // --- 1) Empty / trivial
    CHECK("",      "",         1);  // empty pattern matches empty suffix
    CHECK("a",     "",         1);  // empty pattern matches at every position
    CHECK("",      "a",        0);

    // --- 2) Plain literals
    CHECK("hello", "he",       1);
    CHECK("hello", "ell",      1);
    CHECK("hello", "lo",       1);
    CHECK("hello", "world",    0);

    // --- 3) x* (repeat preceding char)
    CHECK("abbbc", "ab*c",     1);  // many b's
    CHECK("ac",    "ab*c",     1);  // zero b's
    CHECK("ab",    "ab*",      1);  // trailing x* can be empty
    CHECK("zzza",  "z*",       1);  // substring "zzz" matches
    CHECK("abXc",  "ab*c",     0);  // wrong middle char

    // --- 4) Multiple starred atoms (still x* only; no groups)
    CHECK("aaabbb", "a*b*",    1);
    CHECK("abba",   "a*b*",    1);  // substring "abb" matches (substring semantics)
    CHECK("xyz",    "a*b*",    1);  // empty a*, empty b* can match empty substring
    CHECK("cccc",   "a*b*c*",  1);
    CHECK("abdc",   "ab*c",    0);  // 'd' breaks required run before 'c'

    // --- 5) Group: (abc) exactly once
    CHECK("xabcx",     "(abc)",    1);  // matches in middle
    CHECK("abc",       "(abc)",    1);
    CHECK("ab",        "(abc)",    0);  // incomplete
    CHECK("xabbc",     "(abc)",    0);  // wrong sequence

    // --- 6) Group repetition: (abc)*
    CHECK("",          "(abc)*",   1);  // zero repeats
    CHECK("abc",       "(abc)*",   1);  // one repeat
    CHECK("abcabc",    "(abc)*",   1);  // many repeats
    CHECK("abcx",      "(abc)*",   1);  // substring "abc" matches
    CHECK("abca",      "(abc)*",   1);  // substring "abc" matches, trailing 'a' ignored by match()
    CHECK("ab",        "(abc)*",   0);  // incomplete group can't match any substring

    // --- 7) Group + literals around it
    CHECK("xxabcdy",   "x(abc)*dy", 1); // matches "x" + "abc" + "dy"
    CHECK("xxdy",      "x(abc)*dy", 1); // zero repeats: "x" + "" + "dy"
    CHECK("xabdy",     "x(abc)*dy", 0); // "(abc)" doesn’t match "ab"

    // --- 8) Group + x* mix
    CHECK("aaabc",     "a*(abc)",   1); // many 'a' then "abc"
    CHECK("abc",       "a*(abc)",   1); // zero 'a' then "abc"
    CHECK("aaaa",      "a*(abc)",   0); // needs the "(abc)" after a*

    // --- 9) Repetition of group next to literal
    CHECK("abcabcX",   "(abc)*X",   1);
    CHECK("X",         "(abc)*X",   1);
    CHECK("abX",       "(abc)*X",   0);

    // --- 10) Nested-ish usage (optional; if your code doesn’t support nested (), skip these)
    // If your parser supports nested groups, keep these; otherwise set expected to 0:
    // CHECK("abab",      "((ab))*",  1);  // nested group
    // CHECK("ab",        "((ab))*",  1);
    // CHECK("",          "((ab))*",  1);

    // --- 11) Invalid patterns (depending on your validator). If you reject lone '*' or consecutive '**', expect 0:
    CHECK("abc",      "*abc",     0);
    CHECK("abc",      "a**bc",    0);
    CHECK("abc",      "ab**",     0);

    // --- 12) Mismatch guards
    CHECK("cabbbcd",  "ab*cd",    1);  // substring "abbbcd" matches
    CHECK("cabcd",    "(ab)*cd",  1);  // substring "abcd" matches "(ab)*cd"
    CHECK("cd",       "(ab)*cd",  1);  // zero repeats then "cd"
    CHECK("abd",      "(ab)*cd",  0);  // missing 'c'

    printf("\nSummary: %d/%d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}