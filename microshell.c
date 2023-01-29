#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <wait.h>
#include <dirent.h>
#include <termios.h>

//Taken from:
//https://stackoverflow.com/a/3219471/12520385
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define historyLen 20
#define stringLen 256

/*
TODO LIST:
history saving and loading into cfg
Stream handling?
*/

//Variable that contains the current path
char curPath[stringLen] = "";

//Last accessed path
char lastPath[stringLen] = "";

//Help message to be displayed
char helpMessage[] = "Lista funkcji:\nexit-zamyka microshell\ncd-zmienia folder\ndir-wyświetla zawartość folderu\ncp-kopiuj\nhistory-historia komend\n";

//Argument seperator
char commandSep = ' ';

//Array of strings containing last ten issued commands
char** history[historyLen][stringLen] = {}; //TODO:change to dynamic

//Shifts all history elements up . Element with id of 19 is lost and no segfault possibility here
void addToHistory(char newCommand[]){
    for(int i = historyLen - 1; i > 1; i--){
        strcpy((char*) history[i], (char*) history[i - 1]);
    }
    strcpy((char*) history[1], newCommand);
}

//Turn multiple c into 1 in input string
char* replaceMultiple(char* input, char c){
    char* output = calloc(1, sizeof(char));
    int ctr = 0;
    int i2 = 0;
    for(int i = 0; i < strlen(input); i++){
        if(input[i] == c){
            ctr++;
        }
        else{
            ctr = 0;
        }
        if(input[i] != c || ctr < 2){
            output = realloc(output, (strlen(output) + 1) * sizeof(char));
            output[i2] = input[i];
            i2++;
        }
    }
    return output;
}

//Adds curPath/filename into pathPtr
void absolutify(char pathPtr[], char filename[]){
    strcpy(pathPtr, curPath);
    strcat(pathPtr, "/");
    strcat(pathPtr, filename);
}

//Checks if the first letters of issuedCommand match the entirety of the commandName
bool checkIfCommand(char *commandName, char *issuedCommand){
    int i;
    int len = strlen(commandName);
    for(i = 0; i < len; i++){
        if(commandName[i] != issuedCommand[i]){
            return false;
        }
    }
    if(issuedCommand[len] != commandSep && issuedCommand[len] != '\0'){
        return false;
    }
    return true;
}

//Returns the i argument from the input string into the ptrOut
//Input format:"command arg1 arg2 arg3..."
char* getArg(int i, char input[], int lenIn){
    char *ptrOut = calloc(1, sizeof(char));
    ptrOut[0] = '\0';
    i++;
    bool quotes = false;
    //char counter for ptrOut
    int i3 = 0;
    for(int i2 = 0; i2 <= (lenIn-1); i2++){
        //currently analysed char
        char c = input[i2];
        if(c == '"'){
            quotes = !quotes;
        }
        else if(c == commandSep && quotes == false){
            i--;
        }
        else{
            if(i == 1){
                //we have encountered enough seperators to read the arg
                //so append the c to the output
                ptrOut = (char*) realloc(ptrOut, (strlen(ptrOut) + 1) * sizeof(char) + sizeof(char));
                ptrOut[i3] = c;
                ptrOut[i3 + 1] = '\0';
                i3++;
            }
        }
        if(i == 0){
            break;
        }
    }
    return ptrOut;
}

//Returns the start of the issued command
char* getStart(char sep, char input[]){
    char* ptrOut = calloc(1, sizeof(char));
    ptrOut[0] = '\0';
    for(int i = 0; i < strlen(input); i++){
        if(input[i] == sep){
            return ptrOut;
        }
        else{
            ptrOut = (char*) realloc(ptrOut, (strlen(ptrOut) + 1) * sizeof(char) + sizeof(char));
            ptrOut[i] = input[i];
            ptrOut[i + 1] = '\0';
        }
    }
    return ptrOut;
}

