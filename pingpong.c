#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <mpi.h>
#include <argp.h>
#include <stdbool.h>

#include "time_util.h"

const char *argp_program_version = "mpi-pingpong 1.0";
const char *argp_program_bug_address = "jwa2@clemson.edu";

static char doc[] =
    "mpi-pingpong -- utility to conduct a series of send/receive events\n"
    "  between a pair of MPI processes to measure latency at a fine\n"
    "  granularity.";

static char args_doc[] = ""; //"ARG1 ARG2";

static struct argp_option options[] = {
    { "receive",    'r', 0, 0, "conduct a send/recv, rather than send/wait" },
    { "iterations", 'i', "NUM", 0, "number of iterations to perform, default 20" },
    { "duration",   'd', "NUM", 0, "number of seconds to perform test, overrides iterations" },
    { "skip",       's', "NUM", 0, "iters to perform before reporting, default 10" },
    { "frequency",  'f', "NUM", 0, "microseconds between send events, default 0" },
    { "units",      'u', "s|ms|us|ns", 0, "units to output, default microseconds"},
    { "precision",  'p', "NUM", 0, "number of decimal places, default enough for NS"},
    { "timestamp",  't', 0, 0, "print send timestamps" },
    { "bytes",      'b', "NUM", 0, "number of bytes to send in each ping message"},
    { 0 }
};

struct arguments {
    int iterations;
    float duration;
    int frequency;
    int skip;
    int pingpong;
    int timestamp;
    int units;
    int precision;
    int msg_bytes;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;

