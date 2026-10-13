int twice(int n) {
  int d = n + n;
  return d;
}
int printf(const char *, ...);
int main(void) {
  int a = 1;
  int b = twice(a);
  printf("%d %d \n", a, b);
  printf("%d \n", twice(b));
}
