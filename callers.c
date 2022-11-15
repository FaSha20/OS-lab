#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    printf(2, "You must enter exactly 1 inputs!\n");
    exit();
  }
  else
  {
    int syscall_number = atoi(argv[1]);
    printf(1, "User: get_callers() called for system call: %d\n", syscall_number);
    get_callers(syscall_number);
    exit();
  }
  exit();
}