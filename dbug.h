#ifndef DBUG_H
#define DBUG_H

#include <stdio.h>
#include <stdlib.h>

#define DBUG(args...) { fprintf(stderr, "DBUG<%s:%d> ", __FILE__, __LINE__); fprintf(stderr, args); fprintf(stderr, "\n"); }
#define DBUG_HALT { fprintf(stderr, "DBUG_HALT<%s:%d> exit(1)\n", __FILE__, __LINE__); exit(1); }
#define DBUG_HALT_CODE(code) { fprintf(stderr, "DBUG_HALT_CODE<%s:%d> exit(%d)\n", __FILE__, __LINE__, code); exit(code); }
#define ASSERT(ptr) { if (!(ptr)) { fprintf(stderr, "ASSERT<%s:%d>, aborting...\n", __FILE__, __LINE__); abort(); } }

#endif
