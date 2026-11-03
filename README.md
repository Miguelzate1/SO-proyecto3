# Proyecto de Sistemas Operativos

## Proyecto 3: Tablas de páginas en acción (xv6)

### Integrantes

-Miguel Angel ALzate Chavarria

-Samuel Quintero

### Curso

Sistemas Operativos

### Semestre

2026-2

---

# Descripción

Extendimos xv6-riscv (commit `06aad25`) con tres mecanismos que usan directamente la tabla de páginas de cada proceso.

**Parte A: página compartida y `ugetpid()`.** El kernel reserva una página física por proceso, guarda en ella el pid y la mapea en `USYSCALL` (la página justo debajo de `TRAPFRAME`) con permisos `PTE_R | PTE_U`. `ugetpid()` lee el pid desde esa dirección, sin hacer una llamada al sistema. El heap ya no puede crecer por encima de `USYSCALL`.

**Parte B: `vmprint()`.** Llamada al sistema que imprime la tabla de páginas Sv39 del proceso con un recorrido recursivo (modelo de `freewalk()`). Solo imprime las PTEs válidas.

**Parte C: `pgaccess()`.** Llamada al sistema que, para hasta 64 páginas a partir de una dirección, devuelve en una máscara cuáles fueron leídas o escritas desde la última consulta. Lee el bit `PTE_A` y lo limpia después de consultarlo.

---

# Archivos modificados

## kernel/

- `memlayout.h`: se define `USYSCALL` como `TRAPFRAME - PGSIZE` y la estructura `struct usyscall { int pid; }`, dentro de `#ifndef __ASSEMBLER__` porque el archivo también lo incluye código en ensamblador.
- `riscv.h`: se define `PTE_A` (bit 6 de la PTE), el bit de acceso que enciende el hardware.
- `proc.h`: se agrega a `struct proc` el campo `usyscall`, que apunta a la página compartida.
- `proc.c`:
  - `allocproc()` reserva la página con `kalloc()`, la limpia con `memset` y guarda el pid.
  - `proc_pagetable()` la mapea en `USYSCALL` con `PTE_R | PTE_U` y deshace los mapeos anteriores si algo falla.
  - `proc_freepagetable()` la desmapea (sin liberarla).
  - `freeproc()` libera la página física.
  - `growproc()` compara contra `USYSCALL` en vez de `TRAPFRAME`.
- `sysproc.c`: `sys_sbrk()` compara contra `USYSCALL`; se agregan `sys_vmprint()` y `sys_pgaccess()`.
- `vm.c`: se agregan `vmprint()` y su función recursiva auxiliar `vmprint_rec()`.
- `syscall.h` y `syscall.c`: se registran `SYS_vmprint` (23) y `SYS_pgaccess` (24) y sus entradas en la tabla de llamadas.
- `defs.h`: prototipo de `vmprint()`.

## user/

- `ulib.c`: se implementa `ugetpid()`, que lee el pid desde `USYSCALL`.
- `user.h`: declaraciones de `ugetpid()`, `vmprint()` y `pgaccess()`.
- `usys.pl`: entradas para generar los stubs de `vmprint` y `pgaccess`.

---

# Integración con xv6

1. Clonar xv6-riscv y hacer `git switch -c proyecto3 06aad25`.
2. Copiar los archivos entregados en sus rutas.
3. Agregar `$U/_pgtbltest` a UPROGS en el Makefile.
4. Compilar con `make qemu`.
5. Ejecutar `pgtbltest` y `usertests -q`.

---

# Respuestas a las preguntas

## 1. Interpretación de vmprint()

Salida de `vmprint()` de un proceso recién creado (las direcciones físicas cambian en cada ejecución). Las páginas de último nivel son:

| Dirección virtual | Flags de la PTE | Contenido | Permisos |
|---|---|---|---|
| `0x0000` y `0x1000` | `0x5b` | Texto (código) | R, X, U |
| `0x2000` | `0xd7` | Datos | R, W, U |
| `0x3000` | `0x07` | Página de guarda | R, W, **sin U** |
| `0x4000` | `0xd7` | Pila de usuario | R, W, U |
| `0x8000` | `0xd7` | Una página de heap que el proceso tocó | R, W, U |
| `0x3fffffd000` | `0x13` | `USYSCALL` | R, U (solo lectura) |
| `0x3fffffe000` | `0xc7` | Trapframe | R, W, **sin U** |
| `0x3ffffff000` | `0x4b` | Trampolín | R, X, **sin U** |

Cómo se lee: el bit 0 es V (válida), el 1 es R, el 2 es W, el 3 es X y el 4 es U (accesible desde modo usuario). Los bits 6 (A, accedida) y 7 (D, modificada) los enciende el hardware, por eso algunas PTEs muestran `0x40` y `0x80` de más.

Los permisos tienen sentido: el texto se puede leer y ejecutar pero no escribir; los datos y la pila se pueden leer y escribir pero no ejecutar; el trapframe y el trampolín no tienen `PTE_U` porque solo los usa el kernel cuando entra y sale de modo usuario; y `USYSCALL` solo tiene `R | U`, así que el proceso la puede leer pero no escribir.

## 2. Página de guarda

La pila de xv6 tiene un tamaño fijo y justo debajo hay una página de guarda que está mapeada pero **sin `PTE_U`**. Si un programa desborda la pila (por ejemplo, con una recursión infinita) y llega a esa página, el acceso desde modo usuario no está permitido y el hardware genera un page fault. El kernel lo detecta en `usertrap()` y mata al proceso. Sin la guarda, la pila seguiría creciendo hacia abajo sobre otra memoria del proceso (datos o heap) y la corrompería sin que nadie se diera cuenta.