    switch (key) {
        case 'i': arguments->iterations = atoi(arg); break;
        case 'd': arguments->duration = atof(arg); break;
        case 'f': arguments->frequency = atoi(arg); break;
        case 's': arguments->skip = atoi(arg); break;
        case 'r': arguments->pingpong = 1; break;
        case 'u':
            switch (arg[0]) {
                case 's': arguments->units = TIME_UNITS_S;  break;
                case 'm': arguments->units = TIME_UNITS_MS; break;
                case 'u': arguments->units = TIME_UNITS_US; break;
                default:  arguments->units = TIME_UNITS_NS; break;
            }
            break;
        case 'p': arguments->precision = atoi(arg); break;
        case 't': arguments->timestamp = 1; break;
        case 'b': arguments->msg_bytes = atoi(arg); break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

#define CONTROL_STOP     0x01
#define CONTROL_PINGPONG 0x02

#define RESULTS_PAGE_SIZE 1024

struct results_page {
    struct timespec send_ts[RESULTS_PAGE_SIZE];
    struct timespec recv_ts[RESULTS_PAGE_SIZE];
    struct results_page *next;
};

int main(int argc, char *argv[])
{
    int rank;
    MPI_Request req; // for tracking MPI_Isend

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 1) {
        int msg_bytes;
        MPI_Recv(&msg_bytes, 1, MPI_INT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        char buf[msg_bytes];
        char *control = buf;

        while (true) {
            MPI_Recv(&buf, msg_bytes, MPI_BYTE, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (*control & CONTROL_PINGPONG)
                MPI_Isend(&buf, 1, MPI_BYTE, 0, 1, MPI_COMM_WORLD, &req);

            if (*control & CONTROL_STOP)
                break;
        }
    }
    else if (rank == 0) {
        // initialize parameters
        struct arguments args;
        args.iterations = 20;
        args.duration = 0.0;
        args.frequency = 0;
        args.skip = 10;
        args.pingpong = 0;
        args.units = TIME_UNITS_US;
        args.precision = -1;
        args.timestamp = 0;
        args.msg_bytes = 1;

        argp_parse(&argp, argc, argv, 0, 0, &args);

        if (args.precision == -1) {
            // if unspecified, include enough digits to show nanoseconds
            switch (args.units) {
                case TIME_UNITS_S:  args.precision = 9; break;
                case TIME_UNITS_MS: args.precision = 6; break;
                case TIME_UNITS_US: args.precision = 3; break;
                case TIME_UNITS_NS: args.precision = 0; break;
            }
        }

        if (args.duration > 0)
            args.iterations = 0;

        // convey needed parameters to the receiver
        MPI_Ssend(&args.msg_bytes, 1, MPI_INT, 1, 1, MPI_COMM_WORLD); // wait for receiver to get it

        struct results_page *results = NULL, *current_page;
        struct timespec last_ts, this_ts, diff_ts, start_ts;

        int bucket_size_ns = args.frequency * 1000;
        long bucket_ns = bucket_size_ns;

        char buf[args.msg_bytes];
        char *control = buf;
        *control = 0;

        if (args.pingpong)
            *control |= CONTROL_PINGPONG;

        int iters = 0;

        while (true) {
            clock_gettime(CLOCK_MONOTONIC, &this_ts);

            // time to quit?
            if (args.duration > 0) {
                timespec_subtract(&diff_ts, &this_ts, &start_ts);

                if (nsec_to_double(timespec_to_nsec(&diff_ts), TIME_UNITS_S) >= args.duration)
                    break;
            }
            else if (iters > args.iterations + args.skip)
                break;

            // start the duration clock on the first non-skipd ping
            if (iters == args.skip) {
                start_ts.tv_sec = this_ts.tv_sec;
                start_ts.tv_nsec = this_ts.tv_nsec;
            }

            // add to the bucket
            if (iters > 0) {
                timespec_subtract(&diff_ts, &this_ts, &last_ts);
                bucket_ns += diff_ts.tv_sec * NANOS + diff_ts.tv_nsec;
            }

            last_ts.tv_sec = this_ts.tv_sec;
            last_ts.tv_nsec = this_ts.tv_nsec;

            // drain the bucket
            if (bucket_ns >= bucket_size_ns) {
                // allocate a new results page if needed
                if (iters % RESULTS_PAGE_SIZE == 0) {
                    struct results_page *page = (struct results_page *)malloc(sizeof(struct results_page));

                    if (results == NULL) {
                        results = page;
                        current_page = page;
                    }
                    else {
                        current_page->next = page;
                        current_page = page;
                    }
                }

                current_page->send_ts[iters % RESULTS_PAGE_SIZE].tv_sec = this_ts.tv_sec;
                current_page->send_ts[iters % RESULTS_PAGE_SIZE].tv_nsec = this_ts.tv_nsec;

                if (args.pingpong) {
                    MPI_Isend(&buf, args.msg_bytes, MPI_BYTE, 1, 1, MPI_COMM_WORLD, &req);
                    MPI_Recv(&buf, 1, MPI_BYTE, 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                else
                    // use MPI_Ssend here to make sure the receive has started
                    MPI_Ssend(&buf, args.msg_bytes, MPI_BYTE, 1, 1, MPI_COMM_WORLD);

                clock_gettime(CLOCK_MONOTONIC, current_page->recv_ts + (iters % RESULTS_PAGE_SIZE));

                bucket_ns -= bucket_size_ns;
                iters++;
            }
        }

        // send final control message
        *control |= CONTROL_STOP;
        MPI_Send(&buf, args.msg_bytes, MPI_BYTE, 1, 1, MPI_COMM_WORLD);
 
        current_page = results;

        for (int index = 0; index < iters; index++) {

            if (index != 0 && index % RESULTS_PAGE_SIZE == 0) {
                struct results_page *page = current_page;
                current_page = current_page->next;
                free(page);
            }

            if (index < args.skip)
                continue;

            if (args.timestamp) {
                timespec_subtract(&diff_ts, current_page->send_ts + (index % RESULTS_PAGE_SIZE), &start_ts); // difference from first
                printf("%.*f,",
                    args.precision,
                    nsec_to_double(timespec_to_nsec(&diff_ts), args.units)
                );
            }

            timespec_subtract(&diff_ts, current_page->recv_ts + (index % RESULTS_PAGE_SIZE), current_page->send_ts + (index % RESULTS_PAGE_SIZE));
            printf("%.*f\n",
                args.precision,
                nsec_to_double(timespec_to_nsec(&diff_ts), args.units)
            );
        }
    }

    MPI_Finalize();
    return 0;
}
