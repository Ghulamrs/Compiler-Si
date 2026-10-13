int counter = 3;
double scale = 2.5;
typedef struct { int count; int element; double *data; } Reals; typedef struct { int count; int element; unsigned char *data; } Chars;
void bump(int *n) {
  *n = *n + 1;
}
typedef struct { int count; int element; Reals **data; } Rows; int printf(const char *, ...);
double total(Reals *v, int k) {
  double s = 0.;

  for (int i = 0; i <= k - 1; i++) {
    s = s + v->data[i];
  }
  return s;
}
static double vd[3]; static Reals vs = {3, 1, vd}; static unsigned char td[8] = "hi"; static Chars ts = {8, 2, td}; static double m0[2], m1[2]; static Reals r0 = {2, 1, m0}, r1 = {2, 1, m1}; static Reals *rp[2] = {&r0, &r1}; static Rows ms = {2, 3, rp};
int main(void) {
  Reals *v = &vs;
  v->data[0] = 1.5;
  v->data[1] = 2.5;
  v->data[2] = 4.;
  Chars *t = &ts;
  Rows *m = &ms;
  m->data[1]->data[1] = 9.;
  int c = 65;
  double r = total(v, 3);
  bump(&counter);
  printf("%g %d %g \n", r, counter, scale);
  printf("%c %s \n", c, t->data);
}
