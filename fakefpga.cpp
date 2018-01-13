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

using namespace std;

#define BUFFER_SIZE 128
#define BAUDRATE    B115200

void usage(char* cmd) {
    std::cerr << "usage: " << cmd << " slave|master [device, only in slave mode]" << std::endl;
    exit(1);
}

/* Adapted from SINF */
bool readline(int fd, string &line) 
{
    char buffer[5]; // 4 characters + '\0'
    line = "";

    int n = read(fd, buffer, 1024);
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

void* reader_thread(void* args) {
    int fd = *(int*)args;

    string input;
    while (readline(fd, input) >= 1) {
        std::cout << "Received: " << input << endl;
        std::cout.flush();

        /* sample of how to handle communication */
        /* if asked to play on (5,5) (first round), play block 'a' without rotation */
        if(!input.compare("25")) {
            writeline(fd, "55t0");
        }
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

    close(fd);
    return 0;
}