//Returns True if we can access the requested path and returns False if that's not the case
bool canAccessDir(char path[]){
    DIR* dir = opendir(path);
    if (dir) {
        closedir(dir);
        return true;
    }
    return false;
}

//Prints the contents of a directory
void printDir(DIR* dir){
    struct dirent *file;
    while((file = readdir(dir)) != NULL){
        if(file->d_type == DT_REG){
            printf("%s ", file->d_name);
        }
        else if(file->d_type == DT_DIR){
            printf(ANSI_COLOR_YELLOW "%s " ANSI_COLOR_RESET, file->d_name);
        }
        else{
            //It's something WEIRD then so lets just make it pink lmao
            printf(ANSI_COLOR_MAGENTA "%s " ANSI_COLOR_RESET, file->d_name);
        }
    }
    printf("\n");
}

//Returns the number of occurences of c in the input string
int count(char input[], char c){
    int res = 0;
    for(int i = 0; i < strlen(input); i++){
        if(input[i] == c){
            res++;
        }
    }
    return res;
}

//Writes contents to file
int copyToFile(unsigned char *contents, char* filename, size_t fileSize){
    FILE* newFile = fopen(filename, "wb");
    if(newFile == NULL){ //the file couldn't be found so throw and error BUT continue the loop
        fprintf( stderr, "Plik %s nie mógł być otworzony.\n", filename);
        return 1;
    }
    fflush(newFile);
    fwrite(contents, fileSize, 1, newFile);
    fclose(newFile);
    return 0;
}

//Read file contents and then write them using copyToFile()
int copyFile(char filename[], char newFilename[]){
    FILE* file; //Pointer to the file stream
    if(filename[0] != '/'){
        char* path;
        path = (char*) malloc(sizeof(char) * (strlen(curPath) + strlen(filename) + 1));
        absolutify(path, filename);
        file = fopen(path, "r");
        free(path);
    }
    else{
        file = fopen(filename, "rb");
    } 
    if(file == NULL){ //the file couldn't be found so lets assume it was relative
        fprintf( stderr, "Plik %s nie mógł być otworzony.\n", filename);
        return 1;
    }
    //If we reached this stage we must have a working file
    unsigned char *contents;//Array of bytes containing file data
    fseek(file, 0L, SEEK_END);//move stream pointer to the end
    //get steam pointer pos aka file size
    size_t fileSize = ftell(file);//Fize size in bits
    rewind(file);//move the pointer back
    contents = (unsigned char*) malloc(fileSize);
    fread(contents, fileSize, 1, file); //read file bytes
    fclose(file); //close the stream
    copyToFile(contents, newFilename, fileSize);
    free(contents);
    return 0;
}

//Create a new directory and then copy each file within the original directory to the new directory.
//The recursive bool also will make the function run itself for each child directory
int copyDir(char path[], char newPath[], bool recursive){
    //Create a new folder with the same name as the folder to copy
    if(mkdir(newPath, 0700) != 0){
        fprintf( stderr, "Folder %s nie mógł być stworzony.\n", newPath);
    }
    //open the folder to read the elements in the folder
    DIR* dir = opendir(path);
    struct dirent *file;
    //foreach element in the folder
    while((file = readdir(dir)) != NULL){
        //Get the element name
        char* filename;
        filename = (char*) malloc(sizeof(char) * (strlen(file->d_name)));
        strcpy(filename, file->d_name);
        //Create an absolute path for the element
        char* filePath; //Absolute path for origin element
        filePath = (char*) malloc(sizeof(char) * (strlen(path) + strlen(filename) + 2));
        strcpy(filePath, path);
        strcat(filePath, "/");
        strcat(filePath, filename);
        //Create an absolute path for the new element 
        char* newFilePath; //Absolute path for where to copy the element
        newFilePath = (char*) malloc(sizeof(char) * (strlen(newPath) + strlen(filename) + 2));
        strcpy(newFilePath, newPath);
        strcat(newFilePath, "/");
        strcat(newFilePath, filename);
        if(file->d_type == DT_REG){
            copyFile(filePath, newFilePath);
        }
        else if(file->d_type == DT_DIR && recursive && checkIfCommand("..", filename) == false && checkIfCommand(".", filename) == false){
            copyDir(filePath, newFilePath, recursive);
        }
        free(filename);
        free(filePath);
        free(newFilePath);
    }
    return 0;
}

