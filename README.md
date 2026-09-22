# Simulación de Checkpoint/Recovery Multihilo

## Descripción

Este archivo implementa una **simulación simplificada en C** de algunas ideas del artículo *User-level Checkpointing for LinuxThreads Programs*.

La simulación utiliza varios `pthread` con dos tipos de estado:

- **Estado privado:** cada worker mantiene un contador local y registra su TID del kernel.
- **Estado compartido:** todos los workers actualizan un contador global protegido por un mutex.

Después de unos segundos, un thread especial solicita un checkpoint enviando `SIGUSR1` a los workers. Cada worker guarda su estado privado y se sincroniza mediante barreras, mientras que el thread principal guarda el estado compartido. Finalmente, toda la información se persiste en `checkpoint.bin`.

Luego se simula una falla. Al ejecutar nuevamente el programa con `--restore`, se carga el checkpoint, se restaura el contador global y se crean otra vez los workers usando los contadores privados guardados.

La simulación también permite observar que los **TID anteriores pueden ser distintos de los TID obtenidos después del recovery**, una de las dificultades discutidas en el artículo.

> Esta implementación no realiza un checkpoint real de registros, stack, program counter ni memoria completa del proceso. Su objetivo es representar de forma sencilla la coordinación, persistencia y recuperación de estado en un programa multihilo.

## Compilación

En Linux o WSL:

```bash
gcc -pthread checkpoint_demo.c -o checkpoint_demo
```

## Ejecución

### 1. Ejecución normal

```bash
./checkpoint_demo
```

Durante la ejecución se generará:

```text
checkpoint.bin
```

Después del checkpoint, el programa continúa unos segundos y luego simula una falla.

### 2. Recovery

Después de la falla, ejecutar:

```bash
./checkpoint_demo --restore
```

El programa leerá `checkpoint.bin`, restaurará el estado guardado y creará nuevamente los workers para continuar la simulación.
