#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    int p_ratio, t_ratio, c_ratio;

    if(argc > 3) {
        p_ratio = atoi(argv[1]);
        t_ratio = atoi(argv[2]);
        c_ratio = atoi(argv[3]);
    }
    else {
        printf(1, "Insufficient inputs\n", sizeof("Insufficient inputs\n"));
        exit();
    }

    sys_set_bjf_params(p_ratio, t_ratio, c_ratio);
    
    exit();
}