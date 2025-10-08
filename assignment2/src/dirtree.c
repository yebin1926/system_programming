//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                     FALL 2025
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author <yourname>
/// @studid <studentid>
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
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

/// @brief output control flags
#define F_DEPTH    0x1        ///< print directory tree
#define F_Filter   0x2        ///< pattern matching

/// @brief maximum numbers
#define MAX_DIR 64            ///< maximum number of supported directories
#define MAX_PATH_LEN 1024     ///< maximum length of a path
#define MAX_DEPTH 20          ///< maximum depth of directory tree (for -d option)
int max_depth = MAX_DEPTH;    ///< maximum depth of directory tree (for -d option)

/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};

/// @brief print strings used in the output
const char *print_formats[8] = {
  "Name                                                        User:Group           Size    Blocks Type\n",
  "----------------------------------------------------------------------------------------------------\n",
  "%-54s  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "Invalid pattern syntax",
};
const char* pattern = NULL;  ///< pattern for filtering entries

/// @brief abort the program with EXIT_FAILURE and an optional error message
///
/// @param msg optional error message or NULL
/// @param format optional format string (printf format) or NULL
void panic(const char* msg, const char* format)
{
  if (msg) {
    if (format) fprintf(stderr, format, msg);
    else        fprintf(stderr, "%s\n", msg);
  }
  exit(EXIT_FAILURE);
}


/// @brief read next directory entry from open directory 'dir'. Ignores '.' and '..' entries
/// @param dir open DIR* stream
/// @retval entry on success
/// @retval NULL on error or if there are no more entries
struct dirent *get_next(DIR *dir) // A helper function to read the next entry (skipping . and ..)
{
  struct dirent *next;
  int ignore;

  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}

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
    const char *end_of_run = s;                     // end of run: how far we've scanned so far within s
    int run_count = 0;                              // how many repetitions we've been through
    while(1) {
        int matches_count = 0;                      // counter for how many matches there were so far
        const char *s_current = end_of_run;         // current position within s
        const char *p_current = p;                  // current position within p
        // Try to match one full copy of the unit
        while (matches_count < unit_len && *s_current && *p_current && *s_current == *p_current) {  //if we haven't matched all of the pattern yet, but we still have remaining characters to match, continue
          ++matches_count;
          ++s_current;
          ++p_current;
        }
        if (matches_count == unit_len) {            //if all were matched, move end_of_run to check for the next copy
          run_count++;
          end_of_run = s_current;
        } else {
          if(matches_count != 0 && run_count == 0){
            return NULL;
          }
          return end_of_run;                      //else, that was the maximum matches we could find, so return
        }
    }
}

static int submatch(const char *s, const char *p, int is_only_group){
  // printf("Submatch for: %c, %c, %d\n", (char)*s, (char)*p, is_only_group);
  while (*p != '\0'){     
    if (*s == '\0') {
    // If pattern can be skipped (like (abc)* or x* repeated), treat as match
    while (*p) {
        if (*(p + 1) == '*') p += 2;
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
      if (p_closed == NULL) return 0; 
      int len = (int)(p_closed - (p + 1));   // get size of substring
      const char *after = p_closed + 1;            // first char after ')'

      printf("p: %c\tp_closed: %c\tlen: %d\n", (char)*p, (char)*p_closed, len);
      if (*after == '*'){
        const char *end = check_repetition_match(s, p + 1, len); // consume (group)*
        if(end == NULL) return 2;
        if (*end && *(p_closed + 2) && *end != *(p_closed + 2)) return 0;
        return submatch(end, p_closed + 2, is_only_group);       // continue after the '*'
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

/// @brief qsort comparator to sort directory entries. Sorted by name, directories first.
/// @param a pointer to first entry
/// @param b pointer to second entry
/// @retval -1 if a<b
/// @retval 0  if a==b
/// @retval 1  if a>b
static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  // if one of the entries is a directory, it comes first
  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }

  // otherwise sort by name
  return strcmp(e1->d_name, e2->d_name);
}

void process_recurse(const char* path, int depth) //recursive function for iterating through all directories
{
  DIR *dir = opendir(path);                 //open directory
  if(dir == NULL) return;                   //return if directory doesn't exist

  struct dirent *list_directories = NULL;   //list of directories for that depth, for later sorting
  int cap = 0;                              //cap: count of files in that depth
  struct dirent *e;

  while((e = get_next(dir)) != NULL){       //for each file in that depth, store file into list_directories then sort
    cap++;
    list_directories = realloc(list_directories, cap * sizeof(struct dirent)); //reallocate size of array if another file is found
    list_directories[cap-1] = *e;
    //printf("Entry: %s\n", e->d_name);
  }
  qsort(list_directories, cap, sizeof(struct dirent), dirent_compare);

  //char full[MAX_PATH_LEN];

  for(int i=0; i<cap; i++){                 //in order of sorted array, process that file and recurse through its children
    printf("%*s%s\n", (depth) * 2, "", list_directories[i].d_name);

    if(list_directories[i].d_type == DT_DIR){   //check if child exists
      if(depth < max_depth){                //check if depth exceeds max depth
        char full[MAX_PATH_LEN];            //full: full path for the child directory
        snprintf(full, sizeof(full), "%s/%s", path, list_directories[i].d_name);
        process_recurse(full, depth + 1);   //recurse for the child directory
      }
    }
  }
  closedir(dir);
  free(list_directories);

}

/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)

void process_dir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
  //
  // TODO
  //process_recurse(dn, 0);
  //
}

/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string

void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }

  assert(argv0 != NULL);

  fprintf(stderr, "Usage %s [-d depth] [-f pattern] [-h] [path...]\n"
                  "Recursively traverse directory tree and list all entries. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -d depth   | set maximum depth of directory traversal (1-%d)\n"
                  " -f pattern | filter entries using pattern (supports \'?\', \'*\', and \'()\')\n"
                  " -h         | print this help\n"
                  " path...    | list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DEPTH, MAX_DIR);

  exit(EXIT_FAILURE);
}


