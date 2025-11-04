//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                     FALL 2025
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author <편예빈>
/// @studid <2021-10421>
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
  "%-54.54s  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "Invalid pattern syntax",
};
const char* pattern = NULL; 

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
  int depth = 1;                        // start with 1 because we're seeing one '(' (although we're not using nested brackets)
  for (p = p + 1; *p; p++) {            // traverse through string
      if (*p == '(') depth++;           // increase depth count when coming across (, so that we find outermost )
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
    int matches_count = 0;                       // counter for how many matches there were so far
    const char *s_current = end_of_run;          // current position within s
    const char *p_current = p;                   // current position within p
    // Try to match one full copy of the unit
    while (matches_count < unit_len && *s_current && *p_current && (*s_current == *p_current || *p_current == '?')) {  //if we haven't matched all of the pattern yet, but we still have remaining characters to match, continue
      ++matches_count;
      ++s_current;
      ++p_current;
    }
    if (matches_count == unit_len) {            //if all were matched, move end_of_run to check for the next copy
      run_count++;
      end_of_run = s_current;
    } else {                                    // if match fails halfway through scanning, return null
      if(matches_count != 0 && run_count == 0){
        return NULL;
      }
      return end_of_run;                      //else, that was the maximum matches we could find, so return
    }
    }
}

