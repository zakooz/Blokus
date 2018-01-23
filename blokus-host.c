/*
    HEART 2014 Design Competition test (and will be the host) program 

    Platform: 
      - Developed on FreeBSD 8.3 (amd64).
      - On other platforms, 
          - modify /dev entries in serial_dev[]
          - init_serial() may be have to be modified
      - PATCHES FOR OTHER PLATFORM ARE WELCOME!

    Usage:
      See http://lut.eee.u-ryukyu.ac.jp/dc14/tools.html .
    
      To interrupt this program in interactive mode, press Ctrl+D (not Ctrl+C).
      This will transmit "9" to the serial port to make the game over.

    License:
      - Yasunori Osana <osana@eee.u-ryukyu.ac.jp> wrote this file.
      - This file is provided "AS IS" in the beerware license rev 42.
        (see http://people.freebsd.org/~phk/)

    Acknowledgements:
      - Cygwin / AI player compatibility provided by Prof. Akira Kojima 
        at Hiroshima city university
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "pieces.h"
#include "rotate.h"
#include "blokus-host.h"

#define BORDER 3

#define TRUE (1==1)
#define FALSE (!TRUE)

#define PIECE_ALREADY_PLACED -1
#define GRID_ALREADY_OCCUPIED -2
#define SHARES_EDGE -3
#define MUST_SHARE_VERTEX -4
#define MUST_COVER_FIRST_PLACE -5
#define MUST_COVER_SECOND_PLACE -6
#define TIMED_OUT -10
#define BROKEN_CODE -11
#define NO_PASS_ON_FIRST -12

#define TERMINATE_NORMAL -1
#define TERMINATE_WRONG -2

const int serial_devs = 6;
// const char* serial_dev[6] = { "/tmp/fifo0", "/tmp/fifo1", "/tmp/fifo2", "/tmp/fifo3", "/tmp/fifo4", "/tmp/fifo5" };
const char* serial_dev[6] = { "/dev/pts/7", "/dev/pts/2", "/dev/pts/3", "/dev/pts/4", "/dev/pts5", "/dev/pts7" };

int serial_fd[2];
struct termios oldtio[2], tio[2];
char team_ids[2][3] = { "01", "02" };

int board[16][16];
int available[2][21];
int bonus[2] = { 0, 0 };

int show_board_status = TRUE;         // -b
int show_placed_tile = FALSE;         // -p
int show_available_tiles = TRUE;      // -t
int show_hint = FALSE;                // -h
int on_serial = 0;                    // -1, -2, -3
int use_tcp = FALSE;
int first_player = 1;
int move_timeout = 1;
int auto_select = -1;

int error_code = 0;

int timeval_subtract(struct timeval *a, struct timeval *b){
  // return a-b in milliseconds
  
  return (a->tv_usec - b->tv_usec)/1000 + (a->tv_sec - b->tv_sec)*1000;
}

int read_all(int fd, int len, char* buf){
  // read LEN bytes from FD, but without CR and LF.
  int got = 0;
  struct timeval start, timelimit, stop;
  fd_set read_fds, write_fds, except_fds;

  FD_ZERO(&write_fds);
  FD_ZERO(&except_fds);

  gettimeofday(&start, NULL);
  timelimit = start;
  timelimit.tv_sec = timelimit.tv_sec + move_timeout;

  do {
    struct timeval now, timeout;
    int timeout_ms;

    gettimeofday(&now, NULL);
    timeout_ms = timeval_subtract(&timelimit, &now);
    if (timeout_ms <= 0) break; // no remaining time
#ifdef DEBUG
    printf("remaining time: %d\n", timeout_ms);
#endif

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    if (select(fd+1, &read_fds, &write_fds, &except_fds, &timeout) == 1){
      read(fd, &buf[got], 1);
    } else {
      printf("timeout!\n");
      got = 0;
      break; // timeout
    }

    if(buf[got] != 0x0d && buf[got] != 0x0a) got++;
  } while(got < len);

  gettimeofday(&stop, NULL);
#ifdef DEBUG
  printf("read %d bytes in %d msec.\n", got, timeval_subtract(&stop, &start));
#endif
  return got;
}

void show_board(){
  int i, x, y;
  if (show_available_tiles){
    printf("----------------- Available Tiles -----------------\n");
    printf("         ");
    for(i=0; i<21; i++) printf(" %c", 'a'+i); printf("\n");
    printf("Player 1:");
    for(i=0; i<21; i++) printf(" %d", available[0][i]); printf("\n");
    printf("Player 2:");
    for(i=0; i<21; i++) printf(" %d", available[1][i]); printf("\n");
  }

  if (show_board_status){
    printf("---------------------- Board ----------------------\n  ");
    for(x=0; x<16; x++)
      printf(" %c", ((x<10) ? x+'0' : x+'a'-10));
    printf("\n");
    for(y=0; y<16; y++){
      printf(" %c", ((y<10) ? y+'0' : y+'a'-10));
      for(x=0; x<16; x++){
        printf(" %c", ( board[y][x] == 0 ? ' ' : board[y][x] < 3 ? board[y][x]+'0' : '+'));
      }
      printf("\n");
    }
  }
  printf("---------------------------------------------------\n");
  fflush(stdout);
}

int check_code(char* code){
  char c;

  // pass is a valid code!
  if(code[0]=='0' && code[1]=='0' && code[2]=='0' && code[3]=='0') return TRUE;

  c=code[0];
  if(! (('0'<=c && c<='9') || ('a'<= c && c<='e')) ) return FALSE;

  c=code[1];
  if(! (('0'<=c && c<='9') || ('a'<= c && c<='e')) ) return FALSE;

  c=code[2];
  if(! ('a'<= c && c<='u') ) return FALSE;

  c=code[3];
  if(! ('0'<= c && c<='7') ) return FALSE;

  return TRUE;
}

move decode_code(char* code){
  // code must be valid!
  move m;
  char c;

  c=code[0];
  if('0'<=c && c<='9') m.x = c-'0';
  if('a'<=c && c<='e') m.x = c-'a'+10;

  c=code[1];
  if('0'<=c && c<='9') m.y = c-'0';
  if('a'<=c && c<='e') m.y = c-'a'+10;

  c=code[2];
  m.piece = c-'a';

  c=code[3];
  m.rotate = c-'0';

  if (show_placed_tile){
    int x,y;
    printf("------------------ Tile Pattern -------------------\n");

    printf("x: %d, y: %d, piece: %d, rotate: %d\n", m.x, m.y, m.piece, m.rotate);

    for(y=0; y<5; y++){
      for(x=0; x<5; x++){
        printf("%d ", pieces[m.piece][0][rotate[m.rotate][y][x]]);
      }
      printf("\n");
    }
  }

  return m;
}

int interactive(int p){
  return (on_serial == 0) ||
    (on_serial == 1 && p == 2) ||
    (on_serial == 2 && p == 1);
}

char* prompt(int p, char* code, char* prev_code, int must_pass, int turn){
  switch (turn+1){
  case 1:
    if (p==1) printf("turn %d: must cover (5,5)\n", turn);
    else      printf("turn %d: must cover (a,a)\n", turn);
    break;
    
  default:
    printf("turn %d\n", turn);
    break;
  }
    
  if ( interactive(p) ){ 
    // interactive
    if (must_pass){
      printf("(not asking move on console)\n");
      strcpy(code, "0000");
      return code;
    } else {
      printf("Player %d:", p);
      fflush(stdout);

      switch(auto_select){
      case 1:
	auto0(p, turn, code);
	break;
      case 2:
	auto1(p, turn, code);
	break;
      default:
	fgets(code, 6, stdin);
	if(feof(stdin)){
	  code[0] = 0;
	  return NULL;
	}
      }

      code[strlen(code)-1] = 0;
      return code;
    }
  } else {
    // serial
    char prompt_buf[10];
    char code_buf[10];
    int port;

    if(prev_code[0] == 0) printf("no prev code\n");
    else printf("prev code = %s\n", prev_code);

    port = (on_serial == 3) ? p-1 : 0;
    
    printf("TURN+1=%d\n", turn+1);
    switch(turn+1){
    case 1: 
      if (p==1) strcpy(prompt_buf, "25"); 
      else      strcpy(prompt_buf, "2A"); 
      break;
    case 2: 
      if (p==1) sprintf(prompt_buf, "35%s", prev_code); 
      else      sprintf(prompt_buf, "3A%s", prev_code); 
      break;
    default: snprintf(prompt_buf, 6, "4%s", prev_code);
    }

    printf("serial prompt string: %s (%d bytes)\n", 
	   prompt_buf, (int)strlen(prompt_buf));

    write(serial_fd[port], prompt_buf, strlen(prompt_buf));

    if(read_all(serial_fd[port], 4, code_buf) != 4) return NULL;
    code_buf[4] = 0;
    strcpy(code, code_buf);
    printf("(got from serial %d: %s)\n", port, code);
    return code;
  }

  // should not come here
  return NULL;
}

int check_move(int player, int turn, move m){
  //  printf("check_move: player %d, turn %d\n", player, turn);
  int c, r, x, y, x_offset, y_offset;
    
  c = m.piece;
  r = m.rotate;
  x_offset = m.x-2;
  y_offset = m.y-2;
    
  // Check availability
  if(available[player-1][c] == 0){
    return PIECE_ALREADY_PLACED;
  }

  // No piece on already occupied grid
  for(y=0; y<5; y++){
    for(x=0; x<5; x++){
      int b;
      b = pieces[c][0][rotate[r][y][x]];
      if (b==1){
        if (board[y_offset+y][x_offset+x] != 0 ||
            y_offset+y < 0 || 15 < y_offset+y ||
            x_offset+x < 0 || 15 < x_offset+x
            ){
          return GRID_ALREADY_OCCUPIED;
        }
      }
    }
  }

  // New piece can't share the edge
  for(y=0; y<5; y++){
    for(x=0; x<5; x++){
      int b;
      b = pieces[c][0][rotate[r][y][x]];
      if (b==1){
        int xx, yy;
        xx = x_offset+x;
        yy = y_offset+y;
        if (board[yy][xx-1] == player || board[yy][xx+1] == player ||
            board[yy-1][xx] == player || board[yy+1][xx] == player ){
          return SHARES_EDGE;
        }
      }
    }
  }

  // must share the vertex
  if(turn >= 2){
    int got_it = FALSE;
    for(y=0; y<5; y++){
      for(x=0; x<5; x++){
        int b;
        b = pieces[c][0][rotate[r][y][x]];
        if (b==1){
          int xx, yy;
          xx = x_offset+x;
          yy = y_offset+y;
          if (board[yy-1][xx-1] == player || board[yy+1][xx-1] == player ||
              board[yy-1][xx+1] == player || board[yy+1][xx+1] == player){
            got_it = TRUE;
          }
        }
      }
    }
    if(!got_it){
      return MUST_SHARE_VERTEX;
    }
  } else {
    // player 1's first must cover (5,5)
    if(turn < 2){
      if (player == 1 && !(3<= m.x && m.x <= 7 && 3 <= m.y && m.y <= 7 &&
			   pieces[c][0][rotate[r][ 5-y_offset][ 5-x_offset]]==1 )) {
	return MUST_COVER_FIRST_PLACE;
      }
      // player 2's first move must cover (a,a)
      if (player == 2 && !(8<= m.x && m.x <= 12 && 8 <= m.y && m.y <= 12 &&
			   pieces[c][0][rotate[r][10-y_offset][10-x_offset]]==1 )) {
	return MUST_COVER_SECOND_PLACE;
      }
    }
  }

  return 0;
}

void show_error(int e){
  error_code = e;
  switch(e){
  case PIECE_ALREADY_PLACED:
    printf("INVALID MOVE! (the piece is already placed)\n");
    break;
  case GRID_ALREADY_OCCUPIED:
    printf("INVALID MOVE! (move on where already occupied)\n");
    break;
  case SHARES_EDGE:
    printf("INVALID MOVE! (shares edge)\n");
    break;
  case MUST_SHARE_VERTEX:
    printf("INVALID MOVE! (must share vertex)\n");
    break;
  case MUST_COVER_FIRST_PLACE:
    printf("INVALID MOVE! (player 1's first move must cover (5,5)\n");
    break;
  case MUST_COVER_SECOND_PLACE:
    printf("INVALID MOVE! (player 2's first move must cover (a,a))\n");
    break;
  }
}

int check_possibility(int player, int turn){
  move m;

  for (m.y=1; m.y<=15; m.y++){
    for (m.x=1; m.x<15; m.x++){
      for (m.piece=0; m.piece<21; m.piece++){
        for(m.rotate=0; m.rotate<8; m.rotate++){
          if(check_move(player, turn, m) == 0){
            if (show_hint){
              printf("First-found possible move: %c%c%c%c\n", 
                     ((m.x<10) ? m.x+'0' : m.x+'a'-10),
                     ((m.y<10) ? m.y+'0' : m.y+'a'-10),
                     m.piece+'a', m.rotate+'0');
            }
            return TRUE;
          }
        }
      }
    }
  }
  return FALSE;
}

int next_player(int player){
  if(player == 1) return 2;
  return 1;
}

int remaining_size(player){ // player 1 or 2 (not 0 or 1)
  int i, a=0;
  for(i=0; i<21; i++)
    a += available[player-1][i] * piece_sizes[i];

  return a;
}

void init_tcp(int p){
  int sock, on;
  struct sockaddr_in s_addr;

  on = 1;
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock<0){
    perror("opening stream socket failed! ");
    exit(-1);
  }
  on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
#if defined (__FreeBSD__)  || (__APPLE__)
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (char*)&on, sizeof(on));
#endif
      
  bzero((void*)&s_addr, sizeof(s_addr));

  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = INADDR_ANY;
  s_addr.sin_port = htons(10000 + p);

  if(bind(sock, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0){
    perror("bind socket failed! ");
    exit(-1);
  }

  listen(sock, 0); // only 1 client per port
  printf("Waiting on TCP port %d...\n", 10000+p);

  serial_fd[p] = accept(sock, NULL, NULL);
  if(serial_fd[p] == -1){
    perror("accept() failed! ");
    exit(-1);
  }
}

void init_serial(){
  int p, pp;
  int ports = 1;

  //  const char flush_code[11] = "9999999999";
  const char init_code[2] = "0";
  char team_id[4];

  if (on_serial == 0) return;
  if (on_serial == 3) ports = 2;

  pp = 0;
  for (p=0; p<ports; p++){
    if(!use_tcp){ // init serial

      for(/* pp=pp */ ; pp<serial_devs; pp++){
	printf("Try to open %s: ", serial_dev[pp]);
	serial_fd[p] = open(serial_dev[pp], O_RDWR | O_NOCTTY);
	if (serial_fd[p] < 0 ) {
	  printf("Failed.\n");
	}
	if (serial_fd[p] >= 0){
	  printf("Success!\n");
	  pp++;
	  break;
	}
      }
      if (pp == serial_devs && serial_fd[p] < 0){
	printf("Couldn't open all required serial devs.\n");
	exit(-1);
      }
    
      tcgetattr(serial_fd[p], &oldtio[p]); // backup port settings
      tcflush(serial_fd[p], TCIOFLUSH);
  
      memset(&tio[p], 0, sizeof(tio[p]));
      cfsetispeed(&tio[p], B115200);
      cfsetospeed(&tio[p], B115200);
      tio[p].c_cflag |= CS8; // 8N1
      tio[p].c_cflag |= CLOCAL; // local connection, no modem control
      tio[p].c_cflag |= CREAD;

      tcsetattr(serial_fd[p], TCSANOW, &tio[p]);
      tcflush(serial_fd[p], TCIOFLUSH);
    } else { // init tcp
      init_tcp(p);
    }

    // flush 
    write(serial_fd[p], init_code, strlen(init_code));

    if (read_all(serial_fd[p], 3, team_id) != 3){
      printf("Timeout while waiting team code on serial port %d!\n", p);
      p --;
      if (pp==serial_devs) exit(-1);
      
      continue;
      //      exit(-1);
    }
    team_id[3] = 0;
    printf("Team code on serial %d: %s\n", p, &team_id[1]);
    strncpy(team_ids[ ((on_serial==2) ? 1 : p) ], &team_id[1], 2);
  } // for (p<ports)
}

