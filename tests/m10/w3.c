int printf(const char *, ...); int main(void) {
  int i = 100;
  for (int i = 0; i <= 2; i++) {
    int t = i * 10;
    printf("%d \n", t);
  }
  int t = 7;
  if (t > 5) {
    int u = t + 1;
    printf("%d \n", u);
  }
  printf("%d %d \n", i, t);
}
