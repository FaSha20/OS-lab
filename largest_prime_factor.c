#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
	if(argc < 2){
		printf(2, "You must enter exactly 1 inputs!\n");
		exit();
	}
    else
    {
		// We will use ebx register for storing input number
		int saved_ebx, number = atoi(argv[1]);
		asm volatile(
			"movl %%ebx, %0;" // saved_ebx = ebx
			"movl %1, %%ebx;" // ebx = number
			: "=r" (saved_ebx)
			: "r"(number)
		);
		printf(1, "User: find_largest_prime_factor() called for number: %d\n" , number);
		printf(1, "Largest prime factor of %d is: %d\n" , number , find_largest_prime_factor());
		asm("movl %0, %%ebx" : : "r"(saved_ebx)); // ebx = saved_ebx -> restore
		exit();  	
    }
    exit();
} 