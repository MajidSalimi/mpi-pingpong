#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <mpi.h>
#include <argp.h>

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
    { "skip",       's', "NUM", 0, "iters to perform before reporting, default 0" },
    { "frequency",  'f', "NUM", 0, "microseconds between send events, default 0" },
    { "units",      'u', "s|ms|us|ns", 0, "units to output, default microseconds"},
    { "precision",  'p', "NUM", 0, "number of decimal places, default enough for NS"},
    { "timestamp",  't', 0, 0, "print send timestamps" },
    { 0 }
};

struct arguments {
    int iterations;
    int frequency;
    int skip;
    int pingpong;
    int timestamp;
    int units;
    int precision;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;

    switch (key) {
        case 'i': arguments->iterations = atoi(arg); break;
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

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

int main(int argc, char *argv[])
{
    int rank;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 1) {
        int buf, iters, pingpong;
        MPI_Recv(&iters, 1, MPI_INT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&pingpong, 1, MPI_INT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int index = 0; index < iters; index++) {
            MPI_Recv(&buf, 1, MPI_INT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (pingpong)
                MPI_Send(&buf, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);
        }
    }
    else if (rank == 0) {
        struct arguments args;
        args.iterations = 20;
        args.frequency = 0;
        args.skip = 0;
        args.pingpong = 0;
        args.units = TIME_UNITS_US;
        args.precision = -1;
        args.timestamp = 0;

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

        int iters = args.iterations + args.skip;
        MPI_Ssend(&iters, 1, MPI_INT, 1, 1, MPI_COMM_WORLD); // wait for receiver to get it
        MPI_Ssend(&args.pingpong, 1, MPI_INT, 1, 1, MPI_COMM_WORLD); // wait for receiver to get it

        int bucket_size_ns = args.frequency * 1000;

        struct timespec *send_ts = (struct timespec *)malloc(iters * sizeof(struct timespec));
        struct timespec *recv_ts = (struct timespec *)malloc(iters * sizeof(struct timespec));
        struct timespec last_ts, this_ts, diff;

        long bucket_ns = bucket_size_ns;
        int buf, index = 0;

        while (index < iters) {
            clock_gettime(CLOCK_MONOTONIC, &this_ts);

            // add to the bucket
            if (index > 0) {
                timespec_subtract(&diff, &this_ts, &last_ts);
                bucket_ns += diff.tv_sec * NANOS + diff.tv_nsec;
            }

            last_ts.tv_sec = this_ts.tv_sec;
            last_ts.tv_nsec = this_ts.tv_nsec;

            // drain the bucket
            if (bucket_ns >= bucket_size_ns) {
                send_ts[index].tv_sec = this_ts.tv_sec;
                send_ts[index].tv_nsec = this_ts.tv_nsec;

                if (args.pingpong) {
                    MPI_Send(&buf, 1, MPI_INT, 1, 1, MPI_COMM_WORLD);
                    MPI_Recv(&buf, 1, MPI_INT, 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                else
                    MPI_Ssend(&buf, 1, MPI_INT, 1, 1, MPI_COMM_WORLD);

                clock_gettime(CLOCK_MONOTONIC, recv_ts + index);

                bucket_ns -= bucket_size_ns;
                index++;
            }
        }

        for (index = args.skip; index < iters; index++) {
            struct timespec diff_ts;
            long diff_ns;
            double diff_f;

            if (args.timestamp) {
                timespec_subtract(&diff_ts, send_ts + index, send_ts + args.skip); // difference from first
                printf("%.*f,",
                    args.precision,
                    nsec_to_double(timespec_to_nsec(&diff_ts), args.units)
                );
            }

            timespec_subtract(&diff_ts, recv_ts + index, send_ts + index);
            printf("%.*f\n",
                args.precision,
                nsec_to_double(timespec_to_nsec(&diff_ts), args.units)
            );
        }

        free(send_ts);
        free(recv_ts);
    }

    MPI_Finalize();
    return 0;
}
