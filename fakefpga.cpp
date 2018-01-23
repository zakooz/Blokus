/**
 * Serial port emulation layer
 * Compile with: g++ main.cpp -lpthread -o main
 *
 * Based on https://github.com/cymait/virtual-serial-port-example 
 * Cymait http://cymait.com
 *
 * Modified by Tiago Campos
 *
 **/

#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>

#include "pieces.h"
#include "rotate.h"

using namespace std;

#define BUFFER_SIZE 128
#define BAUDRATE    B115200

#define PIECE_ALREADY_PLACED -1
#define GRID_ALREADY_OCCUPIED -2
#define SHARES_EDGE -3
#define MUST_SHARE_VERTEX -4
#define MUST_COVER_FIRST_PLACE -5
#define MUST_COVER_SECOND_PLACE -6
#define TIMED_OUT -10
#define BROKEN_CODE -11
#define NO_PASS_ON_FIRST -12

int board[16][16];
int available[2][21];

typedef struct {
  int x;
  int y;
  int piece;
  int rotate;
} move;

int stop = 0;

void usage(char* cmd) {
    std::cerr << "usage: " << cmd << " slave|master [device, only in slave mode]" << std::endl;
    exit(1);
}

/* Adapted from SINF */
bool readline(int fd, string &line) 
{
    char buffer[32]; // 5 characters + '\0'
    line = "";

    int n = read(fd, buffer, 32);
    if (n == 0) return false;
    buffer[n] = 0; // put a \0 in the end of the buffer
    line += buffer; // add the read text to the string

    return true;  
}

/* Adapted from SINF */
bool writeline(int fd, const string &line) 
{
    write(fd, line.c_str(), line.length());
    cout << "Sent: " << line << endl;
    return true;
}

void place_move_in_board(int piece, int rotation, int x, int y, int player) {
	int x_offset = x - 2;
	int y_offset = y - 2;

	for(y=0; y<5; y++) {
      for(x=0; x<5; x++) {
		    int b;
		    b = pieces[piece][0][rotate[rotation][y][x]];
		    if (b==1)
		    	board[y_offset+y][x_offset+x] = player;
		  }
    }
	available[player][piece] = 0;
}

int check_move(move m){
  //  printf("check_move: player %d, turn %d\n", player, turn);
  int c, r, x, y, x_offset, y_offset;
    
  c = m.piece;
  r = m.rotate;
  x_offset = m.x-2;
  y_offset = m.y-2;
    
  // Check availability
  if(available[1][c] == 0){
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
        if (board[yy][xx-1] == 1 || board[yy][xx+1] == 1 ||
            board[yy-1][xx] == 1 || board[yy+1][xx] == 1 ){
          return SHARES_EDGE;
        }
      }
    }
  }

  // must share the vertex
	int got_it = 0;
	for(y=0; y<5; y++){
	  for(x=0; x<5; x++){
		int b;
		b = pieces[c][0][rotate[r][y][x]];
		if (b==1){
		  int xx, yy;
		  xx = x_offset+x;
		  yy = y_offset+y;
		  if (board[yy-1][xx-1] == 1 || board[yy+1][xx-1] == 1 ||
		      board[yy-1][xx+1] == 1 || board[yy+1][xx+1] == 1){
		    got_it = 1;
		  }
		}
	  }
	}
	if(!got_it){
	  return MUST_SHARE_VERTEX;
	}

  return 1;
}

string string_of_move(move m) {
	char c[5];

	char buf[2];
	sprintf(buf,"%x",m.x);
	c[0] = buf[0];
	sprintf(buf,"%x",m.y);
	c[1] = buf[0];
	c[2] = m.piece + 'a';
	c[3] = m.rotate + '0';
	c[4] = '\0';

	return string(c);
}