void close_serial(){
  int p;
  int ports = 1;

  const char close_code[2] = "9";

  if (on_serial == 0) return;
  if (on_serial == 3) ports = 2;
  
  printf("sending termination code to serial port(s).\n");
  
  for (p=0; p<ports; p++){
    write(serial_fd[p], close_code, 1);
    if(!use_tcp){
      tcdrain(serial_fd[p]);
      usleep(300*1000);
      tcsetattr(serial_fd[p], TCSANOW, &oldtio[p]);
    }
    close(serial_fd[p]);
  }
}

void auto0(int player, int turn, char* code){
  move m;

  for (m.y=1; m.y<=15; m.y++){
    for (m.x=1; m.x<15; m.x++){
      for (m.piece=0; m.piece<21; m.piece++){
        for(m.rotate=0; m.rotate<8; m.rotate++){
          if(check_move(player, turn, m) == 0){
              sprintf(code, "%c%c%c%c\n", 
		      ((m.x<10) ? m.x+'0' : m.x+'a'-10),
		      ((m.y<10) ? m.y+'0' : m.y+'a'-10),
		      m.piece+'a', m.rotate+'0');
            return;
          }
        }
      }
    }
  }
  strcpy(code, "0000\n");
  return;
}

// simple AI by Prof. Kojima
int auto1_plist[] = {
  0x77, 0x87, 0x78, 0x88, 0x66, 0x76, 0x86, 0x96, 0x69, 0x79, 0x89, 0x99, 0x67, 0x68, 0x97, 0x98,
  0x55, 0x65, 0x75, 0x85, 0x95, 0xa5, 0x5a, 0x6a, 0x7a, 0x8a, 0x9a, 0xaa, 0x56, 0x57, 0x58, 0x59,
  0xa6, 0xa7, 0xa8, 0xa9, 0x44, 0x54, 0x64, 0x74, 0x84, 0x94, 0xa4, 0xb4, 0x4b, 0x5b, 0x6b, 0x7b,
  0x8b, 0x9b, 0xab, 0xbb, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
  0x33, 0x43, 0x53, 0x63, 0x73, 0x83, 0x93, 0xa3, 0xb3, 0xc3, 0x3c, 0x4c, 0x5c, 0x6c, 0x7c, 0x8c,
  0x9c, 0xac, 0xbc, 0xcc, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0xc4, 0xc5, 0xc6, 0xc7,
  0xc8, 0xc9, 0xca, 0xcb, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x82, 0x92, 0xa2, 0xb2, 0xc2, 0xd2,
  0x2d, 0x3d, 0x4d, 0x5d, 0x6d, 0x7d, 0x8d, 0x9d, 0xad, 0xbd, 0xcd, 0xdd, 0x23, 0x24, 0x25, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc,
  0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71, 0x81, 0x91, 0xa1, 0xb1, 0xc1, 0xd1, 0xe1, 0x1e, 0x2e,
  0x3e, 0x4e, 0x5e, 0x6e, 0x7e, 0x8e, 0x9e, 0xae, 0xbe, 0xce, 0xde, 0xee, 0x12, 0x13, 0x14, 0x15,
  0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xeb, 0xec, 0xed, 0x00
};

