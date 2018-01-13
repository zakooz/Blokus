typedef struct {
  int x;
  int y;
  int piece;
  int rotate;
} move;

int timeval_subtract(struct timeval *a, struct timeval *b);
int read_all(int fd, int len, char* buf);
void show_board();
int check_code(char* code);
move decode_code(char* code);
int interactive(int p);
char* prompt(int p, char* code, char* prev_code, int must_pass, int turn);
int check_move(int player, int turn, move m);
void show_error(int e);
int check_possibility(int player, int turn);
int next_player(int player);
void init_serial();
void close_serial();

void usage();
int do_game();

void auto0(int, int, char*);
void auto1(int, int, char*);

