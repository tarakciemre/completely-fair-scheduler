#include <math.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <pthread.h>

#pragma region parameters

const int sched_prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};
enum state{RUNNING, WAITING, READY};
enum distribution{FIXED, UNIFORM, EXPONENTIAL};
pthread_cond_t scheduler_cv = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
struct Node* head = NULL; // the running queue
struct Node* history_head = NULL; // the history queue
struct timeval tv;
struct timezone tz;
long start_time = 0;
int outmode;

const int MS_TO_MICRO = 1000;
const int MIN_GRANULARITY = 10;
const int SCHED_LATENCY = 100;

int total_process_count = 10;
pthread_t process_tid[10];
int average_process_length = 3000;
bool print_queue = true;
bool upcoming_processes = true;
int active_process_count = 0;
int total_weight = 0;
int START_TIME;
bool process_running;

FILE* output_file;
bool output_to_file;

#pragma endregion parameters
#pragma region structs
struct PCB {
    int priority;
    int pid;
    enum state state;
    pthread_t thread_id;
    int process_len;
    int cpu_time_spent;
    pthread_cond_t cv;
    double vruntime;
    int context_switches;
    unsigned long arrival_time;
    unsigned long completion_time;
};
struct ConsoleGeneratorInput {
    bool input_from_console;
    int min_prio;
    int max_prio;
    int avgPL;
    int minPL;
    int maxPL;
    int avgIAT;
    int minIAT;
    int maxIAT;
    int rqLen;
    int ALLP;
    enum distribution distIAT;
    enum distribution distPL;
};
struct FileGeneratorInput {
    int rqLen;
    int ALLP;
    char in_file[30];
    char out_file[30];
};
struct Node {
    struct PCB pcb;
    struct Node* next;
};
#pragma endregion structs
#pragma region history_queue
void print_with_check(char* a)
{
    if(outmode >= 2)
    {
        if(output_to_file)
        {
            fputs(a, output_file);
        }
        else
        {
            printf("%s", a);
        }
    }
}
struct Node* insert_node_to_log(struct Node* node_to_insert )
{
    if(history_head == NULL)
    {
        //printf("New node is added: it is now head\n");
        history_head = node_to_insert;
        node_to_insert->next = NULL;
        return history_head;
    }
    else if (history_head->pcb.pid > node_to_insert->pcb.pid)
    {
        node_to_insert->next = history_head;
        history_head = node_to_insert;
        return node_to_insert;
    }
    else
    {
        struct Node* curr_node = history_head;
        double ins_pid = node_to_insert->pcb.pid;
        while (curr_node->next != NULL)
        {
            if(curr_node->next->pcb.pid <= ins_pid)
            {
                curr_node = curr_node->next;
            }
            else
            {
                break;
            }
        }
        if(curr_node->next == NULL)
        {
            curr_node->next = node_to_insert;
            node_to_insert->next = NULL;
        }
        else
        {
            struct Node* temp = curr_node->next;
            curr_node->next = node_to_insert;
            node_to_insert->next = temp;
        }
        return node_to_insert;
    }
}
#pragma endregion history_queue
#pragma region running_queue
struct Node* insert_node(struct Node* node_to_insert )
{
    if(outmode == 3)
    {
        char a[1000];
        sprintf(a ,"Process with PID: %d is being added to the run queue.\n", node_to_insert->pcb.pid);
        print_with_check(a);    
    }
    if(head == NULL)
    {
        //printf("New node is added: it is now head\n");
        head = node_to_insert;
        node_to_insert->next = NULL;
        return head;
    }
    else if (head->pcb.vruntime > node_to_insert->pcb.vruntime)
    {
        node_to_insert->next = head;
        head = node_to_insert;
        return node_to_insert;
    }
    else
    {
        struct Node* curr_node = head;
        double ins_vruntime = node_to_insert->pcb.vruntime;
        while (curr_node->next != NULL)
        {
            if(curr_node->next->pcb.vruntime <= ins_vruntime)
            {
                curr_node = curr_node->next;
            }
            else
            {
                break;
            }
        }
        if(curr_node->next == NULL)
        {
            curr_node->next = node_to_insert;
            node_to_insert->next = NULL;
        }
        else
        {
            struct Node* temp = curr_node->next;
            curr_node->next = node_to_insert;
            node_to_insert->next = temp;
        }
        return node_to_insert;
    }
}
struct Node* remove_node(struct Node* node_to_remove )
{
    //printf("Removing node...\n");
    if(head == NULL)
    {
        //printf("No node could be added\n");
        return NULL;
    }
    else
    {
        if(head == node_to_remove)
        {
            //printf("Head is the node to remove.\n");
            head = node_to_remove->next;
            node_to_remove->next = NULL;
            return node_to_remove;
        }
        else
        {
            struct Node* curr_node = head;
            while (curr_node->next != NULL)
            {
                if(curr_node->next == node_to_remove)
                    break;
                else
                    curr_node = curr_node->next;
            }
            if(curr_node->next == NULL)
            {
                return NULL;
            }
            else
            {
                curr_node->next = curr_node->next->next;
                node_to_remove->next = NULL;
                return node_to_remove;
            }
        }   
    }
}
int currentQueueSize()
{
    int i = 0;
    struct Node* curr_node = head;
    while (curr_node != NULL)
    {
        curr_node = curr_node->next;
        i++;
    }
    return i;
}
#pragma endregion running_queue
#pragma region helpers
long getTime()
{
    gettimeofday(&tv,&tz);
    return ((tv.tv_sec * 1000000 ) + tv.tv_usec) / 1000;
}
long getElapsedTime()
{
    gettimeofday(&tv,&tz);
    return (((tv.tv_sec * 1000000 ) + tv.tv_usec) / 1000 ) - start_time;
}
int getWeight(int input)
{
    return sched_prio_to_weight[input+20]; 
}
int calculateTimeslice(struct Node* input)
{
    int input_weight;
    input_weight = getWeight(input->pcb.priority);
    double calculation = (double) input_weight * (double) SCHED_LATENCY / (double) total_weight;
    calculation = calculation + 0.5 - (calculation<0);
    return (int)calculation;
}
void updateVRuntime(struct Node* input, int actual_runtime)
{
    int input_weight;
    input_weight = getWeight(input->pcb.priority);
    int zero_weight = getWeight(0);
    double vruntime_increase = (double) zero_weight * (double) actual_runtime / (double) input_weight;
    //printf("Increasing runtime ..... Total weight: %d\tInput weight: %d\tActual runtime: %d\tVruntime inc: %f\n", total_weight, input_weight, actual_runtime, vruntime_increase);
    input->pcb.vruntime += vruntime_increase;
}
#pragma endregion helpers
#pragma region process_creation
int getExponentialDistribution(int avgValue, int minValue, int maxValue)
{
    double lambda = 1 / (double) avgValue;
    // get a random number between 0 and 1
    while(1)
    {
        // the code below requires -lm in the compilation, otherwise log wont work
        double u = (double)rand() / (double)RAND_MAX ;
        double u_temp = 1 - u;
        double calculation = ( u_temp ) / lambda;
        if( (double) minValue <= calculation && (double) maxValue >= calculation)
        {
            // round calculation to integer value and return it
            calculation = calculation + 0.5 - (calculation < 0); // x is now 55.499999...
            return (int) calculation;
        }
    }
}
int getNextProcessLength(struct ConsoleGeneratorInput* input)
{
    if(input->input_from_console)
    {
        if(input->distPL == FIXED)
        {
            return input->avgPL;
        }
        else if(input->distPL == UNIFORM)
        {
            return (rand() % (input->maxPL - input->minPL + 1)) + input->minPL;   
        }
        else if (input->distPL == EXPONENTIAL)
        {   
            return getExponentialDistribution(input->avgPL, input->minPL, input->maxPL);
        }
    }
    else
    {

    }
}
int getNextInterArrivalTime(struct ConsoleGeneratorInput* input)
{
    if(input->input_from_console)
    {
        if(input->distIAT == FIXED)
        {
            return input->avgIAT;
        }
        else if(input->distIAT == UNIFORM)
        {
            return (rand() % (input->maxIAT - input->minIAT + 1)) + input->minIAT;   
        }
        else if (input->distIAT == EXPONENTIAL)
        {   
            // exponential code
            return getExponentialDistribution(input->avgIAT, input->minIAT, input->maxIAT);
        }
    }
    else
    {

    }
}
int getPriority(struct ConsoleGeneratorInput* input)
{
    return (rand() % (input->max_prio - input->min_prio + 1)) + input->min_prio;
}
#pragma endregion process_creation
#pragma region print_functions
/* PRINT MODES
    Printing has 3 modes
        (1) Print nothing
        (2) 
        The next running thread will print out 
            - the current time ( in ms from start of simulation)
            - pid
            - state
            - how long it will run in the cpu until preempted
        Example format:
        3450 7 RUNNING 50
            
        (3)
        A lot of information will be printed out.
        - new process created
        - process will be added to the runqueue 
        - process is selected for CPU
        - process is running in CPU
        - process expired timeslice
        - process finished
        - etc.
*/
void printQueue()
{
    if(print_queue)
    {
        struct Node* curr_node = head;
        if(curr_node == NULL)
        {
            //printf("Queue is empty\n");
        }
        else
        {
            printf("\nCurrent state of the queue:\n");
            while (curr_node != NULL)
            {
                printf( "%d:\tvruntime: %f\tprocess_length: %d\tcpu_time: %d\tpriority: %d\n", curr_node->pcb.pid, curr_node->pcb.vruntime, curr_node->pcb.process_len, curr_node->pcb.cpu_time_spent, curr_node->pcb.priority);
                curr_node = curr_node->next;
            }
            printf("\n");
        }
    }
}
void printResults()
{
    if(outmode == 1)
    {

    }
    else if(!output_to_file)
    {
        struct Node* curr_node = history_head;
        unsigned long total_wait_time = 0;
        int number_of_processes = 0;
        printf(" pid       arv      dept       prio      cpu     waitr     turna        cs\n");
        while (curr_node != NULL)
        {

            unsigned long turnaround_time = curr_node->pcb.completion_time - curr_node->pcb.arrival_time;
            unsigned long wait_time = turnaround_time - curr_node->pcb.cpu_time_spent;

            printf( "%4d", curr_node->pcb.pid);
            printf( "%10lu", curr_node->pcb.arrival_time);
            printf( "%10lu", curr_node->pcb.completion_time);
            printf( "%10d", curr_node->pcb.priority);
            printf( "%10d", curr_node->pcb.cpu_time_spent);
            printf( "%10lu", wait_time);
            printf( "%10lu", turnaround_time);
            printf( "%10d", curr_node->pcb.context_switches);
            printf("\n");
            curr_node = curr_node->next;

            total_wait_time += wait_time;
            number_of_processes++;
        }
        unsigned long average_wait_time = total_wait_time / number_of_processes;
        printf("avg waiting time: %lu\n", average_wait_time);
    }
    else
    {
        struct Node* curr_node = history_head;
        unsigned long total_wait_time = 0;
        int number_of_processes = 0;
        fprintf(output_file, " pid       arv      dept       prio      cpu     waitr     turna        cs\n");
        while (curr_node != NULL)
        {

            unsigned long turnaround_time = curr_node->pcb.completion_time - curr_node->pcb.arrival_time;
            unsigned long wait_time = turnaround_time - curr_node->pcb.cpu_time_spent;

            fprintf(output_file, "%4d", curr_node->pcb.pid);
            fprintf(output_file, "%10lu", curr_node->pcb.arrival_time);
            fprintf(output_file, "%10lu", curr_node->pcb.completion_time);
            fprintf(output_file, "%10d", curr_node->pcb.priority);
            fprintf(output_file, "%10d", curr_node->pcb.cpu_time_spent);
            fprintf(output_file, "%10lu", wait_time);
            fprintf(output_file, "%10lu", turnaround_time);
            fprintf(output_file, "%10d", curr_node->pcb.context_switches);
            fprintf(output_file, "\n");
            curr_node = curr_node->next;

            total_wait_time += wait_time;
            number_of_processes++;
        }
        unsigned long average_wait_time = total_wait_time / number_of_processes;
        fprintf(output_file, "avg waiting time: %lu\n", average_wait_time);
    }
}
#pragma endregion print_functions
#pragma region threads
void* processFunction( void *ptr)
{
    bool input_from_console = 1;
    struct Node* process_node = (struct Node*) ptr;
    struct PCB* process_pcb = &(process_node->pcb);
    process_pcb->arrival_time = getElapsedTime();
    /*
    This process is just created. To do:
        1- [x] Add itself to the queue
        2- [x] Signal scheduler thread
        3- [x] Start sleeping on condition variable
        4- [x] Make sure these work with the "mutex"
    */
    pthread_mutex_lock(&lock);
    process_pcb->state = READY;
    insert_node(process_node);          // (1)
    pthread_mutex_unlock(&lock);
    pthread_cond_signal(&scheduler_cv); // (2)


    while(1)
    {
        pthread_cond_wait(&(process_pcb->cv), &lock); // (3)

        process_pcb->state = RUNNING;
        process_pcb->context_switches++;
        process_running = true;
        /* [x] On WOKEN UP 
            1- [x] Remove PCB from runqueue
            2- [x] Calculate
                2.1- [x] Timeslice
                2.2- [x] Time remaining until finish
            3- [x] Get the minimum of calculations (2.1) and (2.2)
            4- [x] Sleep for (3)
        */
        remove_node(process_node); // (1)

        // Calculate running amount
        int running_amount = calculateTimeslice(process_node);
        if(running_amount < MIN_GRANULARITY)
            running_amount = MIN_GRANULARITY;
        int remaining_time = process_pcb->process_len - process_pcb->cpu_time_spent;
        if(remaining_time < running_amount)
            running_amount = remaining_time;
        
        // Work....
        pthread_mutex_unlock(&lock);
        //printf("Process next CPU burst: %d ms\n", running_amount);
        
        char a[1000];
        sprintf(a ,"%lu\t%d\tRUNNING\t%d\n", getTime(), process_pcb->pid, running_amount);
        print_with_check(a);
        if(outmode == 3)
        {
            char a[1000];
            sprintf(a ,"Process with PID: %d running in the CPU.\n", process_pcb->pid);
            print_with_check(a);    
        }
        usleep(running_amount * MS_TO_MICRO);
        //printf("Running complete\n");

        // Update cpu time spent and vruntime
        process_pcb->cpu_time_spent += running_amount;
        updateVRuntime(process_node, running_amount);

        // If there is work to do
        remaining_time = process_pcb->process_len - process_pcb->cpu_time_spent;
        if(remaining_time > 0)
        {
            /* [x] On TIMESLICE EXPIRATION 
                - [x] Signal scheduler_cv
                - [x] Add PCB to runqueue again
            */
            //printf("PID: %d Run for %d ms. Remaining time: %d ms\n", process_pcb->pid, running_amount ,remaining_time);
            //printf("Current vruntime: %f\n", process_pcb->vruntime);
            if(outmode == 3)
            {
                char a[1000];
                sprintf(a ,"Process with PID: %d timeslice expired.\n",process_pcb->pid);
                print_with_check(a);    
            }
            pthread_mutex_lock(&lock);

            process_pcb->state = READY;
            insert_node(process_node);
            process_pcb->context_switches++;
            // NN
            //printQueue();
            // -NN
            process_running = false;
            pthread_mutex_unlock(&lock);
            pthread_cond_signal(&scheduler_cv);
        }
        else
        {
            /* [x] ON PROCESS TERMINATION
                - [] deallocate resources
                - [x] destroy synchronization variables
                - [x] terminate
            */  
            // NN
            //printf("%d - Process successfully finished\n", process_pcb->pid);
            pthread_mutex_lock(&lock);

            //printQueue();

            process_pcb->state = READY;
            active_process_count--;
            total_weight -= getWeight(process_pcb->priority);
            process_running = false;
            process_pcb->completion_time = getElapsedTime();
            insert_node_to_log(process_node);

            pthread_cond_destroy(&process_pcb->cv);

            pthread_mutex_unlock(&lock);
            if(outmode == 3)
            {
                char a[1000];
                sprintf(a ,"Process with PID: %d is completed. Completion time: %lu\n",process_pcb->pid, process_pcb->completion_time);
                print_with_check(a);    
            }
            pthread_cond_signal(&scheduler_cv);
            pthread_exit(NULL);
        }
    }
}
void* consoleGeneratorFunction( void *ptr)
{
    /*
    [x] While loop
            try to create a new thread
                if cannot, do nothing
                if can
                    if input type is console:: reduce the number of processes to create by 1
                    if input type is file   :: move on to the next line

            go to sleep for process_creation_interval
    */
    
    struct ConsoleGeneratorInput* input = (struct ConsoleGeneratorInput*) ptr;
    int pid_counter = 1;
    int i = input->ALLP;

    // if input format is F
    /*
        PL 1000 2  --> process length, priority value
        IAT 3000 
        PL 300 -2 
        IAT 1000 
        PL 700 0 
        IAT 5000 
        PL 2000 5 
        IAT 300 
        PL 1000 -10

        Create a queue from which the commands are taken.
        Questions:
            - What to do about the rqLen?
            - Is the file operations done in this thread?
        If there is a fixed rqLength, the program must not pop an IAT 
            function if the insertion is not successful.
    */

    int inter_arrival_time = getNextInterArrivalTime(input);
    // if input format is C
    while(i > 0)
    {
        // [x] check the rq_length size and current number of processes
        if (i != input->ALLP)
            usleep(inter_arrival_time * MS_TO_MICRO);
        pthread_mutex_lock(&lock);
        bool empty = active_process_count < input->rqLen;
        pthread_mutex_unlock(&lock);
        if(empty)
        {
            struct Node* new_node = malloc(sizeof(struct Node));
            new_node->pcb.pid = pid_counter;
            new_node->pcb.state = READY;
            new_node->pcb.vruntime = 0;
            new_node->pcb.process_len = getNextProcessLength(input);
            new_node->pcb.priority = getPriority(input);
            new_node->pcb.context_switches = 0;
            pid_counter++;
            i--;
            pthread_mutex_lock(&lock);
            int created = pthread_create( &(new_node->pcb.thread_id), NULL, processFunction, (void*) new_node);
            active_process_count++;
            total_weight += getWeight(new_node->pcb.priority);

            if(outmode == 3)
            {
                char a[1000];
                sprintf(a ,"New process created with PID: %d, Length: %d, Priority: %d\n", new_node->pcb.pid, new_node->pcb.process_len, new_node->pcb.priority);
                print_with_check(a);    
            }

            if(i == 0)
            {
                upcoming_processes = false;
            }
            pthread_mutex_unlock(&lock);
            inter_arrival_time = getNextInterArrivalTime(input);
            
        }
    }
    pthread_exit(NULL);
}
void* fileGeneratorFunction( void *ptr)
{
    /*
    [x] While loop
            try to create a new thread
                if cannot, do nothing
                if can
                    if input type is console:: reduce the number of processes to create by 1
                    if input type is file   :: move on to the next line

            go to sleep for process_creation_interval
    */
    
    struct FileGeneratorInput* input = (struct FileGeneratorInput*) ptr;
    int pid_counter = 1;
    int i = input->ALLP;
    int process_length;
    int process_priority;
    int inter_arrival_time;

    // if input format is F
    /*
        PL 1000 2  --> process length, priority value
        IAT 3000 
        PL 300 -2 
        IAT 1000 
        PL 700 0 
        IAT 5000 
        PL 2000 5 
        IAT 300 
        PL 1000 -10

        Create a queue from which the commands are taken.
        Questions:
            - What to do about the rqLen?
            - Is the file operations done in this thread?
        If there is a fixed rqLength, the program must not pop an IAT 
            function if the insertion is not successful.
    */ 

    FILE *fp = fopen(input->in_file, "r");
    char word[64];
    
    int input_value_index = 0;

    // if 1, take in the process length
    // if 2, take in the process priority
    // if 3, take in the IAT

    while(fscanf(fp, "%s", word) != EOF )
    {
        printf("%d\n", input_value_index);
        if(input_value_index == 0 || input_value_index == 3 || input_value_index == 5)
        {
            
            if(input_value_index == 3)
            {
                bool executed = false;
                // execute process
                while(!executed)
                {
                    printf("RQLEN: %d ACTIVE PROCESS: %d\n", input->rqLen, active_process_count);
                    // [x] check the rq_length size and current number of processes
                    pthread_mutex_lock(&lock);
                    bool empty = active_process_count < input->rqLen;
                    pthread_mutex_unlock(&lock);
                    if(empty)
                    {
                        printf("boş\n");
                        struct Node* new_node = malloc(sizeof(struct Node));
                        new_node->pcb.pid = pid_counter;
                        new_node->pcb.state = READY;
                        new_node->pcb.vruntime = 0;
                        new_node->pcb.process_len = process_length;
                        new_node->pcb.priority = process_priority;
                        new_node->pcb.context_switches = 0;

                        pid_counter++;
                        i--;
                        int created = pthread_create( &(new_node->pcb.thread_id), NULL, processFunction, (void*) new_node);
                        pthread_mutex_lock(&lock);
                        active_process_count++;
                        total_weight += getWeight(new_node->pcb.priority);
                        pthread_mutex_unlock(&lock);
                        executed = true;
                    }
                    if(!executed)
                    {
                        usleep(inter_arrival_time * MS_TO_MICRO);
                    }
                }
            }
            else if(input_value_index == 5)
            {
                usleep(inter_arrival_time * MS_TO_MICRO);
            }

            if(strcmp(word, "PL") == 0)
            {
                input_value_index = 1;
            }
            else if(strcmp(word, "IAT") == 0)
            {
                input_value_index = 4;
            }
        }
        else if (input_value_index == 1)
        {
            process_length = atoi(word);
            input_value_index = 2;
        }
        else if (input_value_index == 2)
        {
            process_priority = atoi(word);
            input_value_index = 3;
        }
        else if (input_value_index == 4)
        {
            inter_arrival_time = atoi(word);
            input_value_index = 5;
        }
    }
    if(input_value_index == 3)
    {
        bool executed = false;
        // execute process
        while(!executed)
        {
            printf("RQLEN: %d ACTIVE PROCESS: %d\n", input->rqLen, active_process_count);
            // [x] check the rq_length size and current number of processes
            pthread_mutex_lock(&lock);
            bool empty = active_process_count < input->rqLen;
            pthread_mutex_unlock(&lock);
            if(empty)
            {
                printf("boş\n");
                struct Node* new_node = malloc(sizeof(struct Node));
                new_node->pcb.pid = pid_counter;
                new_node->pcb.state = READY;
                new_node->pcb.vruntime = 0;
                new_node->pcb.process_len = process_length;
                new_node->pcb.priority = process_priority;
                new_node->pcb.context_switches = 0;

                pid_counter++;
                i--;
                int created = pthread_create( &(new_node->pcb.thread_id), NULL, processFunction, (void*) new_node);
                pthread_mutex_lock(&lock);
                active_process_count++;
                total_weight += getWeight(new_node->pcb.priority);
                pthread_mutex_unlock(&lock);
                executed = true;
            }
            if(!executed)
            {
                usleep(inter_arrival_time * MS_TO_MICRO);
            }
        }
    }
    fclose(fp);
    printf("This is the end of FILE\n");
    upcoming_processes = false;
    pthread_cond_signal(&scheduler_cv);

    // if input format is C

    /*
    while(i > 0)
    {
        // READ IAT
        pthread_mutex_lock(&lock);
        bool empty = active_process_count < input->rqLen;
        pthread_mutex_unlock(&lock);
        if(empty)
        {
            // READ PROCESS
            struct Node* new_node = malloc(sizeof(struct Node));
            new_node->pcb.pid = pid_counter;
            new_node->pcb.state = READY;
            new_node->pcb.vruntime = 0;
            new_node->pcb.process_len = getNextProcessLength(input);
            new_node->pcb.priority = getPriority(input);
            new_node->pcb.context_switches = 0;

            pid_counter++;
            i--;
            int created = pthread_create( &(new_node->pcb.thread_id), NULL, processFunction, (void*) new_node);
            pthread_mutex_lock(&lock);
            active_process_count++;
            total_weight += getWeight(new_node->pcb.priority);
            pthread_mutex_unlock(&lock);
        }
        usleep(getNextInterArrivalTime(input) * MS_TO_MICRO);
    }
    pthread_mutex_lock(&lock);
    upcoming_processes = false;
    pthread_mutex_unlock(&lock);
    pthread_exit(NULL);
    */
}
void* schedulerFunction( void *ptr)
{
    /* [x] ON WAKE UP
        - [x] Select a process thread from runqueue (using CFS)
        - [x] Wake up the selected thread (signal its cv)
        - [x] Go to sleep
    */
    while(1)
    {
        pthread_cond_wait(&scheduler_cv, &lock);
        //printf("The scheduler is called...\n");
        if(head != NULL)
        {
            if(!process_running)
            {
                if(outmode == 3)
                {
                    char a[1000];
                    sprintf(a ,"Process with PID: %d selected for CPU.\n", head->pcb.pid);
                    print_with_check(a);    
                }
                pthread_mutex_unlock(&lock);
                pthread_cond_signal(&(head->pcb.cv));
            }
            else
            {
                pthread_mutex_unlock(&lock);
            }
        }
        else
        {
            // -if there are no incoming processes, terminate
            if(!upcoming_processes && active_process_count == 0)
            {
                pthread_mutex_unlock(&lock);
                pthread_exit(NULL);  
            }
            else
            {
                pthread_mutex_unlock(&lock);
            }
        }
    }
}
int main(int argc, char *argv[])
{
    srand(time(NULL));
    start_time = getTime();
    pthread_t generatorThread, schedulerThread;
    int  iret1, iret2;
    //printf("start\n");
    // parse the input variables
    #pragma region input_parsing

    /* Example invocations:
          cfs C minPrio maxPrio distPL avgPL minPL maxPL distIAT avgIAT minIAT maxIAT rqLen ALLP OUTMODE [OUTFILE]
        ./cfs C -20 19 exponential 300 100 1000 exponential 400 100 2000 30 100 2
        ./cfs C -20 19 exponential 300 100 7000 exponential 400 100 2000 20 100 1 
        ./cfs C -20 19 u 300 100 1000 u 400 100 2000 20 100 1 
        ./cfs C -10 10 uniform 0 100 1000 exponential 600 100 2000 10 200 2 out.txt 
        ./cfs C 3 15 fixed 100 100 100 fixed 200 200 200 10 200 2 
        ./cfs F 20 100 2 infile.txt 
        ./cfs F 20 100 2 infile.txt out.txt
    */
    bool input_from_console = false;
    if(argv[1][0] == 'C')
    {
        input_from_console = true;
    }

    if(input_from_console)
    {
        struct ConsoleGeneratorInput* console_generator_input = malloc(sizeof(struct ConsoleGeneratorInput));
        
        console_generator_input->input_from_console = true;

        if( argv[4][0] == 'f')
            console_generator_input->distPL = FIXED;
        else if( argv[4][0] == 'u')
            console_generator_input->distPL = UNIFORM;
        else if( argv[4][0] == 'e')
            console_generator_input->distPL = EXPONENTIAL;

        if( argv[8][0] == 'f')
            console_generator_input->distIAT = FIXED;
        else if( argv[8][0] == 'u')
            console_generator_input->distIAT = UNIFORM;
        else if( argv[8][0] == 'e')
            console_generator_input->distIAT = EXPONENTIAL;

        console_generator_input->min_prio = atoi(argv[2]);
        console_generator_input->max_prio = atoi(argv[3]);
        console_generator_input->avgPL = atoi(argv[5]);
        console_generator_input->minPL = atoi(argv[6]);
        console_generator_input->maxPL = atoi(argv[7]);
        console_generator_input->avgIAT = atoi(argv[9]);
        console_generator_input->minIAT = atoi(argv[10]);
        console_generator_input->maxIAT = atoi(argv[11]);
        console_generator_input->rqLen = atoi(argv[12]);
        console_generator_input->ALLP = atoi(argv[13]);
        outmode = atoi(argv[14]);

        if(argc > 15)
        {
            output_to_file = true;
            char output_file_name[30];
            strcpy(output_file_name, argv[15]);
            output_file = fopen(output_file_name, "w");
        }
        else
        {
            output_to_file = false;
        }

        iret2 = pthread_create( &schedulerThread, NULL, schedulerFunction, NULL);
        iret1 = pthread_create( &generatorThread, NULL, consoleGeneratorFunction, (struct ConsoleGeneratorInput*) console_generator_input);
    }
    else
    {
        //   ./cfs F 20 100 2 infile.txt out.txt        
        struct FileGeneratorInput* file_generator_input = malloc(sizeof(struct FileGeneratorInput));

        strcpy(file_generator_input->in_file, argv[5]);
        if(argc > 6)
        {
            output_to_file = true;
            char output_file_name[30];
            strcpy(output_file_name, argv[6]);
            output_file = fopen(output_file_name, "w");
            if(output_file == NULL)
                while(1);
        }
        else
        {
            output_to_file = false;
        }
        file_generator_input->rqLen = atoi(argv[2]);
        file_generator_input->ALLP = atoi(argv[3]);
        outmode = atoi(argv[4]);

            
        iret2 = pthread_create( &schedulerThread, NULL, schedulerFunction, NULL);
        iret1 = pthread_create( &generatorThread, NULL, fileGeneratorFunction, (struct FileGeneratorInput*) file_generator_input);
    }

    #pragma endregion input_parsing
   
    pthread_join( generatorThread, NULL);
    pthread_join( schedulerThread, NULL);

    /* FINAL PRINTING
    Calculate the 
        - [x] Arrival time
        - [x] Finish time
            - Do these as timestamps. Add them to the PCB of the processes
        - [x] Priority
        - [x] Turnaround time
            - Finish - arrival
        - [x] Wait time
            - Turnaround - process length
        - [x] Context switch count
    */
    printResults();

    if(output_to_file)
    {
        fclose(output_file);
    }
    exit(0);
}
#pragma endregion threads