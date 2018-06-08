#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "chess.h"
#include "data.h"
#if defined(UNIX)
#  include <unistd.h>
#endif

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*   LearnBook() is used to accumulate the evaluations for the first N moves    *
*   out of book.  after these moves have been played, the evaluations are then *
*   used to decide whether the last book move played was a reasonable choice   *
*   or not.  (N is set by the #define LEARN_INTERVAL definition.)              *
*                                                                              *
*   there are three cases to be handled.  (1) if the evaluation is bad right   *
*   out of book, or it drops enough to be considered a bad line, then the book *
*   move will have its "learn" value reduced to discourage playing this move   *
*   again.  (2) if the evaluation is even after N moves, then the learn        *
*   value will be increased, but by a relatively modest amount, so that a few  *
*   even results will offset one bad result.  (3) if the evaluation is very    *
*   good after N moves, the learn value will be increased by a large amount    *
*   so that this move will be favored the next time the game is played.        *
*                                                                              *
********************************************************************************
*/

void LearnBook(TREE *tree, int wtm, int search_value, int search_depth, int lv,
               int force) {
/*
 ----------------------------------------------------------
|                                                          |
|   if we have not been "out of book" for N moves, all     |
|   we need to do is take the search evaluation for the    |
|   search just completed and tuck it away in the book     |
|   learning array (book_learn_eval[]) for use later.      |
|                                                          |
 ----------------------------------------------------------
*/
  int t_wtm=wtm;
  if (!book_file) return;
  if (!(learning&book_learning)) return;
  if (moves_out_of_book <= LEARN_INTERVAL && !force) {
    if (moves_out_of_book) {
      book_learn_eval[moves_out_of_book-1]=search_value;
      book_learn_depth[moves_out_of_book-1]=search_depth;
    }
  }
/*
 ----------------------------------------------------------
|                                                          |
|   check the evaluations we've seen so far.  if they are  |
|   within reason (+/- 1/3 of a pawn or so) we simply keep |
|   playing and leave the book alone.  if the eval is much |
|   better or worse, we need to update the learning count. |
|                                                          |
 ----------------------------------------------------------
*/
  else if (moves_out_of_book == LEARN_INTERVAL+1 || force) {
    int move, i, j, learn_value, read;
    int secs, interval, last_book_move=-1;
    float temp_value;
    char cmd[32], buff[80], *nextc;
    int best_eval=-999999, best_eval_p=0;
    int worst_eval=999999, worst_eval_p=0, save_wtm;
    int best_after_worst_eval=-999999, worst_after_best_eval=999999;
    struct tm *timestruct;
    int n_book_moves[512];
    float book_learn[512], t_learn_value;

    if (moves_out_of_book < 1) return;
    Print(128,"LearnBook() executed\n");
    learning&=~book_learning;
    interval=Min(LEARN_INTERVAL,moves_out_of_book);
    if (interval < 2)  return;

    for (i=0;i<interval;i++) {
      if (book_learn_eval[i] > best_eval) {
        best_eval=book_learn_eval[i];
        best_eval_p=i;
      }
      if (book_learn_eval[i] < worst_eval) {
        worst_eval=book_learn_eval[i];
        worst_eval_p=i;
      }
    }
    if (best_eval_p < interval-1) {
      for (i=best_eval_p;i<interval;i++)
        if (book_learn_eval[i] < worst_after_best_eval)
          worst_after_best_eval=book_learn_eval[i];
    }
    else worst_after_best_eval=book_learn_eval[interval-1];

    if (worst_eval_p < interval-1) {
      for (i=worst_eval_p;i<interval;i++)
        if (book_learn_eval[i] > best_after_worst_eval)
          best_after_worst_eval=book_learn_eval[i];
    }
    else best_after_worst_eval=book_learn_eval[interval-1];

#if defined(DEBUG)
    Print(128,"Learning analysis ...\n");
    Print(128,"worst=%d  best=%d  baw=%d  wab=%d\n",
          worst_eval, best_eval, best_after_worst_eval, worst_after_best_eval);
    for (i=0;i<interval;i++)
      Print(128,"%d(%d) ",book_learn_eval[i],book_learn_depth[i]);
    Print(128,"\n");
#endif

/*
 ----------------------------------------------------------
|                                                          |
|   we now have the best eval for the first N moves out    |
|   of book, the worst eval for the first N moves out of   |
|   book, and the worst eval that follows the best eval.   |
|   this will be used to recognize the following cases of  |
|   results that follow a book move:                       |
|                                                          |
 ----------------------------------------------------------
*/
/*
 ----------------------------------------------------------
|                                                          |
|   (1) the best score is very good, and it doesn't drop   |
|   after following the game further.  this case detects   |
|   those moves in book that are "good" and should be      |
|   played whenever possible.                              |
|                                                          |
 ----------------------------------------------------------
*/
    if (best_eval == best_after_worst_eval) {
      learn_value=best_eval;
      for (i=0;i<interval;i++)
        if (learn_value == book_learn_eval[i])
          search_depth=Max(search_depth,book_learn_depth[i]);
    }
/*
 ----------------------------------------------------------
|                                                          |
|   (2) the worst score is bad, and doesn't improve any    |
|   after the worst point, indicating that the book move   |
|   chosen was "bad" and should be avoided in the future.  |
|                                                          |
 ----------------------------------------------------------
*/
    else if (worst_eval == worst_after_best_eval) {
      learn_value=worst_eval;
      for (i=0;i<interval;i++)
        if (learn_value == book_learn_eval[i])
          search_depth=Max(search_depth,book_learn_depth[i]);
    }
/*
 ----------------------------------------------------------
|                                                          |
|   (3) things seem even out of book and remain that way   |
|   for N moves.  we will just average the 10 scores and   |
|   use that as an approximation.                          |
|                                                          |
 ----------------------------------------------------------
*/
    else {
      learn_value=0;
      search_depth=0;
      for (i=0;i<interval;i++) {
        learn_value+=book_learn_eval[i];
        search_depth+=book_learn_depth[i];
      }
      learn_value/=interval;
      search_depth/=interval;
    }
    if (lv) learn_value=search_value;
    if (lv == 0)
      learn_value=LearnFunction(learn_value,search_depth,
                                crafty_rating-opponent_rating,
                                learn_value<0);
    else learn_value=search_value;
/*
 ----------------------------------------------------------
|                                                          |
|   first, we are going to find every book move in the     |
|   game, and note how many alternatives there were at     |
|   every book move.                                       |
|                                                          |
 ----------------------------------------------------------
*/
    save_wtm=wtm;
    InitializeChessBoard(&tree->position[0]);
    for (i=0;i<512;i++) n_book_moves[i]=0;
    wtm=1;
    for (i=0;i<512;i++) {
      int *mv, cluster, key, test;
      BITBOARD common, temp_hash_key;

      n_book_moves[i]=0;
      fseek(history_file,i*10,SEEK_SET);
      strcpy(cmd,"");
      read=fscanf(history_file,"%s",cmd);
      if (read != 1) break;
      if (strcmp(cmd,"pass")) {
        move=InputMove(tree,cmd,0,wtm,1,0);
        if (!move) break;
        tree->position[1]=tree->position[0];
        tree->last[1]=GenerateCaptures(tree, 1, wtm, tree->last[0]);
        tree->last[1]=GenerateNonCaptures(tree, 1, wtm, tree->last[1]);
        test=HashKey>>49;
        fseek(book_file,test*sizeof(int),SEEK_SET);
        fread(&key,sizeof(int),1,book_file);
        if (key > 0) {
          fseek(book_file,key,SEEK_SET);
          fread(&cluster,sizeof(int),1,book_file);
          fread(book_buffer,sizeof(BOOK_POSITION),cluster,book_file);
        }
        else cluster=0;
        for (mv=tree->last[0];mv<tree->last[1];mv++) {
          common=And(HashKey,mask_16);
          MakeMove(tree, 1,*mv,wtm);
          temp_hash_key=Xor(HashKey,wtm_random[wtm]);
          temp_hash_key=Or(And(temp_hash_key,Compl(mask_16)),common);
          for (j=0;j<cluster;j++)
            if (!Xor(temp_hash_key,book_buffer[j].position) &&
                book_buffer[j].learn > (float) LEARN_COUNTER_BAD/100.0) {
                n_book_moves[i]++;
                last_book_move=i;
            }
          UnMakeMove(tree, 1,*mv,wtm);
        }
        if (move) MakeMoveRoot(tree, move,wtm);
      }
      wtm=ChangeSide(wtm);
    } 
/*
 ----------------------------------------------------------
|                                                          |
|   now we build a vector of book learning results.  we    |
|   give every book move below the last point where there  |
|   were alternatives 100% of the learned score.  We give  |
|   the book move played at that point 100% of the learned |
|   score as well.  then we divide the learned score by    |
|   the number of alternatives, and propogate this score   |
|   back until there was another alternative, where we do  |
|   this again and again until we reach the top of the     |
|   book tree.                                             |
 ----------------------------------------------------------
*/
    t_learn_value=((float) learn_value)/100.0;
    for (i=511;i>=0;i--) {
      book_learn[i]=t_learn_value;
      if (n_book_moves[i] > 1) t_learn_value/=n_book_moves[i];
    }
/*
 ----------------------------------------------------------
|                                                          |
|   finally, we run thru the book file and update each     |
|   book move learned value based on the computation we    |
|   calculated above.                                      |
|                                                          |
 ----------------------------------------------------------
*/
    InitializeChessBoard(&tree->position[0]);
    wtm=1;
    for (i=0;i<512;i++) {
      strcpy(cmd,"");
      fseek(history_file,i*10,SEEK_SET);
      strcpy(cmd,"");
      read=fscanf(history_file,"%s",cmd);
      if (read != 1) break;
      if (strcmp(cmd,"pass")) {
        move=InputMove(tree,cmd,0,wtm,1,0);
        if (!move) break;
        tree->position[1]=tree->position[0];
/*
 ----------------------------------------------------------
|                                                          |
|   now call LearnBookUpdate() to find this position in    |
|   the book database and update the learn stuff.          |
|                                                          |
 ----------------------------------------------------------
*/
        temp_value=book_learn[i];
        if (wtm != crafty_is_white) temp_value*=-1;
        LearnBookUpdate(tree, wtm, move, temp_value);
        MakeMoveRoot(tree, move,wtm);
      }
      wtm=ChangeSide(wtm);
    } 
/*
 ----------------------------------------------------------
|                                                          |
|   now update the "book.lrn" file so that this can be     |
|   shared with other crafty users or else saved in case.  |
|   the book must be re-built.                             |
|                                                          |
 ----------------------------------------------------------
*/
    fprintf(book_lrn_file,"[White \"%s\"]\n",pgn_white);
    fprintf(book_lrn_file,"[Black \"%s\"]\n",pgn_black);
    secs=time(0);
    timestruct=localtime((time_t*) &secs);
    fprintf(book_lrn_file,"[Date \"%4d.%02d.%02d\"]\n",timestruct->tm_year+1900,
            timestruct->tm_mon+1,timestruct->tm_mday);
    nextc=buff;
    for (i=0;i<=last_book_move;i++) {
      fseek(history_file,i*10,SEEK_SET);
      strcpy(cmd,"");
      read=fscanf(history_file,"%s",cmd);
      if (read!=1) break;
      if (strchr(cmd,' ')) *strchr(cmd,' ')=0;
      sprintf(nextc," %s",cmd);
      nextc=buff+strlen(buff);
      if (nextc-buff > 60) {
        fprintf(book_lrn_file,"%s\n",buff);
        nextc=buff;
        strcpy(buff,"");
      }
    }
    fprintf(book_lrn_file,"%s {%d %d %d}\n",
            buff, (t_wtm) ? learn_value:-learn_value, search_depth,
            crafty_rating-opponent_rating);
    fflush(book_lrn_file);
/*
 ----------------------------------------------------------
|                                                          |
|   done.  now restore the game back to where it was       |
|   before we started all this nonsense.  :)               |
|                                                          |
 ----------------------------------------------------------
*/
    RestoreGame();
  }
}

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*   LearnBookUpdate() is called to find the current position in the book and   *
*   update the learn counter.  if it is supposed to mark a move as not to be   *
*   played, and after marking such a move there are no more left at this point *
*   in the database, it returns (0) which will force LearnBook() to back up    *
*   two plies and update that position as well, since no more choices at the   *
*   current position doesn't really do much for us...                          *
*                                                                              *
********************************************************************************
*/
void LearnBookUpdate(TREE *tree, int wtm, int move, float learn_value) {
  int cluster, test, move_index, key;
  BITBOARD temp_hash_key, common;
/*
 ----------------------------------------------------------
|                                                          |
|   first find the appropriate cluster, make the move we   |
|   were passed, and find the resulting position in the    |
|   database.                                              |
|                                                          |
 ----------------------------------------------------------
*/
  test=HashKey>>49;
  if (book_file) {
    fseek(book_file,test*sizeof(int),SEEK_SET);
    fread(&key,sizeof(int),1,book_file);
    if (key > 0) {
      fseek(book_file,key,SEEK_SET);
      fread(&cluster,sizeof(int),1,book_file);
      fread(book_buffer,sizeof(BOOK_POSITION),cluster,book_file);
      common=And(HashKey,mask_16);
      MakeMove(tree, 1,move,wtm);
      temp_hash_key=Xor(HashKey,wtm_random[wtm]);
      temp_hash_key=Or(And(temp_hash_key,Compl(mask_16)),common);
      for (move_index=0;move_index<cluster;move_index++)
        if (!Xor(temp_hash_key,book_buffer[move_index].position)) break;
      UnMakeMove(tree, 1,move,wtm);
      if (move_index >= cluster) return;
      if (book_buffer[move_index].learn == 0.0)
        book_buffer[move_index].learn=learn_value;
      else
        book_buffer[move_index].learn=
          (book_buffer[move_index].learn+learn_value)/2.0;
      fseek(book_file,key+sizeof(int),SEEK_SET);
      fwrite(book_buffer,sizeof(BOOK_POSITION),cluster,book_file);
      fflush(book_file);
    }
  }
}

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*   LearnFunction() is called to compute the adjustment value added to the     *
*   learn counter in the opening book.  it takes three pieces of information   *
*   into consideration to do this:  the search value, the search depth that    *
*   produced this value, and the rating difference (Crafty-opponent) so that   *
*   + numbers means Crafty is expected to win, - numbers mean Crafty is ex-    *
*   pected to lose.                                                            *
*                                                                              *
********************************************************************************
*/
int LearnFunction(int sv, int search_depth, int rating_difference,
                  int trusted_value) {
  float rating_multiplier_trusted[11] = {.00625, .0125, .025, .05, .075, .1,
                                         0.15, 0.2, 0.25, 0.3, 0.35};
  float rating_multiplier_untrusted[11] = {.25, .2, .15, .1, .05, .025, .012,
                                           .006, .003, .001};
  float multiplier;
  int sd, rd;

  sd=Max(Min(search_depth,19),0);
  rd=Max(Min(rating_difference/200,5),-5)+5;
  if (trusted_value)
    multiplier=rating_multiplier_trusted[rd]*sd;
  else
    multiplier=rating_multiplier_untrusted[rd]*sd;
  sv=Max(Min(sv,600),-600);
  return(sv*multiplier);
}

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*  LearnImport() is used to read in a learn data file (*.lrn) and apply        *
*  it to either book.bin (book.lrn file) or position.bin (position.lrn file).  *
*  this allows users to create a new book.bin at any time, adding more games   *
*  as needed, without losing all of the "learned" openings in the database.    *
*                                                                              *
*  the second intent is to allow users to "share" *.lrn files, and to allow me *
*  to keep several of them on the ftp machine, so that anyone can use those    *
*  file(s) and have their version of Crafty (or any other program that wants   *
*  to participate in this) "learn" what other crafty's have already found out  *
*  about which openings and positions are good and bad.                        *
*                                                                              *
*  the basic idea is to (a) stuff each book opening line into the game history *
*  for LearnBook(), then set things up so that LearnBook() can be called and   *
*  it will behave just as though this book line was just "learned".  if the    *
*  file is a position.lrn type of file (which is recognized by finding a       *
*  "setboard" command in the file as well as the word "position" in the first  *
*  eight bytes of the file, then the positions and scores are read in and      *
*  added to the position.bin file.                                             *
*                                                                              *
********************************************************************************
*/
void LearnImport(TREE *tree, int nargs, char **args) {

  FILE *learn_in;
  char text[128];
  int eof;

/*
 ----------------------------------------------------------
|                                                          |
|   first, get the name of the file that contains the      |
|   learned book lines.                                    |
|                                                          |
 ----------------------------------------------------------
*/
  display_options&=4095-128;
  learn_in=fopen(*args,"r");
  if (learn_in == NULL) {
    Print(4095,"unable to open %s for input\n", *args);
    return;
  }
  eof=fscanf(learn_in,"%s",text);
  fclose(learn_in);
  if  (eof == 0) return;
  if (!strcmp(text,"position")) LearnImportPosition(tree,nargs,args);
  else LearnImportBook(tree,nargs,args);
}

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*   LearnImportBook() is used to import book learning and save it in the       *
*   book.bin file (see LearnBook for details.)                                 *
*                                                                              *
********************************************************************************
*/

#if defined(MACOS)
  int index[32768];
#endif

void LearnImportBook(TREE *tree, int nargs, char **args) {

  FILE *learn_in;
  char nextc, text[128], *eof;
  int wtm, learn_value, depth, rating_difference, move=0, i, added_lines=0;

/*
 ----------------------------------------------------------
|                                                          |
|   if the <clear> option was given, first we cycle thru   |
|   the entire book and clear every learned value.         |
|                                                          |
 ----------------------------------------------------------
*/
  learn_in = fopen(args[0],"r");
  if (nargs>1 && !strcmp(args[1],"clear")) {
#if defined(MACOS)
    int i, j, cluster;
#else
    int index[32768], i, j, cluster;
#endif
    fclose(book_lrn_file);
#if defined(MACOS)
    sprintf(text,":%s:book.lrn",book_path);
#else
    sprintf(text,"%s/book.lrn",book_path);
#endif
    book_lrn_file=fopen(text,"w");
    fseek(book_file,0,SEEK_SET);
    fread(index,sizeof(int),32768,book_file);
    for (i=0;i<32768;i++)
      if (index[i] > 0) {
        fseek(book_file,index[i],SEEK_SET);
        fread(&cluster,sizeof(int),1,book_file);
        fread(book_buffer,sizeof(BOOK_POSITION),cluster,book_file);
        for (j=0;j<cluster;j++) book_buffer[j].learn=0.0;
        fseek(book_file,index[i]+sizeof(int),SEEK_SET);
        fwrite(book_buffer,sizeof(BOOK_POSITION),cluster,book_file);
      }
  }
/*
 ----------------------------------------------------------
|                                                          |
|   outer loop loops thru the games (opening lines) one by |
|   one, while the inner loop stuffs the game history file |
|   with moves that were played.  the series of moves in a |
|   line is terminated by the {x y z} data values.         |
|                                                          |
 ----------------------------------------------------------
*/
  while (1) {
    if (added_lines%10==0) {
      printf(".");
      fflush(stdout);
    }
    if ((added_lines+1)%600 == 0) printf(" (%d)\n",added_lines+1);
    InitializeChessBoard(&tree->position[0]);
    wtm=0;
    move_number=0;
    for (i=0;i<100;i++) {
      fseek(history_file,i*10,SEEK_SET);
      fprintf(history_file,"         \n");
    }
    for (i=0;i<3;i++) {
      eof=fgets(text,80,learn_in);
      if (eof) {
        char *delim;
        delim=strchr(text,'\n');
        if (delim) *delim=0;
        delim=strchr(text,'\r');
        if (delim) *delim=' ';
      }
      else break;
      if (strchr(text,'[')) do {
        char *bracket1, *bracket2;
        char value[32];
  
        bracket1=strchr(text,'\"');
        bracket2=strchr(bracket1+1,'\"');
        if (bracket1 == 0 || bracket2 == 0) break;
        *bracket2=0;
        strcpy(value,bracket1+1);
        if (bracket2 == 0) break;
        if (strstr(text,"White")) strcpy(pgn_white,value);
        if (strstr(text,"Black")) strcpy(pgn_black,value);
      } while(0);
    }
    if (eof == 0) break;
    do {
      wtm=ChangeSide(wtm);
      if (wtm) move_number++;
      do {
        nextc=fgetc(learn_in);
      } while(nextc == ' ' || nextc == '\n');
      if (nextc == '{') break;
      ungetc(nextc,learn_in);
      move=ReadChessMove(tree,learn_in,wtm,1);
      if (move < 0) break;
      strcpy(text,OutputMove(tree,move,0,wtm));
      fseek(history_file,((move_number-1)*2+1-wtm)*10,SEEK_SET);
      fprintf(history_file,"%9s\n",text);
      moves_out_of_book=0;
      MakeMoveRoot(tree, move,wtm);
    } while (1);
    if (move < 0) break;
    fscanf(learn_in,"%d %d %d}\n",&learn_value, &depth, &rating_difference);
    moves_out_of_book=LEARN_INTERVAL+1;
    move_number+=LEARN_INTERVAL+1-wtm;
    for (i=0;i<LEARN_INTERVAL;i++) book_learn_eval[i]=learn_value;
    crafty_rating=rating_difference;
    opponent_rating=0;
    learning|=book_learning;
    if (abs(learn_value) != 99900)
      LearnBook(tree, wtm, (wtm) ? learn_value : -learn_value, depth, 1, 1);
    else
      LearnResult(tree, learn_value < 0);
    added_lines++;
  }
  move_number=1;
  Print(4095,"\nadded %d learned book lines to book.bin\n",added_lines);
}

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*   LearnImportPosition() is used to import positions and save them in the     *
*   position.bin file.  (see LearnPosition for details.)                       *
*                                                                              *
********************************************************************************
*/
void LearnImportPosition(TREE *tree, int nargs, char **args) {

  BITBOARD word1, word2;
  int positions, nextp, secs;
  struct tm *timestruct;
  char xlate[15]={'q','r','b',0,'k','n','p',0,'P','N','K',0,'B','R','Q'};
  char empty[9]={' ','1','2','3','4','5','6','7','8'};
  int i, rank, file, nempty, value, move, depth, added_positions=0;
  char *eof, text[80];
  FILE *learn_in;

/*
 ----------------------------------------------------------
|                                                          |
|   open the input file and skip the "position" signature, |
|   since we know it's a position.lrn file because we are  |
|   *here*.                                                |
|                                                          |
 ----------------------------------------------------------
*/
  learn_in = fopen(args[0],"r");
  eof=fgets(text,80,learn_in);
  if (eof) {
    char *delim;
    delim=strchr(text,'\n');
    if (delim) *delim=0;
    delim=strchr(text,'\r');
    if (delim) *delim=' ';
  }
  if (nargs>1 && !strcmp(args[1],"clear")) {
    fclose(position_file);
#if defined(MACOS)
    sprintf(text,":%s:position.lrn",book_path);
#else
    sprintf(text,"%s/position.lrn",book_path);
#endif
    position_lrn_file=fopen(text,"w");
    if (!position_lrn_file) {
#if defined(MACOS)
      printf("unable to open position learning file [:%s:position.lrn].\n",
             book_path);
#else
      printf("unable to open position learning file [%s/position.lrn].\n",
             book_path);
#endif
      return;
    }
    fprintf(position_lrn_file,"position\n");
#if defined(MACOS)
    sprintf(text,":%s:position.bin",book_path);
#else
    sprintf(text,"%s/position.bin",book_path);
#endif
    position_file=fopen(text,"wb+");
    if (position_file) {
      i=0;
      fseek(position_file,0,SEEK_SET);
      fwrite(&i,sizeof(int),1,position_file);
      i--;
      fwrite(&i,sizeof(int),1,position_file);
    }
    else {
#if defined(MACOS)
      printf("unable to open position learning file [:%s:position.bin].\n",
             book_path);
#else
      printf("unable to open position learning file [%s/position.bin].\n",
             book_path);
#endif
      return;
    }
  }
/*
 ----------------------------------------------------------
|                                                          |
|   loop through the file, reading in 5 records at a time, |
|   the White, Black, Date PGN tags, the setboard FEN, and |
|   the search value/depth to store.                       |
|                                                          |
 ----------------------------------------------------------
*/
  while (1) {
    for (i=0;i<3;i++) {
      eof=fgets(text,80,learn_in);
      if (eof) {
        char *delim;
        delim=strchr(text,'\n');
        if (delim) *delim=0;
        delim=strchr(text,'\r');
        if (delim) *delim=' ';
      }
      else break;
      if (strchr(text,'[')) do {
        char *bracket1, *bracket2;
        char value[32];
  
        bracket1=strchr(text,'\"');
        bracket2=strchr(bracket1+1,'\"');
        if (bracket1 == 0 || bracket2 == 0) break;
        *bracket2=0;
        strcpy(value,bracket1+1);
        if (bracket2 == 0) break;
        if (strstr(text,"White")) strcpy(pgn_white,value);
        if (strstr(text,"Black")) strcpy(pgn_black,value);
      } while(0);
    }
    if (eof == 0) break;
    eof=fgets(text,80,learn_in);
    if (eof) {
      char *delim;
      delim=strchr(text,'\n');
      if (delim) *delim=0;
      delim=strchr(text,'\r');
      if (delim) *delim=' ';
    }
    nargs=ReadParse(text,args," 	;\n");
    if (strcmp(args[0],"setboard"))
      Print(4095,"ERROR.  missing setboard command in file.\n");
    SetBoard(nargs-1,args+1,0);
    eof=fgets(text,80,learn_in);
    if (eof) {
      char *delim;
      delim=strchr(text,'\n');
      if (delim) *delim=0;
      delim=strchr(text,'\r');
      if (delim) *delim=' ';
    }
    else break;
    nargs=ReadParse(text+1,args," 	,;{}\n");
    value=atoi(args[0]);
    move=atoi(args[1]);
    depth=atoi(args[2]);
/*
 ----------------------------------------------------------
|                                                          |
|   now "fill in the blank" and build a table entry from   |
|   current search information.                            |
|                                                          |
 ----------------------------------------------------------
*/
    if (abs(value) < MATE-300)
      word1=Shiftl((BITBOARD) (value+65536),43);
    else if (value > 0)
      word1=Shiftl((BITBOARD) (value+65536),43);
    else
      word1=Shiftl((BITBOARD) (value+65536),43);
    word1=Or(word1,Shiftl((BITBOARD) wtm,63));
    word1=Or(word1,Shiftl((BITBOARD) move,16));
    word1=Or(word1,(BITBOARD) depth);
  
    word2=HashKey;

    fseek(position_file,0,SEEK_SET);
    fread(&positions,sizeof(int),1,position_file);
    fread(&nextp,sizeof(int),1,position_file);
    if (positions < 65536) positions++;
    fseek(position_file,0,SEEK_SET);
    fwrite(&positions,sizeof(int),1,position_file);
    nextp++;
    if (nextp == 65536) nextp=0;
    fwrite(&nextp,sizeof(int),1,position_file);
    fseek(position_file,2*(nextp-1)*sizeof(BITBOARD)+2*sizeof(int),SEEK_SET);
    fwrite(&word1,sizeof(BITBOARD),1,position_file);
    fwrite(&word2,sizeof(BITBOARD),1,position_file);
    added_positions++;
/*
 ----------------------------------------------------------
|                                                          |
|   now update the position.lrn file so that the position  |
|   is saved in a form that can be imported later in other |
|   versions of crafty on different machines.              |
|                                                          |
 ----------------------------------------------------------
*/
    fprintf(position_lrn_file,"[Black \"%s\"]\n",pgn_white);
    fprintf(position_lrn_file,"[White \"%s\"]\n",pgn_black);
    secs=time(0);
    timestruct=localtime((time_t*) &secs);
    fprintf(position_lrn_file,"[Date \"%4d.%02d.%02d\"]\n",timestruct->tm_year+1900,
            timestruct->tm_mon+1,timestruct->tm_mday);
    fprintf(position_lrn_file,"setboard ");
    for (rank=RANK8;rank>=RANK1;rank--) {
      nempty=0;
      for (file=FILEA;file<=FILEH;file++) {
        if (PieceOnSquare((rank<<3)+file)) {
          if (nempty) {
            fprintf(position_lrn_file,"%c",empty[nempty]);
            nempty=0;
          }
          fprintf(position_lrn_file,"%c",xlate[PieceOnSquare((rank<<3)+file)+7]);
        }
        else nempty++;
      }
      fprintf(position_lrn_file,"/");
    }
    fprintf(position_lrn_file," %c ",(wtm)?'w':'b');
    if (WhiteCastle(0) & 1) fprintf(position_lrn_file,"K");
    if (WhiteCastle(0) & 2) fprintf(position_lrn_file,"Q");
    if (BlackCastle(0) & 1) fprintf(position_lrn_file,"k");
    if (BlackCastle(0) & 2) fprintf(position_lrn_file,"q");
    if (EnPassant(0)) fprintf(position_lrn_file," %c%c",File(EnPassant(0))+'a',
                              Rank(EnPassant(0))+((wtm)?-1:+1)+'1');
    fprintf(position_lrn_file,"\n{%d %d %d}\n",value,move,depth);
  }
  Print(4095,"added %d new positions to position.bin\n",added_positions);
  Print(4095,"      %d total positions in position.bin\n",positions);
  fflush(position_file);
  fflush(position_lrn_file);
}

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*   LearnPosition() is the driver for the second phase of Crafty's learning    *
*   code.  this procedure takes the result of selected (or all) searches that  *
*   are done during a game and stores them in a permanent hash table that is   *
*   kept on disk.  before a new search begins, the values in this permanent    *
*   file are copied to the active transposition table, so that the values will *
*   be accessible a few plies earlier than in the game where the positions     *
*   were learned.                                                              *
*                                                                              *
*     bits     name  SL  description                                           *
*       1       wtm  63  used to control which transposition table gets this   *
*                        position.                                             *
*      19     value  43  unsigned integer value of this position + 65536.      *
*                        this might be a good score or search bound.           *
*      21      move  16  best move from the current position, according to the *
*                        search at the time this position was stored.          *
*                                                                              *
*      16     draft   0  the depth of the search below this position, which is *
*                        used to see if we can use this entry at the current   *
*                        position.  note that this is in units of 1/60th of a  *
*                        ply.                                                  *
*      64       key   0  complete 64bit hash key.                              *
*                                                                              *
*    the file will, by default, hold 65536 learned positions.  the first word  *
*  indicates how many positions are actually stored in the file, while the     *
*  second word points to the overwrite point.  once the file reaches the max   *
*  size, this overwrite point will wrap to the beginning so that the file will *
*  always contain the most recent 64K positions.                               *
*                                                                              *
********************************************************************************
*/
void LearnPosition(TREE *tree, int wtm, int last_value, int value) {
  BITBOARD word1, word2;
  int positions, nextp, secs;
  struct tm *timestruct;
  char xlate[15]={'q','r','b',0,'k','n','p',0,'P','N','K',0,'B','R','Q'};
  char empty[9]={' ','1','2','3','4','5','6','7','8'};
  int rank, file, nempty;
/*
 ----------------------------------------------------------
|                                                          |
|   is there anything to learn?  if we are already behind  |
|   a significant amount, losing more is not going to help |
|   learning.  otherwise if the score drops by 1/3 of a    |
|   pawn, remember the position.  if we are way out of the |
|   book, learning won't help either, as the position will |
|   not likely show up again.                              |
|                                                          |
 ----------------------------------------------------------
*/
  if ((!position_lrn_file) || (!position_file)) return;
  if (last_value < -2*PAWN_VALUE) return;
  if (last_value < value+PAWN_VALUE/3) return;
  if (moves_out_of_book > 10) return;
/*
 ----------------------------------------------------------
|                                                          |
|   now "fill in the blank" and build a table entry from   |
|   current search information.                            |
|                                                          |
 ----------------------------------------------------------
*/
  Print(4095,"learning position, wtm=%d  value=%d\n",wtm,value);
  word1=Shiftl((BITBOARD) (value+65536),43);
  word1=Or(word1,Shiftl((BITBOARD) wtm,63));
  word1=Or(word1,Shiftl((BITBOARD) tree->pv[0].path[1],16));
  word1=Or(word1,(BITBOARD) (tree->pv[0].path_iteration_depth*INCREMENT_PLY-INCREMENT_PLY));

  
  word2=HashKey;

  fseek(position_file,0,SEEK_SET);
  fread(&positions,sizeof(int),1,position_file);
  fread(&nextp,sizeof(int),1,position_file);
  if (positions < 65536) positions++;
  fseek(position_file,0,SEEK_SET);
  fwrite(&positions,sizeof(int),1,position_file);
  nextp++;
  if (nextp == 65536) nextp=0;
  fwrite(&nextp,sizeof(int),1,position_file);
  fseek(position_file,2*nextp*sizeof(BITBOARD)+2*sizeof(int),SEEK_SET);
  fwrite(&word1,sizeof(BITBOARD),1,position_file);
  fwrite(&word2,sizeof(BITBOARD),1,position_file);
  fflush(position_file);
/*
 ----------------------------------------------------------
|                                                          |
|   now update the position.lrn file so that the position  |
|   is saved in a form that can be imported later in other |
|   versions of crafty on different machines.              |
|                                                          |
 ----------------------------------------------------------
*/
  fprintf(position_lrn_file,"[White \"%s\"]\n",pgn_white);
  fprintf(position_lrn_file,"[Black \"%s\"]\n",pgn_black);
  secs=time(0);
  timestruct=localtime((time_t*) &secs);
  fprintf(position_lrn_file,"[Date \"%4d.%02d.%02d\"]\n",timestruct->tm_year+1900,
          timestruct->tm_mon+1,timestruct->tm_mday);
  fprintf(position_lrn_file,"setboard ");
  for (rank=RANK8;rank>=RANK1;rank--) {
    nempty=0;
    for (file=FILEA;file<=FILEH;file++) {
      if (PieceOnSquare((rank<<3)+file)) {
        if (nempty) {
          fprintf(position_lrn_file,"%c",empty[nempty]);
          nempty=0;
        }
        fprintf(position_lrn_file,"%c",xlate[PieceOnSquare((rank<<3)+file)+7]);
      }
      else nempty++;
    }
    fprintf(position_lrn_file,"/");
  }
  fprintf(position_lrn_file," %c ",(wtm)?'w':'b');
  if (WhiteCastle(0) & 1) fprintf(position_lrn_file,"K");
  if (WhiteCastle(0) & 2) fprintf(position_lrn_file,"Q");
  if (BlackCastle(0) & 1) fprintf(position_lrn_file,"k");
  if (BlackCastle(0) & 2) fprintf(position_lrn_file,"q");
  if (EnPassant(0)) fprintf(position_lrn_file," %c%c",File(EnPassant(0))+'a',
                            Rank(EnPassant(0))+'1');
  fprintf(position_lrn_file,"\n{%d %d %d}\n",value,
          tree->pv[0].path[1],
          tree->pv[0].path_iteration_depth*INCREMENT_PLY);
  fflush(position_lrn_file);
}

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*   simply read from the learn.bin file, and stuffed into the correct table.   *
*                                                                              *
********************************************************************************
*/
void LearnPositionLoad(TREE *tree) {
  register BITBOARD temp_hash_key;
  BITBOARD word1, word2, worda, wordb;
  register HASH_ENTRY *htable;
  int n, positions;
/*
 ----------------------------------------------------------
|                                                          |
|   If position learning file not accessible: exit.  also, |
|   if the time/move is very short, skip this.             |
|                                                          |
 ----------------------------------------------------------
*/
  if (!position_file) return; 
  if (time_limit < 100) return;
/*
 ----------------------------------------------------------
|                                                          |
|   first, find out how many learned positions are in the  |
|   file and set up to start reading/stuffing them.        |
|                                                          |
 ----------------------------------------------------------
*/
  if (moves_out_of_book >= 10) return;
  fseek(position_file,0,SEEK_SET);
  fread(&positions,sizeof(int),1,position_file);
  fseek(position_file,2*sizeof(int),SEEK_SET);
/*
 ----------------------------------------------------------
|                                                          |
|   first, find out how many learned positions are in the  |
|   file and set up to start reading/stuffing them.        |
|                                                          |
 ----------------------------------------------------------
*/
  for (n=0;n<positions;n++) {
    fread(&word1,sizeof(BITBOARD),1,position_file);
    fread(&word2,sizeof(BITBOARD),1,position_file);
    temp_hash_key=word2;
    htable=((Shiftr(word1,63)) ? trans_ref_wa : trans_ref_ba)+
      (((int) temp_hash_key)&hash_maska);
    temp_hash_key=temp_hash_key>>16;
    wordb=Shiftr(word2,16);
    wordb=Or(wordb,Shiftl(And(word1,mask_112),48));

    worda=Shiftl((BITBOARD) (Shiftr(word1,43) & 01777777),21);
    worda=Or(worda,Shiftl((BITBOARD) 3,59));
    htable->word1=worda;
    htable->word2=wordb;
  }
}

/* last modified 03/11/98 */
/*
********************************************************************************
*                                                                              *
*   LearnResult() is used to learn from the result of a game that was played   *
*   to mate or resignation.  Crafty will use the result (if it lost the game)  *
*   to mark the book move made at the last position where it had more than one *
*   book move, so that that particular move won't be played again, just as if  *
*   the learned-value was extremely bad.                                       *
*                                                                              *
********************************************************************************
*/

void LearnResult(TREE *tree, int wtm) {
  int move, i, j;
  int secs, last_choice;
  char cmd[32], buff[80], *nextc;
  struct tm *timestruct;
  int n_book_moves[512];
  int t_wtm=wtm;
/*
 ----------------------------------------------------------
|                                                          |
|   first, we are going to find every book move in the     |
|   game, and note how many alternatives there were at     |
|   every book move.                                       |
|                                                          |
 ----------------------------------------------------------
*/
  if (!book_file) return;
  if (!(learning & result_learning)) return;
  Print(128,"LearnResult() executed\n");
  learning&=~result_learning;
  InitializeChessBoard(&tree->position[0]);
  for (i=0;i<512;i++) n_book_moves[i]=0;
  wtm=1;
  for (i=0;i<512;i++) {
    int *mv, cluster, key, test;
    BITBOARD common, temp_hash_key;

    n_book_moves[i]=0;
    strcpy(cmd,"");
    fseek(history_file,i*10,SEEK_SET);
    fscanf(history_file,"%s",cmd);
    if (strcmp(cmd,"pass")) {
      move=InputMove(tree,cmd,0,wtm,1,0);
      if (!move) break;
      tree->position[1]=tree->position[0];
      tree->last[1]=GenerateCaptures(tree, 1, wtm, tree->last[0]);
      tree->last[1]=GenerateNonCaptures(tree, 1, wtm, tree->last[1]);
      test=HashKey>>49;
      fseek(book_file,test*sizeof(int),SEEK_SET);
      fread(&key,sizeof(int),1,book_file);
      if (key > 0) {
        fseek(book_file,key,SEEK_SET);
        fread(&cluster,sizeof(int),1,book_file);
        fread(book_buffer,sizeof(BOOK_POSITION),cluster,book_file);
      }
      else cluster=0;
      for (mv=tree->last[0];mv<tree->last[1];mv++) {
        common=And(HashKey,mask_16);
        MakeMove(tree, 1,*mv,wtm);
        temp_hash_key=Xor(HashKey,wtm_random[wtm]);
        temp_hash_key=Or(And(temp_hash_key,Compl(mask_16)),common);
        for (j=0;j<cluster;j++)
          if (!Xor(temp_hash_key,book_buffer[j].position) &&
              book_buffer[j].learn > (float) LEARN_COUNTER_BAD/100.0)
            n_book_moves[i]++;
        UnMakeMove(tree, 1,*mv,wtm);
      }
      if (move) MakeMoveRoot(tree, move,wtm);
    }
    wtm=ChangeSide(wtm);
  } 
/*
 ----------------------------------------------------------
|                                                          |
|   now, find the last book move where there was more than |
|   one alternative to choose from.                        |
|                                                          |
 ----------------------------------------------------------
*/
  if (!t_wtm) {
    for (last_choice=511;last_choice>=0;last_choice-=2)
      if (n_book_moves[last_choice] > 1) break;
  }
  else {
    for (last_choice=510;last_choice>=0;last_choice-=2)
      if (n_book_moves[last_choice] > 1) break;
  }
  if (last_choice < 0) return;
/*
 ----------------------------------------------------------
|                                                          |
|   finally, we run thru the book file and update each     |
|   book move learned value based on the computation we    |
|   calculated above.                                      |
|                                                          |
 ----------------------------------------------------------
*/
  InitializeChessBoard(&tree->position[0]);
  wtm=1;
  for (i=0;i<512;i++) {
    strcpy(cmd,"");
    fseek(history_file,i*10,SEEK_SET);
    strcpy(cmd,"");
    fscanf(history_file,"%s",cmd);
    if (strcmp(cmd,"pass")) {
      move=InputMove(tree,cmd,0,wtm,1,0);
      if (!move) break;
      tree->position[1]=tree->position[0];
      if (i == last_choice) LearnBookUpdate(tree, wtm, move, -999.0);
      MakeMoveRoot(tree, move,wtm);
    }
    wtm=ChangeSide(wtm);
  } 
/*
 ----------------------------------------------------------
|                                                          |
|   now update the "book.lrn" file so that this can be     |
|   shared with other crafty users or else saved in case.  |
|   the book must be re-built.                             |
|                                                          |
 ----------------------------------------------------------
*/
  fprintf(book_lrn_file,"[White \"%s\"]\n",pgn_white);
  fprintf(book_lrn_file,"[Black \"%s\"]\n",pgn_black);
  secs=time(0);
  timestruct=localtime((time_t*) &secs);
  fprintf(book_lrn_file,"[Date \"%4d.%02d.%02d\"]\n",timestruct->tm_year+1900,
          timestruct->tm_mon+1,timestruct->tm_mday);
  nextc=buff;
  for (i=0;i<=last_choice;i++) {
    fseek(history_file,i*10,SEEK_SET);
    fscanf(history_file,"%s",cmd);
    if (strchr(cmd,' ')) *strchr(cmd,' ')=0;
    sprintf(nextc," %s",cmd);
    nextc=buff+strlen(buff);
    if (nextc-buff > 60) {
      fprintf(book_lrn_file,"%s\n",buff);
      nextc=buff;
      strcpy(buff,"");
    }
  }
  fprintf(book_lrn_file,"%s {%d %d %d}\n",
          buff, (t_wtm) ? -99900:99900, 10, 0);
  fflush(book_lrn_file);
/*
 ----------------------------------------------------------
|                                                          |
|   done.  now restore the game back to where it was       |
|   before we started all this nonsense.  :)               |
|                                                          |
 ----------------------------------------------------------
*/
  RestoreGame();
}