static int submatch(const char *s, const char *p, int is_only_group){
  while (*p != '\0'){       
    if (*s == '\0') {
      // if search keyword is "", and if pattern can be skipped (like (abc)* or x* repeated), treat as match. Else, return 0
      while (*p) {
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
      if (submatch(s, p + 1, is_only_group)) return 1;            //when getting to *, skip it -- it should not be counted/compared as a character
      // try consuming one character and stay on '*'
      return (*s && submatch(s + 1, p, is_only_group)) ? 1 : 0;
    } else if (*(p + 1) == '*') {                                 //if next char is *, save current *p to test repetitions of it    
      char c = *p;                                                // repeat this literal
      const char *rest = p + 2;                                   // pattern after the 'x*'

      if (submatch(s, rest, is_only_group)) return 1;            // try zero copies, if it works return 1

      while (*s == c) {                                          // try one-or-more copies
        s++;
        if (submatch(s, rest, is_only_group)) return 1;
      }
      return 0;
    }
    else if (*p == '('){                           // if it could be a group
      //endless repetition here 
      const char* p_closed = find_close(p);        // find pointer to closing braket ), if none call stderr
      if (p_closed == NULL) {
        panic("Invalid pattern syntax", NULL);
        return 0;
      }
      int len = (int)(p_closed - (p + 1));         // get size of substring of group

      const char *after = p_closed + 1;            // get first char after ')'

      if (*after == '*'){                          // this group should be checked for repetition
        const char *end = check_repetition_match(s, p + 1, len); // check group for repetition
        if(end == NULL) return 2;
        const char next = *(p_closed + 2);         // 
        if (next && next != '(' && next != ')' && next != '*' && next != '?') {
          if (*end != next) return 0;
        }
        return submatch(end, p_closed + 2, is_only_group);      // continue after the '*'
      } else {
        int k = 0;
        const char *ts = s, *tp = p + 1;               // compare inner literally
        while (k < len && *ts && *tp && (*tp == '?' || *ts == *tp)) { k++; ts++; tp++; }
        if (k != len) return 0;                        // inner didn't match once

        // advance both: we consumed the group once
        s = ts;                                        // move input past inner
        p = after;                                     // move pattern past ')'
        continue;                                      // continue the while-loop in submatch
      }
    } else if (*s != *p && *p != '?') return 0;        // if it's not a star case: must match literally or be '?'. If not, return 0
    else {
      s++;
      p++;
    }
  }
  while (*p == '*') p += 1;           // trailing x*

  return (*p == '\0');  //if p = "", return true
}

static int match(const char *str, const char *pattern){
  // handling cases for invalid pattern syntax 
  if (*pattern == '*' || *pattern == '\0') {    //if pattern starts with * or is empty
    panic("Invalid pattern syntax", NULL);
    return 0;
  }
  for (const char *q = pattern; *q; ++q) {      //if pattern has double **
    if (*q == '*' && q[1] == '*') {
      panic("Invalid pattern syntax", NULL);
      return 0;
    }
    if (*q == '(') {                            //if pattern has unbalanced ( or ), or has empty group
      const char *close = find_close(q);
      if (!close) {
        panic("Invalid pattern syntax", NULL);
        return 0;
      }
      if (close == q + 1) {            // empty group "()"
        panic("Invalid pattern syntax", NULL);
        return 0;
      }
      q = close;                        
    } else if (*q == ')') {             // stray ')'
      panic("Invalid pattern syntax", NULL);
      return 0;
    }
  }

  //check if the whole search keyword is a group, and change is_only_group accordingly
  const char* p_closed = find_close(pattern);
  int is_only_group = 0;
  if (*pattern == '(') {
    p_closed = find_close(pattern);
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

//checks if the directory's subtree has a match
static int subtree_has_match(const char *path, const char *pstr, int depth, struct summary *stats, char typech) {
  DIR *dir = opendir(path);
  if (!dir) return 0;

  struct dirent *e;
  char full[MAX_PATH_LEN];
  int found = 0;

  while ((e = get_next(dir)) != NULL) {
    if (match(e->d_name, pstr)) { 
      found = 1; break; 
    }

    // descend into child directory
    if (e->d_type == DT_DIR && depth < max_depth) {
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        if (subtree_has_match(full, pstr, depth + 1, stats, typech)) { found = 1; break; }
    }
  }

  closedir(dir);
  return found;
}

/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)

static int process_dir(const char *path, int depth, const char *pstr, struct summary *stats, unsigned int flags)
{
  // TODO
  DIR *dir = opendir(path);                  //open directory
  if(dir == NULL) return -1;                 //return if directory doesn't exist

  struct dirent *list_directories = NULL;   //list of directories for that depth, for later sorting
  int cap = 0;                              //cap: count of files in that depth
  struct dirent *e;

  while((e = get_next(dir)) != NULL){       //for each file in that depth, store file into list_directories then sort
    cap++;
    list_directories = realloc(list_directories, cap * sizeof(struct dirent)); //reallocate size of array if another file is found
    list_directories[cap-1] = *e;
  }
  qsort(list_directories, cap, sizeof(struct dirent), dirent_compare); //sort directories in that depth first showing directories then alphabetical

  // ------ NO F FILTER ------
  if (pstr == NULL) {
    for (int i = 0; i < cap; i++) {
      const char *name = list_directories[i].d_name;
      char full[MAX_PATH_LEN];
      snprintf(full, sizeof full, "%s/%s", path, name);           //make the full path for later

      struct stat st;
      if (lstat(full, &st) == -1) { perror("lstat"); continue; }  //get lstat of path, and increment the directory's stats
      stats->size   += st.st_size;
      stats->blocks += st.st_blocks;
      
      struct passwd *pw = getpwuid(st.st_uid);                    //get necessary info (user, group, type, etc)
      struct group  *gr = getgrgid(st.st_gid);
      const char *user  = pw ? pw->pw_name : "?";
      const char *group = gr ? gr->gr_name : "?";

      char typech = ' ';
      if (S_ISDIR(st.st_mode))  typech = 'd';
      else if (S_ISLNK(st.st_mode))  typech = 'l';
      else if (S_ISSOCK(st.st_mode)) typech = 's';
      else if (S_ISFIFO(st.st_mode)) typech = 'f';
      else if (S_ISREG(st.st_mode)) stats->files++;

      char namecol[256];                                          //format for the path name column
      int written = snprintf(namecol, sizeof namecol, "%*s%s", depth * 2, "", name);
      if(written > 54){                                          //if the path string exceeds max length, truncate it
        namecol[51] = '.';
        namecol[52] = '.';
        namecol[53] = '.';
        namecol[54] = '\0';
      }
      
      switch(typech){                                            //update individual summary stats
        case 'd':
          stats->dirs++;
          break;
        case 'l':
          stats->links++;
          break;
        case 's':
          stats->socks++;
          break;
        case 'f':
          stats->fifos++;
        default:
          break;
      }

      printf(print_formats[2], namecol, user, group, (unsigned long long)st.st_size, (unsigned long long)st.st_blocks, typech);

      if (list_directories[i].d_type == DT_DIR && depth < max_depth) {
        (void)process_dir(full, depth + 1, pstr, stats, flags); // keep printing children
      }
    }
    closedir(dir);
    free(list_directories);
    return 1;
  }

  // ------ WITH F FILTER ------
  int any_match_in_this_dir = 0;

  for (int i = 0; i < cap; i++) {
    const char *name = list_directories[i].d_name;
    char full[MAX_PATH_LEN];
    snprintf(full, sizeof full, "%s/%s", path, name);

    if (list_directories[i].d_type == DT_DIR) {           //check whether current directory or child has match 
      int child_has_match = (depth < max_depth) ? subtree_has_match(full, pstr, depth + 1, stats, 'd') : 0;
      int self_matches    = match(name, pattern);

      if (self_matches || child_has_match) {              //if either both is a match, print current file name
        // Build the name column (indent + name), with simple truncation into 54 chars
        char namecol[256];
        int written = snprintf(namecol, sizeof namecol, "%*s%s", depth * 2, "", name);
        if (written > 54) {              // if path exceeds max length, keep last 3 as "..."
          namecol[51] = '.';
          namecol[52] = '.';
          namecol[53] = '.';
          namecol[54] = '\0';
        }

        struct stat st;                                  //get necessary info (user, group, type, etc)
        if (lstat(full, &st) == -1) { perror("lstat"); continue; }

        struct passwd *pw = getpwuid(st.st_uid);
        struct group  *gr = getgrgid(st.st_gid);
        const char *user  = pw ? pw->pw_name : "?";
        const char *group = gr ? gr->gr_name : "?";

        char typech = ' ';
        if      (S_ISDIR(st.st_mode))  typech = 'd';
        else if (S_ISLNK(st.st_mode))  typech = 'l';
        else if (S_ISSOCK(st.st_mode)) typech = 's';
        else if (S_ISFIFO(st.st_mode)) typech = 'f';
        else if (S_ISCHR(st.st_mode))  typech = 'c';
        else if (S_ISBLK(st.st_mode))  typech = 'b';

        if (self_matches) {                               //if the current directory is also a match, increment file, size, and block count
          printf(print_formats[2], namecol, user, group,
              (unsigned long long)st.st_size,
              (unsigned long long)st.st_blocks, typech);
          stats->size   += st.st_size;
          stats->blocks += st.st_blocks;
          if      (typech == 'd') stats->dirs++;
          else if (typech == 'l') stats->links++;
          else if (typech == 's') stats->socks++;
          else if (typech == 'f') stats->fifos++;
          // (devices ignored; add if you need)
        } else {
          printf("%s\n", namecol);
        }

        // Recurse to print matching descendants (only if some child matched)
        if (child_has_match && depth < max_depth) {
          (void)process_dir(full, depth + 1, pstr, stats, flags);
        }

        any_match_in_this_dir = 1;
      }


    } else {  //if it's not a directory:
      // print and count only if its own name matches
      int file_matches = match(name, pattern);
      if (file_matches) {
        char namecol[256];
        int written = snprintf(namecol, sizeof namecol, "%*s%s", depth * 2, "", name);  //format file name, and truncate if needed
        if (written >= 54) {
          namecol[51] = '.';
          namecol[52] = '.';
          namecol[53] = '.';
          namecol[54] = '\0';
        }

        struct stat st;
        if (lstat(full, &st) == -1) { perror("lstat"); continue; }

        //get necessary info (user, group, type, etc)
        struct passwd *pw = getpwuid(st.st_uid);
        struct group  *gr = getgrgid(st.st_gid);
        const char *user  = pw ? pw->pw_name : "?";
        const char *group = gr ? gr->gr_name : "?";

        char typech = ' ';                // keep regular files as space in the Type column
        if      (S_ISLNK(st.st_mode))  typech = 'l';
        else if (S_ISSOCK(st.st_mode)) typech = 's';
        else if (S_ISFIFO(st.st_mode)) typech = 'f';
        else if (S_ISCHR(st.st_mode))  typech = 'c';
        else if (S_ISBLK(st.st_mode))  typech = 'b';

        printf(print_formats[2], namecol, user, group,
              (unsigned long long)st.st_size,
              (unsigned long long)st.st_blocks, typech);
  
        stats->size   += st.st_size;    //increment individual file statistics
        stats->blocks += st.st_blocks;

        if      (S_ISREG(st.st_mode))  stats->files++;
        else if (S_ISLNK(st.st_mode))  stats->links++;
        else if (S_ISSOCK(st.st_mode)) stats->socks++;
        else if (S_ISFIFO(st.st_mode)) stats->fifos++;

        any_match_in_this_dir = 1;
      }
    }
  }

  closedir(dir);
  free(list_directories);
  return any_match_in_this_dir;
}

/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
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
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR]; //to-do list of paths the program will traverse.
  int   ndir = 0; //counter that keeps track of how many directories are currently stored in that array

  struct summary tstat = { 0 }; // a structure to store the total statistics
  unsigned int flags = 0; // the -d -f flags
  //
  // parse arguments
  //
  for (int i = 1; i < argc; i++) {
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
      }
      else {
        fprintf(stderr, "Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;

  // after arg parsing, before any printing
  if (pattern) {
    (void)match("", pattern);   // validate once; on invalid, panic() exits now
  }

  //TODO
  for (int j = 0; j < ndir; j++) {
    if (directories[j]){
      struct summary individual_summary = {0};

      //process each directory
      printf("%s%s", print_formats[0], print_formats[1]);
      printf("%s\n", directories[j]);
      process_dir(directories[j], 1, pattern, &individual_summary, flags);
      printf("%s", print_formats[1]);

      //different string formats depending on singular/plural
      const char *s_files  = (individual_summary.files  == 1) ? "" : "s";
      const char *s_links  = (individual_summary.links  == 1) ? "" : "s";
      const char *s_pipes  = (individual_summary.fifos  == 1) ? "" : "s";
      const char *s_socks  = (individual_summary.socks  == 1) ? "" : "s";
      const char *dir_word = (individual_summary.dirs   == 1) ? "directory" : "directories";

      char left[256];
      snprintf(left, sizeof left,
              "%u file%s, %u %s, %u link%s, %u pipe%s, and %u socket%s",
              individual_summary.files, s_files,
              individual_summary.dirs,  dir_word,
              individual_summary.links, s_links,
              individual_summary.fifos, s_pipes,
              individual_summary.socks, s_socks);
            
      char namecol[256];
      snprintf(namecol, sizeof namecol, "%s", left);

      // Print a footer row aligned to Size/Blocks
      printf("%-54s  %8s  %10llu  %8llu    %c\n",
            left,               /* full sentence, not truncated */
            "",                 /* blank User:Group field (8 spaces) */
            (unsigned long long)individual_summary.size,
            (unsigned long long)individual_summary.blocks,
            ' ');               /* blank Type column */
      printf("\n");

      //update total summary statistics
      tstat.files  += individual_summary.files;
      tstat.dirs   += individual_summary.dirs;
      tstat.links  += individual_summary.links;
      tstat.fifos  += individual_summary.fifos;
      tstat.socks  += individual_summary.socks;
      tstat.size   += individual_summary.size;
      tstat.blocks += individual_summary.blocks;
    }
  }
  // print aggregate statistics if more than one directory was traversed
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
  return EXIT_SUCCESS;
}