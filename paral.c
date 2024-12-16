/* 
    COP 4600 Project 2
    
    Modified given serial.c file to implement threading in order to improve the runtime of the compression while maintaining
    the same output as serial.c.
    Original serial.c file compresses a directory of .ppm files with zlib functions at max (9) compression value. 
    Takes one argument at commandline: the directory that the ppm files are at.
    Output hardcoded as "video.vzip".

    By: Sheng Rao
*/

#include <dirent.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>
#include <pthread.h>    //for pthread_create(), pthread_exit(), pthread_join()
			//also variable type pthread_t to store pthread id's
			//also pthread_mutex/lock/unlock()

#define BUFFER_SIZE 1048576 // 1MB
#define MAX_FILES 1500  //hardcoded limit suffices, if not, do realloc like with files[]
#define MAX_THREADS 8   //must be 19 or lower (accounting for main thread), 8 is experimentally the best for 4-core test cpu
//#define DEBUG	//comment this to remove debug printfs

pthread_mutex_t totalLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t jobLock = PTHREAD_MUTEX_INITIALIZER;

//global arrays for file paths, buffers, and compressed sizes
char *fullpath_array[MAX_FILES];
unsigned char *buffer_array[MAX_FILES];
int bytes_array[MAX_FILES];

int total_in = 0;       //Global total for input bytes
int job_counter = 0;     //threads grab this (and increments it) for their index to process
int max_jobs = 0;        //must initialize with max of nfiles in main()

// comparator function for sorting filenames
int cmp(const void *a, const void *b) {
    return strcmp(*(char **)a, *(char **)b);
}

//thread function for compressing files
void *strm_deflate(void *arg) {

    //lock interaction with job_counter
    pthread_mutex_lock(&jobLock);
    int index = job_counter++;  //get next index to run on global arrays
    pthread_mutex_unlock(&jobLock);
    //unlock interaction with job_counter
    
    //while loop, updates index with job_counter++ at the end, keeps running until global job_counter reaches or exceeds max_jobs
    while(index < max_jobs) {   //is possible for later ending threads to have job_counter and thus index higher than max_jobs
        unsigned char buffer_in[BUFFER_SIZE];   //each file in /frames is ~870 KB, less than BUFFER_SIZE
                                                //which guarantees that deflate finishes in a single run
        unsigned char *buffer_out = (unsigned char *)malloc(BUFFER_SIZE * sizeof(unsigned char));   //changed to heap storage so a pointer to
                                                //it can be passed to global array buffer_array and used after thread ends 	
        assert(buffer_out != NULL);

        // open file with fullpath for reading
        FILE *f_in = fopen(fullpath_array[index], "r");
        assert(f_in != NULL);
        
        //read file(fullpath) into buffer_in
        int nbytes = fread(buffer_in, sizeof(unsigned char), BUFFER_SIZE, f_in);
        fclose(f_in);

        z_stream strm;  //used to pass info to/from zlib functions
        int ret = deflateInit(&strm, 9);    //initialize for deflate(), must be run before deflate(), 9 is compression level (max)
        assert(ret == Z_OK);
        strm.avail_in = nbytes; //explicitly define how many bytes goes into compression function
        strm.next_in = buffer_in;   //pointer to where to grab bytes from, updated every call to deflate() (not relevant here since each deflate call is guaranteed to finish completely)
        strm.avail_out = BUFFER_SIZE;   //avail_out tracks how much space is available to flush to, is reduced every time stuff is flushed out
        strm.next_out = buffer_out; //pointer to where to flush output to, updated every call to deflate() (not relevant here since each deflate call is guaranteed to finish completely)

        ret = deflate(&strm, Z_FINISH); //takes info from 'strm', Z_FINISH forces deflate to flush after running instead of waiting for the next call of deflate
        assert(ret == Z_STREAM_END);
        
        bytes_array[index] = BUFFER_SIZE - strm.avail_out;  //update global array with bytes written out
        buffer_array[index] = buffer_out;   //store the buffer_out into global array
        
        #ifdef DEBUG    //debug to check if output matches serial.c
        printf("nb: %d s: %.5s\n", nbytes, buffer_out);
        #endif
        
        //lock interaction with job_counter
        pthread_mutex_lock(&jobLock);
        index = job_counter++;  //get next index to run on global arrays
        total_in += nbytes;     //Update total input bytes (global), locked by totalLock
        pthread_mutex_unlock(&jobLock);
        //unlock interaction with job_counter
    }
 
    return NULL;
}

