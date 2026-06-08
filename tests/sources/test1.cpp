int global_int_a = 1;
int global_int_b = 2;
int global_int_c = 3;

bool global_bool_a = true;
bool global_bool_b = false;

long long global_long_a = 999999999999L;

void function1() {}
int function2() { return 5; }

void function3(int i, int j) {
  while (i <= j) {
    i++;
    j--;
  }
}

int main() {
  int a = global_int_a + 1;
  int b = global_int_b + 2;
  int c = global_int_c + 3;

  bool d = !global_bool_a;
  bool e = !global_bool_b;

  long long f = global_long_a + 10L;

  function1();
  int g = function2();
  function3(a, c);
}