void auto1(int player, int turn, char* code) {
  move m;
  int *p;

  for (m.piece = 20; m.piece >= 0; m.piece--) {
    for (p = auto1_plist; *p != 0; p++) {
      m.y = *p >> 4;
      m.x = *p & 0xf;
      for (m.rotate=0; m.rotate<8; m.rotate++) {
        if (check_move(player, turn, m) == 0) {
          // found the first move
          code[0] = ((m.x < 10) ? m.x + '0' : m.x + 'a' - 10);
          code[1] = ((m.y < 10) ? m.y + '0' : m.y + 'a' - 10);
          code[2] = m.piece + 'a';
          code[3] = m.rotate + '0';
          code[4] = '\n';
          code[5] = '\0';
          return;
        }
      }
    }
  }
  // pass
  strcpy(code, "0000\n");
  return;
}

void usage(){
  printf("Command line options: \n" \
	 "   -a X: Automatic player instead of interactive player\n"\
	 "          X=1: simple mode: moves same as -h\n"\
	 "          X=2: advanced mode\n"\
         "   -b: Hide board status\n" \
         "   -p: Show placed tile on move\n"\
         "   -t: Don't show available tiles\n"\
         "   -h: Show hint (for quick testplay)\n"\
	 "   -r: Player 2 moves first\n"\
         "   -1: First move player on serial port 0\n"\
         "   -2: Second move player on serial port 0\n"\
         "   -3: Players on serial port 0 and 1. (still does NOT work)\n"\
         "   -T: tcp port 10000+10001 instead of serial port\n"\
	 "   -o XX: set timeout to XX seconds (default: 1)\n"\
         ""
         );
}

