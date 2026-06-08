#include <climits>
#include <cstdint>
#include <cstdio>
#include <iostream>

using namespace std;

const int GLOBAL_A = INT_MAX;
const int GLOBAL_B = INT_MIN;

const char *GLOBAL_S = "This is my app";

int main() {
  int a = GLOBAL_A - 10;
  int b = GLOBAL_B + 10;

  cout << GLOBAL_S;
  cout << "wow";
}