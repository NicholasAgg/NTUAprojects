#include<stdio.h>
#include<unistd.h>
#include<signal.h>
#include<stdlib.h>
#include<sys/wait.h>
#include<string.h>
#include<stdbool.h>

/* Volatile makes sure that the compiler won't optimize the value of the flags as they may change from EXTERNAL signal.
 * sig_atomic_t An integer type which can be accessed as an atomic entity even in the presence of asynchronous           * interrupts made by signals. */

volatile sig_atomic_t usr1_flag = 0;
volatile sig_atomic_t term_flag = 0;
volatile sig_atomic_t child_flag = 0;

int how_many_children(int argc,char *argv[]){
	/*Use : Error handling for user input.Checks if argc and argv are as expected.
	  Return Values : -1 if input is invalid | If input is valid the number of children (>0) is returned.
	 */
	if (argc != 2 || argv[1] == NULL){
		//If more or less than one argument are given to the program we reject them.
		printf("Invalid Input.Use: %s tftt...(any number of gates) "
		"where 't':open gate and 'f':closed gate for each child process.\n",argv[0]);
		return -1;
	}
	if(strspn(argv[1],"tf") != strlen(argv[1]) || strlen(argv[1]) == 0){
		//strspn returns the span of the substring made up only of t,f.
		//If the user gives some other character (e.g 'e' or '') we reject it.
		printf("Invalid Input.Only characters allowed are 't' for open gate and 'f' for closed.\n");
		return -1;
	}
	return strlen(argv[1]); //Number of children is the length of the given string.
}

bool set_handler(int signal,void (*handler) (int)){
	/* Use : Sets handler function for the given signal.Signal should be any of the available macros (e.g SIGUSR1).
	   Return Values : false if setting the handler fails otherwise true. 
	 */
        //Initialise struct to empty.Otherwise garbage values in fields(sigaction,mask,flags,restorer).
        //That would produce undefined behaviour :(
        struct sigaction action = {};
        action.sa_handler = handler;
        if(sigaction(signal,&action,NULL) == -1){
                return false;
        }
        return true;
}

void usr1_handler(int sig){
	usr1_flag = 1;
	return;
}

void term_handler(int sig){
	term_flag = 1;
	return;
}

void child_handler(int sig){
	child_flag = 1;
	return;
}

int run_child(char *exec_path,int child_id,char gate_stat){
	/* Use : The function accepts the path of the executable for the child process (string),the id of the child
	   process and the status that the child's gate should have.
	   Return Values : -1 if there is an error when calling execv | 0 return value should not be reached normally
	   since execv takes control.
	 */
	char *id = malloc(sizeof(child_id));
	snprintf(id,sizeof(child_id),"%i",child_id);
	char gate_status[2];
	snprintf(gate_status,2,"%c",gate_stat);
	//Child needs to know its id and its gate status which were given to the parent.
        //We pass those as arguments into its *argv[] pointer array.
	char *argv_child[] = {exec_path,id,gate_status,NULL};
	if(execv(argv_child[0],argv_child) == -1){
		perror("Error while initiating child.");
		return -1;
 	}
	return 0;
}

int get_child_id(pid_t to_look_pid,int children,pid_t *child_pid){
	/* Use : Looks for the id of the child based on its pid.Accepts as arguments the pid to look for,the number
	   of children and the array of the pids of the children processes.
	   Return Value : -1 if the id does not correspond to the given pid | Otherwise the id of the child with the
	   given pid.
	 */
	for(int i = 0;i < children;i++){
		if(child_pid[i] == to_look_pid) return i;
	}
	return -1;
}

int main(int argc,char *argv[]){
	if(!set_handler(SIGUSR1,usr1_handler)){
		perror("Error while setting SIGUSR1 handler.");
		return 1;
	}
	if(!set_handler(SIGTERM,term_handler)){
                perror("Error while setting SIGTERM handler.");
		return 1;
        }
	if(!set_handler(SIGCHLD,child_handler)){
                perror("Error while setting SIGCHLD handler.");
		return 1;
        }
	const int children = how_many_children(argc,argv);
	if(children == -1){
		return 1;
	}
	pid_t *child_pid = malloc(children * sizeof(pid_t));
	//Here the pids of the created children will be stored.
	pid_t father_pid = getpid();
	for(int i = 0;i < children;i++){
		int child_pid_now = fork();
		if(child_pid_now < 0){
                	perror("Error while creating child.");
                	exit(1);
		}
		if(child_pid_now == 0){
			//This code is executed only by the new child process.
			if(run_child("./childexec",i,argv[1][i]) == -1){
				exit(1);		
			}
		}
		if(child_pid_now > 0){
			//This code is executed only by the parent process.
		       	printf("[PARENT/PID=%d] Created child %d (PID=%d) and intial state '%c'\n"
                	,father_pid,i,child_pid_now,argv[1][i]);
			child_pid[i] = child_pid_now;
		}
	}
	while(true){
		if(usr1_flag){
			//When SIGUSR1 is given send same siganl to all children.
			for(int i = 0;i < children;i++){ 
				kill(child_pid[i],SIGUSR1);
			}
			usr1_flag = false;
		}
		if(term_flag){
			//When SIGTERM is given terminate all children and then self terminate.
			for(int i = 0;i < children;i++){ 
				kill(child_pid[i],SIGTERM);
			}
			term_flag = false;
			int status;
			waitpid(-1,&status,0); //Wait for all the children to terminate before self termination.
			exit(0);
		}
		if(child_flag){
			//Wait here retuns the pid of the terminated or stopped child.
			int wstatus;
			pid_t affected_child_pid = waitpid(-1,&wstatus,WUNTRACED | WCONTINUED);
			int child_id = get_child_id(affected_child_pid,children,child_pid);
			if(WIFCONTINUED(wstatus)){
				//SIGCHLD is also sent after continuation but in that case we just continue.
				continue;
			}
			if(child_id == -1) {
				//Serious error if we cannot find the child id that means tha the pid we got is garbage.
				printf("Some child died and couldn't be found.\n");
				for(int i = 0;i < children;i++){                                                                                                kill(child_pid[i],SIGTERM);                                                                                     }
				exit(1);
			}
			if(WIFSTOPPED(wstatus)){
				printf("[PARENT/PID=%u] Continued child %u (PID=%u) after stop signal\n"
				,father_pid,child_id,affected_child_pid);
				kill(affected_child_pid,SIGCONT);
			}
			if(WIFSIGNALED(wstatus) || WIFEXITED(wstatus)){
				pid_t new_child_pid = fork();
				if(new_child_pid == 0){
					//This code is executed only by the new child process.
					if(run_child("./childexec",child_id,argv[1][child_id]) == -1){
						return 1;
					};
					
				}
				child_pid[child_id] = new_child_pid;
				printf("[PARENT/PID=%d] Child %d with PID=%d exited\n"
				,father_pid,child_id,affected_child_pid);
				printf("[PARENT/PID=%d] Created new child for gate %d (PID=%d) and intial state '%c'\n"
				,father_pid,child_id,new_child_pid,argv[1][child_id]);
			}
		}
	}
	free(child_pid);
        return 0;
}