int do_game(){
  int c, x, y, r, player, turn;
  char code[6], prev_code[6];

  // ------------------------------------------------------------
  // start!
   
  show_board();

  player = first_player;
  turn = 0;
  prev_code[0] = 0;

  while(prompt(player, code, prev_code, 
	       !check_possibility(player, turn), turn)){
    move m;
    int e, x_offset, y_offset;
    if(code[0] == 0) return TERMINATE_WRONG;  // ctrl+d

    // retry if invalid code
    while(!check_code(code)){
      if(interactive(player)){
	prompt(player, code, prev_code, FALSE, turn);
      } else {
	error_code = BROKEN_CODE;
	printf("Invalid code on serial port.\n");
	return player;
      }
      if(code[0] == 0) return TERMINATE_WRONG;
    }
    if(code[0] == 0) return TERMINATE_WRONG;  // ctrl+d
    if(strcmp(prev_code, "0000") == 0 &&
       strcmp(code,      "0000") == 0){
      printf("Both pass!\n");

      show_hint = FALSE;

      if(! (check_possibility(player, turn) ||
	    check_possibility(next_player(player), turn-1))){
	return TERMINATE_NORMAL;
      }
      return TERMINATE_NORMAL; // is this OK?
    }

    strcpy(prev_code, code);

    // pass
    if(strcmp(code, "0000") == 0){
      if(turn >= 2){
	player = next_player(player);
	turn++;
	continue;
      } else {
	printf("First move must not be a pass.\n");
	return player;
      }
    }

    m = decode_code(code);

    c = m.piece;
    r = m.rotate;
    x_offset = m.x-2;
    y_offset = m.y-2;
    

    if((e = check_move(player, turn, m)) != 0){
      show_error(e);
      return player; 
    }
  
    // OK, now place the move
    for(y=0; y<5; y++){
      for(x=0; x<5; x++){
        int b;
        b = pieces[c][0][rotate[r][y][x]];
        if (b==1)
          board[y_offset+y][x_offset+x] = player;
      }
    }
    available[player-1][c] = 0;

    if(remaining_size(player)==0){
      bonus[player-1] = 15;
      if(c==0) bonus[player-1] = 20; // end with monomino
      show_board();
      printf("Player %d won the game, since the player has no more pieces.\n",
	     player);
      return TERMINATE_NORMAL;
    }
  
    // show the board & next player
    show_board();
    player = next_player(player);
    turn++;
  }

  error_code = TIMED_OUT;
  printf("Player %d timed out.\n", player);
  return player;
}



