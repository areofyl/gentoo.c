#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void cleanup(void) {
  printf("\033[?25h");
  fflush(stdout);
}

static void handle_signal(int sig) {
  (void)sig;
  cleanup();
  _exit(0);
}

#define WIDTH 80
#define HEIGHT 24
#define PI 3.14159265f

// Gentoo logo as ASCII (from fastfetch). Any non-space cell is "filled".
static const char *LOGO[] = {
    "         -/oyddmdhs+:.            ",
    "     -odNMMMMMMMMNNmhy+-`         ",
    "   -yNMMMMMMMMMMMNNNmmdhy+-       ",
    " `omMMMMMMMMMMMMNmdmmmmddhhy/`    ",
    " omMMMMMMMMMMMNhhyyyohmdddhhhdo`  ",
    ".ydMMMMMMMMMMdhs++so/smdddhhhhdm+`",
    " oyhdmNMMMMMMMNdyooydMddddhhhhyhNd.",
    "  :oyhhdNNMMMMMMMNNMMMdddhhhhhyymMh",
    "    .:+sydNMMMMMNNMMMMdddhhhhhhmMmy",
    "       /mMMMMMMNNNMMMdddhhhhhmMNhs:",
    "    `oNMMMMMMMNNNMMMddddhhdmMNhs+` ",
    "  `sNMMMMMMMMNNNMMMdddddmNMmhs/.   ",
    " /NMMMMMMMMNNNNMMMdddmNMNdso:`     ",
    "+MMMMMMMNNNNNMMMMdMNMNdso/-        ",
    "yMMNNNNNNNMMMMMNNMmhs+/-`          ",
    "/hMMNNNNNNNNMNdhs++/-`             ",
    "`/ohdmmddhys+++/:.`                ",
    "  `-//////:--.                     ",
};
#define LOGO_ROWS 18
#define LOGO_COLS 34

#define MAX_POINTS 16000
static float PX[MAX_POINTS], PY[MAX_POINTS], PZ[MAX_POINTS];
static float NX[MAX_POINTS], NY[MAX_POINTS], NZ[MAX_POINTS];
static float PWEIGHT[MAX_POINTS]; // original logo density
static int POINT_COUNT = 0;

static float char_weight(char ch) {
  switch (ch) {
  case 'M':
    return 1.00f;
  case 'N':
    return 0.88f;
  case 'm':
    return 0.76f;
  case 'd':
    return 0.66f;
  case 'h':
    return 0.56f;
  case 'b':
    return 0.56f;
  case 'y':
    return 0.46f;
  case 'o':
    return 0.38f;
  case 'n':
    return 0.38f;
  case 's':
    return 0.30f;
  case '+':
    return 0.22f;
  case ':':
    return 0.18f;
  case '=':
    return 0.22f;
  case '-':
    return 0.14f;
  case '`':
    return 0.08f;
  case '.':
    return 0.10f;
  case '/':
    return 0.12f;
  case '\'':
    return 0.06f;
  default:
    return 0.0f;
  }
}

static char screen[HEIGHT][WIDTH];
static float zbuf[HEIGHT][WIDTH];
// Color per cell: 0 = magenta (outer), 1 = white (inner detail)
static int colorbuf[HEIGHT][WIDTH];

static void clear_buf(void) {
  for (int i = 0; i < HEIGHT; i++)
    for (int j = 0; j < WIDTH; j++) {
      screen[i][j] = ' ';
      zbuf[i][j] = -1e9f;
      colorbuf[i][j] = 0;
    }
}

