#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int64_t test_integer_sizes(int8_t a, int16_t b, int32_t c, int64_t d) {
  return a + b + c + d;
}

int64_t test_signed(int64_t a, int64_t b) { return (a >> 2) + (b >> 3); }

uint64_t test_unsigned(uint64_t u, uint64_t v) { return (u >> 2) + (v >> 3); }

void test_stack_vars() {
  char buf[64];
  snprintf(buf, sizeof(buf), "hello %d", 42);
  size_t n = strlen(buf);
  printf("len = %zu\n", n);
}

void test_malloc_free() {
  uint64_t size = 128;
  void *ptr = malloc(size);
  if (ptr != nullptr)
    free(ptr);
}

void test_charptr(const char *fmt, const char *msg) {
  size_t len = strlen(msg);
  printf(fmt, msg, len);
}

static int process_string(const char *s) { return (int)strlen(s); }

void test_interprocedural() {
  const char *greeting = "hello world";
  int result = process_string(greeting);
  printf("len = %d\n", result);
}

double test_float(float x, double y) { return (double)x + y; }

float test_float_array(const float *arr, int count) {
  float sum = 0.0f;
  for (int i = 0; i < count; ++i)
    sum += arr[i];
  return sum;
}

void test_combined(int n, float scale) {
  void *data = malloc((uint64_t)n * sizeof(float));
  if (data == nullptr)
    return;

  float *arr = (float *)data;
  for (int i = 0; i < n; ++i)
    arr[i] = (float)i * scale;

  char *buf = (char *)malloc(64);
  if (buf != nullptr) {
    const char *label = "result";
    snprintf(buf, 64, "%s: %f", label, (double)arr[0]);
    printf("%s\n", buf);
    free(buf);
  }

  free(data);
}

int main() {
  printf("sizes:       %lld\n", (long long)test_integer_sizes(1, 2, 3, 4));
  printf("signed:      %lld\n", (long long)test_signed(-8, -16));
  printf("unsigned:    %llu\n", (unsigned long long)test_unsigned(8, 16));
  test_stack_vars();
  test_malloc_free();
  test_charptr("msg='%s' len=%zu\n", "world");
  test_interprocedural();
  printf("float:       %f\n", test_float(1.5f, 2.5));
  float arr[] = {1.0f, 2.0f, 3.0f};
  printf("float_array: %f\n", (double)test_float_array(arr, 3));
  test_combined(4, 2.5f);
  return 0;
}