int main(int argc, char* argv[]){
  int x, y, result;

  int ch;
  while ((ch = getopt(argc, argv, "a:bpthro:123T?")) != -1) {
    switch (ch) {
    case 'a': auto_select = atoi(optarg); break;
    case 'b': show_board_status = FALSE;  break;
    case 'p': show_placed_tile = TRUE; break;
    case 't': show_available_tiles = FALSE; break;
    case 'h': show_hint = TRUE; break;
    case 'r': first_player = 2; break; // player 2 moves first
    case '1': on_serial = 1; break; // player 1 on serial
    case '2': on_serial = 2; break; // player 2 on serial
    case '3': on_serial = 3; break; // both player on serial
    case 'T': use_tcp = TRUE; break; // use TCP
    case 'o': move_timeout = atoi(optarg); break; // set timeout
    case '?':
    default:
      usage();
      return 0;
    }

    switch(auto_select){
    case -1: case 1: case 2:
      break;
    default:
      usage();
      return 0;
    }
  }

  init_serial();

  printf(">> %s %s vs %s %s \n", 
	 team_ids[0], ((first_player==1) ? "*" : ""),
	 team_ids[1], ((first_player==2) ? "*" : ""));

  // ------------------------------
  // clear board & available pieces

  for(y=0; y<16; y++)
    for(x=0; x<16; x++)
      board[y][x] = 0;

  for(y=0; y<2; y++)
    for(x=0; x<21; x++)
      available[y][x] = 1;

  // setup board: border is already filled
  for(x=0; x<16; x++){
    board[ 0][ x] = BORDER;
    board[15][ x] = BORDER;
    board[ x][ 0] = BORDER;
    board[ x][15] = BORDER;
  }

#ifdef DEBUG_FILL
  // for test (some grids already occupied on start)
  for(y=7; y<=14; y++)
    for(x=12; x<=14; x++)
      board[y][x] = 2;

  for(y=12; y<=14; y++)
    for(x=7; x<=14; x++)
      board[y][x] = 2;
#endif

#ifdef DEBUG_LESS_TILES
  for(x=5; x<21; x++){
    available[0][x] = 0;
    available[1][x] = 0;
  }
#endif


  switch(result = do_game()){
    int a1, a2;
    
  case TERMINATE_NORMAL:
    a1 = remaining_size(1);
    a2 = remaining_size(2);
    
    printf("** Tiles: %d / %d, Score: %d / %d. ",
	   a1, a2, bonus[0]-a1, bonus[1]-a2);
    if(a1 != a2)
      printf("Player %d won the game!\n", ( (a1<a2) ? 1 : 2 ) );
    else
      printf("Draw game.\n");
    break;

  case 1:
  case 2:
    printf("** Player %d lost the game by violation (code %d). Tiles: %d / %d, Score: %d / %d\n", result, error_code,
	   remaining_size(1), remaining_size(2),
	   -remaining_size(1), -remaining_size(2));
    break;

  default:
  case TERMINATE_WRONG:
    printf("** Game terminated unexpectedly. %d / %d\n",
	   remaining_size(1), remaining_size(2));
    break;
  }
  
  close_serial();
  return 0;
}
