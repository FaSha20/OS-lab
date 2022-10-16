#include "types.h"
#include "stat.h"
#include "fcntl.h" 
#include "user.h"
#define MAX_SIZE 100

int primes_between(int start, int end, int ans[MAX_SIZE]){
    int ans_size = 0;
	if(start < 2){start = 2;}
    for(int i = start; i <= end; i++){
		int flag = 0;
        for(int j = 2; j < i; j++){
            if(i % j == 0){
                flag = 1;
                break;
            }
        }
        if(flag == 0){
            ans[ans_size] = i;
            ans_size++;
        }
    }
    return ans_size;
}

int main(int argc, char *argv[]){

    int ans[MAX_SIZE];
    int fd = open("prime_numbers.txt", O_WRONLY | O_CREATE); 
    int start = atoi(argv[1]);
    int end = atoi(argv[2]);
    if(start > end){
        int c = end;
        end = start;
        start = c;
    }
    int len =  primes_between(start, end , ans);
    
    for(int i = 0; i < len; i++){
        char s[MAX_SIZE];
        itoa(ans[i], s);
        write(fd, s, 4); 
    } 
    close(fd); 
    exit();
}

