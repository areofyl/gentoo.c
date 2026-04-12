# gentoo.c

A spinning 3D Gentoo logo in your terminal, written in C. Inspired by the
classic donut.c.

It takes the ASCII Gentoo logo (the one fastfetch shows), turns each filled
character into a point in 3D space, gives it a bit of thickness, and rotates
the whole thing on two axes with depth shading and a z-buffer.

## Build

```
cc gentoo.c -o gentoo -lm
```

## Run

```
./gentoo
```

It clears the screen and animates in place. Ctrl-C to stop.

## How it works

- The logo is embedded as an array of strings
- Dense glyphs (`M N m n d h y b`) become the logo body, lighter chars stay
  empty so the natural notch in the swirl shows through as a hole
- A Chebyshev distance transform finds how deep into the shape each cell is,
  which is used to round the edges (cells near the silhouette get a thinner
  z profile, interior cells get a fuller one)
- Each cell is extruded into a few z layers for thickness
- Every frame: rotate every point around X and Y, project with perspective,
  write to a depth buffer, and shade by depth so closer points use brighter
  characters

~200 lines, no dependencies beyond libm.
