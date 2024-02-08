/**********
 * Programacao Concorrente
 * MEEC 21/22
 *
 * Projecto - Parte A
 *
 *********/

#include <gd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "image-lib.h"
#include <time.h>

/* the directories wher output files will be placed*/
#define OLD_IMAGE_DIR "Old-image-dir/"
#define MAX_CARACTERES 150
#define MAX_IMAGENS 150
#define MAX_CARACTERES_CAMINHO 150
#define MAX_THREADS 64

char diretoria[MAX_CARACTERES_CAMINHO];
char imagens_thread[MAX_IMAGENS][MAX_CARACTERES];
char lista_nomes_imagens[MAX_IMAGENS][MAX_CARACTERES];
int num_imagens_p_thread[MAX_IMAGENS];
struct timespec start_time[MAX_IMAGENS], end_time[MAX_IMAGENS];
gdImagePtr in_texture_img;
pthread_mutex_t timing_mutex = PTHREAD_MUTEX_INITIALIZER;

// Estrutura para armazenar os tempos de processamento de cada imagem
struct image_time_info
{
    char image_name[MAX_CARACTERES];
    double time;
};

// Array global para armazenar os tempos de processamento de cada imagem
struct image_time_info image_times[MAX_IMAGENS];
// Contador global para o número de tempos de imagens armazenados
int image_time_count = 0;

// Estrutura para armazenar os tempos de cada thread
struct thread_time_info
{
    int num_images_processed;
    double total_time;
};

struct thread_time_info thread_times[MAX_THREADS];

void search_txt_file(char *basePath, char *txtfilePath)
{
    char path[1000];
    struct dirent *dp;
    DIR *dir = opendir(basePath);

    if (!dir)
    {
        return; // Não foi possível abrir o diretório
    }

    while ((dp = readdir(dir)) != NULL)
    {
        if (dp->d_type == DT_DIR)
        {
            if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0)
            {
                strcpy(path, basePath);
                strcat(path, "/");
                strcat(path, dp->d_name);
                search_txt_file(path, txtfilePath);
            }
        }
        else if (strstr(dp->d_name, ".txt"))
        {
            strcpy(txtfilePath, basePath);
            strcat(txtfilePath, "/");
            strcat(txtfilePath, dp->d_name);
            break;
        }
    }

    closedir(dir);
}

int le_imagens(char *filePath)
{
    FILE *file;
    char line[MAX_CARACTERES];
    int count = 0;

    file = fopen(filePath, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Não foi possível abrir o arquivo %s\n", filePath);
        exit(1);
    }

    while (fgets(line, sizeof(line), file) != NULL && count < MAX_IMAGENS)
    {
        strtok(line, "\n");
        strcpy(lista_nomes_imagens[count], line);
        count++;
    }

    fclose(file);
    for (int i = 0; i < count; i++)
    {
        // printf("Imagem lida: %s\n", lista_nomes_imagens[i]);
    }
    return count;
}

void div_imagens(int num_threads, int n_imagens_total)
{
    int count = 0;
    int imagens_por_thread = n_imagens_total / num_threads;
    int imagens_extra = n_imagens_total % num_threads;

    for (int i = 0; i < num_threads; i++)
    {
        num_imagens_p_thread[i] = imagens_por_thread + (imagens_extra > i ? 1 : 0);

        for (int j = 0; j < num_imagens_p_thread[i]; j++, count++)
        {
            strcpy(imagens_thread[count], lista_nomes_imagens[count]);
        }
    }

    for (int i = 0; i < num_threads; i++)
    {
        int startIndex = (i == 0) ? 0 : (i * imagens_por_thread + (i < imagens_extra ? i : imagens_extra));
        for (int j = startIndex; j < startIndex + num_imagens_p_thread[i]; j++)
        {
            // printf("Thread %d: %s\n", i, imagens_thread[j]);
        }
    }
}