/// @brief program entry point
int main(int argc, char *argv[]) //argc : argument count, argv: array of strings (char*) holding the actual arguments.
{
  //
  // default directory is the current directory (".")
  //
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR]; //to-do list of paths the program will traverse.
  int   ndir = 0; //counter that keeps track of how many directories are currently stored in that array

  struct summary tstat = { 0 }; // a structure to store the total statistics
  unsigned int flags = 0; // the -d -f flags
  //
  // parse arguments
  //
  for (int i = 1; i < argc; i++) {

    for (int j = 0; j < ndir; j++) { //delete later
      if (directories[j])
          printf("Directory: %s\n", directories[j]);
    }

    if (argv[i][0] == '-') {
      // format: "-<flag>"
      if (!strcmp(argv[i], "-d")) {
        flags |= F_DEPTH;
        if (++i < argc && argv[i][0] != '-') {
          max_depth = atoi(argv[i]);
          if (max_depth < 1 || max_depth > MAX_DEPTH) {
            syntax(argv[0], "Invalid depth value '%s'. Must be between 1 and %d.", argv[i], MAX_DEPTH);
          }
        } 
        else {
          syntax(argv[0], "Missing depth value argument.");
        }
      }
      else if (!strcmp(argv[i], "-f")) {
        if (++i < argc && argv[i][0] != '-') {
          flags |= F_Filter;
          pattern = argv[i];
        }
        else {
          syntax(argv[0], "Missing filtering pattern argument.");
        }
      }
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    }
    else {
      // anything else is recognized as a directory
      if (ndir < MAX_DIR) {
        directories[ndir++] = argv[i];
        printf("%s\n", argv[i]);
        process_recurse(argv[i], 1);
      }
      else {
        fprintf(stderr, "Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;

  //
  // process each directory
  //
  // TODO
  //
  // Pseudo-code
  // - reset statistics (tstat)
  // - loop over all entries in 'root directories' (number of entires stored in 'ndir')
  //   - reset statistics (dstat)
  //   - print header
  //   - print directory name
  //   - call process_dir() for the directory
  //   - print summary & update statistics
  //...


  //
  // print aggregate statistics if more than one directory was traversed
  //
  if (ndir > 1) {
    printf("Analyzed %d directories:\n"
      "  total # of files:        %16d\n"
      "  total # of directories:  %16d\n"
      "  total # of links:        %16d\n"
      "  total # of pipes:        %16d\n"
      "  total # of sockets:      %16d\n"
      "  total # of entries:      %16d\n"
      "  total file size:         %16llu\n"
      "  total # of blocks:       %16llu\n",
      ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks,
      tstat.files + tstat.dirs + tstat.links + tstat.fifos + tstat.socks, 
      tstat.size, tstat.blocks);
  }

  //
  // that's all, folks!
  //
  return EXIT_SUCCESS;
}

