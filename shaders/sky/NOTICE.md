# Atmosphere provenance

Atmosphere medium coefficients, analytical segment integration and isotropic multiple-scattering closure adapt the
MIT-licensed reference accompanying Sébastien Hillaire, *A Scalable and Production Ready Sky and Atmosphere Rendering
Technique* (2020): https://github.com/sebh/UnrealEngineSkyAtmosphere at
183ead5bdacc701b3b626347a680a2f3cd3d4fbd. Copyright (c) 2020 Epic Games, Inc.; see LICENSE-Epic.txt.

OFG uses raster passes, Y-up kilometres internally, a simple horizon-dense transmittance mapping and lat-long sky
lookup, two celestial sources, 16-direction multiple-scattering quadrature, and mean RGB aerial transmission.
The layered cloud model is an OFG approximation, not the reference's volumetric-cloud implementation.
