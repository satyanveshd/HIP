/*
Copyright (c) 2015 - present Advanced Micro Devices, Inc. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/* HIT_START
 * BUILD: %t %s ../../test_common.cpp
 * TEST: %t
 * HIT_END
 */

//#include "test_common.h"
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include "hip/hip_runtime.h"

using namespace std;
#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

#define passed()                                                                                   \
    printf("%sPASSED!%s\n", KGRN, KNRM);                                                           \
    exit(0);

#define failed(...)                                                                                \
    printf("%serror: ", KRED);                                                                     \
    printf(__VA_ARGS__);                                                                           \
    printf("\n");                                                                                  \
    printf("error: TEST FAILED\n%s", KNRM);                                                        \
    abort();

#define HIPCHECK(error)                                                                            \
    {                                                                                              \
        hipError_t localError = error;                                                             \
        if ((localError != hipSuccess) && (localError != hipErrorPeerAccessAlreadyEnabled)) {      \
            printf("%serror: '%s'(%d) from %s at %s:%d%s\n", KRED, hipGetErrorString(localError),  \
                   localError, #error, __FILE__, __LINE__, KNRM);                                  \
            failed("API returned error code.");                                                    \
        }                                                                                          \
    }

typedef struct mem_handle {
    //int a;
    hipIpcMemHandle_t memHandle;
} hip_ipc_t;

#define N 10000
int main()
{
    sem_t *sem_ob1, *sem_ob2;
    
    if ((sem_ob1 = sem_open ("/my-sem-object1", O_CREAT|O_EXCL, 0660, 0)) == SEM_FAILED) {
        perror ("my-sem-object1");
	exit (1);
    }
    if ((sem_ob2 = sem_open ("/my-sem-object2", O_CREAT|O_EXCL, 0660, 0)) == SEM_FAILED) {
        perror ("my-sem-object2");
	exit (1);
    }

    hip_ipc_t *shrd_mem=(hip_ipc_t *)mmap(NULL, sizeof(hip_ipc_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
    //assert(MAP_FAILED != shrd_mem);

    pid_t pid;
    int Nbytes = N * sizeof(int);
    int *A_d;
    int *A_h, *B_h, *C_h, *D_h, *E_h;

    A_h = (int*)malloc(Nbytes);
    B_h = (int*)malloc(Nbytes);
    C_h = (int*)malloc(Nbytes);
    D_h = (int*)malloc(Nbytes);
    E_h = (int*)malloc(Nbytes);

    for (int i = 0; i < N; i++) {
        A_h[i] = i;
        B_h[i] = i+1;
    }
    
    pid = fork();
    if(pid != 0) {

        HIPCHECK(hipMalloc((void **) &A_d, Nbytes));
   
        HIPCHECK(hipIpcGetMemHandle((hipIpcMemHandle_t *) &shrd_mem->memHandle, (void *) A_d));

        HIPCHECK(hipMemcpy((void *) A_d, (void *) A_h, Nbytes, hipMemcpyHostToDevice));

        HIPCHECK(hipDeviceSynchronize());
	
        if(sem_post(sem_ob1) == -1) {
            perror ("sem_post"); exit (1);
	}
 
    } else {
 		
        if(sem_wait(sem_ob1) == -1) {
            perror ("sem_wait"); exit (1);
	}
        
        void *S_d = nullptr;  
        int *R_d;
        HIPCHECK(hipMalloc((void **) &R_d, Nbytes));
        
        HIPCHECK(hipIpcOpenMemHandle((void **) &S_d, *(hipIpcMemHandle_t *)&shrd_mem->memHandle, hipIpcMemLazyEnablePeerAccess));
        if (S_d == nullptr) {
            cout<<"S_d is Null"<<endl;
        }
 
        HIPCHECK(hipMemcpy(C_h, S_d, Nbytes, hipMemcpyDeviceToHost));

        HIPCHECK(hipMemcpy(R_d, S_d, Nbytes, hipMemcpyDeviceToDevice));
        HIPCHECK(hipMemcpy(D_h, R_d, Nbytes, hipMemcpyDeviceToHost));

        HIPCHECK(hipMemcpy(S_d, B_h, Nbytes, hipMemcpyHostToDevice));
 
        HIPCHECK(hipMemcpy(E_h, S_d, Nbytes, hipMemcpyDeviceToHost));
        
        HIPCHECK(hipIpcCloseMemHandle((void*)S_d));
  
        for (unsigned i = 0; i < N; i++) {
           assert(C_h[i] == A_h[i]);
           assert(D_h[i] == A_h[i]);
           assert(E_h[i] == B_h[i]);
        }
 
        if(sem_post(sem_ob2) == -1) {
            perror ("sem_post"); exit (1); 
        }

        if(sem_close(sem_ob1) == -1) {
            perror ("sem_close error!");
            exit (1);
        } 

        if(sem_close(sem_ob2) == -1) {
            perror ("sem_close error!");
            exit (1);
        }
        exit(0);	
    }

    if(sem_wait(sem_ob2) == -1) {
        perror ("sem_wait");
        exit (1);
    }
     
    if(sem_unlink("/my-sem-object1") == -1) {
        perror ("sem_unlink error!");
	exit (1);
    }
    if(sem_unlink("/my-sem-object2") == -1) {
        perror ("sem_unlink error!");
        exit (1);
    }
   
    HIPCHECK(hipFree((void*)A_d));
    free(A_h);
    free(B_h);
    free(C_h);
    free(D_h);
    free(E_h);

    passed();

}

