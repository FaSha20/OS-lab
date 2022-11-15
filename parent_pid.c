#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
    printf(1, "User: get_parent_pid() called for current proc(first)\n");
    int pid = fork();
    if(pid == 0){
        printf(1, "Parent PID is: %d\n" , get_parent_pid());
        printf(1, "User: get_parent_pid() called for current proc(second)\n");
        int pid2 = fork();
        if(pid2 == 0){
            printf(1, "Parent PID is: %d\n" , get_parent_pid());
            printf(1, "User: get_parent_pid() called for current proc(third)\n");
            int pid3 = fork();
            if(pid3 == 0){
                printf(1, "Parent PID is: %d\n" , get_parent_pid());
                exit();
            }
            else if(pid3 > 0){
                wait();
            }  
            exit(); 
        } 
        else if(pid2 > 0){
            wait();
        }  
        exit();
    }  
    else if(pid > 0){
        wait();
    }  
    exit();
    return 0;	
} 