#include <math.h>
#include <stdio.h>
#include <unistd.h>

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
    " oyhdmNMMMMMMMNdyooydmddddhhhhyhNd.",
    "  :oyhhdNNMMMMMMMNNNmmdddhhhhhyymMh",
    "    .:+sydNMMMMMNNNmmmdddhhhhhhmMmy",
    "       /mMMMMMMNNNmmmdddhhhhhmMNhs:",
    "    `oNMMMMMMMNNNmmmddddhhdmMNhs+` ",
    "  `sNMMMMMMMMNNNmmmdddddmNMmhs/.   ",
    " /NMMMMMMMMNNNNmmmdddmNMNdso:`     ",
    "+MMMMMMMNNNNNmmmmdmNMNdso/-        ",
    "yMMNNNNNNNmmmmmNNMmhs+/-`          ",
    "/hMMNNNNNNNNMNdhs++/-`             ",
    "`/ohdmmddhys+++/:.`                ",
    "  `-//////:--.                     ",
};
#define LOGO_ROWS 18
#define LOGO_COLS 34

#define MAX_POINTS 8000
static float PX[MAX_POINTS], PY[MAX_POINTS], PZ[MAX_POINTS];
static int POINT_COUNT = 0;

static char screen[HEIGHT][WIDTH];
static float zbuf[HEIGHT][WIDTH];

static void clear_buf(void) {
  for (int i = 0; i < HEIGHT; i++)
    for (int j = 0; j < WIDTH; j++) {
      screen[i][j] = ' ';
      zbuf[i][j] = -1e9f;
    }
}

static void build_points(void) {
  const float sx = 0.07f;
  const float sy = 0.14f;
  const float cx = (LOGO_COLS - 1) * 0.5f;
  const float cy = (LOGO_ROWS - 1) * 0.5f;

  // Build a 2D mask, then compute Chebyshev distance from each filled
  // cell to the nearest empty cell. Edge cells get d=1; deep interior
  // cells get larger d. Cells with d > MAX_D become the hole.
  int mask[LOGO_ROWS][LOGO_COLS];
  int dist[LOGO_ROWS][LOGO_COLS];
  for (int r = 0; r < LOGO_ROWS; r++) {
    int len = 0;
    while (LOGO[r][len])
      len++;
    for (int c = 0; c < LOGO_COLS; c++) {
      char ch = (c < len) ? LOGO[r][c] : ' ';
      // Only the dense glyphs count as logo body. The light chars form
      // the natural "hole" (the notch in the middle of the gentoo swirl).
      int filled = (ch == 'M' || ch == 'N' || ch == 'm' || ch == 'n' ||
                    ch == 'd' || ch == 'h' || ch == 'y' || ch == 'b');
      mask[r][c] = filled;
      dist[r][c] = filled ? 999 : 0;
    }
  }
  for (int iter = 1; iter < 12; iter++) {
    int changed = 0;
    for (int r = 0; r < LOGO_ROWS; r++) {
      for (int c = 0; c < LOGO_COLS; c++) {
        if (dist[r][c] != 999)
          continue;
        int found = 0;
        for (int dr = -1; dr <= 1 && !found; dr++) {
          for (int dc = -1; dc <= 1 && !found; dc++) {
            int nr = r + dr, nc = c + dc;
            if (nr < 0 || nr >= LOGO_ROWS || nc < 0 || nc >= LOGO_COLS)
              continue;
            if (dist[nr][nc] == iter - 1) {
              dist[r][c] = iter;
              changed = 1;
              found = 1;
            }
          }
        }
      }
    }
    if (!changed)
      break;
  }

  const int MAX_D = 99;       // keep all interior cells (no fake hole)
  const float zmax = 0.13f;   // overall thickness (smaller = thinner)
  const int Z_LAYERS = 5;

  int idx = 0;
  for (int row = 0; row < LOGO_ROWS; row++) {
    for (int col = 0; col < LOGO_COLS; col++) {
      if (!mask[row][col])
        continue;
      int d = dist[row][col];
      if (d > MAX_D)
        continue; // hole
      // Rounded edge profile: z-half-extent grows with depth into the
      // shell, like a half-tube cross section.
      float t_depth = (float)(d - 1) / (float)(MAX_D - 1 + 0.0001f); // 0..1
      float zr = zmax * sqrtf(0.15f + 0.85f * t_depth);

      float ox = (col - cx) * sx;
      float oy = (cy - row) * sy;
      for (int k = 0; k < Z_LAYERS; k++) {
        if (idx >= MAX_POINTS)
          break;
        float t = ((float)k / (Z_LAYERS - 1)) - 0.5f; // -0.5..0.5
        PX[idx] = ox;
        PY[idx] = oy;
        PZ[idx] = t * 2.0f * zr;
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

  printf("\033[?25l\033[2J");
  fflush(stdout);

  for (int frame = 0; frame < 2000; frame++) {
    clear_buf();
    A += 0.04f;
    B += 0.06f;
    float cA = cosf(A), sA = sinf(A);
    float cB = cosf(B), sB = sinf(B);

    for (int i = 0; i < POINT_COUNT; i++) {
      float px = PX[i], py = PY[i], pz = PZ[i];

      // Rotate X by A
      float y1 = py * cA - pz * sA;
      float z1 = py * sA + pz * cA;
      float x1 = px;
      // Rotate Y by B
      float x2 = x1 * cB + z1 * sB;
      float z2 = -x1 * sB + z1 * cB;
      float y2 = y1;

      float zc = z2 + K2;
      if (zc < 0.1f)
        continue;
      float ooz = 1.0f / zc;
      // Aspect-corrected projection: x doubled to compensate terminal cells
      int xs = (int)((float)WIDTH * 0.5f + K1 * 2.0f * x2 * ooz);
      int ys = (int)((float)HEIGHT * 0.5f - K1 * y2 * ooz);
      if (xs < 0 || xs >= WIDTH || ys < 0 || ys >= HEIGHT)
        continue;

      if (ooz > zbuf[ys][xs]) {
        zbuf[ys][xs] = ooz;
        // Shade by depth: closer (larger ooz) = brighter
        // ooz range roughly 1/(K2+0.3) .. 1/(K2-0.3)
        float lo = 1.0f / (K2 + 1.5f);
        float hi = 1.0f / (K2 - 1.5f);
        float t = (ooz - lo) / (hi - lo);
        if (t < 0)
          t = 0;
        if (t > 1)
          t = 1;
        const char *chars = ".,-~:;=!*#$@";
        int ci = (int)(t * 11.0f);
        if (ci < 0)
          ci = 0;
        if (ci > 11)
          ci = 11;
        screen[ys][xs] = chars[ci];
      }
    }

    printf("\033[H");
    for (int i = 0; i < HEIGHT; i++) {
      fwrite(screen[i], 1, WIDTH, stdout);
      fputc('\n', stdout);
    }
    fflush(stdout);
    usleep(50000);
  }

  printf("\033[?25h");
  fflush(stdout);
  return 0;
}
