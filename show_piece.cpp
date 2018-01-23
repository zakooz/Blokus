#include "pieces.h"
#include "rotate.h"
#include <iostream>
using namespace std;

int get_piece_at(int piece, int rotation, int x, int y) {
	return pieces[piece][0][rotate[rotation][x][y]];
}

int main(int argc, char** argv) {
	char c;
	int rotation;
	
	cout << "Piece: "; 
	cin >> c;
	
	cout << "Rotation: ";
	cin >> rotation;
	
	int x, y;
	for(x = 0; x < 5; x++) {
		for(y = 0; y < 5; y++) {
			cout << get_piece_at(c-'a', rotation, x, y);
			cout << " ";
		}
		cout << endl;
	}
	return 0;
}