int main(int argc, char **argv) {    
    // time computation header
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    // end of time computation header
    
    assert(argc == 2);  //allow exactly (excluding the command itself) one argument: the directory of files to process

    DIR *d; //Directory stream, to be used in reading filenames from a directory
    struct dirent *dir; //struct, main use is dir->d_name holds filename
    char **files = NULL;    //array of filenames
    int nfiles = 0; //number of entries in 'files'

    d = opendir(argv[1]);   //d set to directory stream of directory provided in argument
    assert(d != NULL);

    // create sorted list of PPM files
    while ((dir = readdir(d)) != NULL) {    //readdir() returns next entry of struct dirent, or returns NULL if no more entries
        files = realloc(files, (nfiles + 1) * sizeof(char *));  //increase size of files array by one entry
            //tried to change realloc algo, but something bugs out and check fails so left it alone
        assert(files != NULL);
        int len = strlen(dir->d_name);
        //check if filename ends in ".ppm", if so, add to 'files' array and increment nfiles
        if (len > 4 && strcmp(dir->d_name + len - 4, ".ppm") == 0) {
            files[nfiles] = strdup(dir->d_name);
            assert(files[nfiles] != NULL);
            nfiles++;
        }
    }
    closedir(d);

    //Quicksort 'files' array in ascending lexographical order due to cmp compare function.
    qsort(files, nfiles, sizeof(char *), cmp);  //expected-case O(n log n) time complexity

    //for each filename in 'files', assemble the full path in 'fullpath_array'
    for (int i = 0; i < nfiles; i++) {
        int len = strlen(argv[1]) + strlen(files[i]) + 2;   //to be put into thread worker func, argv needs to be passed, which complicates things
        fullpath_array[i] = malloc(len * sizeof(char));     //^^ now that index no longer needs to be passed to thread, fullpath could be passed instead
        assert(fullpath_array[i] != NULL);
        strcpy(fullpath_array[i], argv[1]);
        strcat(fullpath_array[i], "/");
        strcat(fullpath_array[i], files[i]);
    }

    //THREADS THREADS THREADS
    pthread_t threads[MAX_THREADS];
    max_jobs = nfiles;  //set the max jobs to the number of files

    //start threads up to max number of threads, each will run worker function until job_counter reaches max_jobs
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_create(&threads[i], NULL, strm_deflate, NULL);
    }
 
    //wait for all threads to end (they end if they run out of jobs to process)
    for (int j = 0; j < MAX_THREADS; j++) {
        pthread_join(threads[j], NULL);
    }

    FILE *f_out = fopen("video.vzip", "w"); //opens "video.vzip" for writing, file descriptor assigned to f_out
    assert(f_out != NULL);
    
    // dump zipped file
    int total_out = 0;  //sum of bytes written out
    for (int i = 0; i < nfiles; i++) {
        fwrite(&bytes_array[i], sizeof(int), 1, f_out); //apparently this format appends size of each file
        fwrite(buffer_array[i], sizeof(unsigned char), bytes_array[i], f_out);
        total_out += bytes_array[i];    //update sum of bytes written out
    }
    fclose(f_out);

    #ifdef DEBUG
    printf("Total input bytes (total_in): %d\n", total_in);
    printf("Total output bytes (total_out): %d\n", total_out);
    #endif
    printf("Compression rate: %.2lf%%\n", 100.0 * (total_in - total_out) / total_in);

    //free all used elements in global arrays and files[]
    for (int i = 0; i < nfiles; i++) {
        free(fullpath_array[i]);
        free(buffer_array[i]);
        free(files[i]);
    }
    free(files);

    // time computation footer
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Time: %.2f seconds\n", ((double)end.tv_sec+1.0e-9*end.tv_nsec)-((double)start.tv_sec+1.0e-9*start.tv_nsec));
    // end of time computation footer

    return 0;
}
