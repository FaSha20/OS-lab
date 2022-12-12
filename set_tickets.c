#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    int pid, count;

    if(argc > 2) {
        pid = atoi(argv[1]);
        count = atoi(argv[2]);
    }
    else {
        printf(1, "Insufficient inputs\n", sizeof("Insufficient inputs\n"));
        exit();
    }

    set_tickets(pid, count);
    
    exit();
}


