#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    int pid, p_ratio, t_ratio, c_ratio;

    if(argc > 4) {
        pid = atoi(argv[1]);
        p_ratio = atoi(argv[2]);
        t_ratio = atoi(argv[3]);
        c_ratio = atoi(argv[4]);
    }
    else {
        printf(1, "Insufficient inputs\n", sizeof("Insufficient inputs\n"));
        exit();
    }

    proc_set_bjf_params(pid, p_ratio, t_ratio, c_ratio);
    
    exit();
}