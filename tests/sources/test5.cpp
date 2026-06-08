#include <cstdint>
#include <cstring>

static int32_t g_counter = 0;
static float g_scale = 1.5f;
static double g_threshold = 100.0;
static uint8_t g_table[16] = {0, 1, 2,  3,  4,  5,  6,  7,
                              8, 9, 10, 11, 12, 13, 14, 15};

int32_t add_sub(int32_t a, int32_t b) { return a + b - 1; }

int64_t mul_div(int64_t a, int64_t b) {
  int64_t product = a * b;
  int64_t quotient = product / b;
  int64_t remainder = product % b;
  return quotient + remainder;
}

int32_t div_by_constant(int32_t x) {
  int32_t q7 = x / 7;
  int32_t r7 = x % 7;
  int32_t q10 = x / 10;
  return q7 + r7 + q10;
}

uint32_t unsigned_ops(uint32_t a, uint32_t b) {
  uint32_t q = a / b;
  uint32_t r = a % b;
  return q * r;
}

uint32_t bitwise_ops(uint32_t a, uint32_t b) {
  uint32_t and_result = a & b;
  uint32_t or_result = a | b;
  uint32_t xor_result = a ^ b;
  uint32_t not_result = ~a;
  uint32_t shl_result = a << 3;
  uint32_t shr_result = a >> 2;
  return and_result | or_result | xor_result | not_result | shl_result |
         shr_result;
}

int32_t arithmetic_shift(int32_t x) { return x >> 4; }

float float_ops(float a, float b) {
  float sum = a + b;
  float diff = a - b;
  float prod = a * b;
  float quot = a / b;
  return sum + diff + prod + quot;
}

double double_ops(double a, double b) {
  double sum = a + b;
  double prod = a * b;
  return sum / prod;
}

double int_to_double(int32_t x) {
  return (double)x; // cvtsi2sd
}

float int_to_float(int64_t x) {
  return (float)x; // cvtsi2ss
}

int32_t float_to_int(float x) {
  return (int32_t)x; // cvttss2si
}

float double_to_float(double x) {
  return (float)x; // cvtsd2ss
}

double float_to_double(float x) {
  return (double)x; // cvtss2sd
}

int compare_floats(float a, float b) {
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  return 0;
}

int compare_doubles(double a, double b) {
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  return 0;
}

int32_t if_else_chain(int32_t x) {
  if (x < 0)
    return -1;
  else if (x == 0)
    return 0;
  else if (x < 100)
    return 1;
  else
    return 2;
}

int32_t switch_stmt(int32_t x) {
  switch (x) {
  case 0:
    return 10;
  case 1:
    return 20;
  case 2:
    return 30;
  case 3:
    return 40;
  default:
    return -1;
  }
}

int32_t loop_for(int32_t n) {
  int32_t sum = 0;
  for (int32_t i = 0; i < n; ++i) {
    sum += i;
  }
  return sum;
}

int32_t loop_while(int32_t x) {
  int32_t count = 0;
  while (x > 0) {
    x >>= 1;
    ++count;
  }
  return count;
}

int32_t loop_do_while(int32_t n) {
  int32_t result = 1;
  do {
    result *= n;
    --n;
  } while (n > 1);
  return result;
}

int32_t fibonacci(int32_t n) {
  if (n <= 1)
    return n;
  return fibonacci(n - 1) + fibonacci(n - 2);
}

int64_t factorial(int32_t n) {
  if (n <= 1)
    return 1;
  return (int64_t)n * factorial(n - 1);
}

void array_fill(int32_t *arr, int32_t n, int32_t value) {
  for (int32_t i = 0; i < n; ++i) {
    arr[i] = value;
  }
}

int32_t array_sum(const int32_t *arr, int32_t n) {
  int32_t sum = 0;
  for (int32_t i = 0; i < n; ++i) {
    sum += arr[i];
  }
  return sum;
}

int32_t pointer_deref(int32_t *p) { return *p + *(p + 1); }

void swap(int32_t *a, int32_t *b) {
  int32_t tmp = *a;
  *a = *b;
  *b = tmp;
}

struct Point {
  int32_t x;
  int32_t y;
};

int32_t point_distance_sq(struct Point p1, struct Point p2) {
  int32_t dx = p1.x - p2.x;
  int32_t dy = p1.y - p2.y;
  return dx * dx + dy * dy;
}

void increment_global() { g_counter++; }

float scale_value(float x) { return x * g_scale; }

int32_t threshold_check(double value) { return value > g_threshold ? 1 : 0; }

uint8_t table_lookup(int32_t index) {
  if (index < 0 || index >= 16)
    return 0;
  return g_table[index];
}

void no_params() { g_counter = 0; }

int32_t one_param(int32_t a) { return a * 2; }

int32_t two_params(int32_t a, int32_t b) { return a + b; }

int32_t three_params(int32_t a, int32_t b, int32_t c) { return a + b + c; }

int32_t four_params(int32_t a, int32_t b, int32_t c, int32_t d) {
  return a + b + c + d;
}

int32_t five_params(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e) {
  return a + b + c + d + e;
}

void write_byte(uint8_t *dst, uint8_t value) { *dst = value; }

void memcopy(void *dst, const void *src, int32_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (int32_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
}

double weighted_sum(int32_t a, double wa, int32_t b, double wb) {
  return (double)a * wa + (double)b * wb;
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

int32_t nested_calls(int32_t x) {
  int32_t a = add_sub(x, fibonacci(5));
  int32_t b = loop_for(x);
  return a + b;
}

int main() {
  int32_t arr[8] = {3, 1, 4, 1, 5, 9, 2, 6};

  int32_t s = array_sum(arr, 8);
  array_fill(arr, 8, 0);

  int32_t fib = fibonacci(10);
  int64_t fact = factorial(10);

  float f = lerp(0.0f, 1.0f, 0.5f);
  double d = weighted_sum(fib, 1.5, (int32_t)fact, 0.001);

  increment_global();
  int32_t tc = threshold_check(d);
  uint8_t tl = table_lookup(fib % 16);

  int32_t nc = nested_calls(s);
  int32_t sw = switch_stmt(fib % 5);

  struct Point p1 = {0, 0};
  struct Point p2 = {3, 4};
  int32_t dist = point_distance_sq(p1, p2);

  return nc + sw + dist + tc + (int32_t)tl + (int32_t)f + (int32_t)d;
}
