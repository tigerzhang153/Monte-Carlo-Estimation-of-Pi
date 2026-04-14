#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>
#include <errno.h>

#include "monte_carlo.h"

#define MAX_RETRIES 3

typedef struct{
    long hits;
    long total;
} result_t;



// Rerun a worker job if it fails 
void run_worker(int write_fd, long total){
    long hits = 0;

    unsigned int seed = (unsigned int)getpid();

    for (long j = 0; j < total; j++) {
        double x = rand_r(&seed) / (double)RAND_MAX;
        double y = rand_r(&seed) / (double)RAND_MAX;

        if (x * x + y * y <= 1.0) {
            hits++;
        }
    }

    result_t res;
    res.hits = hits;
    res.total = total;

    if (write(write_fd, &res, sizeof(res)) != sizeof(res)) {
        perror("write failed in worker");
        close(write_fd);
        exit(1);
    }

    close(write_fd);
    exit(0);
}



// Spawn a worker and return PID
pid_t spawn_worker(int pipes[][2], int i){
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // CHILD

        close(pipes[i][0]);

        run_worker(pipes[i][1], POINTS_PER_CHILD);
    }

    return pid;
}


int main() {
    int pipes[NUM_CHILDREN][2];
    pid_t pids[NUM_CHILDREN];

    // Create pipes
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            exit(1);
        }
    }

    // Fork children
    for (int i = 0; i < NUM_CHILDREN; i++){
        pids[i] = spawn_worker(pipes, i);
        if (pids[i] < 0) {
            fprintf(stderr, "failed to spawn worker %d\n", i);
            exit(1);
        }
    }

    long total_hits = 0;
    long total_points = 0;

    //Parent collects results with failure detection
    for (int i = 0; i < NUM_CHILDREN; i++){

        close(pipes[i][1]);

        result_t res;
        int retries = 0;
        int success = 0;

        while (retries < MAX_RETRIES && !success) {

            ssize_t r = read(pipes[i][0], &res, sizeof(res));

            if (r == sizeof(res)) {
                success = 1;
                break;
            }

            //Read failure
            if (r == -1){
                perror("read failed");
            } 
            else {
                fprintf(stderr, "partial read or worker failure\n");
            }

            // Check worker exit status
            int status;
            pid_t w = waitpid(pids[i], &status, WNOHANG);

            if (w == pids[i]) {
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    fprintf(stderr, "worker %d died → restarting\n", i);

                    close(pipes[i][0]);

                    pipe(pipes[i]); // recreate pipe

                    pids[i] = spawn_worker(pipes, i);

                    retries++;

                    continue;
                }
            }

            retries++;
        }

        if (!success){
            fprintf(stderr, "worker %d failed permanently\n", i);
            continue;
        }

        total_hits += res.hits;
        total_points += res.total;

        close(pipes[i][0]);
    }

    for (int i = 0; i < NUM_CHILDREN; i++) {
        waitpid(pids[i], NULL, 0);
    }

    double pi = 4.0 * (double)total_hits / (double)total_points;

    printf("Total points: %ld\n", total_points);
    printf("Points inside circle: %ld\n", total_hits);
    printf("Estimated pi = %.8f\n", pi);

    double p_hat = (double)total_hits / (double)total_points;

    double se = 4.0 * sqrt(p_hat * (1.0 - p_hat) / (double)total_points);

    double ci_low = pi - 1.96 * se;
    double ci_high = pi + 1.96 * se;

    printf("95%% confidence interval: [%.8f, %.8f]\n", ci_low, ci_high);
    printf("Standard error: %.10f\n", se);

    return 0;
}