static void build_points(void) {
  const float sx = 0.07f;
  const float sy = 0.14f;
  const float cx = (LOGO_COLS - 1) * 0.5f;
  const float cy = (LOGO_ROWS - 1) * 0.5f;
  const float zmax = 0.18f;
  const int Z_LAYERS = 6;

  // Build height map from character weights
  float hmap[LOGO_ROWS][LOGO_COLS];
  for (int r = 0; r < LOGO_ROWS; r++) {
    int len = 0;
    while (LOGO[r][len])
      len++;
    for (int c = 0; c < LOGO_COLS; c++) {
      char ch = (c < len) ? LOGO[r][c] : ' ';
      hmap[r][c] = char_weight(ch);
    }
  }

  // Compute normals from height gradient (central differences)
  float gnx[LOGO_ROWS][LOGO_COLS];
  float gny[LOGO_ROWS][LOGO_COLS];
  float gnz[LOGO_ROWS][LOGO_COLS];
  for (int r = 0; r < LOGO_ROWS; r++) {
    for (int c = 0; c < LOGO_COLS; c++) {
      if (hmap[r][c] <= 0.0f) {
        gnx[r][c] = gny[r][c] = 0;
        gnz[r][c] = 1;
        continue;
      }
      float dhdx = 0, dhdy = 0;
      if (c > 0 && c < LOGO_COLS - 1)
        dhdx = (hmap[r][c + 1] - hmap[r][c - 1]) * 0.5f;
      else if (c == 0)
        dhdx = hmap[r][c + 1] - hmap[r][c];
      else
        dhdx = hmap[r][c] - hmap[r][c - 1];

      if (r > 0 && r < LOGO_ROWS - 1)
        dhdy = (hmap[r + 1][c] - hmap[r - 1][c]) * 0.5f;
      else if (r == 0)
        dhdy = hmap[r + 1][c] - hmap[r][c];
      else
        dhdy = hmap[r][c] - hmap[r - 1][c];

      // Scale gradients by cell spacing
      dhdx /= sx;
      dhdy /= sy;

      // Normal = normalize(-dh/dx, dh/dy, 1)
      // (dh/dy sign flipped because row increases downward but y increases
      // upward)
      float nnx = -dhdx;
      float nny = dhdy;
      float nnz = 1.0f;
      float l = sqrtf(nnx * nnx + nny * nny + nnz * nnz);
      gnx[r][c] = nnx / l;
      gny[r][c] = nny / l;
      gnz[r][c] = nnz / l;
    }
  }

  int idx = 0;
  for (int row = 0; row < LOGO_ROWS; row++) {
    for (int col = 0; col < LOGO_COLS; col++) {
      float h = hmap[row][col];
      if (h <= 0.0f)
        continue;

      float ox = (col - cx) * sx;
      float oy = (cy - row) * sy;
      float zr = h * zmax;

      for (int k = 0; k < Z_LAYERS; k++) {
        if (idx >= MAX_POINTS)
          break;
        float t = ((float)k / (Z_LAYERS - 1)) - 0.5f; // -0.5..0.5
        PX[idx] = ox;
        PY[idx] = oy;
        PZ[idx] = t * 2.0f * zr;
        PWEIGHT[idx] = h;

        if (k == 0) {
          // Bottom face: mirror of top gradient normal
          NX[idx] = gnx[row][col];
          NY[idx] = gny[row][col];
          NZ[idx] = -gnz[row][col];
        } else if (k == Z_LAYERS - 1) {
          // Top face: gradient-based normal
          NX[idx] = gnx[row][col];
          NY[idx] = gny[row][col];
          NZ[idx] = gnz[row][col];
        } else {
          // Side layers: blend between edge outward and z-axis
          // Find direction toward nearest lower neighbor
          float ex = 0, ey = 0;
          for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
              if (dr == 0 && dc == 0)
                continue;
              int nr = row + dr, nc = col + dc;
              float nh = 0;
              if (nr >= 0 && nr < LOGO_ROWS && nc >= 0 && nc < LOGO_COLS)
                nh = hmap[nr][nc];
              if (nh < h) {
                ex += (float)dc;
                ey += (float)(-dr);
              }
            }
          }
          float el = sqrtf(ex * ex + ey * ey);
          if (el > 1e-6f) {
            ex /= el;
            ey /= el;
          }
          float tn = ((float)k / (Z_LAYERS - 1)) * 2.0f - 1.0f;
          float side = sqrtf(1.0f - tn * tn);
          NX[idx] = ex * side;
          NY[idx] = ey * side;
          NZ[idx] = tn;
        }
        idx++;
      }
    }
  }
  POINT_COUNT = idx;
}