//Taken from https://stackoverflow.com/a/4553076
//Returns true if is file and false if not file
int is_regular_file(const char *path)
{
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

//Returns pointer to a string or NULL if failed
char* parseExistingPath(char* input){
    char* path;
    path = (char*) calloc(strlen(curPath) + strlen(input) + 2, sizeof(char)); //used to be +1 but changing to +2 made valgrind happier
    path[0] = '\0';
    if(input[0] != '/'){
        absolutify(path, input);
        char* ptr = realpath(path, NULL); //valgrind keeps pointing out this line
        free(path);
        if(ptr != NULL){
            return ptr;
        }
        else{
            return NULL;
        }
    }
    else{
        char* ptr = realpath(input, NULL);
        if(ptr != NULL){
            return ptr;
        }
        else{
            return NULL;
        }
    }
}

//Taken from: https://web.archive.org/web/20180401093525/http://cc.byexamples.com/2007/04/08/non-blocking-user-input-in-loop-without-ncurses/
//Though all comments are mine :)

//Returns true if stdin is ready to be read. AKA if there is some input
int kbhit()
{
    //Create a new timeval entity that will tell select to check immediatly if stdin can be ready
    struct timeval tv; //New variable for storing elapsed time
    tv.tv_sec = 0; //set elapsed seconds to 0
    tv.tv_usec = 0; //set elapsed microseconds to 0

    fd_set fds; //create a new file description set
    FD_ZERO(&fds); //clear the set just to be sure
    FD_SET(STDIN_FILENO, &fds); //add stdin to the set
    select(STDIN_FILENO, &fds, NULL, NULL, &tv); //check if stdin is ready to be read. Store that value in fds and check now since tv is set to 0
    return FD_ISSET(STDIN_FILENO, &fds); //if stdin is ready to be read return 1
}

//Returns a single input char from stdin without waiting for Enter
//Requires disabled canon terminal mode
char getKey(){
    while(!kbhit()){ //as long as there is no keyboard input do nothing
        usleep(1);
    }
    char c = '\0';
    //stdin is ready to be read so let's do it
    c = fgetc(stdin);
    return c;
}

//Returns user input and handles arrow keys alongside ctrl+Z and ctrl+C
char* getInput(){
    struct termios terminalState;
    tcgetattr(STDIN_FILENO, &terminalState);
    //Ignore BREAK
    terminalState.c_lflag &= ~IGNBRK;
    terminalState.c_lflag &= ~BRKINT;
    //disable echo. We will do our own echo when chars aint special
    terminalState.c_lflag &= ~ECHO;
    //turn off canonical mode
    terminalState.c_lflag &= ~ICANON;
    //minimum of number input read.
    terminalState.c_cc[VMIN] = 1;
    //Apply changes
    tcsetattr(STDIN_FILENO, TCSANOW, &terminalState);
    //Initialise the string
    char* output; //The string that will be returned
    output = (char*) calloc(2, sizeof(char));
    output[0] = '\0';
    //Last read history element
    int historyId = 0;
    //Current cursor position
    int i = 0;
    //Currently read key
    char c = '\0';
    while((int) c != 10){
        //Get the pressed key
        c = getKey();
        //Get char code for some low level signal handling
        int code = (int) c; //Char code of the currently read key
        //printf("%d\n", code);
        if(code == 27){//Some sort of arrowkey
            //Arrowkeys are sent like this (27)[A
            fgetc(stdin); //skip [
            c = fgetc(stdin); //get the 3rd char
            if(c == 'C' && i < strlen(output)){ //right arrow key
                printf("\033[1C");
                i++;
            }
            else if(c == 'D' && i > 0){ //left arrow key
                printf("\033[1D");
                i--;
            }
            else if(c == 'A' || c == 'B'){ //up and down keys - history access
                //wipe the input field
                for(int n = 0; n < i; n++){
                    printf("\b \b");
                }
                //move the id
                if(c == 'B' && historyId > 0){
                    historyId--;
                }
                else if(c == 'A' && historyId < historyLen){
                    historyId++;
                }
                i = strlen((char*) history[historyId]); 
                output = (char*) realloc(output, (i + 2) * sizeof(char)); //allocate necessary memory
                strcpy(output, (char*) history[historyId]);
                printf("%s", (char*) history[historyId]); 
            }
            else if((int)c == 51){ //Delete
                addToHistory(output);
                //wipe the input field
                for(int n = 0; n < i; n++){
                    printf("\b \b");
                }
                i = 0;
                output[0] = '\0';
                fgetc(stdin);
            }
        }
        else if(code == 127){//Backspace
            if(i > 0){
                i--;
                output[i] = ' ';
                printf("\b \b");
                output = (char*) realloc(output, (i + 2) * sizeof(char));
            }
        }
        else if(code == 26){//Ctrl+Z
            //wipe the input field
            for(int n = 0; n < (i); n++){
                printf("\b \b");
            }
            i = strlen((char*) history[1]); 
            output = (char*) realloc(output, (i + 2) * sizeof(char)); //allocate necessary memory
            strcpy(output, (char*) history[1]);
            printf("%s", (char*) history[1]); 
        }
        else if(code == 3){//Ctrl+C
            printf("\n");
            free(output);
            exit(0);
        }
        else if(code == 10){
            //break out since user presed enter
            break;
        }
        else{
            if(i >= strlen(output)){
                output = (char*) realloc(output, (strlen(output) + 2) * sizeof(char));
                output[i + 1] = '\0';
            }
            printf("%c", c); //print the character if it ain't special since we disabled echo
            output[i] = c;
            i++;
            
        }
    }
    printf("\033[%dC\n", (int) strlen(output) - i); //move the cursor to the end of the input
    //Reenable BREAK
    terminalState.c_lflag |= IGNBRK;
    terminalState.c_lflag |= BRKINT;
    //reenable echo
    terminalState.c_lflag |= ECHO;
    //turn on canonical mode
    terminalState.c_lflag |= ICANON;
    //Apply changes
    tcsetattr(STDIN_FILENO, TCSANOW, &terminalState);
    return output;
}

int main(int argc, char *argv[]){
    getcwd(curPath, sizeof(curPath));
    getcwd(lastPath, sizeof(lastPath));
    //Current user login retrieved from enviroment variables
    const char* userName = getenv("USER");
    char* client = malloc(HOST_NAME_MAX);
    gethostname(client, HOST_NAME_MAX);
    //arg parsing
    if(argc > 1){
        for(int i = 1; i <= argc - 1; i++){
            //simple flags
            if(checkIfCommand("-home", argv[i])){
                const char* home = getenv("HOME");
                strcpy(curPath, home);
            }
            //advanced flags
            else if(argc - (i + 1) > 0){
                //Path argument that makes the program start at a certain path
                if(checkIfCommand("-p", argv[i])){
                    bool access = canAccessDir(argv[i + 1]);
                    if(access){
                        strcpy(curPath, argv[i + 1]);
                        i++;
                    }
                    else{
                        printf("Próba uruchomienia programu z argumentem startowym -p zakończyła się niepowodzeniem.\n");
                    }
                }
                //Path argument for changing seperator
                else if(checkIfCommand("-sep", argv[i])){
                    if(strlen(argv[i + 1]) == 1){
                        if(argv[i + 1][0] != '"' && argv[i + 1][0] != ';'){
                            commandSep = argv[i + 1][0];
                            i++;
                            printf("Zmieniono separator argumentów na %c \n", commandSep);
                        }
                        else{
                            printf("Niedozwolony seperator argumentów \n");
                        }                    
                    }
                    else{
                        printf("Próba uruchomienia programu z argumentem startowym -sep zakończyła się niepowodzeniem.\nSeperator powinien być jednoliterowy.\n");
                    }
                }
            }
        }
    }
    //main microshell loop
    while(true){
        printf("[" ANSI_COLOR_GREEN "%s" ANSI_COLOR_RESET "@" ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET "|" ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET "]$", userName, client, curPath);
        //Current command input
        char *command;
        command = getInput();
        if(checkIfCommand("exit", command)){
            free(command);
            exit(0);
        }
        else if(checkIfCommand("cd", command)){//cd command here because curPath is only in parent process memory
            //argument nr1 - in this case path
            char* arg1 = getArg(1, command, strlen(command));
            if(strlen(arg1) > 0){
                if(arg1[0] == '~'){
                    strcpy(lastPath, curPath);
                    strcpy(curPath, getenv("HOME"));
                }
                else if(arg1[0] == '-'){
                    strcpy(curPath, lastPath);
                }
                else{
                    char* path;
                    if((path = parseExistingPath(arg1)) != NULL){
                        strcpy(lastPath, curPath);
                        strcpy(curPath, path);
                    }
                    else{
                        printf("Brak dostępu do ścieżki " ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET " lub podana ścieżka jest błędna.\n", arg1);
                    }
                    free(path);
                }
            }
            else{
                printf("Brak dostępu do ścieżki " ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET " lub podana ścieżka jest błędna.\n", arg1);
            }
            free(arg1);
        }
        else if(strlen(command) > 0){//command recieved lets try and interpret it
            //fork return value
            int pid = fork();
            //process return value
            int res = 0;
            if(pid == 0){
                //command handler
                if(checkIfCommand("help", command)){
                    printf("%s", helpMessage);
                    exit(0);
                }
                else if(checkIfCommand("dir", command)){
                    //argument 1 - optional path
                    char* arg1 = getArg(1, command, strlen(command));
                    if(strlen(arg1) > 0){
                        char* path;
                        if((path = parseExistingPath(arg1)) != 0){
                            DIR* dir = opendir(path);
                            free(path);
                            if (dir) {
                                printDir(dir);
                                closedir(dir);
                                exit(0);
                            }
                            else{
                                closedir(dir);
                                fprintf( stderr, "Podana ścieżka " ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET " istnieje ale podczas otwierania nastąpił nieoczekiwany błąd.\n", arg1);
                                exit(1);
                            }
                        }
                        else{
                            fprintf( stderr, "Podana ścieżka " ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET " nie mogła być zinterpretowana.\n", arg1);
                            exit(1);
                        }
                    }
                    else{//arg wasnt provided so we just assume the user wants to see the contents of the working dir
                        DIR* dir = opendir(curPath);
                        if (dir) {
                            printDir(dir);
                            closedir(dir);
                            exit(0);
                        }
                        else{
                            closedir(dir);
                            exit(1);
                        }
                    }
                    free(arg1);
                }
                else if(checkIfCommand("cp", command)){
                    //argument 1 - source
                    char* arg1 = getArg(1, command, strlen(command));
                    if(is_regular_file(arg1)){//we are dealing with a file
                        //this is not done with the copyFile function because this is slightly more efficient when copying multiple files
                        char* path = parseExistingPath(arg1);
                        FILE* file = fopen(path, "rb"); //Pointer to the file stream
                        free(path);
                        if(file == NULL){ //the file couldn't be found so lets assume it was relative
                            fprintf( stderr, "Podana ścieżka " ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET " nie mogła być zinterpretowana.\n", arg1);
                            exit(1);
                        }
                        //If we reached this stage we must have a working file
                        unsigned char *contents;//Array of bytes containing file data
                        fseek(file, 0L, SEEK_END);//move stream pointer to the end
                        //get steam pointer pos aka file size
                        size_t fileSize = ftell(file);//Fize size in bits
                        rewind(file);//move the pointer back
                        contents = (unsigned char*) malloc(fileSize);
                        fread(contents, fileSize, 1, file); //read file bytes
                        fclose(file); //close the stream
                        int occ = count(command, commandSep);
                        //foreach arg apart from 1 try to write contents to file named arg
                        for(int i = 2; i <= occ; i++){
                            char* filename = getArg(i, command, strlen(command));
                            if(filename[0] != '/'){
                                char *absFilename = calloc(strlen(curPath) + strlen(filename) + 1, sizeof(char));
                                absolutify(absFilename, filename);
                                filename = realloc(filename, strlen(curPath) + strlen(filename) + 1);
                                strcpy(filename, absFilename);
                                free(absFilename);
                            }
                            copyToFile(contents, filename, fileSize);
                            free(filename);
                        }
                        free(contents);
                    }
                    else{
                        char* path;
                        if((path = parseExistingPath(arg1)) == NULL){
                            fprintf( stderr, "Podana ścieżka " ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET " nie mogła być zinterpretowana.\n", arg1);
                            exit(1);
                        }
                        //argument 2 - in this case target
                        char* arg2 = getArg(2, command, strlen(command));
                        printf("%s", arg2);
                        if(arg2[0] != '/'){
                            char *absArg2 = calloc(strlen(curPath) + strlen(arg1) + 2, sizeof(char));
                            absolutify(absArg2, arg2);
                            arg2 = realloc(arg2, (strlen(curPath) + strlen(arg1) + 2) * sizeof(char));
                            strcpy(arg2, absArg2);
                            free(absArg2);
                        }
                        char* arg3 = getArg(3, command, strlen(command));
                        bool recursive = checkIfCommand("-r", arg3);
                        free(arg3);
                        if(copyDir(path, arg2, recursive) != 0){
                            fprintf( stderr, "Skopiowanie folderu " ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET " się nie powiodło.\n", arg1);
                            exit(1);
                        }
                        free(path);
                        free(arg2);
                    }
                    free(arg1);
                    exit(0);
                }
                else if(checkIfCommand("history", command)){
                    for(int i = historyLen - 1; i >= 0; i--){
                        if(strlen((char*) history[i]) > 0)
                        printf("%d %s\n", i, (char*)history[i]);
                    }
                    printf("0 %s\n", command);
                    exit(0);
                }
                else{//all else failed lets try and launch a exe in PATH
                    //name of the executable the user might be trying to invoke
                    char* name = getStart(commandSep, command);
                    //Parse the launch arguments
                    char **args; //Pointer to array of strings terminated with NULL
                    command = replaceMultiple(command, commandSep); // replace the multiple spaces
                    int occ = count(command, commandSep); //number of command seperators
                    args = calloc(occ + 2, sizeof(*args));
                    args[0] = malloc(sizeof(command));
                    args[0] = command;
                    for(int i = 0; i <= occ; i++){ //foreach seperator get the argument following it
                        char* argx = getArg(i + 1, command, strlen(command));
                        args[i + 1] = malloc((strlen(argx) + 1) * sizeof(char));
                        strcpy(args[i + 1], argx);
                        free(argx);
                    }
                    args[occ + 1] = malloc(sizeof(NULL));
                    args[occ + 1] = NULL;
                    //Launch the requested process with parsed arguments
                    res = execvp(name, args);
                    //free all memory
                    for(int i = 0; i <= occ; i++){ //foreach seperator get the argument following it
                        free(args[i]);
                    }
                    free(args);
                    free(name);
                    exit(res);
                }
            }
            else{
                //await for the child process to finish
                waitpid(pid, &res, 0);
                if(res != 0){ //command error handler
                    printf("Wystąpił błąd podczas wykonywania komendy\n");
                }
            }
        }
        addToHistory(command); //add the issued command to history
        free(command); //free the memory associated with command
    }
    return 0;
}
