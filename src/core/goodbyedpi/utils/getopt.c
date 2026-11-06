#include "getopt.h"
#include <string.h>
#include <stdio.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;
static char *nextchar = NULL;

void getopt_reset(void) {
    optarg = NULL;
    optind = 1;
    opterr = 1;
    optopt = 0;
    nextchar = NULL;
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    if (optind >= argc || argv[optind] == NULL) {
        return -1;
    }

    char *arg = argv[optind];

    if (arg[0] != '-' || arg[1] == '\0') {
        return -1;
    }

    if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0') {
        optind++;
        return -1;
    }

    // Long option
    if (arg[0] == '-' && arg[1] == '-') {
        const char *name = arg + 2;
        const char *equal = strchr(name, '=');
        size_t namelen = equal ? (size_t)(equal - name) : strlen(name);

        if (longopts) {
            for (int i = 0; longopts[i].name != NULL; i++) {
                if (strncmp(longopts[i].name, name, namelen) == 0 &&
                    strlen(longopts[i].name) == namelen) {
                    if (longindex) *longindex = i;
                    optind++;

                    if (longopts[i].has_arg == required_argument) {
                        if (equal) {
                            optarg = (char*)(equal + 1);
                        } else if (optind < argc) {
                            optarg = argv[optind++];
                        } else {
                            optopt = longopts[i].val;
                            return (optstring && optstring[0] == ':') ? ':' : '?';
                        }
                    } else if (longopts[i].has_arg == optional_argument) {
                        if (equal) {
                            optarg = (char*)(equal + 1);
                        } else {
                            optarg = NULL;
                        }
                    } else {
                        if (equal) {
                            return '?';
                        }
                        optarg = NULL;
                    }

                    if (longopts[i].flag != NULL) {
                        *(longopts[i].flag) = longopts[i].val;
                        return 0;
                    }
                    return longopts[i].val;
                }
            }
        }
        optopt = 0;
        optind++;
        return '?';
    }

    // Short option
    if (!nextchar || *nextchar == '\0') {
        nextchar = arg + 1;
    }

    char c = *nextchar++;
    if (*nextchar == '\0') {
        optind++;
        nextchar = NULL;
    }

    const char *oli = strchr(optstring, c);
    if (!oli || c == ':') {
        optopt = c;
        return '?';
    }

    if (oli[1] == ':') {
        if (oli[2] == ':') {
            if (nextchar) {
                optarg = nextchar;
                optind++;
                nextchar = NULL;
            } else {
                optarg = NULL;
            }
        } else {
            if (nextchar) {
                optarg = nextchar;
                optind++;
                nextchar = NULL;
            } else if (optind < argc) {
                optarg = argv[optind++];
            } else {
                optopt = c;
                return (optstring[0] == ':') ? ':' : '?';
            }
        }
    } else {
        optarg = NULL;
    }

    return c;
}

int getopt(int argc, char *const argv[], const char *optstring) {
    return getopt_long(argc, argv, optstring, NULL, NULL);
}

