# Terrain System

This document is the overall design goal for the terrain system on OFG.

Whilst the world in OFG is designed to be infinite, a given landmass is guarunteed to be no larger than 128km x 128km. In this way, we can perform global hydrology, erosion and climate simulations at the landmass level.

## Layers

The terrain can be thought of as multiple generated layers, each adding/modifying the prior, increasing in detail. For now, the more coarse (lower) layers are only height fields, however the design does not lock in height field vs voxel representation at any point.

### Layer 0: Seed Layer

Input to this layer is the single seed. It then generates some core properties and geometric features of the landmass on which the rest of the world is built. This could involve:
- some base climate/biome bias, such as how tropical it is, that are typically driven by where a landmass is on the planet
- calculate the base coast line (or lines if we support multiple smaller landmasses) by generating some 2d geometrical shape and warping it using noise
- baking the coast line into a 2D SDF (probably needs to be compressed to quadtree representation with high detail at the borders)
- mountain skeletons
- placed blobs that roughly define where a large basin or plateau should exist
- voronoi land form regions

No actual terrain sampling is done in the seed layer - it is specifically there to generate features that will influence the terrain.

Critically, whilst we will probably want optimizations to avoid making it expensive, the seed layer could be arbitrarilly sampled at any point, to return a set of properties that influence what the terrain should be at that point.

### Layer 1: Base Layer

The base layer takes the seed layer and turns it into actual terrain, initially a height field, where each point in the height field has a set of terrain properties in addition to height.

This layer would likely for example:
- have some procedural noise, influenced by the features across the layer
- slope towards sea level at the coast, using the signed distance field provided by the seed layer
- add mountain ranges using ridged noise along the mountain skeletons

It might provide, in addition to height, properties such as erosion resistance, mountainess, flatness etc, as these properties will influence how further layers add detail.

### Layer 2: Climate Layer

This is a modifier that performs a minimal climate simulation on the base layer shape in order to get an idea of properties like:
- temperature
- moisture
- rainfall
- aridity
- snowline / snow persistence
- wind exposure
- fertility
- vegetation density

The climate does not define any properties of the terrain, but it does provide input for further layers.

### Layer 3: Hydrology and Erosion Layer

This is a simulation that runs offline (though we will need to test with it in an editor to observe it) that simulates rainfall (driven from the climate layer) on the landmass, in order to extract how water would flow around it - the hydrology pass. Once an iteration of hyrdrology is complete, water depth and flow maps are available, which can then be fed to a simple erosion simulation to modify the terrain. This process is repeated multiple times until the landmass has been realistically eroded and believable water channels are discovered.

Layer 3 may modify the properties that it was fed, as it is designed to simulate how adding water and erosion to the system would change the initial, purely procedural version. It also introduces flow maps and water depth information.

The output of layer 3 is designed to be compacted and baked offline, and streamed in from a server. The output likely contains a compressed height field (at a lower resolution than the original bake), along with other data per vertex, and details like the river mesh.

### Layer 4: Macro Terrain Layer

This is the first layer that is generated at runtime in the background. It is a 1km x 1km layer, derived from the output of the hydrology and then refined into a form that can be sampled by the render layers for final populating. It likely contains the same properties passed through but refined at a resolution of 1mx1m and adding in higher frequencies of noise.

To some degree, the macro layer is just recovering data that couldn't possibly be compressed and shipped from hydrology. For example, the rough shape provided by hydrology will be correct, but the rivers will be 're-cut' based on the data provided by the water mesh.

