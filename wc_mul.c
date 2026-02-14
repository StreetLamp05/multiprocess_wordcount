/*************************************************
 * C program to count no of lines, words and 	 *
 * characters in a file.		                 *
 *************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_PROC 100
#define MAX_FORK 1000

typedef struct count_t {
  int linecount;
  int wordcount;
  int charcount;
} count_t;

typedef struct plist_t {
  int pid;
  int offset;
  long size;
  int pipefd[2];
  int done; // 1 if recieved, 0 if pending
} plist_t;

int CRASH = 0;

count_t word_count(FILE* fp, long offset, long size)
{
  char ch;
  long rbytes = 0;

  count_t count;
  // Initialize counter variables
  count.linecount = 0;
  count.wordcount = 0;
  count.charcount = 0;

  printf("[pid %d] reading %ld bytes from offset %ld\n", getpid(), size, offset);

  if(fseek(fp, offset, SEEK_SET) < 0) {
    printf("[pid %d] fseek error!\n", getpid());
  }

  while ((ch=getc(fp)) != EOF && rbytes < size) {
    // Increment character count if NOT new line or space
    if (ch != ' ' && ch != '\n') { ++count.charcount; }

    // Increment word count if new line or space character
    if (ch == ' ' || ch == '\n') { ++count.wordcount; }

    // Increment line count if new line character
    if (ch == '\n') { ++count.linecount; }
    rbytes++;
  }

  srand(getpid());
  if(CRASH > 0 && (rand()%100 < CRASH))
    {
      printf("[pid %d] crashed.\n", getpid());
      abort();
    }

  return count;
}



int launch_child(plist_t *plist, int i , char *filename, int *nFork) {
  int pid;
 



  // close old pipe fds if they were open, new pipe each time
  if (pipe(plist[i].pipefd) < 0) {
    printf("pipe creation failed for job %d \n", i);
    return -1;
  }

  if ((*nFork) ++ > MAX_FORK) {
    printf("max fork limit reached. \n");
    return -1;
  }


  pid = fork();
  if (pid < 0) {
    printf("fork fialed for job %d. \n", i);
    close(plist[i].pipefd[0]);
    close(plist[i].pipefd[1]);
    return -1;

  } else if (pid == 0) {
    close(plist[i].pipefd[0]);
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
      printf("[pid %d] error opening file: %s\n", getpid(), filename);
      _exit(1);
    }

    count_t count = word_count(fp, plist[i].offset, plist[i].size);

    if (write(plist[i].pipefd[1], &count, sizeof(count_t)) < 0) {
      printf("[pid %d] write error\n", getpid());

    }

    close(plist[i].pipefd[1]);
    fclose(fp);
    _exit(0);

  } else {
    // parent process
    close(plist[i].pipefd[1]);
    plist[i].pid = pid;
    plist[i].done = 0;
  }

  return 0;
}





int main(int argc, char **argv)
{
  long fsize;
  FILE *fp;
  int numJobs;
  plist_t plist[MAX_PROC];
  count_t total;
  int i, status;
  int nFork = 0;

  if(argc < 3) {
    printf("usage: wc_mul <# of processes> <filename>\n");
    return 0;
  }

  if(argc > 3) {
    CRASH = atoi(argv[3]);
    if(CRASH < 0) CRASH = 0;
    if(CRASH > 50) CRASH = 50;
  }
  printf("CRASH RATE: %d\n", CRASH);


  numJobs = atoi(argv[1]);
  if(numJobs < 1) { numJobs = 1;}
  if(numJobs > MAX_PROC) numJobs = MAX_PROC;

  total.linecount = 0;
  total.wordcount = 0;
  total.charcount = 0;

  // Open file in read-only mode
  fp = fopen(argv[2], "r");

  if(fp == NULL) {
    printf("File open error: %s\n", argv[2]);
    printf("usage: wc <# of processes> <filname>\n");
    return 0;
  }

  fseek(fp, 0L, SEEK_END);
  fsize = ftell(fp);

  fclose(fp);
  // calculate file offset and size to read for each child
  long chunkSize = fsize / numJobs;
  long remainder = fsize % numJobs;

  for (i = 0; i < numJobs; i++) {
    plist[i].offset = i *chunkSize;
    plist[i].size = chunkSize;
    plist[i].done = 0;
    plist[i].pid = -1;
  }

  // last child gets remainder num bytes
  plist[numJobs - 1].size += remainder;


  // launch children
  for(i = 0; i < numJobs; i ++) {
    if (launch_child(plist, i , argv[2], &nFork) < 0) {
      printf("failed to launch child %d\n", i ); 
    }
  }



  // Parent
  int pendingJobs = numJobs;
 

  while (pendingJobs > 0) {
    int wpid = waitpid(-1, &status, 0);
    if (wpid < 0) {
      // all children done
      break;
    }



    int jobIdx = -1;
    for ( i = 0; i < numJobs; i++) {
      if (plist[i].pid == wpid && !plist[i].done) {
        jobIdx = i;
        break;
      }
    }

    if(jobIdx < 0) {
      // unkownn, skip
      continue;
    }


    if(!(WIFEXITED(status)) || WEXITSTATUS(status) != 0)
    {
        if (WIFSIGNALED(status))
        {
            // Abnormal termination by signal - re-launch
            close(plist[jobIdx].pipefd[0]);
            printf("[parent] child %d (pid %d) killed by signal %d. Re-launching...\n",
                   jobIdx, wpid, WTERMSIG(status));
            if (launch_child(plist, jobIdx, argv[2], &nFork) < 0)
            {
                printf("Failed to re-launch child %d, giving up.\n", jobIdx);
                pendingJobs--;
            }
        }
        else
        {
            // Other abnormal exit - re-launch
            close(plist[jobIdx].pipefd[0]);
            printf("[parent] child %d (pid %d) exited abnormally. Re-launching...\n",
                   jobIdx, wpid);
            if (launch_child(plist, jobIdx, argv[2], &nFork) < 0)
            {
                printf("Failed to re-launch child %d, giving up.\n", jobIdx);
                pendingJobs--;
            }
        }
    }
    else
    {
        // Normal termination - read result from pipe
        count_t count;
        ssize_t bytesRead = read(plist[jobIdx].pipefd[0], &count, sizeof(count_t));
        close(plist[jobIdx].pipefd[0]);

        if (bytesRead == sizeof(count_t))
        {
            printf("[parent] child %d (pid %d) completed successfully.\n", jobIdx, wpid);
            total.linecount += count.linecount;
            total.wordcount += count.wordcount;
            total.charcount += count.charcount;
            plist[jobIdx].done = 1;
            pendingJobs--;
        }
        else
        {
            // Failed to read - treat as crash, re-launch
            printf("[parent] child %d (pid %d) read error, re-launching.\n", jobIdx, wpid);
            if (launch_child(plist, jobIdx, argv[2], &nFork) < 0)
            {
                printf("Failed to re-launch child %d, giving up.\n", jobIdx);
                pendingJobs--;
            }
        }
    }
  }
  // wait for all children
  // check their exit status
  // read the result of normalliy terminated child
  // re-crete new child if there is one or more failed child
  // close pipe

  printf("\n========== Final Results ================\n");
  printf("Total Lines : %d \n", total.linecount);
  printf("Total Words : %d \n", total.wordcount);
  printf("Total Characters : %d \n", total.charcount);
  printf("=========================================\n");

  return(0);
}

