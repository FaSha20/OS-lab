#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
	if(argc < 3){
		printf(2, "You must enter exactly 2 inputs!\n");
		exit();
	}
    else
    {
		char path[255];
		strcpy(path, argv[1]);
		int size = atoi(argv[2]);
		printf(1, "User: change_file_size() called for path: %s, size: %d\n" , path, size);
		int status = change_file_size(path, size);
		if(status == 1)printf(1, "Size of file in path %s changed to %d.\n" ,path, size);
		exit();  	
    }

    exit();
} 