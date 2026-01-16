# OK

to install dependencies (from root):

`../vcpkg/vcpkg install`

to configure fresh build (inside of ./build folder):
`cmake --fresh -B build -S src -G Ninja -DCMAKE_TOOLCHAIN_FILE="X:/Code/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_DIR="X:/Code/von-neumann-toy"`

if that doesnt work, try this:
`cmake --fresh -B build -S src -DCMAKE_TOOLCHAIN_FILE="X:/Code/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_DIR="X:/Code/von-neumann-toy"`

to build (inside of root folder):

`cmake --build build --config Release`

# Game Description

First of all, I need to address that this is not a simulation, and the algorithms controlling the bots will not scale to reality.

This is just a toy for many reasons:

1. 
2. Asteroids, planets, etc. are not moving at all (except for comets).
3. There is no concept of time relativity.
4. Distances are trivial compared to reality.
5. The "von Neumann" drones assume fancy sci-fi manufacturing, which is just magic.
6. Energy consumption and material properties are not simulated.
7. The economic impact of unbounded resource collection is not considered.
8. No fluid dynamics
9. Mass of an object is assumed to be centered and uniform with uniform density.
10. 

Etc. Etc. Etc.

Stuff we do simulate:

1. Classical Gravity
2. Rigid Body Physics
3. 2D Projection Rendering from 3D Space
4. GPS Navigation
5. Astronomical Navigation
6. 

I think it's a very cool toy for many reasons:

1. We can see how swarms of autonomous agents can collaborate to form emergent supply chains.
2. We can watch multiple swarms compete with generational evolutionary algorithms for resources and wage "space wars."
3. We can set initial conditions to see how that affects the result.


## Tech/Concepts Used

- OpenGL / C++ / Qt
- Von Neumann Probes
- Generational Evolutionary Algorithms
- Swarm Intelligence Search Algorithms
- Convolutional Neural Nets
- Economics Equations (Economics of scale, opportunity costs, supply/demand)
- Datasets for simulating Earth Economy
  - World Cities basic data: (https://simplemaps.com/data/world-cities)
- Classical Physics Equations (Gravity, Mass, Rigid Bodies, Astrophysics, etc.)
- Datasets used for Realism:
  - SuperNOVAS dataset parser (https://smithsonian.github.io/SuperNOVAS/)
  - Horizons SPICE dataset for planets and moon positions (https://ssd.jpl.nasa.gov/horizons/)
  - Hipparcos 2 for stars data on celesial skybox
  - World Magnetic Model High Resolution 2025 for Earth's magnetic field
  - Blue Marble Next Generation for Earth's surface color from 2004
  - ETOPO 2022 for Earth's surface elevation (topology) (https://essd.copernicus.org/articles/17/1835/2025/) 
  - NOAA-20 VIIRS Day/Night Band Black Marble At Sensor Radiance for city lights
  - Terra MODIS 3-6-7 Corrected Reflectance for earth surface reflectance
  - RSS CCMP 6-Hourly 10 Meter Surface Winds Level 4 Version 3.1 for wind patterns
- Decentralized Zero-Knowledge-Proof Consensus Algorithms
- State Machines, Decision Trees, Knowledge Graphs
- 3D Graphics Rendering (Projection math, coordinate systems)

## Environment

1. Planets
  - Time, fuel, and massive wear cost to entering/exiting atmosphere
  - Earth is the final destination of resources
  - Mars is closer to the asteroid belt, and has no atmosphere
  - Planets cannot be mined
2. Moons
  - Phobos, Deimos, and Luna are available as locations that cannot be mined, but can serve as bases
3. Asteroids
  - Can be mined, have 3 types:
  - C-Type
  - S-Type
  - M-Type
4. Gas Clouds

## Architecture of a Drone

Parts:
- CPU
- Solar Panels
- Propulsion Jets
- Fuel Tank
- Camera Lens
- Mining Drill
- Communications Laser (light-based comms)
- Radar
- Additive Manufacturing
  - Metal Sintering
  - Ceramic Sintering
- Smelting Furnace
- Ceramic Furnace

It's "mental model"
- 

# Rendering Logic
- signed distance field for all celestial objects and rigid bodies
- cone marching with SDF
- Physics Based Rendering (PBRs)
