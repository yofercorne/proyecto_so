#define _GNU_SOURCE

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#define N_WORKERS 3
#define CHECKPOINT_FILE "checkpoint.bin"

typedef struct {
    int local_counter;     // estado privado del thread
    long kernel_tid;       // TID que tenia al hacer checkpoint
} WorkerState;

typedef struct {
    int global_counter;    // estado compartido
    WorkerState workers[N_WORKERS];
} Checkpoint;

 //  VARIABLES GLOBALES   
static pthread_t worker_threads[N_WORKERS]; 
static int worker_ids[N_WORKERS];

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Barrera 1: todos los workers deben haberse detenido.
 * Barrera 2: permanecen detenidos mientras main guarda el checkpoint.
 */
static pthread_barrier_t barrier_pause_workers;
static pthread_barrier_t barrier_resume_workers;

static int global_counter = 0;
static int is_recovery_mode = 0;

static volatile sig_atomic_t stop_program = 0;

// Cada thread tiene su propia bandera.
static _Thread_local volatile sig_atomic_t checkpoint_requested = 0;

static Checkpoint checkpoint;

//   TID DEL THREAD EN EL KERNEL
static long get_tid(void) {
    return (long) syscall(SYS_gettid);
}

  // SIGNAL
static void checkpoint_handler(int sig) {
    (void) sig;
    checkpoint_requested = 1;
}

 //  ARCHIVO DE CHECKPOINT
static void save_checkpoint(void) {
    FILE *f = fopen(CHECKPOINT_FILE, "wb");
    if (f == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fwrite(&checkpoint, sizeof(checkpoint), 1, f);
    fclose(f);
}

static void load_checkpoint(void) {
    FILE *f = fopen(CHECKPOINT_FILE, "rb");
    if (f == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fread(&checkpoint, sizeof(checkpoint), 1, f);
    fclose(f);
}

   //APPLICATION THREAD
static void *worker(void *arg) {
    int id = *(int *) arg;

    int local_counter =
        is_recovery_mode ? checkpoint.workers[id].local_counter : 0;

    long tid = get_tid();

    if (is_recovery_mode) {
        printf("[RECOVERY] thread %d: TID anterior=%ld, nuevo=%ld\n",
               id,
               checkpoint.workers[id].kernel_tid,
               tid);
    }

    while (!stop_program) {

        /* -------- trabajo normal -------- */
        pthread_mutex_lock(&state_mutex);

        local_counter++;
        global_counter++;

        printf("thread=%d local=%d global=%d\n",
               id,
               local_counter,
               global_counter);

        pthread_mutex_unlock(&state_mutex);


        /* -------- checkpoint -------- */
        if (checkpoint_requested) {

            checkpoint_requested = 0;

            /* Cada thread guarda su estado privado. */
            checkpoint.workers[id].local_counter = local_counter;
            checkpoint.workers[id].kernel_tid = tid;

            printf("[CHECKPOINT] thread %d detenido\n", id);

            /* Espera hasta que TODOS los workers estén en pausa */
            pthread_barrier_wait(&barrier_pause_workers);

            /* Espera hasta que main termine de escribir y dé luz verde */
            pthread_barrier_wait(&barrier_resume_workers);
        }

        usleep(300000);
    }

    return NULL;
}


 //  CHECKPOINT THREAD (Disparador)
static void *checkpoint_thread(void *arg) {
    (void) arg;

    sleep(3);

    printf("\n=== CHECKPOINT SOLICITADO ===\n");

    /* Detiene a los application threads mediante una señal. */
    for (int i = 0; i < N_WORKERS; i++)
        pthread_kill(worker_threads[i], SIGUSR1);

    return NULL;
}


 //  CREAR APPLICATION THREADS
static void create_workers(void) {
    for (int i = 0; i < N_WORKERS; i++) {
        worker_ids[i] = i;

        /* Durante recovery también se crean en el mismo orden lógico. */
        pthread_create(
            &worker_threads[i],
            NULL,
            worker,
            &worker_ids[i]
        );
    }
}


//   EJECUCION NORMAL
static void normal_execution(void) {

    pthread_t trigger_thread; 

    printf("=== EJECUCION NORMAL ===\n");

    create_workers();

    /* Thread especial encargado de iniciar el checkpoint. */
    pthread_create(
        &trigger_thread,
        NULL,
        checkpoint_thread,
        NULL
    );

    pthread_join(trigger_thread, NULL);

    /* MAIN espera hasta que todos los workers hayan guardado su estado privado. */
    pthread_barrier_wait(&barrier_pause_workers);

    printf("[MAIN] todos los threads estan detenidos\n");


    /* MAIN guarda el estado compartido. */
    checkpoint.global_counter = global_counter;

    printf("[MAIN] estado compartido guardado: global=%d\n",
           checkpoint.global_counter);


    /* Finalmente se escribe todo a disco. */
    save_checkpoint();

    printf("[MAIN] checkpoint escrito en disco\n");

    /* Los workers pueden continuar. */
    pthread_barrier_wait(&barrier_resume_workers);

    printf("=== EJECUCION REANUDADA ===\n\n");

    sleep(3);
    printf("\n=== FALLA SIMULADA ===\n");
    printf("global actual = %d\n", global_counter);
    printf("global guardado = %d\n", checkpoint.global_counter);
    printf("Ejecute: ./checkpoint_demo --restore\n");

    exit(99);
}

 //  RECOVERY
static void recovery_execution(void) {

    printf("=== RECOVERY ===\n");

    /* Recuperamos el estado que habia sido persistido. */
    load_checkpoint();

    global_counter = checkpoint.global_counter;

    printf("[RECOVERY] global restaurado=%d\n",
           global_counter);

    /* Creamos nuevamente la misma cantidad de threads y en el mismo orden. */
    is_recovery_mode = 1;

    create_workers();

    /* Los nuevos threads continúan usando el estado privado del checkpoint. */
    sleep(4);

    stop_program = 1;

    for (int i = 0; i < N_WORKERS; i++)
        pthread_join(worker_threads[i], NULL);

    printf("\n=== RECOVERY TERMINADO ===\n");
}


int main(int argc, char **argv) {

    int is_restore_requested = argc == 2 && strcmp(argv[1], "--restore") == 0;


    if (is_restore_requested) {
        recovery_execution();
    } else {
        struct sigaction sa = {0};
        sa.sa_handler = checkpoint_handler;
        sigaction(SIGUSR1, &sa, NULL);

        /* Participan: N workers + main */
        pthread_barrier_init(
            &barrier_pause_workers,
            NULL,
            N_WORKERS + 1
        );

        pthread_barrier_init(
            &barrier_resume_workers,
            NULL,
            N_WORKERS + 1
        );

        normal_execution();
    }

    return 0;
}
