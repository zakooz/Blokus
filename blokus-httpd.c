#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

// stdin to TCP/11000
// use env BLOKUS for the folder that has blokus.html

#define TRUE (1==1)
#define FALSE (!TRUE)

int log_serial(){
  FILE* seqfile;
  int seq = 0;

  seqfile = fopen("log-seq", "r");
  if (seqfile != NULL){
    fscanf(seqfile, "%d", &seq);
    fclose(seqfile);
  }

  seq++;
  seqfile = fopen("log-seq", "w");
  fprintf(seqfile, "%d", seq);
  fclose(seqfile);

  return seq;
}

int find(const char* a, const char *b){ // find b in a
  int la, lb, i;
  la = strlen(a);
  lb = strlen(b);

  for(i=0; i<=la-lb; i++)
    if(strncmp(b, &a[i], lb)==0) return i;

  return -1; // not found
}

int http_header(FILE* sock_file){
  char buf[100];
  int retval = 0;

  while(fgets(buf, 99, sock_file)){
    printf("* %s\n", buf);
    if(strncmp(buf, "GET /favicon", 12)==0 ){ retval = -1; break; }
    if(strncmp(buf, "GET", 3)==0){ retval = 0; break; }
  }

  do {
    fgets(buf, 99, sock_file); 
  } while(strlen(buf) != 2);
    

  fprintf(sock_file,
	  "HTTP/1.1 200 OK\x0d\x0a"\
	  "Content-Type: text/html\x0d\x0a"\
	  //	    "Transfer-Encoding: chunked\x0d\x0a"	
	  "\x0d\x0a");
  
  return retval;
}

FILE* accept_connection(int sock){
  int sock_fd;
  FILE* sock_file;

  sock_fd = accept(sock, NULL, NULL);
  if(sock_fd == -1){
    perror("accept() failed! ");
    exit(-1);
  }
  
  sock_file = fdopen(sock_fd, "w+"); // R/W
  setvbuf(sock_file, NULL, _IONBF, 0);
  
  return sock_file;
}

int main(int argc, char* argv[]){
  int sock, on;
  struct sockaddr_in s_addr;
  char buf[100];
  int line = -1;
  FILE *html_file, *sock_file, *log_file;

  char team1[5], team2[5];
  
  char *blokus_env, *html_filename;

  log_file = fopen("blokus-httpd-log.tmp", "w");
  if (log_file == NULL) {
    perror("opening temporary log file failed! ");
    exit(-1);
  }

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
  s_addr.sin_port = htons(11000);

  if(bind(sock, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0){
    perror("bind socket failed! ");
    exit(-1);
  }

  listen(sock, 0); // only 1 client per port
  printf("Waiting on TCP port %d...\n", 11000);


  // find HTML file (default blokus.html)
  blokus_env = getenv("BLOKUS");
  if(blokus_env == NULL){
    html_filename = strdup("blokus.html");
  } else {
    html_filename = (char*)malloc(strlen(blokus_env) + strlen("/blokus.html") + 1);
    sprintf(html_filename, "%s/blokus.html", blokus_env);
  }

  html_file = fopen(html_filename, "r");
  if (!html_file){
    perror("blokus.html ");
    exit(-1);
  }

  // send HTML file for first time
  sock_file = accept_connection(sock);
  http_header(sock_file);

  while(fgets(buf, 99, html_file)){
    fprintf(sock_file, "%s", buf);
  }
  
  fclose(sock_file);
  fclose(html_file);

  // send board status
  do {
    sock_file = accept_connection(sock);
    printf("got connected\n");

    if(http_header(sock_file) == 0){ // send nothing for favicon.ico
      do {
	if (fgets(buf, 99, stdin) == NULL) break;
	fprintf(log_file, "%s", buf);
	printf("%s", buf);

	if (strncmp(buf, "----------------------------------------", 40) == 0){
	  line = -1;
	  break;
	}

	if (strncmp(buf, ">>", 2) == 0 ){
	  char s1[5], s2[5], s3[5], s4[5];
	  sscanf(&buf[3], "%s %s %s %s", s1, s2, s3, s4);
	  printf("A:'%s'\nB:'%s'\nC:'%s'\nD:'%s'\n", s1, s2, s3, s4);

	  if(s4[0]!='*'){
	    strcpy(team1, s1);
	    strcpy(team2, s4);
	  } else {
	    strcpy(team1, s3);
	    strcpy(team2, s1);
	  }
	}

	if (strncmp(buf, "**", 2) == 0 || strncmp(buf, ">>", 2) == 0 ){
	  line = -1;
	  buf[strlen(buf)-1] = 0; // trim \n
	  fprintf(sock_file, "%s\n", &buf[0]);
	  break;
	}	


	if (line >= 0){
	  int head, tail, skip = FALSE, i;

	  switch(line){
	  case 0:
	  case 3: // --- Board
	  case 4: case 5:  case 20:
	    skip = TRUE; break;

	  case 1:
	  case 2:
	    head = 10; tail = 1; break;  // tail 1 for \n
	  default: 
	    for(i=0; i<15; i++)
	      if(buf[5+i*2]==' ') buf[5+i*2]='0';
	    head = 5; tail = 3; break;
	  }

	  if(line==0) fprintf(sock_file, "M\n");

	  if(!skip){
	    buf[strlen(buf)-tail] = 0;
	    fprintf(sock_file, "%s\n", &buf[head]);
	  }
	  line++;
	}

	if (find(buf, "-- Available Tiles --") >= 0) line = 0;
      } while(1==1);
    } else {
      printf("favicon\n");
    }
    fprintf(sock_file, "\x04");

    fflush(sock_file);
    fclose(sock_file);
  } while(strncmp(buf, "**", 2) != 0);
  
  fclose(log_file);

  sprintf(buf, "%04d-%s-%s.log", log_serial(), team1, team2);

  rename("blokus-httpd-log.tmp", buf);

  return 0;
}
