#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image-lib.h"

#define NUM_STAGES 4
#define BUFFER_SIZE 1024

// Mutex para proteger a escrita no arquivo de timings
pthread_mutex_t timing_mutex;

// Estrutura para medições de tempo
typedef struct
{
    double start_time;
    double end_time;
} Timing;

// Pipes para comunicação entre as threads
int pipes[NUM_STAGES][2];

// Estrutura para passar informações entre threads
typedef struct
{
    char image_path[BUFFER_SIZE];
    gdImagePtr image;
    Timing timings[NUM_STAGES]; // Um para cada estágio do pipeline
} ImageData;

// Função para obter o tempo atual
double get_time()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

// Função para escrever as medições de tempo no arquivo
void write_timing_data(const char *image_path, Timing timing[], int stage)
{
    pthread_mutex_lock(&timing_mutex);
    FILE *timing_file = fopen("timing_pipeline.txt", "a");
    if (timing_file == NULL)
    {
        perror("Não foi possível abrir o arquivo de timing");
        pthread_mutex_unlock(&timing_mutex);
        return;
    }
    fprintf(timing_file, "%s stage %d start %.6f\n", image_path, stage, timing[stage].start_time);
    fprintf(timing_file, "%s stage %d end %.6f\n", image_path, stage, timing[stage].end_time);
    fclose(timing_file);
    pthread_mutex_unlock(&timing_mutex);
}

void *contrast_thread(void *arg)
{

    ImageData data;
    while (read(pipes[0][0], &data, sizeof(data)) > 0)
    {
        if (data.image == NULL)
        {
            // Enviar sinal de término para o próximo estágio e sair
            write(pipes[1][1], &data, sizeof(data));
            return NULL;
        }
        // Iniciar a medição de tempo
        data.timings[0].start_time = get_time();

        // Processamento de imagem
        data.image = contrast_image(data.image);

        // Finalizar a medição de tempo
        data.timings[0].end_time = get_time();

        // Escrever os tempos de processamento no arquivo de timings
        write_timing_data(data.image_path, data.timings, 0);

        // Passar dados para o próximo estágio
        write(pipes[1][1], &data, sizeof(data));
    }
    close(pipes[0][0]);
    return NULL;
}

void *sepia_thread(void *arg)
{
    ImageData data;
    while (read(pipes[3][0], &data, sizeof(data)) > 0)
    {
        if (data.image == NULL)
        {
            // Não há mais nenhuma thread para passar o sinal de término
            return NULL;
        }

        // Captura o tempo de início
        data.timings[3].start_time = get_time();

        // Processamento de imagem
        data.image = sepia_image(data.image);

        // Captura o tempo de fim
        data.timings[3].end_time = get_time();

        // Escreve os tempos no arquivo de timing
        write_timing_data(data.image_path, data.timings, 3);

        // Aqui você também pode querer registrar o tempo de escrita no disco
        double write_start_time = get_time();
        double write_end_time = get_time();
        gdImageDestroy(data.image); // Limpa a imagem
    }
    close(pipes[3][0]);
    return NULL;
}

void *texture_thread(void *arg)
{
    ImageData data;
    gdImagePtr texture; // Declarar a textura aqui
    while (read(pipes[2][0], &data, sizeof(data)) > 0)
    {

        if (data.image == NULL)
        {
            write(pipes[3][1], &data, sizeof(data));
            return NULL;
        }
        // Captura o tempo de início
        data.timings[2].start_time = get_time();

        // Processamento de imagem
        data.image = texture_image(data.image, texture); // Presumindo que texture é uma gdImagePtr global

        // Captura o tempo de fim
        data.timings[2].end_time = get_time();

        // Escreve os tempos no arquivo de timing
        write_timing_data(data.image_path, data.timings, 2);

        // Envia os dados para o próximo estágio
        write(pipes[3][1], &data, sizeof(data));
    }
    close(pipes[2][0]);
    return NULL;
}

void *smooth_thread(void *arg)
{
    ImageData data;
    while (read(pipes[1][0], &data, sizeof(data)) > 0)
    {
        if (data.image == NULL)
        {
            write(pipes[2][1], &data, sizeof(data));
            return NULL;
        }
        // Captura o tempo de início
        data.timings[1].start_time = get_time();

        // Processamento de imagem
        data.image = smooth_image(data.image);

        // Captura o tempo de fim
        data.timings[1].end_time = get_time();

        // Escreve os tempos no arquivo de timing
        write_timing_data(data.image_path, data.timings, 1);

        // Envia os dados para o próximo estágio
        write(pipes[2][1], &data, sizeof(data));
    }
    close(pipes[1][0]);
    return NULL;
}

// Função main que cria as threads e pipes
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <input file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&timing_mutex, NULL);

    // Crie pipes
    for (int i = 0; i < NUM_STAGES; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    // Crie threads
    pthread_t threads[NUM_STAGES];
    pthread_create(&threads[0], NULL, contrast_thread, NULL);
    pthread_create(&threads[3], NULL, sepia_thread, NULL);
    pthread_create(&threads[2], NULL, texture_thread, NULL);
    pthread_create(&threads[1], NULL, smooth_thread, NULL);

    double start_time = get_time(); // Declare start_time here

    // Iniciar o pipeline lendo o arquivo de entrada
    FILE *input_file = fopen(argv[1], "r");
    char image_path[BUFFER_SIZE];

    while (fgets(image_path, BUFFER_SIZE, input_file))
    {
        // Remove newline character
        image_path[strcspn(image_path, "\n")] = 0;

        // Carregar a imagem e iniciar o pipeline
        ImageData data;
        strncpy(data.image_path, image_path, BUFFER_SIZE);
        data.image = read_jpeg_file(image_path);
        write(pipes[0][1], &data, sizeof(data));
    }
    fclose(input_file);

    // Enviar o sinal de término para a primeira thread
    ImageData end_signal = {0};
    end_signal.image = NULL;
    write(pipes[0][1], &end_signal, sizeof(end_signal));

    // Fechar o lado da escrita do primeiro pipe
    close(pipes[0][1]);

    // Aguarda a conclusão das threads
    for (int i = 0; i < NUM_STAGES; i++)
    {
        pthread_join(threads[i], NULL);
    }
    // Aguarda a conclusão das threads
    for (int i = 0; i < NUM_STAGES; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Registra o tempo de fim do programa
    double end_time = get_time();
    FILE *timing_file = fopen("timing_pipeline.txt", "a");
    if (timing_file == NULL)
    {
        perror("Não foi possível abrir o arquivo de timing");
        exit(1);
    }
    fprintf(timing_file, "total %.6f\n", end_time - start_time);
    fclose(timing_file);
    // Fecha pipes e liberta os recursos aqui
    for (int i = 0; i < NUM_STAGES; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    // Destruir o mutex
    pthread_mutex_destroy(&timing_mutex);

    return 0;
}
