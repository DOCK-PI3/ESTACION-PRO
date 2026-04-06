# ESTACION-PRO

ESTACION-PRO es un frontend multiplataforma para explorar y lanzar juegos de una coleccion multi-sistema.

Este es el repositorio actual del proyecto:
- Repositorio GitHub: https://github.com/DOCK-PI3/ESTACION-PRO
- Proyecto base/origen: ES-DE (EmulationStation Desktop Edition)

La base se mantiene compatible con la arquitectura multiplataforma original de ES-DE, aplicando el renombrado del producto y ajustes de build para esta distribucion.

## Estado de esta base

- Base sincronizada desde el proyecto base ES-DE
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

## Documentación completa de ESTACION-PRO

La siguiente es la documentación técnica y de usuario completa:

- [FAQ.md](FAQ.md) - Preguntas frecuentes
- [USERGUIDE.md](USERGUIDE.md) - Guía de usuario
- [INSTALL.md](INSTALL.md) - Instrucciones de instalación
- [THEMES.md](THEMES.md) - Temas y personalización
- [CHANGELOG.md](CHANGELOG.md) - Historial de versiones

## Convenciones de codigo para esta rama

- Google C++ Style Guide
- Uso preferente de `std::unique_ptr` y RAII
- Evitar punteros crudos salvo necesidad tecnica justificada
