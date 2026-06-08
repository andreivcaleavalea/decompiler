#include <cctype>
#include <cstring>
void ops1() {
  // 32 bits
  int a = 1;
  int b = 2;
  int c = 3;
  a = b + c;
  b = a - c;
  c = a * b;
  a = b / c;
  b = a % c;
  a = a + b + c;
  a = a - b - c;
  a = a * b * c;
  a = a / b / c;
  a = a % b % c;
  a = 20 * a + 30 * c + 40 * b;
  a = b * (c + a);
  a = (a * (b + c * (a + (b * c))));

  unsigned int d = 10;
  signed int e = -10;
}

void ops2() {
  // 8 bits
  char a = 'a';
  char b = 'b';
  char c = 'c';
  a = a + 3;
  b = b * 9;
  c = c - 2;
  unsigned char d = 2;
  signed char e = -2;
}

void ops3() {
  // 16 bits
  short int a = 1;
  unsigned short int b = 2;
  signed short int c = -2;
  a = a + b + c;
  b = a - b - c;
  c = a * b * c;
  a = a / b / c;
  b = a % b % c;
}

void ops4() {
  // 64 bits
  long int a = 12345;
  signed long int b = -12345;
  unsigned long int c = 12345;
  long long int d = 12345;
  unsigned long long int e = 12345;
  a = a + b + c + d + e;
  a = a - b - c - d - e;
  a = a * b * c * d * e;
  a = a / b / c / d / e;
  a = a % b % c % d % e;
}

void ops5() {
  float a = 1.0f;
  double b = 15.0f;
  a = a + b;
  a = a - b;
  a = a * b;
  a = a / b;
}

int func1() { return -15; }
unsigned int func2() { return 15; }
short int func3() { return 15; }
long long func4() { return 15; }
char func5() { return 'a'; };
float func6() { return 1.0f; }
double func7() { return 1.0; }
int func8(int a) { return a + 2; }
long long func9(long long a) { return a + 2; }
float func10(float a) { return a + 1.5f; }
double func11(double a) { return a + 1.5; }
int func12(int a, int b, int c) { return a + b + c; }
void func13() {
  int a[3] = {0, 1, 2};
  a[0]++;
  a[1]++;
  a[2]++;
}
void func14() {
  int a[3] = {0, 1, 2};
  int i;
  for (i = 0; i < 3; ++i) {
    a[i]++;
  }
  char b[10] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'};
  for (i = 0; i < strlen(b); ++i) {
    b[i] += 1;
  }
}
int main() {
  ops1();
  ops2();
  ops3();
  ops4();
  ops5();
  int a = func1();
  int b = func2();
  int c = func3();
  long long d = func4();
  char e = func5();
  float f = func6();
  double g = func7();
  int h = func8(a);
  long long i = func9(d);
  float j = func10(f);
  double k = func11(g);
  int l = func12(a, b, c);
  func13();
  func14();
}