int main(void) {
  build_points();

  float A = 0.0f;
  float B = 0.0f;
  const float K1 = 38.0f; // screen scale
  const float K2 = 5.5f;  // camera distance

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
  atexit(cleanup);

  printf("\033[?25l\033[2J");
  fflush(stdout);

  for (int frame = 0; frame < 2000; frame++) {
    clear_buf();
    A += 0.04f;
    B += 0.06f;
    float cA = cosf(A), sA = sinf(A);
    float cB = cosf(B), sB = sinf(B);

    // Light direction: upper-right-front, normalized
    const float lx = 0.4082f, ly = 0.8165f, lz = -0.4082f;
    // View direction: straight into screen
    const float vx = 0.0f, vy = 0.0f, vz = -1.0f;
    // Half-vector for Blinn-Phong: H = normalize(L + V)
    float hx = lx + vx, hy = ly + vy, hz = lz + vz;
    float hl = sqrtf(hx * hx + hy * hy + hz * hz);
    hx /= hl;
    hy /= hl;
    hz /= hl;

    for (int i = 0; i < POINT_COUNT; i++) {
      float px = PX[i], py = PY[i], pz = PZ[i];
      float nx = NX[i], ny = NY[i], nz = NZ[i];

      // Rotate point: X by A, then Y by B
      float y1 = py * cA - pz * sA;
      float z1 = py * sA + pz * cA;
      float x2 = px * cB + z1 * sB;
      float z2 = -px * sB + z1 * cB;
      float y2 = y1;

      // Rotate normal the same way
      float ny1 = ny * cA - nz * sA;
      float nz1 = ny * sA + nz * cA;
      float nx2 = nx * cB + nz1 * sB;
      float nz2 = -nx * sB + nz1 * cB;
      float ny2 = ny1;

      float zc = z2 + K2;
      if (zc < 0.1f)
        continue;
      float ooz = 1.0f / zc;
      int xs = (int)((float)WIDTH * 0.5f + K1 * 2.0f * x2 * ooz);
      int ys = (int)((float)HEIGHT * 0.5f - K1 * y2 * ooz);
      if (xs < 0 || xs >= WIDTH || ys < 0 || ys >= HEIGHT)
        continue;

      if (ooz > zbuf[ys][xs]) {
        // Diffuse (Lambertian)
        float diff = nx2 * lx + ny2 * ly + nz2 * lz;
        if (diff < 0)
          diff = 0;

        // Specular (Blinn-Phong)
        float spec_dot = nx2 * hx + ny2 * hy + nz2 * hz;
        if (spec_dot < 0)
          spec_dot = 0;
        float spec = spec_dot * spec_dot;
        spec = spec * spec; // ^4
        spec = spec * spec; // ^8

        float L = 0.08f + 0.62f * diff + 0.30f * spec;
        if (L > 1.0f)
          L = 1.0f;

        zbuf[ys][xs] = ooz;
        const char *chars = ".,-~:;=!*#$@";
        int ci = (int)(L * 11.0f);
        if (ci < 0)
          ci = 0;
        if (ci > 11)
          ci = 11;
        screen[ys][xs] = chars[ci];
        colorbuf[ys][xs] = (PWEIGHT[i] >= 0.5f) ? 1 : 0;
      }
    }

    printf("\033[H");
    for (int i = 0; i < HEIGHT; i++) {
      int prev_color = -1;
      for (int j = 0; j < WIDTH; j++) {
        if (screen[i][j] == ' ') {
          if (prev_color != -1) {
            printf("\033[0m");
            prev_color = -1;
          }
          fputc(' ', stdout);
        } else {
          int c = colorbuf[i][j];
          if (c != prev_color) {
            if (c == 1)
              printf("\033[1;37m"); // bold white
            else
              printf("\033[1;35m"); // bold magenta
            prev_color = c;
          }
          fputc(screen[i][j], stdout);
        }
      }
      if (prev_color != -1)
        printf("\033[0m");
      fputc('\n', stdout);
    }
    fflush(stdout);
    usleep(50000);
  }

  printf("\033[?25h");
  fflush(stdout);
  return 0;
}
