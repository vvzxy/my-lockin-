#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

//主要用于通过 RAPL（Running Average Power Limit）接口读取并监控 CPU 能耗信息。具体功能是通过命令行参数指定监控时间，并在运行时采集能耗数据。以下是详细说明：

#define XEON2

#include "platform_defs.h"
#include "rapl_read.h"

int
main(int argc, char **argv) 
{
int core = 0;
  int seconds = 10;
  int seconds_before = 0;
  int num_sockets = 1;

  if (argc > 1)
    {
      seconds = atoi(argv[1]);
    }

  if (argc > 2)
    {
      seconds_before = atoi(argv[2]);
    }

  if (argc > 3)
    {
      num_sockets =  atoi(argv[3]);
    }

      sleep(seconds_before);

  if (num_sockets > 1)
    {
      RR_INIT_ALL();

      RR_START_UNPROTECTED_ALL();
      sleep(seconds);
      RR_STOP_UNPROTECTED_ALL();
    }
  else
    {
      RR_INIT(0);

      RR_START_UNPROTECTED();
      sleep(seconds);
      RR_STOP_UNPROTECTED();
    }


  RR_PRINT_UNPROTECTED(RAPL_PRINT_ENE);


  return 0;
}
