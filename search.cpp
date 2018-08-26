#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <iostream>
using namespace std;

int main(){
        ifstream test_in;
    test_in.open("./test.data", ios_base::binary | ios_base::in | ios_base::ate);
    size_t length = (size_t)test_in.tellg();
    test_in.seekg (0, test_in.beg);

    string buffer;
    buffer.resize(length+1);
    test_in.read((char*)buffer.data(), length);
    for(int i = 0;i<1000000;++i){
        char pat_buf[64];
        int length = snprintf(pat_buf, 64, "|%d ", i);
        int j = 0;
        size_t current_pos = 0;
        while(true){
            current_pos = buffer.find(pat_buf, current_pos);
            if(current_pos == string::npos){
                break;
            }
            ++j;
            current_pos += length;
        }
        cout << "number:" << i << endl;
        cout << "count:" << j << endl;
        if(j != 18){
            cout << "bad" << endl;
        }
    }
    return 0;
}