## 3. Otras llamadas que se pueden acelerar

- **Se podría:** `getpid()` (ya hecha), porque el pid de un proceso no cambia durante su vida. También `uptime()`, si el kernel actualiza el contador de ticks en una página de solo lectura que el proceso pueda leer; en ese caso el valor cambia, pero solo lo escribe el kernel.
- **No se pueden acelerar:** `fork`, `exec`, `exit`, `wait`, `kill`, `sbrk`, `open`, `close`, `read`, `write`, `pipe`, `dup`, `mkdir`, `unlink`, etc. Todas modifican estado del kernel (procesos, tabla de archivos, sistema de archivos, memoria del proceso), pueden bloquear al proceso, o necesitan que el kernel valide argumentos y haga operaciones privilegiadas. Eso no se puede resolver con una lectura de memoria.

## 4. Riesgo de USYSCALL con PTE_W

Si la página se mapeara con `PTE_W`, el proceso podría escribir en ella. Con la estructura actual, el daño se limita a que el proceso falsee su propio pid en `ugetpid()`. Pero el riesgo real es de diseño: es una página de memoria del kernel, y si ahora o en el futuro el kernel confía en lo que hay ahí (más campos, contadores, punteros), un proceso podría corromper esos datos y engañar al kernel. Por eso el kernel debe tratar esa página como información que el usuario solo lee y nunca como algo en lo que se pueda confiar si el usuario puede escribirla. Además, `pgtbltest` verifica que escribir en `USYSCALL` mate al proceso.

## 5. pgaccess() y páginas perezosas

Una página de `sbrklazy()` que no se ha tocado todavía no tiene memoria física asignada: su PTE no existe o no es válida. El hardware solo enciende `PTE_A` cuando traduce una dirección a través de una PTE válida, así que sobre esa página no pudo haber ningún acceso y se reporta 0. `pgaccess()` tampoco debe asignarle memoria porque es una consulta: si asignara memoria, gastaría memoria física que el proceso nunca usó (anulando la ventaja de la asignación perezosa), cambiaría lo que muestra `vmprint()`, y haría que la página pareciera usada cuando no lo fue. Por eso se usa `walk(..., 0)` (sin asignar) y nunca se llama a `vmfault()`.

---

# Resultados de las pruebas

Ejecutadas con `ejecutar_pruebas.py` sobre xv6 limpio (commit `06aad25`) con los archivos entregados. La salida completa está en `pruebas/`.

- **pgtbltest:** `TODAS LAS PRUEBAS PASARON (11 de 11)` y `Revisión del formato de vmprint(): OK` (`pruebas/pgtbltest.txt`).
- **usertests -q:** `ALL TESTS PASSED` (`pruebas/usertests.txt`).
- El kernel compila sin advertencias con `make qemu` y no hubo ningún `panic`.

---

# Decisiones de diseño

- **Una página compartida por proceso** (no una global): cada proceso solo ve su propio pid, y al hacer `fork()` el hijo recibe su propia página con su pid.
- **La página se limpia con `memset`** al crearla: `kalloc()` devuelve memoria con basura, y así el proceso no puede leer restos del kernel.
- **`proc_freepagetable()` desmapea `USYSCALL` sin liberar la página** (`do_free = 0`); la libera `freeproc()`. Así no hay doble liberación ni fuga, igual que con el trapframe.
- **`pgaccess()` usa `walk(..., 0)`**: nunca crea tablas ni asigna páginas.
- **Se llama a `sfence_vma()` después de limpiar `PTE_A`**: sin eso el TLB podría conservar la entrada con el bit encendido y el siguiente acceso no volvería a marcarlo.
- **El rango de `pgaccess()` se valida contra `PGROUNDUP(p->sz)`** y la máscara se arma con `uint64` para que funcione con 64 páginas (`1 << 63` con `int` sería desbordamiento).
- **`vmprint_rec()` calcula la dirección virtual** como `vabase + (i << (30 - 9 * nivel))` y baja un nivel solo si la PTE no tiene permisos R/W/X, igual que `freewalk()`.

---

# Limitaciones conocidas

- `pgaccess()` acepta como máximo 64 páginas, porque la máscara es de 64 bits.
- Solo se usa el bit `PTE_A`; no se usa `PTE_D` ni hay algoritmos de reemplazo de páginas.
- La página compartida solo contiene el pid; no se aceleran otras llamadas al sistema.
- Solo probamos en QEMU, no en hardware real. `pgaccess()` depende de que el hardware (o QEMU) actualice `PTE_A` en cada acceso.
- La salida de `vmprint()` va por `printk()` a la consola, sin ningún tipo de sincronización adicional.

---
# Declaración de uso de IA

Usamos Claude (Anthropic) como herramienta de apoyo interactivo para debatir soluciones, entender mejor los pánicos del kernel (como el de `freewalk: leaf`), revisar detalles de sintaxis con `argint` en `sys_sbrk()` y verificar el comportamiento del bit `PTE_A` en QEMU. 

Por nuestra parte, el equipo diseñó e implementó la arquitectura de la página compartida, la recursión de niveles en `vmprint()`, la lógica de bits y máscaras en `pgaccess()`, además de realizar todas las pruebas de integración en el entorno local de Linux Mint, la depuración en QEMU y la redacción final de las respuestas teóricas.

Los integrantes asumen plena responsabilidad académica sobre el contenido entregado y podemos explicar y justificar cada fragmento de código.
