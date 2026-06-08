# Decompiler

Acest decompiler a fost creat pentru programe executabile PE x64 si x86.

Va functia cel mai bine pe aplicatii compilate cu MinGW cu flagul `-O0` (fara optimizari).

Aici sunt sunt mai multe fisiere .cpp cu outputul lor dat de aceasta aplicatie.

[TESTE](/tests)

Exemplu:

Cod sursa:

```c++
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
```

```c++
void ops1() {
    int32_t var_4;
    int32_t var_8;
    int32_t var_12;
    int32_t var_16;
    int32_t var_20;
    var_4 = 1;
    var_8 = 2;
    var_12 = 3;
    var_4 = var_12 + var_8;
    var_8 = var_4 - var_12;
    var_12 = var_4 * var_8;
    var_4 = var_8 / var_12;
    var_8 = var_4 % var_12;
    var_4 = var_4 + var_8 + var_12;
    var_4 = var_4 - var_8 - var_12;
    var_4 = var_4 * var_8 * var_12;
    var_4 = var_4 / var_8 / var_12;
    var_4 = var_4 % var_8 % var_12;
    var_4 = ((var_4 << 2) + var_4 << 2) + var_12 * 30 + ((var_8 << 2) + var_8 << 3);
    var_4 = var_8 *(var_12 + var_4);
    var_4 = var_4 *(var_4 + var_8 * var_12 * var_12 + var_8);
    var_16 = 10;
    var_20 = -10;
}

void ops2() {
    int8_t var_1;
    int8_t var_2;
    int8_t var_3;
    int8_t var_4;
    int8_t var_5;
    var_1 = 97;
    var_2 = 98;
    var_3 = 99;
    var_1 = var_1 + 3;
    var_2 = (var_2 << 3) + var_2;
    var_3 = var_3 - 2;
    var_4 = 2;
    var_5 = -2;
}

void ops3() {
    int16_t var_2;
    int16_t var_4;
    int16_t var_6;
    var_2 = 1;
    var_4 = 2;
    var_6 = -2;
    var_2 = var_2 + var_4 + var_6;
    var_4 = var_2 - var_4 - var_6;
    var_6 = var_2 * var_4 * var_6;
    var_2 = var_2 / var_4 / var_6;
    var_4 = var_2 % var_4 % var_6;
}

void ops4() {
    int32_t var_4;
    int32_t var_8;
    int32_t var_12;
    int64_t var_24;
    int64_t var_32;
    var_4 = 12345;
    var_8 = -12345;
    var_12 = 12345;
    var_24 = 12345;
    var_32 = 12345;
    var_4 = var_12 + var_8 + var_4 + var_24 + var_32;
    var_4 = var_4 - var_8 - var_12 - var_24 - var_32;
    var_4 = var_4 * var_8 * var_12 * var_24 * var_32;
    var_4 = var_4 / var_8 / var_12 / var_24 / var_32;
    var_4 = var_4 % var_8 % var_12 % var_24 % var_32;
}

void ops5() {
    float var_4;
    double var_16;
    var_4 = 1.0f;
    var_16 = 15.0;
    var_4 = (float)(double)(var_4 + var_16);
    var_4 = (float)(double)(var_4 - var_16);
    var_4 = (float)(double)(var_4 * var_16);
    var_4 = (float)(double)(var_4 / var_16);
}

int func1__() {
    return -15;
}

int func2__() {
    return 15;
}

int func3__() {
    return 15;
}

int func4__() {
    return 15;
}

int func5__() {
    return 97;
}

float func6__() {
    return 1.0f;
}

double func7__() {
    return 1.0;
}

int32_t func8_int_(int32_t arg_0) {
    return arg_0 + 2;
}

int64_t func9_long_long_(int64_t arg_0) {
    return arg_0 + 2;
}

float func10_float_(float arg_0) {
    return 1.5f + arg_0;
}

double func11_double_(double arg_0) {
    return 1.5 + arg_0;
}

int32_t func12_int__int__int_(int32_t arg_0, int32_t arg_1, int32_t arg_2) {
    return arg_0 + arg_1 + arg_2;
}

void func13__() {
    int32_t var_4;
    int32_t var_8;
    int32_t var_12;
    var_12 = 0;
    var_8 = 1;
    var_4 = 2;
    var_12 = var_12 + 1;
    var_8 = var_8 + 1;
    var_4 = var_4 + 1;
}

void func14__() {
    char arr_26[10] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'};
    int32_t arr_16[3] = {0, 1, 2};
    int32_t var_4;
    for (var_4 = 0; var_4 <= 2; var_4++) {
        arr_16[var_4] = arr_16[var_4] + 1;
    }
    for (var_4 = 0; (unsigned)(var_4) < (unsigned)(fthunk(arr_26)); var_4++) {
        arr_26[var_4] = arr_26[var_4] + 1;
    }
}

int64_t main() {
    int32_t var_4;
    int32_t var_8;
    int32_t var_12;
    int64_t var_24;
    int8_t var_25;
    float var_32;
    double var_40;
    int32_t var_44;
    int64_t var_56;
    float var_60;
    double var_72;
    int32_t var_76;
    ops1();
    ops2();
    ops3();
    ops4();
    ops5();
    var_4 = func1__();
    var_8 = func2__();
    var_12 = func3__();
    var_24 = func4__();
    var_25 = func5__();
    var_32 = func6__();
    var_40 = func7__();
    var_44 = func8_int_(var_4);
    var_56 = func9_long_long_(var_24);
    var_60 = func10_float_(var_32);
    var_72 = func11_double_(var_40);
    var_76 = func12_int__int__int_(var_4, var_8, var_12);
    func13__();
    func14__();
    return 0;
}
```

