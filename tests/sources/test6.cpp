int main() {
  int i, cnt;
  for (i = 0; i < 10; ++i) {
    cnt = cnt * cnt + cnt;
  }

  for (int i = 0; i < 10; ++i) {
    cnt++;
  }

  for (i = 1; i <= 10; ++i) {
    cnt++;
  }

  for (int i = 100; i >= 0; i--) {
    cnt++;
  }

  for (i = 0; i <= 100; i = i * 3) {
    cnt++;
  }

  for (int i = 0; i < 10; ++i) {
    for (int j = 0; j < 10; ++j) {
      for (int k = 0; k < 10; k++)
        cnt++;
    }
  }

  for (int i = 0; i < 10; i += 2) {
    cnt++;
  }

  for (int i = 0; i < 10; ++i) {
    if (cnt == 5) {
      break;
    }
    if (cnt == 6) {
      continue;
    }
    cnt++;
  }
  for (int i = 0; i < 5; ++i) {
    int j = 0;
    while (j <= 10) {
      j++;
      cnt += 15;
      do {
        cnt -= 15;
      } while (cnt >= 0);
    }
  }
}