double calculate_elapsed_time(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

void print_thread_times(FILE *file, struct thread_time_info *thread_times, int num_threads)
{
    for (int i = 0; i < num_threads; i++)
    {
        fprintf(file, "Thread_%d %d %.2f\n", i, thread_times[i].num_images_processed, thread_times[i].total_time);
    }
}

// Função para gravar os tempos no ficheiro
void save_time_data(int num_threads, double total_time)
{
    char filename[50];
    sprintf(filename, "timing_%d.txt", num_threads);

    FILE *file = fopen(filename, "w"); // Modificado para "w" para sobrescrever o arquivo existente
    if (file == NULL)
    {
        fprintf(stderr, "Não foi possível abrir o arquivo %s\n", filename);
        return;
    }

    // Primeiro, os tempos de processamento de cada imagem já deveriam ter sido gravados.

    // Segundo, grava os tempos das threads
    print_thread_times(file, thread_times, num_threads);

    // Por último, grava o tempo total
    fprintf(file, "total %d %.2f\n", num_threads - 1, total_time);

    fclose(file);
}

// Retorna 1 se o arquivo já foi processado, 0 caso contrário
int foiProcessado(const char *nomeArquivo)
{
    return (access(nomeArquivo, F_OK) != -1);
}

// funcao para calcular a diferença entre dois tempos
void *funcao_thread(void *arg)
{
    int thread_index = *(int *)arg;
    struct timespec start_time_thread, end_time_thread, start_time_image, end_time_image;

    clock_gettime(CLOCK_MONOTONIC, &start_time_thread);

    char out_file_name[500];
    char im_path[500];
    gdImagePtr in_img, out_smoothed_img, out_contrast_img, out_textured_img, out_sepia_img;
    char caminhoSaida[200];
    sprintf(caminhoSaida, "%s%s", diretoria, OLD_IMAGE_DIR);

    gdImagePtr in_texture_img = read_png_file("./paper-texture.png");

    int start_index = 0;
    for (int i = 0; i < thread_index; i++)
    {
        start_index += num_imagens_p_thread[i];
    }

    int end_index = start_index + num_imagens_p_thread[thread_index];

    for (int i = start_index; i < end_index; i++)
    {
        if (!strlen(imagens_thread[i]))
            continue; // Verifica se o nome da imagem está vazio
        sprintf(im_path, "%s%s", diretoria, imagens_thread[i]);
        sprintf(out_file_name, "%s%s%s", diretoria, OLD_IMAGE_DIR, imagens_thread[i]);

        if (foiProcessado(out_file_name))
        {
            continue; // Se a imagem já foi processada, vai para a próxima iteração do loop
        }

        clock_gettime(CLOCK_MONOTONIC, &start_time_image);

        if (access(out_file_name, F_OK) != -1)
        {
            continue;
        }

        in_img = read_jpeg_file(im_path);
        if (in_img == NULL)
        {
            fprintf(stderr, "Impossible to read %s image\n", im_path);
            continue;
        }

        out_contrast_img = contrast_image(in_img);
        out_smoothed_img = smooth_image(out_contrast_img);
        out_textured_img = texture_image(out_smoothed_img, in_texture_img);
        out_sepia_img = sepia_image(out_textured_img);

        write_jpeg_file(out_sepia_img, out_file_name); // Escreve a imagem final no disco

        // Limpa as imagens para evitar vazamento de memória
        gdImageDestroy(in_img);
        gdImageDestroy(out_contrast_img);
        gdImageDestroy(out_smoothed_img);
        gdImageDestroy(out_textured_img);
        gdImageDestroy(out_sepia_img);

        clock_gettime(CLOCK_MONOTONIC, &end_time_image);

        // Calcula o tempo de processamento da imagem
        double image_time_in_sec = calculate_elapsed_time(start_time_image, end_time_image);

        // Grava o tempo de processamento da imagem no arquivo timing
        pthread_mutex_lock(&timing_mutex);
        strcpy(image_times[image_time_count].image_name, imagens_thread[i]);
        image_times[image_time_count].time = image_time_in_sec;
        image_time_count++;
        pthread_mutex_unlock(&timing_mutex);
        // Atualiza o tempo total e o contador da thread
        thread_times[thread_index].total_time += image_time_in_sec;
        thread_times[thread_index].num_images_processed++;
    }

    // Calcula o tempo total gasto pela thread
    clock_gettime(CLOCK_MONOTONIC, &end_time_thread);
    // Atualiza os dados de tempo da thread
    thread_times[thread_index].total_time = calculate_elapsed_time(start_time_thread, end_time_thread);

    return NULL;
}

/**********
 * main()
 *
 * Arguments: (none)
 * Returns: 0 in case of sucess, positive number in case of failure
 * Side-Effects: creates thumbnail, resized copy and watermarked copies
 *               of images
 *
 * Description: implementation of the complex serial version
 *              This application only works for a fixed pre-defined set of files
 *
 *********/
int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s <Diretoria> <Número de Threads>\n", argv[0]);
        exit(-1);
    }

    strcpy(diretoria, argv[1]);
    strcat(diretoria, "/");

    char txtFilePath[MAX_CARACTERES_CAMINHO] = "";
    search_txt_file(diretoria, txtFilePath);

    if (strlen(txtFilePath) == 0)
    {
        fprintf(stderr, "Arquivo .txt não encontrado em %s\n", diretoria);
        exit(-1);
    }

    // Leitura das imagens
    int count = le_imagens(txtFilePath); // count agora armazena o número de imagens lidas
    if (count == 0)
    {
        fprintf(stderr, "Não foi possível ler o arquivo image-list.txt\n");
        exit(1);
    }

    // Número de threads
    int num_threads = atoi(argv[2]);
    // Verificação do número de threads
    if (num_threads <= 0 || num_threads > MAX_THREADS)
    {
        fprintf(stderr, "Número de threads inválido. Deve ser entre 1 e %d.\n", MAX_THREADS);
        exit(-1);
    }

    // Criação de diretórios de saída
    char out_path[250];
    sprintf(out_path, "%s%s", diretoria, OLD_IMAGE_DIR);
    if (create_directory(out_path) == 0)
    {
        fprintf(stderr, "Impossible to create %s directory\n", out_path);
        exit(-1);
    }

    // Divisão das imagens pelas threads
    div_imagens(num_threads, count);

    // Criação das threads
    pthread_t threads_id[num_threads];
    int thread_indices[num_threads];

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < num_threads; i++)
    {
        thread_indices[i] = i;
        if (pthread_create(&threads_id[i], NULL, funcao_thread, &thread_indices[i]))
        {
            fprintf(stderr, "Erro ao criar a thread %d\n", i);
            exit(-1);
        }
    }

    double total_time = 0.0; // Aguardar a conclusão das threads e calcular o tempo total de execução
    // Aguardar a conclusão das threads
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads_id[i], NULL);
        total_time += thread_times[i].total_time;
    }

    // Cálculo do tempo total de execução
    clock_gettime(CLOCK_MONOTONIC, &end);
    double execution_time = calculate_elapsed_time(start, end);

    // Gravar os tempos no arquivo
    char timing_filename[50];
    sprintf(timing_filename, "timing_%d.txt", num_threads);
    FILE *file = fopen(timing_filename, "w");
    if (file == NULL)
    {
        fprintf(stderr, "Não foi possível abrir o arquivo %s\n", timing_filename);
        return -1;
    }
    // Primeiro, gravar os tempos de processamento de cada imagem
    for (int i = 0; i < image_time_count; i++)
    {
        fprintf(file, "%s %.2f\n", image_times[i].image_name, image_times[i].time);
    }

    // Em seguida, gravar os tempos de cada thread
    for (int i = 0; i < num_threads; i++)
    {
        fprintf(file, "Thread_%d %d %.2f\n", i, thread_times[i].num_images_processed, thread_times[i].total_time);
    }

    // Por último, gravar o tempo total de execução
    fprintf(file, "total %d %.2f\n", num_threads, execution_time);

    fclose(file);

    return 0;
}