void print_failure(int ret) {
	switch(ret){
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

string find_first_move() {
	 move m;
	int ret;

	  for (m.y=0; m.y<=14; m.y++){
		for (m.x=0; m.x<14; m.x++){
		  for (m.piece=0; m.piece<21; m.piece++){
		    for(m.rotate=0; m.rotate<8; m.rotate++){
				//cout << "Checking move: " << string_of_move(m) << endl;
		      if((ret = check_move(m)) == 1){
				//cout << "Move OK!" << endl;
				return string_of_move(m);
		      } else {
				//print_failure(ret);
			}
		    }
		  }
		}
	  }
	  return string("0000");
}

int hex_to_int(char d) {
    if (d >= '0' && d <= '9') {
        return d - '0';
        }
    d = tolower(d);
    if (d >= 'a' && d <= 'f') {
        return (d - 'a') + 10;
        }
    return -1;
}

void make_move(int fd, string input) {	
	if(!input.compare("25")) {
		/* sample of how to handle communication */
		/* if asked to play on (5,5) (first round), play block 't' with rotation 0 */
		writeline(fd, "55t0");
		place_move_in_board('t'-'a', 0, 5, 5, 1);
		return;
	} else if(!input.compare("2a")) {
		writeline(fd, "aat0");
		place_move_in_board('t'-'a', 0, hex_to_int('a'), hex_to_int('a'), 1);
		return;
	} else { 
		int x = hex_to_int(input[1]);
		int y = hex_to_int(input[2]);
		int piece = input[3] - 'a';
		int rotation = input[4] - '0';
		
		place_move_in_board(piece, rotation, x, y, 2);

		string to_send = find_first_move();
		writeline(fd, to_send);

		if(to_send.compare("0000"))
			place_move_in_board(to_send[2] - 'a', to_send[3] - '0', hex_to_int(to_send[0]), hex_to_int(to_send[1]), 1);

		return; 
	}
}

void* reader_thread(void* args) {
    int fd = *(int*)args;

    string input;
    while (readline(fd, input) >= 1) {
		for(int i = 0; i < input.length(); i++) {
			if(input[i] > 'z' || input[i] < '0') { cout << "zz";
				return 0;}
		}
        std::cout << "Received: " << input << endl;
        std::cout.flush();

		if(!input.compare("9"))
			return 0;

		if(input.length() > 0)
			make_move(fd, input);
    }

    cout << "Reader thread shutting down..." << endl;
    return 0;
}

/* read_byte is only used for the negotiation stage */
char read_byte(int fd) { 
    char inputbyte;

    // locks the application while waiting
    read(fd, &inputbyte, 1); 
    
    // write the char to the console
    cout << "Received: " << string(1, inputbyte) << " (" << (int) inputbyte << ")" << endl;
    std::cout.flush();

    return inputbyte;
}

int main(int argc, char** argv) {
    cout << "----- FakeFPGA ------" << endl;

    if (argc < 2) usage(argv[0]);

    int fd = 0;
    std::string mode = argv[1];

    if (mode == "slave") {
        if (argc < 3) usage(argv[1]);

        fd = open(argv[2], O_RDWR);
        if (fd == -1) {
            std::cerr << "error opening file" << std::endl;
            return -1;
        }

    }else if (mode=="master") {
        fd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
        if (fd == -1) {
            std::cerr << "error opening file" << std::endl;
            return -1;
        }

        grantpt(fd);
        unlockpt(fd);

        char* pts_name = ptsname(fd);
        std::cerr << "Connected to serial port at: " << pts_name << std::endl;
    } else {
        usage(argv[1]);
    }

    /* serial port parameters */
    struct termios newtio;
    memset(&newtio, 0, sizeof(newtio));
    struct termios oldtio;
    tcgetattr(fd, &oldtio);

    newtio = oldtio;
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = 0;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VMIN] = 1;
    newtio.c_cc[VTIME] = 0;
    tcflush(fd, TCIFLUSH);

    cfsetispeed(&newtio, BAUDRATE);
    cfsetospeed(&newtio, BAUDRATE);
    tcsetattr(fd, TCSANOW, &newtio);
	
	/* initialize board memory */
	for(int x = 0; x < 16; x++)
		for(int y = 0; y < 16; y++)
			board[x][y] = 0;

	for(int i = 0; i < 21; i++) {
		available[1][i] = 1;
		available[2][i] = 1;
	}

  // setup board: border is already filled
  for(int x=0; x<16; x++){
    board[ 0][ x] = 3;
    board[15][ x] = 3;
    board[ x][ 0] = 3;
    board[ x][15] = 3;
  }

    /* negociation stage */
    if(mode == "master") {
        /* wait for blokus-host to ask our team code */
        if(read_byte(fd) == '0') {
            cout << "Our team code is being requested..." << endl;

            /* team code is of type 1XX [101-199] */
            string team_code = "101";

            /* send the team code */
            writeline(fd, team_code);
        }
    }

    pthread_t thread;
    pthread_create(&thread, 0, reader_thread, (void*)&fd);

    /* read from stdin and send it to the serial port */
    char c;
    while (true) {
        std::cin >> c;
        write(fd, &c, 1);
    }

	int aaaa;
	scanf("%d", &aaaa);

    close(fd);
    return 0;
}

