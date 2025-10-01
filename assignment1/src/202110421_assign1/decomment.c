// 편예빈, Assignment 1, File name: decomment.c

#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>

enum DFAState {START, QUOTE, CHAR, WAIT_COMMENT, SINGLELINE, MULTILINE, WAIT_END};
static void handleQuote(enum DFAState *state, int ich, char cquote_type, int *backslash);
static void handleWaitComment(enum DFAState *state, int ich, int line_cur, int *line_com);
static void handleWaitEnd(enum DFAState *state, int ich);

int main(void)
{
  // ich: int type variable to store character input from getchar()
  int ich;
  // line_cur & line_com: current line number and comment line number
  int line_cur, line_com;
  // ch: character that comes from casting (char) on ich
  char ch; 
  //char that specifies if the prev input was a single ' or double quote "
  char cquote_type;
  // int that specifies if prev input was a backslash \ or not
  int ibackslash = 0;
  
  line_cur = 1;
  line_com = -1;
  enum DFAState state = START;

  while (1) { //reads all characters from standard input one by one
    ich = getchar();
    ch = (char)ich;

    if(ich == EOF) {
      if(state == MULTILINE || state == WAIT_END) //if it's EOF without closing comment, break and output error
        fprintf(stderr, "Error: line %d: unterminated comment\n", line_com);
      break;
    }

    switch(state) {
      case START: //if state is START
        if(ich == '/') //if input is '/', wait to see if it is a comment
          state = WAIT_COMMENT;
        else if(ich == '\"' || ich == '\''){ //if input is a quote, change to QUOTE state
          cquote_type = ich; //indicate which quote was used (single or double)
          putchar(ich);
          state = QUOTE;
        }
        else
          putchar(ich); //if input is not '/', output it
        break;

      case QUOTE: //if input is a quote, move to corresponding function
        handleQuote(&state, ich, cquote_type, &ibackslash); 
        break;

      case WAIT_COMMENT: //if input is /, it could be a comment - move to corresponding function
        handleWaitComment(&state, ich, line_cur, &line_com); 
        break;

      case SINGLELINE: //if it is a single-line comment, ignore the input until /n is inputted
        if(ich == '\n'){ //if input is \n, that will be the end of the comment so move back to start
          printf(" \n");
          state = START;
        }
        break;

      case MULTILINE: //if it could be a multi-line comment
        if(ich == '*') //if it's *, it could be an ending comment - move to WAIT_END
          state = WAIT_END;
        else if(ich == '\n'){ //print input only if it's a \n within the comment
          putchar('\n');
        }
        break;

      case WAIT_END: //if input is *, it could be end of comment - move to corresponding function
        handleWaitEnd(&state, ich); 
        break;
      default: // safety
        break;
    }

    if (ch == '\n')
      line_cur++;
  }
  
  return(EXIT_SUCCESS);
}

//handles quotes
static void handleQuote(enum DFAState *state, int ich, char cquote_type, int *ibackslash){
  //if input is " again without backslash, string is finished so go back to the start.
  if(ich == cquote_type && !*ibackslash){
    putchar(ich); //print the quote
    *state = START;
  }
  else if(ich == 92){ //if input is \, handle next input as char
    putchar(ich);
    *ibackslash = 1; //indicate that a backslash was used
  } else if(*ibackslash){ //if a backslash was used before, print input as char
    putchar(ich);
    *ibackslash = 0; //revert ibackslash back to 0
  }
  else
    putchar(ich);
}

static void handleWaitComment(enum DFAState *state, int ich, int line_cur, int *line_com){
  if(ich == 47){ //if input is '/' again, it is a single-line comment
    *state = SINGLELINE;
    putchar(' '); //replace single line comment with \s
  }
  else if(ich == '*') { //if input is '*', it is a multi-line comment
    *state = MULTILINE;
    putchar(' '); //replace multiline comment with \s
    *line_com = line_cur; //update line_com to be the start of this multiline
  }
  else { //if input was neither * or /, it was not a comment so move to START
    *state = START; 
    printf("/%c", ich); //print the char /
  }
}

static void handleWaitEnd(enum DFAState *state, int ich){
  if(ich == '/'){ //if it's /, end of comment - go back to start
    *state = START;
  }
  else if(ich == '*') //if it's * again, it could be another end of comment
    *state = WAIT_END;
  else{ //if not, it's still inside comment - go back to MULTILINE
    *state = MULTILINE;
  }
}