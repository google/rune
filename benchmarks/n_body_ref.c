// n-body benchmark - C reference (The Computer Language Benchmarks Game).
// Uses libm sqrt. Build: clang -O3 -o n_body_ref n_body_ref.c -lm
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.141592653589793
#define SOLAR_MASS (4 * PI * PI)
#define DAYS_PER_YEAR 365.24
#define NBODIES 5

static double x[NBODIES]    = {0.0, 4.84143144246472090, 8.34336671824457987, 12.894369562139131, 15.379697114850917};
static double y[NBODIES]    = {0.0, -1.16032004402742839, 4.12479856412430479, -15.111151401698631, -25.919314609987964};
static double z[NBODIES]    = {0.0, -0.103622044471123109, -0.403523417114321381, -0.223307578892655734, 0.179258772950371181};
static double vx[NBODIES]   = {0.0, 0.00166007664274403694, -0.00276742510726862411, 0.00296460137564761618, 0.00268067772490389322};
static double vy[NBODIES]   = {0.0, 0.00769901118419740425, 0.00499852801234917238, 0.00237847173959480950, 0.00162824170038242295};
static double vz[NBODIES]   = {0.0, -0.0000690460016972063023, 0.0000230417297573763929, -0.0000296589568540237556, -0.0000951592254519715870};
static double mass[NBODIES] = {1.0, 0.000954791938424326609, 0.000285885980666130812, 0.0000436624404335156298, 0.0000515138902046611451};

static void advance(double dt) {
  for (int i = 0; i < NBODIES; i++) {
    for (int j = i + 1; j < NBODIES; j++) {
      double dx = x[i] - x[j], dy = y[i] - y[j], dz = z[i] - z[j];
      double d2 = dx*dx + dy*dy + dz*dz;
      double dist = sqrt(d2);
      double mag = dt / (d2 * dist);
      vx[i] -= dx * mass[j] * mag; vy[i] -= dy * mass[j] * mag; vz[i] -= dz * mass[j] * mag;
      vx[j] += dx * mass[i] * mag; vy[j] += dy * mass[i] * mag; vz[j] += dz * mass[i] * mag;
    }
  }
  for (int i = 0; i < NBODIES; i++) {
    x[i] += dt * vx[i]; y[i] += dt * vy[i]; z[i] += dt * vz[i];
  }
}

static double energy(void) {
  double e = 0.0;
  for (int i = 0; i < NBODIES; i++) {
    e += 0.5 * mass[i] * (vx[i]*vx[i] + vy[i]*vy[i] + vz[i]*vz[i]);
    for (int j = i + 1; j < NBODIES; j++) {
      double dx = x[i] - x[j], dy = y[i] - y[j], dz = z[i] - z[j];
      double dist = sqrt(dx*dx + dy*dy + dz*dz);
      e -= mass[i] * mass[j] / dist;
    }
  }
  return e;
}

static void offsetMomentum(void) {
  double px = 0.0, py = 0.0, pz = 0.0;
  for (int i = 0; i < NBODIES; i++) { px += vx[i]*mass[i]; py += vy[i]*mass[i]; pz += vz[i]*mass[i]; }
  vx[0] = -px / SOLAR_MASS; vy[0] = -py / SOLAR_MASS; vz[0] = -pz / SOLAR_MASS;
}

int main(int argc, char **argv) {
  long n = argc > 1 ? atol(argv[1]) : 1000;
  for (int i = 0; i < NBODIES; i++) {
    vx[i] *= DAYS_PER_YEAR; vy[i] *= DAYS_PER_YEAR; vz[i] *= DAYS_PER_YEAR;
    mass[i] *= SOLAR_MASS;
  }
  offsetMomentum();
  printf("%.9f\n", energy());
  for (long i = 0; i < n; i++) advance(0.01);
  printf("%.9f\n", energy());
  return 0;
}
