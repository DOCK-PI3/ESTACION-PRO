# ESTACION-PRO

ESTACION-PRO es un frontend multiplataforma para explorar y lanzar juegos de una coleccion multi-sistema.

Este repositorio es un fork directo de ES-DE (EmulationStation Desktop Edition):
- Upstream oficial: https://gitlab.com/es-de/emulationstation-de
- Fork/derivado: ESTACION-PRO

La base se mantiene compatible con la arquitectura multiplataforma de ES-DE, aplicando renombrado de producto y ajustes de build para esta distribucion.

## Estado de esta base

- Base sincronizada desde ES-DE
- Nombre del proyecto ajustado a ESTACION-PRO en CMake y empaquetado principal
- Estandar de compilacion migrado a C++20
- Estructura multiplataforma preservada (Linux, Windows, macOS, Android, etc.)

## Stack

- Lenguaje: C++20
- Build system: CMake
- Plataforma objetivo: Linux (GCC/Clang) y Windows (MSVC)

## Compilacion rapida (Linux)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Ejecucion general

El binario principal se genera con el nombre ESTACION-PRO (segun plataforma y generador).

```bash
./ESTACION-PRO
```

Si compilas dentro de `build`, ubica el ejecutable en la salida del generador (por ejemplo `build/` o segun configuracion de CMake/IDE).

## Documentacion base heredada

- [FAQ.md](FAQ.md)
- [USERGUIDE.md](USERGUIDE.md)
- [INSTALL.md](INSTALL.md)
- [THEMES.md](THEMES.md)
- [CHANGELOG.md](CHANGELOG.md)

## Convenciones de codigo para esta rama

- Google C++ Style Guide
- Uso preferente de `std::unique_ptr` y RAII
- Evitar punteros crudos salvo necesidad tecnica justificada
